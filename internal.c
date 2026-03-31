// internal: control panel inside the car (technician simulation)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include "common.h"
#include "floors.h"

static void signal_change(car_shared_mem *mem) {
    (void)pthread_cond_broadcast(&mem->cond);
}

static int open_shm(const char *name, car_shared_mem **out) {
    char shmname[64];
    snprintf(shmname, sizeof(shmname), "/car%s", name);

    int fd = shm_open(shmname, O_RDWR, 0600);
    if (fd < 0) return -1;
    *out = (car_shared_mem*)mmap(NULL, sizeof(car_shared_mem), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (*out == MAP_FAILED) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Invalid operation.\n");
        return 1;
    }
    const char *carname = argv[1];
    const char *op = argv[2];

    car_shared_mem *mem = NULL;
    if (open_shm(carname, &mem) != 0) {
        printf("Unable to access car %s.\n", carname);
        return 1;
    }

    pthread_mutex_lock(&mem->mutex);

    if (strcmp(op, "open") == 0) {
        mem->open_button = 1;
        signal_change(mem);
    } else if (strcmp(op, "close") == 0) {
        mem->close_button = 1;
        signal_change(mem);
    } else if (strcmp(op, "stop") == 0) {
        mem->emergency_stop = 1;
        signal_change(mem);
    } else if (strcmp(op, "service_on") == 0) {
        mem->individual_service_mode = 1;
        mem->emergency_mode = 0;
        signal_change(mem);
    } else if (strcmp(op, "service_off") == 0) {
        mem->individual_service_mode = 0;
        signal_change(mem);
    } else if (strcmp(op, "up") == 0 || strcmp(op, "down") == 0) {
        if (mem->individual_service_mode != 1) {
            pthread_mutex_unlock(&mem->mutex);
            printf("Operation only allowed in service mode.\n");
            munmap(mem, sizeof(*mem));
            return 1;
        }
        if (strcmp(mem->status, ST_OPEN) == 0 || strcmp(mem->status, ST_OPENING) == 0) {
            pthread_mutex_unlock(&mem->mutex);
            printf("Operation not allowed while doors are open.\n");
            munmap(mem, sizeof(*mem));
            return 1;
        }
        if (strcmp(mem->status, ST_BETWEEN) == 0) {
            pthread_mutex_unlock(&mem->mutex);
            printf("Operation not allowed while elevator is moving.\n");
            munmap(mem, sizeof(*mem));
            return 1;
        }
        char next[4];
        int ok = 0;
        if (strcmp(op, "up") == 0) ok = floor_up(mem->current_floor, next);
        else ok = floor_down(mem->current_floor, next);
        if (!ok) {
            // Reached limit: reset destination to current
            COPY_FIELD(mem->destination_floor, mem->current_floor);
        } else {
            COPY_FIELD(mem->destination_floor, next);
        }
        signal_change(mem);
    } else {
        pthread_mutex_unlock(&mem->mutex);
        printf("Invalid operation.\n");
        munmap(mem, sizeof(*mem));
        return 1;
    }

    pthread_mutex_unlock(&mem->mutex);
    munmap(mem, sizeof(*mem));
    return 0;
}
