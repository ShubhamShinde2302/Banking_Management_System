#ifndef CONNECTION_HANDLER_H
#define CONNECTION_HANDLER_H

#include <netinet/in.h>

/* Handle an individual client connection in a dedicated child process */
void handle_client(int client_sock, struct sockaddr_in client_addr);

#endif /* CONNECTION_HANDLER_H */
