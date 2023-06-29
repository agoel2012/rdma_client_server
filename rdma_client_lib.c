#include "rdma_client_lib.h"
#include "client_server_shared.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_PENDING_CONNECTIONS 64
#define SKADDR_TO_IP(skaddr)                                                   \
    inet_ntoa(((struct sockaddr_in *)(skaddr))->sin_addr)

static void *client_event_monitor(void *arg) {
    client_ctx_t *ctx = (client_ctx_t *)(arg);
    struct rdma_cm_event *event = malloc(sizeof(struct rdma_cm_event));
    int rc = 0;

    while (1) {
        rc = rdma_get_cm_event(ctx->channel, &event);
        API_STATUS(
            rc,
            {
                free(event);
                return (NULL);
            },
            "Invalid RDMA CM Event. Reason: %s\n", strerror(errno));
        printf("Got RDMA CM Event: %s\n", rdma_event_str(event->event));
        switch (event->event) {
        case RDMA_CM_EVENT_ADDR_RESOLVED: {
            pthread_mutex_lock(&(ctx->evt_mtx));
            ctx->addr_id = event->id;
            pthread_cond_signal(&(ctx->evt_cv));
            pthread_mutex_unlock(&(ctx->evt_mtx));
        } break;
        case RDMA_CM_EVENT_ROUTE_RESOLVED: {
            pthread_mutex_lock(&(ctx->evt_mtx));
            ctx->connect_id = event->id;
            pthread_cond_signal(&(ctx->evt_cv));
            pthread_mutex_unlock(&(ctx->evt_mtx));
        } break;
        case RDMA_CM_EVENT_ESTABLISHED: {
            pthread_mutex_lock(&(ctx->evt_mtx));
            ctx->is_connected = true;
            pthread_cond_signal(&(ctx->evt_cv));
            pthread_mutex_unlock(&(ctx->evt_mtx));
        } break;
        case RDMA_CM_EVENT_DISCONNECTED: {
            pthread_mutex_lock(&(ctx->evt_mtx));
            ctx->is_connected = false;
            pthread_cond_signal(&(ctx->evt_cv));
            pthread_mutex_unlock(&(ctx->evt_mtx));
        } break;
        default:
            break;
        }

        rdma_ack_cm_event(event);
    }

    free(event);
    return (NULL);
}

client_ctx_t *setup_client(struct sockaddr *src_addr,
                           struct sockaddr *dst_addr) {
    int rc = 0;
    pthread_attr_t tattr;
    int ndevices = 0;
    struct ibv_context **rdma_verbs = NULL;
    struct ibv_qp_init_attr qp_attr = {};
    struct rdma_conn_param conn_param = {};
    memset(&qp_attr, 0, sizeof(struct ibv_qp_init_attr));
    memset(&conn_param, 0, sizeof(struct rdma_conn_param));

    // Check if any RDMA devices exist
    rdma_verbs = rdma_get_devices(&ndevices);
    API_NULL(
        rdma_verbs, { return (NULL); }, "No RDMA devices found\n");
    printf("Got %d RDMA devices\n", ndevices);
    rdma_free_devices(rdma_verbs);

    // Allocate a context instance
    client_ctx_t *ctx = calloc(1, sizeof(client_ctx_t));
    API_NULL(
        ctx, { return (NULL); }, "Unable to allocate client context\n");

    // create an event channel
    ctx->channel = rdma_create_event_channel();
    API_NULL(
        ctx->channel, { goto free_ctx_fields; },
        "Unable to create RDMA event channel. Reason: %s\n", strerror(errno));

    // open a connection
    rc = rdma_create_id(ctx->channel, &(ctx->cm_id), NULL, RDMA_PS_TCP);
    API_STATUS(
        rc, { goto free_channel; },
        "Unable to create RDMA Connection ID. Reason: %s\n", strerror(errno));

    // initialize event monitor
    ctx->evt_fn = &client_event_monitor;
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&(ctx->evt_mtx), NULL);
    pthread_cond_init(&(ctx->evt_cv), NULL);
    rc = pthread_create(&(ctx->evt_thread), &tattr, ctx->evt_fn, (void *)ctx);
    API_STATUS(
        rc, { goto free_cm_id; },
        "Unable to create RDMA event channel monitor\n");

    printf("Attempting to connect between src: %s dst: %s\n",
           SKADDR_TO_IP(src_addr), SKADDR_TO_IP(dst_addr));

    // bind a connection to source IP
    rc = rdma_bind_addr(ctx->cm_id, src_addr);
    API_STATUS(
        rc, { goto free_cm_id; },
        "Unable to bind RDMA device IP: %s. Reason: %s\n",
        SKADDR_TO_IP(src_addr), strerror(errno));

    // resolve an IP address to RDMA address within 1sec
    rc = rdma_resolve_addr(ctx->cm_id, src_addr, dst_addr, 2000);
    API_STATUS(
        rc, { goto free_cm_id; },
        "Unable to resolve RDMA address for IP: %s. Reason: %s\n",
        SKADDR_TO_IP(dst_addr), strerror(errno));

    pthread_mutex_lock(&(ctx->evt_mtx));
    while (!ctx->addr_id) {
        pthread_cond_wait(&(ctx->evt_cv), &(ctx->evt_mtx));
    }
    pthread_mutex_unlock(&(ctx->evt_mtx));

    // resolve an RDMA route
    rc = rdma_resolve_route(ctx->cm_id, 2000);
    API_STATUS(
        rc, { goto free_cm_id; },
        "Unable to resolve RDMA route for IP: %s. Reason: %s\n",
        SKADDR_TO_IP(dst_addr), strerror(errno));

    pthread_mutex_lock(&(ctx->evt_mtx));
    while (!ctx->connect_id) {
        pthread_cond_wait(&(ctx->evt_cv), &(ctx->evt_mtx));
    }
    pthread_mutex_unlock(&(ctx->evt_mtx));

    // init RDMA device resources - CQs/PDs/etc
    ctx->verbs = ctx->cm_id->verbs;
    ctx->pd = ibv_alloc_pd(ctx->verbs);
    API_NULL(
        ctx->pd, { goto free_cm_id; },
        "Unable to alloc RDMA Protection Domain. Reason: %s\n",
        strerror(errno));

    ctx->scq = ibv_create_cq(ctx->verbs, 128, NULL, NULL, 0);
    API_NULL(
        ctx->scq, { goto free_pd; },
        "Unable to create RDMA Send CQE of size 128 entries. Reason: %s\n",
        strerror(errno));

    // Create RDMA QPs for initialized RDMA device rsc
    qp_attr.cap.max_send_wr = 128;
    qp_attr.cap.max_recv_wr = 128;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.send_cq = ctx->scq;
    qp_attr.recv_cq = ctx->scq;
    rc = rdma_create_qp(ctx->cm_id, ctx->pd, &qp_attr);
    API_STATUS(
        rc, { goto free_cq; }, "Unable to RDMA QPs. Reason: %s\n",
        strerror(errno));

    // Connect to the target RDMA address
    conn_param.initiator_depth = 16;
    conn_param.responder_resources = 16;
    conn_param.retry_count =
        5; // maximum # of retry for send/RDMA for conn when conn error occurs
    conn_param.rnr_retry_count =
        1; // maximum # of retry for send/RDMA for conn when data arrives before
           // request is posted
    rc = rdma_connect(ctx->cm_id, &conn_param);
    API_STATUS(
        rc, { goto disconnect_free_cq; },
        "Unable to connect to RDMA device IP: %s. Reason: %s\n",
        SKADDR_TO_IP(dst_addr), strerror(errno));

    // Assert that connection to target is established
    pthread_mutex_lock(&(ctx->evt_mtx));
    while (!ctx->is_connected) {
        pthread_cond_wait(&(ctx->evt_cv), &(ctx->evt_mtx));
    }
    pthread_mutex_unlock(&(ctx->evt_mtx));

    printf("Connected RDMA_RC between src: %s dst: %s\n",
           SKADDR_TO_IP(rdma_get_local_addr(ctx->cm_id)),
           SKADDR_TO_IP(rdma_get_peer_addr(ctx->cm_id)));

    pthread_attr_destroy(&tattr);
    return (ctx);

disconnect_free_cq:
    rdma_disconnect(ctx->cm_id);
    pthread_attr_destroy(&tattr);

free_cq:
    if (ctx->scq) {
        ibv_destroy_cq(ctx->scq);
    }
    if (ctx->rcq) {
        ibv_destroy_cq(ctx->rcq);
    }
free_pd:
    ibv_dealloc_pd(ctx->pd);
free_cm_id:
    rdma_destroy_id(ctx->cm_id);
free_channel:
    rdma_destroy_event_channel(ctx->channel);
free_ctx_fields:
    free(ctx);
    return (NULL);
}

int process_client(void **buf, size_t nbytes) {
    size_t i = 0;
    // Print Rx on client after recv
    // Randomize Tx on client before send
    srand(time(NULL));
    printf("\n============[RX %zu bytes START]===========\n", nbytes);
    for (i = 0; i < nbytes; i++) {
        if (i % 8 == 0) {
            printf("\n");
        }

        printf("%02x \t", *(uint8_t *)(*buf + i));
        *(uint8_t *)(*buf + i) = (rand() % 256);
    }
    printf("\n============[RX %zu bytes END]=============\n", nbytes);

    return (0);
}
