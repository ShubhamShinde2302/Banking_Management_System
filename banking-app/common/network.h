#ifndef NETWORK_H
#define NETWORK_H

#include <unistd.h>
#include "common/protocol.h"

/* Low-level robust read/write over socket stream descriptors using POSIX read/write */
ssize_t read_exact(int fd, void *buf, size_t count);
ssize_t write_exact(int fd, const void *buf, size_t count);

/* Packet transmission utilities */
int send_request(int sock, const RequestPacket *req);
int recv_request(int sock, RequestPacket *req);

int send_response(int sock, const ResponsePacket *res);
int recv_response(int sock, ResponsePacket *res);

#endif /* NETWORK_H */
