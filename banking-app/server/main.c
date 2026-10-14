#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "common/models.h"
#include "common/protocol.h"
#include "server/db.h"
#include "server/connection_handler.h"

/* Signal handler to reap zombie child processes */
static void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0);
    errno = saved_errno;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number. Defaulting to %d\n", DEFAULT_PORT);
            port = DEFAULT_PORT;
        }
    }

    printf("=====================================================\n");
    printf("   Banking Management System - Server Daemon         \n");
    printf("=====================================================\n");

    /* Ensure database files exist */
    if (db_init() != 0) {
        perror("Error initializing database storage");
        exit(EXIT_FAILURE);
    }
    printf("[+] Database persistence initialized.\n");

    /* Register SIGCHLD handler to prevent zombie processes */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("Error setting up SIGCHLD handler");
        exit(EXIT_FAILURE);
    }

    /* Create TCP socket */
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    /* Set socket options: SO_REUSEADDR */
    int optval = 1;
    if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("Error setting SO_REUSEADDR");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    /* Bind address to port */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Error binding socket");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    /* Listen for connections */
    if (listen(server_sock, 16) < 0) {
        perror("Error listening on socket");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("[+] Banking Server listening on 0.0.0.0:%d\n", port);
    printf("[+] Multi-process concurrency enabled (fork-per-client model).\n");
    printf("[*] Ready for client connections. Press Ctrl+C to stop.\n\n");

    /* Main Connection Loop */
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (client_sock < 0) {
            if (errno == EINTR) {
                continue; /* Interrupted by signal handler, retry accept */
            }
            perror("Error accepting connection");
            continue;
        }

        /* Spawn a dedicated child process for the connected client */
        pid_t pid = fork();
        if (pid < 0) {
            perror("Error forking child process");
            close(client_sock);
            continue;
        }

        if (pid == 0) {
            /* Child Process: Close listening socket and serve client */
            close(server_sock);
            handle_client(client_sock, client_addr);
            exit(0);
        } else {
            /* Parent Process: Close accepted socket descriptor and continue listening */
            close(client_sock);
        }
    }

    close(server_sock);
    return 0;
}
