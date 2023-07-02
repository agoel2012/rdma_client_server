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

    // Setup Server control plane
    server_ctx_t *ctx = setup_server(sv->ip_addr, sv->app_port);
    API_NULL(
        ctx, { return (-1); }, "Server Setup Failed\n");

    // Connect server to a client
    API_STATUS(
        connect_server(ctx), { return (-1); }, "Server Connect Failed\n");

    // Prepare request/response structures
    API_STATUS(
        prepare_server_data(ctx), { return -1; },
        "Unable to prepare the server request data\n");

    while (ctx->is_connected) {
        API_STATUS(
            send_recv_server(ctx), { return -1; },
            "Unable to send/recv request/response to/from server\n");
    }

    return (0);
}

int main(int argc, char *argv[]) {
    if (argc < SERVER_ARGS) {
        printf("Usage: ./server <server IP:port>\n");
        return 1;
    }

    server_info_t *sv = parse_saddress_info(argv[1]);
    return (start_server(sv));
}
