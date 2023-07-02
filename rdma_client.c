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

#define CLIENT_ARGS 6

static int start_client(const client_info_t *sv) {

    int i = 0;
    // TODO: Debug the struct to ip conversion bug !
    client_ctx_t *ctx = setup_client(sv->my_addr, sv->peer_addr);
    API_NULL(
        ctx, { return -1; },
        "Unable to setup client control plane and connect to server\n");

    // Prepare request/response structures
    API_STATUS(
        prepare_client_data(ctx, sv->opcode), { return -1; },
        "Unable to prepare the client request data\n");

    for (i = 0; i < sv->iterations; i++) {
        // Send request based the opcode
        API_STATUS(
            send_client_request(ctx, sv->opcode, sv->msg_sz), { return -1; },
            "Unable to send request to server\n");

        // Recv response based on the opcode
        API_STATUS(
            process_client_response(ctx, sv->opcode, sv->msg_sz),
            { return -1; }, "Unable to recv response from server\n");
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < CLIENT_ARGS) {
        printf(
            "Usage: ./RDMAClient <source IP> <target IP:target port> <opcode> "
            "<iterations> <message size>\n");
        return 1;
    }

    const client_info_t *sv =
        parse_caddress_info(argv[1], argv[2], argv[3], argv[4], argv[5]);
    return (start_client(sv));
}
