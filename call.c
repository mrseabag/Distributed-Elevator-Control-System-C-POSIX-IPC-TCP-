// call: destination dispatch call pad
// Usage: ./call {source floor} {destination floor}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "floors.h"
#include "net.h"

static int connect_controller(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    if (argc != 3) {
        fprintf(stderr, "Invalid floor(s) specified.\n");
        return 1;
    }
    const char *src = argv[1];
    const char *dst = argv[2];

    if (!floor_is_valid(src) || !floor_is_valid(dst)) {
        printf("Invalid floor(s) specified.\n");
        return 1;
    }
    if (strcmp(src, dst) == 0) {
        printf("You are already on that floor!\n");
        return 1;
    }

    int fd = connect_controller();
    if (fd < 0) {
        printf("Unable to connect to elevator system.\n");
        return 1;
    }

    char msg[64];
    snprintf(msg, sizeof(msg), "CALL %s %s", src, dst);
    if (send_lp_msg(fd, msg) != 0) {
        printf("Unable to connect to elevator system.\n");
        close(fd);
        return 1;
    }

    char buf[128];
    int n = recv_lp_msg(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) {
        printf("Unable to connect to elevator system.\n");
        return 1;
    }

    // Expect "CAR {name}" or "UNAVAILABLE"
    if (strncmp(buf, "CAR ", 4) == 0) {
        const char *name = buf + 4;
        printf("Car %s is arriving.\n", name);
    } else {
        printf("Sorry, no car is available to take this request.\n");
    }
    return 0;
}
