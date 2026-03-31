// controller: elevator scheduler server on TCP port 3000
// NOTE: This is a simplified scheduler sufficient for basic tests.
// It registers cars, tracks their status, and handles CALL requests by selecting
// a car that can reach both floors. It maintains a queue per car and sends FLOOR
// commands as the car reaches each queued floor and opens doors.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>

#include "floors.h"
#include "net.h"

#define MAX_CARS 16
#define MAX_QUEUE 64

typedef struct {
    char items[MAX_QUEUE][4];
    int count;
} queue_t;

typedef struct {
    int sock;               // car connection
    char name[32];
    char low[4], high[4];
    char status[8];
    char cur[4], dest[4];
    queue_t q;
    int in_service;         // 1 if removed (INDIVIDUAL or EMERGENCY)
} car_t;

static car_t cars[MAX_CARS];
static int num_cars = 0;
static int listenfd = -1;
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig){ (void)sig; g_stop = 1; }

static int make_listener(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt=1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(fd); return -1; }
    if (listen(fd, 64) != 0) { close(fd); return -1; }
    return fd;
}

static void drop_car(int idx) {
    if (idx < 0 || idx >= num_cars) return;
    close(cars[idx].sock);
    // compact list
    for (int i=idx+1; i<num_cars; ++i) cars[i-1] = cars[i];
    --num_cars;
}

static int car_can_service(car_t *c, const char *a, const char *b) {
    return floor_is_valid(a) && floor_is_valid(b) &&
           floor_cmp_phys(c->low, a) <= 0 && floor_cmp_phys(a, c->high) <= 0 &&
           floor_cmp_phys(c->low, b) <= 0 && floor_cmp_phys(b, c->high) <= 0 &&
           !c->in_service;
}

static void q_push_unique(queue_t *q, const char *f) {
    for (int i=0; i<q->count; ++i) { if (strcmp(q->items[i], f)==0) return; }
    if (q->count < MAX_QUEUE) { strncpy(q->items[q->count], f, 4); q->items[q->count][3]='\0'; q->count++; }
}

static void q_pop_front(queue_t *q) {
    if (q->count <= 0) return;
    for (int i=1;i<q->count;++i) memcpy(q->items[i-1], q->items[i], 4);
    q->count--;
}

// Send next FLOOR command if needed
static void maybe_send_next(car_t *c) {
    if (c->q.count <= 0) return;
    if (strncmp(c->dest, c->q.items[0], 4) != 0) {
        char msg[32]; snprintf(msg, sizeof(msg), "FLOOR %s", c->q.items[0]);
        (void)send_lp_msg(c->sock, msg);
        strncpy(c->dest, c->q.items[0], 4);
    }
}

static void handle_car_msg(int idx, const char *msg) {
    car_t *c = &cars[idx];
    if (strncmp(msg, "STATUS ", 7)==0) {
        // STATUS {status} {cur} {dest}
        char st[16], cu[4], de[4];
        st[0]=cu[0]=de[0]='\0';
        (void)sscanf(msg+7, "%15s %3s %3s", st, cu, de);
        strncpy(c->status, st, sizeof(c->status)-1); c->status[sizeof(c->status)-1]='\0';
        strncpy(c->cur, cu, 4); c->cur[3]='\0';
        strncpy(c->dest, de, 4); c->dest[3]='\0';
        // When doors opening on the head of queue, pop and send next
        if (c->q.count > 0 && strcmp(c->cur, c->q.items[0])==0 && strcmp(c->status, "Opening")==0) {
            q_pop_front(&c->q);
            maybe_send_next(c);
        }
    } else if (strncmp(msg, "INDIVIDUAL SERVICE", 18)==0 || strncmp(msg, "EMERGENCY", 9)==0) {
        c->in_service = 1;
        c->q.count = 0;
    } else {
        // ignore
    }
}

// Register a new car from an accepted socket after receiving "CAR ..."
static void register_car(int sock, const char *msg) {
    // CAR {name} {low} {high}
    char nm[32], lo[4], hi[4];
    nm[0]=lo[0]=hi[0]='\0';
    (void)sscanf(msg+4, "%31s %3s %3s", nm, lo, hi);
    if (!floor_is_valid(lo) || !floor_is_valid(hi) || floor_cmp_phys(lo, hi)>0) {
        close(sock);
        return;
    }
    if (num_cars >= MAX_CARS) { close(sock); return; }
    car_t *c = &cars[num_cars++];
    memset(c, 0, sizeof(*c));
    c->sock = sock;
    strncpy(c->name, nm, sizeof(c->name)-1);
    strncpy(c->low, lo, 4); c->low[3]='\0';
    strncpy(c->high, hi, 4); c->high[3]='\0';
    strncpy(c->status, "Closed", sizeof(c->status)-1);
    strncpy(c->cur, lo, 4); c->cur[3]='\0';
    strncpy(c->dest, lo, 4); c->dest[3]='\0';
    c->in_service = 0;
    c->q.count = 0;
}

static void handle_call(int sock, const char *msg) {
    // CALL {src} {dst}
    char src[4], dst[4];
    src[0]=dst[0]='\0';
    (void)sscanf(msg+5, "%3s %3s", src, dst);

    int pick = -1;
    // Naïve selection: first available car that can service both floors
    for (int i=0;i<num_cars;++i) {
        if (car_can_service(&cars[i], src, dst)) { pick = i; break; }
    }
    if (pick < 0) {
        (void)send_lp_msg(sock, "UNAVAILABLE");
        return;
    }
    car_t *c = &cars[pick];
    char reply[64]; snprintf(reply, sizeof(reply), "CAR %s", c->name);
    (void)send_lp_msg(sock, reply);

    // Enqueue src then dst (avoid duplicates)
    q_push_unique(&c->q, src);
    q_push_unique(&c->q, dst);
    maybe_send_next(c);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa; memset(&sa,0,sizeof(sa)); sa.sa_handler = on_sigint; sigaction(SIGINT, &sa, NULL);

    listenfd = make_listener();
    if (listenfd < 0) {
        perror("listen");
        return 1;
    }

    while (!g_stop) {
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(listenfd, &rfds);
        int maxfd = listenfd;
        for (int i=0;i<num_cars;++i) {
            FD_SET(cars[i].sock, &rfds);
            if (cars[i].sock > maxfd) maxfd = cars[i].sock;
        }
        // ephemeral call connections will be accepted then read and replied synchronously
        struct timeval tv = {1, 0};
        int rv = select(maxfd+1, &rfds, NULL, NULL, &tv);
        if (rv < 0) continue;

        if (FD_ISSET(listenfd, &rfds)) {
            int csock = accept(listenfd, NULL, NULL);
            if (csock >= 0) {
                // Read one message to classify endpoint
                char buf[128];
                int n = recv_lp_msg(csock, buf, sizeof(buf));
                if (n <= 0) { close(csock); }
                else if (strncmp(buf, "CAR ", 4)==0) {
                    register_car(csock, buf);
                } else if (strncmp(buf, "CALL ", 5)==0) {
                    handle_call(csock, buf);
                    close(csock); // call pads disconnect after reply
                } else {
                    // Unknown; drop
                    close(csock);
                }
            }
        }

        // Handle car sockets
        for (int i=0;i<num_cars;) {
            int consumed = 0;
            if (FD_ISSET(cars[i].sock, &rfds)) {
                char buf[128];
                int n = recv_lp_msg(cars[i].sock, buf, sizeof(buf));
                if (n <= 0) {
                    drop_car(i);
                    consumed = 1;
                } else {
                    handle_car_msg(i, buf);
                }
            }
            if (!consumed) ++i;
        }
    }

    if (listenfd >= 0) close(listenfd);
    return 0;
}
