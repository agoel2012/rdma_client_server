#ifndef RDMA_CLIENT_LIB_H
#define RDMA_CLIENT_LIB_H

#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/**
 * @struct thread_fn_t
 * @brief Client Thread Function Type
 */
typedef void *(*thread_fn_t)(void *);

/**
 * @struct client_ctx_t
 * @brief Client Connection Context Info
 */
typedef struct client_ctx_s {
    /* RDMA Connection Specific Attributes */
    struct rdma_cm_id *cm_id;      //< RDMA CM Core Identifier
    struct rdma_cm_id *addr_id;    //< RDMA CM Address Identifier
    struct rdma_cm_id *connect_id; //< RDMA CM Connect Identifier
    struct ibv_context *verbs;     //< Verbs Context
    struct ibv_pd *pd;             //< Verbs Protection Domain
    struct ibv_cq *scq;            //< Verbs Send CQ
    struct ibv_cq *rcq;            //< Verbs Recv CQ

    /* Event Monitor Specific attributes */
    struct rdma_event_channel *channel; //< RDMA Event Channel
    pthread_t evt_thread;               //< RDMA Event Thread
    thread_fn_t evt_fn;                 //< RDMA Event Thread Function Callback
    pthread_mutex_t evt_mtx;            //< RDMA Event Thread Sync Mtx
    pthread_cond_t evt_cv;              //< RDMA Event Thread Sync Cv
    bool is_connected;
} client_ctx_t;

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
 * @brief Given a source and target IP address, setup & connect a client
 * control plane to a target server
 */
client_ctx_t *setup_client(struct sockaddr *src_addr,
                           struct sockaddr *dst_addr);

/**
 * @brief Given a user-defined msgbuf and nbytes, process the data
 * and produce outbut msgbuf (in-place) of the same size
 */
int process_client(void **buf, size_t nbytes);

/**
 * @brief Given a previously allocated client context info,
 * destroy the client control plane
 */
int destroy_client(client_ctx_t *ctx);

#endif /*! RDMA_CLIENT_LIB_H */
