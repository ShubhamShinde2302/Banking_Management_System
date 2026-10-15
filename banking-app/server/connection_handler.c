#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include "server/connection_handler.h"
#include "server/lock.h"
#include "server/db.h"
#include "common/protocol.h"
#include "common/network.h"

void handle_client(int client_sock, struct sockaddr_in client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);
    pid_t pid = getpid();

    printf("[WORKER %d] Handling connection from %s:%d\n", pid, client_ip, client_port);

    int active_user_id = 0;
    int session_fd = -1;

    RequestPacket req;
    ResponsePacket res;

    while (1) {
        memset(&req, 0, sizeof(req));
        int ret = recv_request(client_sock, &req);
        if (ret < 0) {
            printf("[WORKER %d] Client %s:%d disconnected abruptly.\n", pid, client_ip, client_port);
            break;
        }

        memset(&res, 0, sizeof(res));

        if (req.opcode == REQ_EXIT) {
            if (active_user_id > 0 && session_fd >= 0) {
                release_user_session(active_user_id, session_fd);
                active_user_id = 0;
                session_fd = -1;
            }
            res.status_code = RESP_OK;
            snprintf(res.message, sizeof(res.message), "Session closed. Goodbye!");
            send_response(client_sock, &res);
            printf("[WORKER %d] Client %s:%d requested exit.\n", pid, client_ip, client_port);
            break;
        }

        /* Generic handler - will be dispatched to role handlers in Day 5+ */
        printf("[WORKER %d] Received opcode %d from user_id %d\n", pid, req.opcode, req.user_id);
        res.status_code = RESP_OK;
        snprintf(res.message, sizeof(res.message), "Server Worker PID %d ready.", pid);
        snprintf(res.payload, sizeof(res.payload), "ACK: Opcode %d received.", req.opcode);
        res.data_len = strlen(res.payload);

        if (send_response(client_sock, &res) < 0) {
            printf("[WORKER %d] Failed to send response to %s:%d\n", pid, client_ip, client_port);
            break;
        }
    }

    /* Safety clean-up on exit or abrupt crash */
    if (active_user_id > 0 && session_fd >= 0) {
        release_user_session(active_user_id, session_fd);
    }

    close(client_sock);
    printf("[WORKER %d] Connection with %s:%d closed cleanly. Exiting child process.\n", pid, client_ip, client_port);
    exit(0);
}
