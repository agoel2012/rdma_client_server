#ifndef RDMA_SERVER_LIB_H
#define RDMA_SERVER_LIB_H

#include <netinet/in.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
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
    struct rdma_cm_id *cm_id; //< RDMA CM Identifier
    struct rdma_cm_id *listen_id; //< RDMA CM Listen Identifier
    struct ibv_context *verbs; //< Verbs Context
    struct ibv_pd *pd; //< Verbs Protection Domain
    struct ibv_cq *scq; //< Verbs Send CQ
    struct ibv_cq *rcq; //< Verbs Recv CQ

    /* Event Monitor Specific attributes */
    struct rdma_event_channel *channel; //< RDMA Event Channel
    pthread_t evt_thread; //< RDMA Event Thread
    thread_fn_t evt_fn; //< RDMA Event Thread Function Callback
    pthread_mutex_t evt_mtx; //< RDMA Event Thread Sync Mtx
    pthread_cond_t evt_cv; //< RDMA Event Thread Sync Cv
    bool is_connected;
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
server_ctx_t *setup_server(struct sockaddr_in *addr, uint16_t port_id);

/**
 * @brief Given a user-defined msgbuf and nbytes, process the data
 * and produce outbut msgbuf (in-place) of the same size
 */
int process_server(void **buf, size_t nbytes);

/**
 * @brief Given a previously allocated server context info,
 * destroy the server control plane
 */
int destroy_server(server_ctx_t *ctx);

#endif /*! RDMA_SERVER_LIB_H */
