#include "client_server_shared.h"
#include "rdma_server_lib.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#define SERVER_ARGS 2

int start_server(server_info_t *sv) {

    int rc = 0;

    // Setup Server control plane
    server_ctx_t *ctx = setup_server(sv->ip_addr, sv->app_port);
    API_NULL(
        ctx, { return (-1); }, "Server Setup Failed\n");

    while (ctx->is_connected) {
        // TODO: Add recv/send information
    }

    return (rc);
}

int main(int argc, char *argv[]) {
    if (argc < SERVER_ARGS) {
        printf("Usage: ./server <server IP:port>\n");
        return 1;
    }

    server_info_t *sv = parse_saddress_info(argv[1]);
    return (start_server(sv));
}
