/*
 * safety: Safety-critical monitor for one elevator car.
 *
 * Safety-critical notes (MISRA-C style deviations and justifications):
 *  - Dynamic memory allocation is avoided after start-up; only shared memory mapping is used.
 *  - Use of printf for operator messages is allowed for this simulation; in real systems an
 *    async-signal-safe logging mechanism with buffering would be preferred.
 *  - We use pthreads condition waits on shared memory. The cond/mutex are created by car with
 *    pshared attributes to permit inter-process use.
 *  - Integer types are fixed-width where appropriate. All uint8_t flags are constrained to 0/1.
 *  - All shared state changes are protected by the mutex and followed by a broadcast.
 *  - Input validation explicitly checks every field for range and string validity.
 */

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

static int open_shm(const char *name, car_shared_mem **out) {
    char shmname[64];
    (void)snprintf(shmname, sizeof(shmname), "/car%s", name);

    int fd = shm_open(shmname, O_RDWR, 0600);
    if (fd < 0) return -1;
    *out = (car_shared_mem*)mmap(NULL, sizeof(car_shared_mem), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    (void)close(fd);
    if (*out == MAP_FAILED) return -1;
    return 0;
}

static void put_emergency(car_shared_mem *mem) {
    mem->emergency_mode = 1;
    (void)pthread_cond_broadcast(&mem->cond);
}

static int valid_status(const char *s) {
    return (strcmp(s, ST_OPENING)==0 || strcmp(s, ST_OPEN)==0 ||
            strcmp(s, ST_CLOSING)==0 || strcmp(s, ST_CLOSED)==0 ||
            strcmp(s, ST_BETWEEN)==0);
}

static int valid_u01(uint8_t v) { return v==0 || v==1; }

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s {car name}\n", argv[0]);
        return 1;
    }
    const char *carname = argv[1];
    car_shared_mem *mem = NULL;
    if (open_shm(carname, &mem) != 0) {
        printf("Unable to access car %s.\n", carname);
        return 1;
    }

    // Mark safety system operational
    pthread_mutex_lock(&mem->mutex);
    mem->safety_system = 1u;
    pthread_cond_broadcast(&mem->cond);

    for (;;) {
        // Wait for changes
        (void)pthread_cond_wait(&mem->cond, &mem->mutex);

        // Ensure safety_system remains 1
        if (mem->safety_system != 1u) {
            mem->safety_system = 1u;
            pthread_cond_broadcast(&mem->cond);
        }

        // Door obstruction while closing -> reopen
        if (mem->door_obstruction == 1u && strcmp(mem->status, ST_CLOSING) == 0) {
            COPY_FIELD(mem->status, ST_OPENING);
            pthread_cond_broadcast(&mem->cond);
        }

        // Emergency stop button
        if (mem->emergency_stop == 1u && mem->emergency_mode == 0u) {
            printf("The emergency stop button has been pressed!\n");
            put_emergency(mem);
            mem->emergency_stop = 0u;
            pthread_cond_broadcast(&mem->cond);
        }

        // Overload
        if (mem->overload == 1u && mem->emergency_mode == 0u) {
            printf("The overload sensor has been tripped!\n");
            put_emergency(mem);
            pthread_cond_broadcast(&mem->cond);
        }

        // Data consistency checks (only if not already in emergency)
        if (mem->emergency_mode != 1u) {
            int ok = 1;
            if (!floor_is_valid(mem->current_floor)) ok = 0;
            if (!floor_is_valid(mem->destination_floor)) ok = 0;
            if (!valid_status(mem->status)) ok = 0;
            if (!valid_u01(mem->open_button) ||
                !valid_u01(mem->close_button) ||
                !valid_u01(mem->safety_system) ||
                !valid_u01(mem->door_obstruction) ||
                !valid_u01(mem->overload) ||
                !valid_u01(mem->emergency_stop) ||
                !valid_u01(mem->individual_service_mode) ||
                !valid_u01(mem->emergency_mode)) ok = 0;
            if (mem->door_obstruction == 1u &&
                strcmp(mem->status, ST_OPENING) != 0 &&
                strcmp(mem->status, ST_CLOSING) != 0) ok = 0;

            if (!ok) {
                printf("Data consistency error!\n");
                put_emergency(mem);
                pthread_cond_broadcast(&mem->cond);
            }
        }
    }

    // Unreachable
    // pthread_mutex_unlock(&mem->mutex);
    // munmap(mem, sizeof(*mem));
    // return 0;
}
