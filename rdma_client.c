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

#define CLIENT_ARGS 3

static int start_client(client_info_t *sv) {
    (void)sv;
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < CLIENT_ARGS) {
        printf("Usage: ./RDMAClient <target IP> <iterations>\n");
        return 1;
    }

    client_info_t *sv = parse_caddress_info(argv[1], argv[2]);
    return (start_client(sv));
}
