#include "net.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>

int send_lp_msg(int fd, const char *msg) {
    size_t n = strlen(msg);
    if (n > 0xFFFFu) return -1;
    uint16_t len = htons((uint16_t)n);
    ssize_t w = write(fd, &len, sizeof(len));
    if (w != (ssize_t)sizeof(len)) return -1;
    size_t sent = 0;
    while (sent < n) {
        ssize_t x = write(fd, msg + sent, n - sent);
        if (x <= 0) return -1;
        sent += (size_t)x;
    }
    return 0;
}

int recv_lp_msg(int fd, char *buf, size_t size) {
    uint16_t len_be = 0;
    ssize_t r = read(fd, &len_be, sizeof(len_be));
    if (r == 0) return 0; // peer closed
    if (r != (ssize_t)sizeof(len_be)) return -1;
    uint16_t len = ntohs(len_be);
    if ((size_t)len >= size) return -1;
    size_t got = 0;
    while (got < len) {
        ssize_t x = read(fd, buf + got, len - got);
        if (x <= 0) return -1;
        got += (size_t)x;
    }
    buf[len] = '\0';
    return (int)len;
}
