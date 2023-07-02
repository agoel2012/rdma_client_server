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
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

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

    ctx->scq = ibv_create_cq(ctx->verbs, MAX_CQE, NULL, NULL, 0);
    API_NULL(
        ctx->scq, { goto free_pd; },
        "Unable to create RDMA Send CQE of size %d entries. Reason: %s\n",
        MAX_CQE, strerror(errno));

    // Create RDMA QPs for initialized RDMA device rsc
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.cap.max_send_wr = MAX_SEND_WR;
    qp_attr.cap.max_recv_wr = MAX_RECV_WR;
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
    if (ctx->scq || ctx->rcq) {
        ibv_destroy_cq(ctx->scq);
    }
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

static const char *wc_opcode_str(enum ibv_wc_opcode opc) {
    switch (opc) {
    case IBV_WC_RECV:
        return "WC_RECV";
    case IBV_WC_RECV_RDMA_WITH_IMM:
        return "WC_RECV_RDMA_WITH_IMM";
    case IBV_WC_SEND:
        return "WC_SEND";
    case IBV_WC_RDMA_WRITE:
        return "WC_RDMA_WRITE";
    case IBV_WC_RDMA_READ:
        return "WC_RDMA_READ";
    default:
        return "UNKNOWN WCQE OPCODE";
    }

    return "";
}

static void *server_wcq_monitor(void *arg) {
    server_ctx_t *ctx = (server_ctx_t *)(arg);
    struct ibv_wc wc[MAX_CQE] = {0};
    int ncqe = 0;

    while (ctx->is_connected) {
        ncqe = ibv_poll_cq(ctx->scq, MAX_CQE, &wc[0]);
        for (int i = 0; i < ncqe; i++) {
            // Check for errors
            if (wc[i].status != IBV_WC_SUCCESS) {
                printf("WCQE for WR[%ld] Status: %s\n", wc[i].wr_id,
                       ibv_wc_status_str(wc[i].status));
            } else {
#if 0
                printf("WCQE for WR[%ld] Opcode: %s\n", wc[i].wr_id,
                       wc_opcode_str(wc[i].opcode));
#endif
            }
            // Based on the opcode decide the action
            switch (wc[i].opcode) {
            case IBV_WC_RECV:
            case IBV_WC_RECV_RDMA_WITH_IMM:
                pthread_mutex_lock(&(ctx->wcq_mtx));
                // This only works because all messages are of the same size and
                // opcode, else we would need to organize this info by wr_id and
                // use that to correlate the dispatcher
                ctx->recv_opc = (wc[i].imm_data);
                ctx->recv_sz = (wc[i].byte_len);
                pthread_cond_signal(&(ctx->wcq_cv));
                pthread_mutex_unlock(&(ctx->wcq_mtx));
                break;
            case IBV_WC_RDMA_WRITE:
            case IBV_WC_SEND:
            default:
                break;
            }
        }
    }
    return (NULL);
}

int prepare_server_data(server_ctx_t *ctx) {
    // Unconditonally allocate req & response structures
    // Register memory with RDMA stack
    // Save keys and mrs into ctx
    // Exchange addresses with client using a passive server
    size_t send_sz = (MAX_MR_SZ);
    size_t recv_sz = (MAX_MR_SZ);
    int rc = 0;
    // Allocate 1MB of buffer space
    void *send_buf = mmap(NULL, send_sz, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    EXT_API_STATUS(
        send_buf == MAP_FAILED, { return (-1); },
        "Unable to allocate 1MB send buffer. Reason: %s\n", strerror(errno));
    void *recv_buf = mmap(NULL, recv_sz, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    EXT_API_STATUS(
        recv_buf == MAP_FAILED,
        {
            munmap(send_buf, send_sz);
            return (-1);
        },
        "Unable to allocate 1MB recv buffer. Reason: %s\n", strerror(errno));

    ctx->send_server_buf = send_buf;
    ctx->send_server_buf_sz = send_sz;
    ctx->recv_server_buf = recv_buf;
    ctx->recv_server_buf_sz = recv_sz;

    ctx->send_buf_mr =
        ibv_reg_mr(ctx->pd, send_buf, send_sz, RDMA_ACCESS_FLAGS);
    API_NULL(
        ctx->send_buf_mr,
        {
            munmap(send_buf, send_sz);
            munmap(recv_buf, recv_sz);
            return (-1);
        },
        "Unable to register send buf with RDMA. Reason: %s\n", strerror(errno));
    ctx->recv_buf_mr =
        ibv_reg_mr(ctx->pd, recv_buf, recv_sz, RDMA_ACCESS_FLAGS);
    API_NULL(
        ctx->recv_buf_mr,
        {
            munmap(send_buf, send_sz);
            munmap(recv_buf, recv_sz);
            ibv_dereg_mr(ctx->send_buf_mr);
            return (-1);
        },
        "Unable to register recv buf with RDMA. Reason: %s\n", strerror(errno));

    randomize_buf(&(ctx->send_server_buf), ctx->send_server_buf_sz);

    // Start a separate thread to poll for completion
    pthread_attr_t tattr;
    pthread_attr_init(&tattr);
    pthread_attr_setdetachstate(&tattr, PTHREAD_CREATE_DETACHED);
    pthread_mutex_init(&(ctx->wcq_mtx), NULL);
    pthread_cond_init(&(ctx->wcq_cv), NULL);
    ctx->wcq_fn = &(server_wcq_monitor);
    rc = pthread_create(&(ctx->wcq_thread), &tattr, ctx->wcq_fn, (void *)ctx);
    API_STATUS(
        rc, { return (-1); },
        "Unable to create WCQ shared send/recv monitor\n");

    // TODO: Use TCP-IP client/server socket to exchg this
    return (0);
}

int send_recv_server(server_ctx_t *ctx) {
    static int count = 0;
    int rc = 0, opc = 0;
    // Based on the IMM data opc, prepare wqe structures for response
    // IBV_SEND: lkey, no rkey is needed, zcopy local send, 1-copy remote
    // Protocol-1: Measure RTT time from client<->server
    struct ibv_recv_wr recv_wr = {0}, *recv_bad_wr = NULL;
    struct ibv_send_wr send_wr = {0}, *send_bad_wr = NULL;
    struct ibv_sge sge = {0};

    sge.addr = (uint64_t)ctx->recv_server_buf;
    sge.length = ctx->recv_server_buf_sz;
    sge.lkey = ctx->recv_buf_mr->lkey;
    recv_wr.wr_id = count++;
    recv_wr.next = NULL;
    recv_wr.sg_list = &sge;
    recv_wr.num_sge = 1;
    rc = ibv_post_recv(ctx->listen_id->qp, &recv_wr, &recv_bad_wr);
    API_STATUS(
        rc, { return (-1); }, "Unable to post receive wr. Reason: %s\n",
        strerror(errno));

    // sync with WCQ to make sure RECV_RDMA is consumed
    pthread_mutex_lock(&(ctx->wcq_mtx));
    while (ctx->recv_opc == OPC_INVALID) {
        pthread_cond_wait(&(ctx->wcq_cv), &(ctx->wcq_mtx));
    }

    opc = ctx->recv_opc;
    ctx->recv_opc = OPC_INVALID; // Reset for next request
    pthread_mutex_unlock(&(ctx->wcq_mtx));

    if (opc == OPC_SEND_ONLY) {
        send_wr.wr_id = count++;
        send_wr.next = NULL;
        sge.addr = (uint64_t)ctx->recv_server_buf; // zcopy round about !
        sge.length = ctx->recv_sz;
        sge.lkey = ctx->recv_buf_mr->lkey;
        send_wr.sg_list = &sge;
        send_wr.num_sge = 1;
        send_wr.opcode = IBV_WR_SEND;
        // remote address doesn't matter
        send_wr.wr.rdma.remote_addr = 0;
        send_wr.wr.rdma.rkey = 0;
        rc = ibv_post_send(ctx->listen_id->qp, &send_wr, &send_bad_wr);
        API_STATUS(
            rc, { return (-1); }, "Unable to post send request. Reason: %s\n",
            strerror(errno));
        // Ignore the processing of send completion as client synchronizes for
        // it!
    } else {
        // Protocol-2: Measure RDMA_WRITE RTT from client<->server
        printf("Unsupported OPC received\n");
        return (-1);
    }

    return (0);
}
