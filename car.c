// car: elevator car process (one per car)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "common.h"
#include "floors.h"
#include "net.h"

typedef struct {
    char name[32];
    char low[4];
    char high[4];
    int  delay_ms;
} car_cfg_t;

static car_shared_mem *g_mem = NULL;
static char g_shmname[64];
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int sig) {
    (void)sig;
    g_stop = 1;
}

static void msleep(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int within_range(const char *f, const char *lo, const char *hi) {
    return floor_cmp_phys(lo, f) <= 0 && floor_cmp_phys(f, hi) <= 0;
}

static void broadcast(car_shared_mem *m) { pthread_cond_broadcast(&m->cond); }

// Networking thread: connect to controller when safety_system==1 and not in service/emergency.
typedef struct {
    car_cfg_t cfg;
    int sockfd;
} net_state_t;

static void send_status(int fd, car_shared_mem *m) {
    char msg[64];
    snprintf(msg, sizeof(msg), "STATUS %s %s %s", m->status, m->current_floor, m->destination_floor);
    (void)send_lp_msg(fd, msg);
}

static int connect_controller(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3000);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        close(fd); return -1;
    }
    return fd;
}

static void *net_thread(void *arg) {
    net_state_t *ns = (net_state_t*)arg;
    signal(SIGPIPE, SIG_IGN);

    int connected = 0;
    int fd = -1;
    char buf[128];

    while (!g_stop) {
        pthread_mutex_lock(&g_mem->mutex);
        int ss = g_mem->safety_system;
        int svc = g_mem->individual_service_mode;
        int emg = g_mem->emergency_mode;
        pthread_mutex_unlock(&g_mem->mutex);

        if (!connected) {
            if (ss == 1 && svc == 0 && emg == 0) {
                fd = connect_controller();
                if (fd >= 0) {
                    connected = 1;
                    char init[64];
                    snprintf(init, sizeof(init), "CAR %s %s %s", ns->cfg.name, ns->cfg.low, ns->cfg.high);
                    if (send_lp_msg(fd, init) != 0) { close(fd); connected = 0; fd = -1; }
                    else {
                        pthread_mutex_lock(&g_mem->mutex);
                        send_status(fd, g_mem);
                        pthread_mutex_unlock(&g_mem->mutex);
                    }
                } else {
                    msleep(ns->cfg.delay_ms);
                }
            } else {
                msleep(ns->cfg.delay_ms);
            }
        } else {
            // Increment safety_system watchdog every delay ms
            msleep(ns->cfg.delay_ms);
            pthread_mutex_lock(&g_mem->mutex);
            g_mem->safety_system = (uint8_t)(g_mem->safety_system + 1u);
            if (g_mem->safety_system >= 3u) {
                printf("Safety system disconnected! Entering emergency mode.\n");
                g_mem->emergency_mode = 1u;
                (void)send_lp_msg(fd, "EMERGENCY");
                broadcast(g_mem);
                pthread_mutex_unlock(&g_mem->mutex);
                close(fd); connected = 0; fd = -1;
                continue;
            }
            // Send status on any change (simplified: send every tick)
            send_status(fd, g_mem);
            pthread_mutex_unlock(&g_mem->mutex);

            // Try non-blocking read of commands
            struct timeval tv = {0, 0};
            fd_set rfds; FD_ZERO(&rfds); FD_SET(fd, &rfds);
            int sel = select(fd+1, &rfds, NULL, NULL, &tv);
            if (sel > 0 && FD_ISSET(fd, &rfds)) {
                int n = recv_lp_msg(fd, buf, sizeof(buf));
                if (n <= 0) {
                    // Controller died
                    close(fd); connected = 0; fd = -1;
                } else if (strncmp(buf, "FLOOR ", 6) == 0) {
                    const char *fl = buf + 6;
                    pthread_mutex_lock(&g_mem->mutex);
                    if (floor_is_valid(fl) && within_range(fl, ns->cfg.low, ns->cfg.high)) {
                        COPY_FIELD(g_mem->destination_floor, fl);
                        broadcast(g_mem);
                        // If already on that floor: trigger door cycle by leaving state machine do it
                    }
                    pthread_mutex_unlock(&g_mem->mutex);
                } else if (strncmp(buf, "INDIVIDUAL SERVICE", 18) == 0) {
                    // Not expected here; car initiates this on entering service
                }
            }

            // If we entered service/emergency, inform controller then disconnect
            pthread_mutex_lock(&g_mem->mutex);
            if (g_mem->individual_service_mode == 1u) {
                (void)send_lp_msg(fd, "INDIVIDUAL SERVICE");
                pthread_mutex_unlock(&g_mem->mutex);
                close(fd); connected = 0; fd = -1;
            } else if (g_mem->emergency_mode == 1u) {
                (void)send_lp_msg(fd, "EMERGENCY");
                pthread_mutex_unlock(&g_mem->mutex);
                close(fd); connected = 0; fd = -1;
            } else {
                pthread_mutex_unlock(&g_mem->mutex);
            }
        }
    }
    if (connected) close(fd);
    return NULL;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa; memset(&sa,0,sizeof(sa));
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    if (argc != 5) {
        fprintf(stderr, "Usage: %s {name} {lowest floor} {highest floor} {delay_ms}\n", argv[0]);
        return 1;
    }
    car_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, argv[1], sizeof(cfg.name)-1);
    strncpy(cfg.low, argv[2], sizeof(cfg.low)-1);
    strncpy(cfg.high, argv[3], sizeof(cfg.high)-1);
    cfg.delay_ms = atoi(argv[4]);
    if (!floor_is_valid(cfg.low) || !floor_is_valid(cfg.high) || floor_cmp_phys(cfg.low, cfg.high) > 0 || cfg.delay_ms < 0) {
        fprintf(stderr, "Invalid arguments.\n");
        return 1;
    }

    snprintf(g_shmname, sizeof(g_shmname), "/car%s", cfg.name);
    int fd = shm_open(g_shmname, O_CREAT|O_EXCL|O_RDWR, 0600);
    if (fd < 0) {
        perror("shm_open");
        return 1;
    }
    if (ftruncate(fd, sizeof(car_shared_mem)) != 0) {
        perror("ftruncate");
        close(fd); shm_unlink(g_shmname); return 1;
    }
    g_mem = (car_shared_mem*)mmap(NULL, sizeof(car_shared_mem), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_mem == MAP_FAILED) {
        perror("mmap");
        shm_unlink(g_shmname);
        return 1;
    }

    // Initialise pshared mutex/cond
    pthread_mutexattr_t mattr; pthread_mutexattr_init(&mattr);
    pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
    pthread_condattr_t cattr; pthread_condattr_init(&cattr);
    pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&g_mem->mutex, &mattr);
    pthread_cond_init(&g_mem->cond, &cattr);

    pthread_mutexattr_destroy(&mattr);
    pthread_condattr_destroy(&cattr);

    pthread_mutex_lock(&g_mem->mutex);
    COPY_FIELD(g_mem->current_floor, cfg.low);
    COPY_FIELD(g_mem->destination_floor, cfg.low);
    COPY_FIELD(g_mem->status, ST_CLOSED);
    g_mem->open_button = 0u;
    g_mem->close_button = 0u;
    g_mem->safety_system = 0u; // safety process will set to 1 when it starts
    g_mem->door_obstruction = 0u;
    g_mem->overload = 0u;
    g_mem->emergency_stop = 0u;
    g_mem->individual_service_mode = 0u;
    g_mem->emergency_mode = 0u;
    pthread_mutex_unlock(&g_mem->mutex);

    // Launch networking thread
    net_state_t ns; memset(&ns, 0, sizeof(ns)); ns.cfg = cfg;
    pthread_t th_net;
    (void)pthread_create(&th_net, NULL, net_thread, &ns);

    // State machine loop
    while (!g_stop) {
        pthread_mutex_lock(&g_mem->mutex);

        // Manual buttons: open/close
        if (g_mem->open_button == 1u) {
            g_mem->open_button = 0u;
            if (strcmp(g_mem->status, ST_CLOSED)==0 || strcmp(g_mem->status, ST_CLOSING)==0) {
                COPY_FIELD(g_mem->status, ST_OPENING);
                broadcast(g_mem);
            }
        }
        if (g_mem->close_button == 1u) {
            g_mem->close_button = 0u;
            if (strcmp(g_mem->status, ST_OPEN)==0) {
                COPY_FIELD(g_mem->status, ST_CLOSING);
                broadcast(g_mem);
            }
        }

        // Emergency mode: only allow manual door open/close (handled above); no movement.
        if (g_mem->emergency_mode == 1u) {
            pthread_mutex_unlock(&g_mem->mutex);
            msleep(cfg.delay_ms);
            continue;
        }

        // Individual service mode: doors do not auto-close/open, movement only one-step when dest differs
        if (g_mem->individual_service_mode == 1u) {
            if (strcmp(g_mem->status, ST_CLOSED)==0 &&
                strcmp(g_mem->current_floor, g_mem->destination_floor) != 0) {
                COPY_FIELD(g_mem->status, ST_BETWEEN);
                broadcast(g_mem);
                pthread_mutex_unlock(&g_mem->mutex);
                msleep(cfg.delay_ms);
                pthread_mutex_lock(&g_mem->mutex);
                COPY_FIELD(g_mem->current_floor, g_mem->destination_floor);
                COPY_FIELD(g_mem->status, ST_CLOSED);
                broadcast(g_mem);
            }
            pthread_mutex_unlock(&g_mem->mutex);
            msleep(cfg.delay_ms);
            continue;
        }

        // Normal operation
        int need_move = (strcmp(g_mem->current_floor, g_mem->destination_floor) != 0);
        if (!need_move && strcmp(g_mem->status, ST_CLOSED) == 0) {
            // idle
            pthread_mutex_unlock(&g_mem->mutex);
            msleep(cfg.delay_ms);
            continue;
        }

        if (need_move && strcmp(g_mem->status, ST_CLOSED) == 0) {
            COPY_FIELD(g_mem->status, ST_BETWEEN);
            broadcast(g_mem);
            pthread_mutex_unlock(&g_mem->mutex);
            msleep(cfg.delay_ms);
            pthread_mutex_lock(&g_mem->mutex);
            // Move one floor
            char nxt[4]; floor_step_towards(g_mem->current_floor, g_mem->destination_floor, nxt);
            COPY_FIELD(g_mem->current_floor, nxt);
            COPY_FIELD(g_mem->status, ST_CLOSED);
            broadcast(g_mem);
        }

        // Arrived?
        if (strcmp(g_mem->current_floor, g_mem->destination_floor) == 0 &&
            strcmp(g_mem->status, ST_CLOSED) == 0) {
            // Door cycle: Opening -> Open (wait) -> Closing -> Closed
            COPY_FIELD(g_mem->status, ST_OPENING); broadcast(g_mem);
            pthread_mutex_unlock(&g_mem->mutex); msleep(cfg.delay_ms); pthread_mutex_lock(&g_mem->mutex);
            // Check for safety intervention
            COPY_FIELD(g_mem->status, ST_OPEN); broadcast(g_mem);
            pthread_mutex_unlock(&g_mem->mutex); msleep(cfg.delay_ms); pthread_mutex_lock(&g_mem->mutex);
            COPY_FIELD(g_mem->status, ST_CLOSING); broadcast(g_mem);
            pthread_mutex_unlock(&g_mem->mutex); msleep(cfg.delay_ms); pthread_mutex_lock(&g_mem->mutex);
            COPY_FIELD(g_mem->status, ST_CLOSED); broadcast(g_mem);
        }

        pthread_mutex_unlock(&g_mem->mutex);
        msleep(cfg.delay_ms);
    }

    // Cleanup
    pthread_cancel(th_net); // cooperative loop checks g_stop, but cancel as fallback
    pthread_join(th_net, NULL);

    // Unlink shared memory on SIGINT
    munmap(g_mem, sizeof(*g_mem));
    shm_unlink(g_shmname);
    return 0;
}
