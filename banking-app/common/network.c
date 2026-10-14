#include <unistd.h>
#include <errno.h>
#include "common/network.h"

/* Read exact number of bytes from a descriptor using POSIX read() */
ssize_t read_exact(int fd, void *buf, size_t count) {
    size_t total_read = 0;
    char *ptr = (char *)buf;

    while (total_read < count) {
        ssize_t bytes_read = read(fd, ptr + total_read, count - total_read);
        if (bytes_read < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (bytes_read == 0) {
            /* Peer closed connection */
            return total_read;
        }
        total_read += bytes_read;
    }
    return (ssize_t)total_read;
}

/* Write exact number of bytes to a descriptor using POSIX write() */
ssize_t write_exact(int fd, const void *buf, size_t count) {
    size_t total_written = 0;
    const char *ptr = (const char *)buf;

    while (total_written < count) {
        ssize_t bytes_written = write(fd, ptr + total_written, count - total_written);
        if (bytes_written <= 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total_written += bytes_written;
    }
    return (ssize_t)total_written;
}

/* Transmit a RequestPacket over socket */
int send_request(int sock, const RequestPacket *req) {
    ssize_t written = write_exact(sock, req, sizeof(RequestPacket));
    return (written == (ssize_t)sizeof(RequestPacket)) ? 0 : -1;
}

/* Receive a RequestPacket from socket */
int recv_request(int sock, RequestPacket *req) {
    ssize_t n = read_exact(sock, req, sizeof(RequestPacket));
    if (n <= 0) return -1;
    return (n == (ssize_t)sizeof(RequestPacket)) ? 0 : -1;
}

/* Transmit a ResponsePacket over socket */
int send_response(int sock, const ResponsePacket *res) {
    ssize_t written = write_exact(sock, res, sizeof(ResponsePacket));
    return (written == (ssize_t)sizeof(ResponsePacket)) ? 0 : -1;
}

/* Receive a ResponsePacket from socket */
int recv_response(int sock, ResponsePacket *res) {
    ssize_t n = read_exact(sock, res, sizeof(ResponsePacket));
    if (n <= 0) return -1;
    return (n == (ssize_t)sizeof(ResponsePacket)) ? 0 : -1;
}
