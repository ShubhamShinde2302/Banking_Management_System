#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common/models.h"
#include "common/protocol.h"
#include "common/network.h"

int main(int argc, char *argv[]) {
    const char *host = DEFAULT_HOST;
    int port = DEFAULT_PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    printf("=====================================================\n");
    printf("   Banking Management System - Client Terminal       \n");
    printf("=====================================================\n");
    printf("[*] Connecting to Banking Server at %s:%d...\n", host, port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed. Is the banking server running?");
        close(sock);
        return 1;
    }

    printf("[+] Successfully connected to Banking Server!\n\n");

    /* Initial handshake test packet */
    RequestPacket req;
    ResponsePacket res;
    memset(&req, 0, sizeof(req));
    req.opcode = 100; /* Initial handshake probe */
    snprintf(req.payload, sizeof(req.payload), "Hello from Client PID %d", getpid());

    if (send_request(sock, &req) == 0 && recv_response(sock, &res) == 0) {
        printf("[SERVER RESPONSE] Status: %d | Message: %s\n", res.status_code, res.message);
        if (res.data_len > 0) {
            printf("[SERVER DATA] %s\n\n", res.payload);
        }
    }

    int choice = 0;
    while (1) {
        printf("--- Day 3 Client Connection Menu ---\n");
        printf("1. Send Ping / Handshake Probe\n");
        printf("2. Disconnect & Exit\n");
        printf("Enter choice (1-2): ");
        if (scanf("%d", &choice) != 1) {
            break;
        }

        if (choice == 1) {
            memset(&req, 0, sizeof(req));
            req.opcode = 100;
            snprintf(req.payload, sizeof(req.payload), "Ping from PID %d", getpid());

            if (send_request(sock, &req) < 0 || recv_response(sock, &res) < 0) {
                printf("Error communicating with server.\n");
                break;
            }
            printf("\n--> [RESPONSE %d]: %s\n\n", res.status_code, res.message);
        } else if (choice == 2) {
            memset(&req, 0, sizeof(req));
            req.opcode = REQ_EXIT;
            send_request(sock, &req);
            recv_response(sock, &res);
            printf("\n%s\n", res.message);
            break;
        } else {
            printf("Invalid choice. Try again.\n");
        }
    }

    close(sock);
    printf("[*] Client closed connection.\n");
    return 0;
}
