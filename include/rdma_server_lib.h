#ifndef RDMA_SERVER_LIB_H
#define RDMA_SERVER_LIB_H

#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/**
 * @struct thread_fn_t
 * @brief Server Thread Function Type
 */
typedef void *(*thread_fn_t)(void *);

/**
 * @struct server_ctx_t
 * @brief Server Connection Context Info
 */
typedef struct server_ctx_s {
    /* RDMA Connection Specific Attributes */
    struct rdma_cm_id *cm_id;     //< RDMA CM Identifier
    struct rdma_cm_id *listen_id; //< RDMA CM Listen Identifier
    struct ibv_context *verbs;    //< Verbs Context
    struct ibv_pd *pd;            //< Verbs Protection Domain
    struct ibv_cq *scq;           //< Verbs Send CQ
    struct ibv_cq *rcq;           //< Verbs Recv CQ

    /* Event Monitor Specific attributes */
    struct rdma_event_channel *channel; //< RDMA Event Channel
    pthread_t evt_thread;               //< RDMA Event Thread
    thread_fn_t evt_fn;                 //< RDMA Event Thread Function Callback
    pthread_mutex_t evt_mtx;            //< RDMA Event Thread Sync Mtx
    pthread_cond_t evt_cv;              //< RDMA Event Thread Sync Cv
    bool is_connected;

    /* Poll Monitor Specific attributes */
    pthread_t wcq_thread;
    thread_fn_t wcq_fn;
    pthread_mutex_t wcq_mtx;
    pthread_cond_t wcq_cv;

    /* Memory to be registered and used by client-server communication */
    void *send_client_buf;      //< RDMA compliant send buf
    size_t send_client_buf_sz;  //< size of send buf
    void *recv_client_buf;      //< RDMA complaint recv for send buf
    size_t recv_client_buf_sz;  //< size of recv for send buf
    struct ibv_mr *send_buf_mr; //< RDMA compliant send buf mr
    struct ibv_mr *recv_buf_mr; //< RDMA compliant recv buf mr
    int lkey;                   //< RDMA compliant local pkey
    int rkey;                   //< RDMA complaint remote pkey
} server_ctx_t;

/**
 * @name API_STATUS_INTERNAL
 * @brief Convenience macro to check predicate, log error on console
 * and return
 */
#define API_STATUS_INTERNAL(expr, code_block, ...)                             \
    do {                                                                       \
        if (expr) {                                                            \
            printf(__VA_ARGS__);                                               \
            code_block;                                                        \
        }                                                                      \
    } while (0)

#define API_STATUS(rv, code_block, ...)                                        \
    API_STATUS_INTERNAL(((rv) < 0), code_block, __VA_ARGS__)

#define API_NULL(obj, code_block, ...)                                         \
    API_STATUS_INTERNAL(((obj) == NULL), code_block, __VA_ARGS__)

/**
 * @brief Given a user-defined IP and port, setup the server
 * control plane
 */
server_ctx_t *setup_server(struct sockaddr *addr, uint16_t port_id);

/**
 * @brief Prepare the input/output req/response data for server
 */
int prepare_server_data(server_ctx_t *ctx);

/**
 * @brief Recv the request, based on the immediate opcode, send response
 * to client
 */
int send_recv_server(server_ctx_t *ctx);

#endif /*! RDMA_SERVER_LIB_H */
