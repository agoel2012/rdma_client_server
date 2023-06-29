#include "client_server_shared.h"
#include "rdma_client_lib.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#define CLIENT_ARGS 4

static int start_client(client_info_t *sv) {
    client_ctx_t *ctx = setup_client(sv->my_addr, sv->peer_addr);
    API_NULL(
        ctx, { return -1; },
        "Unable to setup client control plane and connect to server\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < CLIENT_ARGS) {
        printf("Usage: ./RDMAClient <source IP> <target IP> <iterations>\n");
        return 1;
    }

    client_info_t *sv = parse_caddress_info(argv[1], argv[2], argv[3]);
    return (start_client(sv));
}
