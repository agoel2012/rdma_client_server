#include "rdma_server_lib.h"
#include "client_server_shared.h"
#include <arpa/inet.h>
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

static void *server_event_monitor(void *arg) {
    server_ctx_t *ctx = (server_ctx_t *)(arg);
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
        case RDMA_CM_EVENT_CONNECT_REQUEST: {
            pthread_mutex_lock(&(ctx->evt_mtx));
            ctx->listen_id = (event->id);
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

server_ctx_t *setup_server(struct sockaddr *addr, uint16_t port_id) {
    int rc = 0;
    pthread_attr_t tattr;
    int ndevices = 0;
    struct ibv_context **rdma_verbs = NULL;
    struct ibv_qp_init_attr qp_attr = {};
    memset(&qp_attr, 0, sizeof(struct ibv_qp_init_attr));

    // Check if any RDMA devices exist
    rdma_verbs = rdma_get_devices(&ndevices);
    API_NULL(
        rdma_verbs, { return (NULL); }, "No RDMA devices found\n");
    printf("Got %d RDMA devices\n", ndevices);
    rdma_free_devices(rdma_verbs);

    // Allocate a context instance
    server_ctx_t *ctx = calloc(1, sizeof(server_ctx_t));
    API_NULL(
        ctx, { return (NULL); }, "Unable to allocate server context\n");

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

    // bind a connection to an IP address
    rc = rdma_bind_addr(ctx->cm_id, addr);
    API_STATUS(
        rc, { goto free_cm_id; },
        "Unable to bind RDMA device IP: %s. Reason: %s\n", SKADDR_TO_IP(addr),
        strerror(errno));

    // listen for incoming requests on a connection
    rc = rdma_listen(ctx->cm_id, MAX_PENDING_CONNECTIONS);
    API_STATUS(
        rc, { goto free_cm_id; },
        "Unable to listen for incoming requests on RDMA device IP: %s. Reason: "
        "%s\n",
        SKADDR_TO_IP(addr), strerror(errno));

    // initialize event monitor
    ctx->evt_fn = &server_event_monitor;
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&(ctx->evt_mtx), NULL);
    pthread_cond_init(&(ctx->evt_cv), NULL);
    rc = pthread_create(&(ctx->evt_thread), &tattr, ctx->evt_fn, (void *)ctx);
    API_STATUS(
        rc, { goto free_cm_id; },
        "Unable to create RDMA event channel monitor\n");

    // Accept incoming valid client connections
    pthread_mutex_lock(&ctx->evt_mtx);
    while (!ctx->listen_id) {
        pthread_cond_wait(&ctx->evt_cv, &ctx->evt_mtx);
    }
    pthread_mutex_unlock(&ctx->evt_mtx);

    // init RDMA device resources - CQs/PDs/etc
    ctx->verbs = ctx->listen_id->verbs;
    struct ibv_device_attr dev_attr = {};
    rc = ibv_query_device(ctx->verbs, &dev_attr);
    assert(!(dev_attr.max_cqe < 1024));

    ctx->pd = ibv_alloc_pd(ctx->verbs);
    API_NULL(
        ctx->pd, { goto close_device; },
        "Unable to alloc RDMA Protection Domain. Reason: %s\n",
        strerror(errno));

    ctx->scq = ibv_create_cq(ctx->verbs, 1024, NULL, NULL, 0);
    API_NULL(
        ctx->scq, { goto free_pd; },
        "Unable to create RDMA Send CQE of size 128 entries. Reason: %s\n",
        strerror(errno));

    // Create RDMA QPs for initialized RDMA device rsc
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.cap.max_send_wr = 1024;
    qp_attr.cap.max_recv_wr = 1024;
    qp_attr.qp_context = NULL;
    qp_attr.sq_sig_all = 0;
    qp_attr.srq = NULL;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.send_cq = ctx->scq;
    qp_attr.recv_cq = ctx->scq;
    rc = rdma_create_qp(ctx->listen_id, ctx->pd, &qp_attr);
    API_STATUS(
        rc, { goto disconnect_free_cq; }, "Unable to RDMA QPs. Reason: %s\n",
        strerror(errno));

    rc = rdma_accept(ctx->listen_id, NULL);
    API_STATUS(
        rc, { goto disconnect_free_cq; },
        "Unable to accept RDMA connection rqst. Reason: %s\n", strerror(errno));

    // Assert that connection is established
    pthread_mutex_lock(&ctx->evt_mtx);
    while (!ctx->is_connected) {
        pthread_cond_wait(&ctx->evt_cv, &ctx->evt_mtx);
    }

    pthread_mutex_unlock(&ctx->evt_mtx);

    pthread_attr_destroy(&tattr);
    return (ctx);

disconnect_free_cq:
    pthread_mutex_unlock(&ctx->evt_mtx);
    rdma_disconnect(ctx->listen_id);
    pthread_attr_destroy(&tattr);

free_pd:
    ibv_dealloc_pd(ctx->pd);
close_device:
    ibv_close_device(ctx->verbs);
free_cm_id:
    rdma_destroy_id(ctx->cm_id);
free_channel:
    rdma_destroy_event_channel(ctx->channel);
free_ctx_fields:
    free(ctx);
    return (NULL);
}

int process_server(void **buf, size_t nbytes) {
    size_t i = 0;
    // Print Rx on server after recv
    // Randomize Tx on server before send
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
