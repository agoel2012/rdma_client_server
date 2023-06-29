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

/**
 * @struct server_info_t
 * @brief Server Address Info Type
 */
typedef struct server_info_s {
    struct sockaddr *ip_addr;
    uint16_t app_port;
    uint16_t rank;
} server_info_t;

/**
 * @struct client_info_t
 * @brief Client Address Info Type
 */
typedef struct client_info_s {
    struct sockaddr *my_addr;
    struct sockaddr *peer_addr;
    uint16_t rank;
    int iterations;
} client_info_t;

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
    struct addrinfo *res;
    struct addrinfo hints = {};
    memset(&hints, 0, sizeof(struct addrinfo));

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
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(ip, port, &hints, &res);
    obj->ip_addr = malloc(sizeof(struct sockaddr));
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        memcpy(obj->ip_addr, a->ai_addr, a->ai_addrlen);
        break;
    }

    freeaddrinfo(res);

    // Extract rank from IP octet
    obj->rank = ntohl(inet_addr(ip));

    // Test out the extracted IP and port
    printf("Server IP: %s, Port: %s, Rank: %u\n", ip, port, obj->rank);
    // Release the tmp buffer
    free(ip);
    return obj;
}

static inline client_info_t *parse_caddress_info(char *sip, char *__dip,
                                                 char *iterations) {
    client_info_t *obj = (client_info_t *)calloc(1, sizeof(client_info_t));
    struct addrinfo *res;
    struct addrinfo hints = {};
    memset(&hints, 0, sizeof(struct addrinfo));
    printf("SIP: %s DIP: %s\n", sip, __dip);

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

    // Store the IP, port into sockaddr structure
    obj->my_addr = malloc(sizeof(struct sockaddr));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(sip, NULL, &hints, &res);
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        memcpy(obj->my_addr, a->ai_addr, a->ai_addrlen);
        break;
    }
    freeaddrinfo(res);

    hints.ai_flags &= ~AI_PASSIVE;
    getaddrinfo(dip, port, &hints, &res);
    obj->peer_addr = malloc(sizeof(struct sockaddr));
    for (struct addrinfo *a = res; a; a = a->ai_next) {
        memcpy(obj->peer_addr, a->ai_addr, a->ai_addrlen);
        break;
    }
    freeaddrinfo(res);

    // Extract rank from IP octet
    obj->rank = ntohl(inet_addr(sip));

    // Store the rank and iterations into obj structure
    obj->iterations = atoi(iterations);
    printf("Client IP: %s, Iterations: %s, Rank: %u => Target Server: %s:%s\n",
           sip, iterations, obj->rank, dip, port);
    free(dip);
    return obj;
}

#endif /*! CLIENT_SERVER_SHARED_H */
