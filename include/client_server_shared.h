#ifndef CLIENT_SERVER_SHARED_H
#define CLIENT_SERVER_SHARED_H

#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <netinet/in.h>
#include <rdma/rdma_cma.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_PENDING_CONNECTIONS 64
#define SOCKADDR2IPADDR(skaddr, ip)                                            \
    do {                                                                       \
        struct sockaddr_in *__inp = NULL;                                      \
        __inp = (struct sockaddr_in *)(skaddr);                                \
        (ip) = inet_ntoa(__inp->sin_addr);                                     \
    } while (0)

#define SKADDR_TO_IP(skaddr)                                                   \
    inet_ntoa(((struct sockaddr_in *)(skaddr))->sin_addr)

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

#define EXT_API_STATUS(exp, code_block, ...)                                   \
    API_STATUS_INTERNAL(exp, code_block, __VA_ARGS__)

#define API_NULL(obj, code_block, ...)                                         \
    API_STATUS_INTERNAL(((obj) == NULL), code_block, __VA_ARGS__)

/**
 * @name RDMA_ACCESS_FLAG
 * @brief shared flags for client/server datapath
 */
#define RDMA_ACCESS_FLAGS (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE)

/**
 * @name OPC_RDMA_READ/OPC_SEND_ONLY/OPC_RDMA_WRITE
 * @brief shared opcode(s) for client/server datapath
 */
#define OPC_INVALID 0x0
#define OPC_RDMA_READ 0x01
#define OPC_SEND_ONLY 0x02
#define OPC_RDMA_WRITE 0x04

/**
 * @name MAX_MR_SZ
 * @brief Maximum size of the memory region for RDMA send/read/write
 */
#define MAX_MR_SZ (1024 * 1024)

/**
 * @name TIME_DECLARATIONS/TIME_START/TIME_GET_ELAPSED_TIME
 * @brief shared wall-clock time measurement utilities for client/server
 */
#define NSEC_TO_SEC (1000000000ULL)

#define TIME_DECLARATIONS()                                                    \
    struct timespec __start;                                                   \
    struct timespec __end;

#define TIME_START() clock_gettime(CLOCK_MONOTONIC, &__start);

#define TIME_GET_ELAPSED_TIME(nsec_elapsed)                                    \
    clock_gettime(CLOCK_MONOTONIC, &__end);                                    \
    (nsec_elapsed) = (__end.tv_nsec + (__end.tv_sec * NSEC_TO_SEC)) -          \
                     (__start.tv_nsec + (__start.tv_sec * NSEC_TO_SEC));

/**
 * @struct server_info_t
 * @brief Server Address Info Type
 */
typedef struct server_info_s {
    struct sockaddr *ip_addr;
    uint16_t app_port;
    uint16_t rank;
} __attribute__((packed)) server_info_t;

/**
 * @struct client_info_t
 * @brief Client Address Info Type
 */
typedef struct client_info_s {
    struct sockaddr *my_addr;
    struct sockaddr *peer_addr;
    uint16_t rank;
    int iterations;
    int opcode;
    size_t msg_sz;
} __attribute__((packed)) client_info_t;

/**
 * @struct msgbuf_t
 * @brief Server-side app rx/tx buffer
 */
typedef struct msgbuf_s {
    char _[1024];
} msgbuf_t;

// Use : as delimiter and separate out IP address and port
static inline server_info_t *parse_saddress_info(char *args) {
    server_info_t *obj = (server_info_t *)calloc(1, sizeof(server_info_t));
    struct sockaddr_in server_addr_in = {};

    // Find where TCP port begins
    char *port = strstr(args, ":");
    // Calculate length of IP address with dot notation
    size_t ip_len = (port - args);

    // Allocate a tmp buffer for IP address
    char *ip = calloc(ip_len, sizeof(char));

    // Copy the IP address from args
    memcpy(ip, args, ip_len);

    // Calculate TCP port from port string
    port++;
    obj->app_port = (uint16_t)atoi(port);

    // Store the IP, port into sockaddr structure
    obj->ip_addr = calloc(1, sizeof(struct sockaddr));
    server_addr_in.sin_family = AF_INET;
    server_addr_in.sin_port = htons(obj->app_port);
    inet_pton(AF_INET, ip, (void *)&(server_addr_in.sin_addr));
    memcpy(obj->ip_addr, (struct sockaddr *)(&server_addr_in),
           sizeof(struct sockaddr));

    // Extract rank from IP octet
    obj->rank = ntohl(inet_addr(ip));

    // Test out the extracted IP and port
    printf("Server IP: %s, Port: %s, Rank: %u\n", ip, port, obj->rank);
    // Release the tmp buffer
    free(ip);
    return obj;
}

static inline client_info_t *parse_caddress_info(char *sip, char *__dip,
                                                 char *opcode, char *iterations,
                                                 char *msg_sz) {
    client_info_t *obj = (client_info_t *)calloc(1, sizeof(client_info_t));
    struct sockaddr_in server_addr_in = {};
    struct sockaddr_in client_addr_in = {};

    // Find where TCP port begins
    char *port = strstr(__dip, ":");
    // Calculate length of IP address with dot notation
    size_t ip_len = (port - __dip);

    // Allocate a tmp buffer for IP address
    char *dip = calloc(ip_len, sizeof(char));

    // Copy the IP address from args
    memcpy(dip, __dip, ip_len);

    // Calculate TCP port from port string
    port++;
    int p = (uint16_t)atoi(port);

    // Store the IP, port into sockaddr structure
    obj->my_addr = malloc(sizeof(struct sockaddr));
    obj->peer_addr = malloc(sizeof(struct sockaddr));
    server_addr_in.sin_family = AF_INET;
    server_addr_in.sin_port = htons(p);
    inet_pton(AF_INET, dip, (void *)&(server_addr_in.sin_addr));
    memcpy(obj->peer_addr, (struct sockaddr *)(&server_addr_in),
           sizeof(struct sockaddr));

    client_addr_in.sin_family = AF_INET;
    client_addr_in.sin_port = 0;
    inet_pton(AF_INET, sip, (void *)&(client_addr_in.sin_addr));
    memcpy(obj->my_addr, (struct sockaddr *)(&client_addr_in),
           sizeof(struct sockaddr));
#if 0
    printf("PEER: %s\n", SKADDR_TO_IP(obj->peer_addr));
    printf("MY: %s\n", SKADDR_TO_IP(obj->my_addr));
#endif
    // Extract rank from IP octet
    obj->rank = (uint16_t)ntohl(inet_addr(sip));

    // Store the rank and iterations into obj structure
    obj->iterations = (int)atoi(iterations);
    if (strncmp(opcode, "SEND", strlen(opcode)) == 0) {
        obj->opcode = OPC_SEND_ONLY;
    } else if (strncmp(opcode, "RDMA_WRITE", strlen(opcode)) == 0) {
        obj->opcode = OPC_RDMA_WRITE;
    } else if (strncmp(opcode, "RDMA_READ", strlen(opcode)) == 0) {
        obj->opcode = OPC_RDMA_READ;
    } else {
        obj->opcode = OPC_INVALID;
    }

    obj->msg_sz = (size_t)atoi(msg_sz);
    printf("Client IP: %s, Iterations: %s, Rank: %u, %s Msg Size: %zu bytes => "
           "Target Server: %s:%s\n",
           sip, iterations, obj->rank, opcode, obj->msg_sz, dip, port);
    free(dip);
    return obj;
}

static inline void print_buf(void *buf, size_t nbytes) {
    size_t i = 0;
    // Print Rx on client after recv
    printf("\n============[%zu bytes START]===========\n", nbytes);
    for (i = 0; i < nbytes; i++) {
        if (i % 8 == 0) {
            printf("\n");
        }

        printf("%02x \t", *(uint8_t *)(buf + i));
    }

    printf("\n============[RX %zu bytes END]=============\n", nbytes);
}

static inline int randomize_buf(void **buf, size_t nbytes) {
    size_t i = 0;
    // Randomize Tx on client before send
    srand(time(NULL));
    for (i = 0; i < nbytes; i++) {
        *(uint8_t *)(*buf + i) = (rand() % 256);
    }

    return (0);
}

#endif /*! CLIENT_SERVER_SHARED_H */
