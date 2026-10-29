/*
 * Coverage of a process that is killed rather than exited (Lab 7.2).
 *
 * The validation campaign stops the host simulation with SIGKILL (the ground
 * link's stop()), and the host simulation's own shutdown uses _exit(); in
 * both cases libgcov never writes its counters. Linked into an instrumented
 * build only, this file starts a thread that every 200 ms merges the counters
 * into the .gcda files (__gcov_dump) and zeroes them (__gcov_reset), and
 * wraps _exit() for a last merge. Counts made between a dump and the reset
 * that follows it can be lost; a line executed only in that window would be
 * reported as not covered (a conservative error).
 */
#include <pthread.h>
#include <time.h>

extern void __gcov_dump(void);
extern void __gcov_reset(void);
void __real__exit(int status);
void __wrap__exit(int status);

static void *flusher(void *arg)
{
    struct timespec ts = { 0, 200000000L };
    (void)arg;
    for (;;) {
        nanosleep(&ts, NULL);
        __gcov_dump();
        __gcov_reset();
    }
    return NULL;
}

__attribute__((constructor)) static void start_flusher(void)
{
    pthread_t t;
    (void)pthread_create(&t, NULL, flusher, NULL);
    (void)pthread_detach(t);
}

void __wrap__exit(int status)
{
    __gcov_dump();
    __real__exit(status);
}
