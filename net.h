#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>

// Writes a length-prefixed (16-bit big-endian) ASCII message to fd.
int send_lp_msg(int fd, const char *msg);

// Reads one length-prefixed message into buf (size bytes). Returns length read (>=0) or -1 on error.
int recv_lp_msg(int fd, char *buf, size_t size);

#endif // NET_H
