#ifndef CLIENT_SERVER_SHARED_H
#define CLIENT_SERVER_SHARED_H

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
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
    struct sockaddr_in ip_addr;
    struct sockaddr_in peer_addr;
    uint16_t app_port;
    uint16_t rank;
} server_info_t;

/**
 * @struct client_info_t
 * @brief Client Address Info Type
 */
typedef struct client_info_s {
    struct sockaddr_in ip_addr;
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
    obj->ip_addr.sin_family = AF_INET;
    obj->ip_addr.sin_port = htons(obj->app_port);
    inet_aton(ip, &(obj->ip_addr.sin_addr));
    // Extract rank from IP octet
    obj->rank = ntohl(inet_addr(ip));

    // Test out the extracted IP and port
    printf("IP: %s, Port: %s, Rank: %u\n", ip, port, obj->rank);
    // Release the tmp buffer
    free(ip);
    return obj;
}

static inline client_info_t *parse_caddress_info(char *ip, char *iterations) {
    client_info_t *obj = (client_info_t *)calloc(1, sizeof(client_info_t));

    // Store the IP, port into sockaddr structure
    obj->ip_addr.sin_family = AF_INET;
    inet_aton(ip, &(obj->ip_addr.sin_addr));
    // Extract rank from IP octet
    obj->rank = ntohl(inet_addr(ip));

    // Store the rank and iterations into obj structure
    obj->iterations = atoi(iterations);
    printf("IP: %s, Iterations: %s, Rank: %u\n", ip, iterations, obj->rank);
    return obj;
}

#endif /*! CLIENT_SERVER_SHARED_H */
