/* Session 5 TM/TC chain. See tmtc5.h. Derived from Session 4 tmtc_tasks.c. */
#include "tmtc5.h"

#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "fdir_app.h"

#define LINE_MAX_CHARS (3u + 2u * PUS_MAX_PACKET + 1u)

static rtems_id tm_queue_id;
static rtems_id tc_lock_id;
static tc_ctx_t tc_ctx;
static volatile uint32_t tm_dropped;

static const char HEX[] = "0123456789ABCDEF";

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

static void console_write(const char *buf, size_t len)
{
    while (len > 0u) {
        ssize_t n = write(STDOUT_FILENO, buf, len);
        if (n <= 0) {
            return;
        }
        buf += n;
        len -= (size_t)n;
    }
}

obt_t tmtc_now(void)
{
    return obt_from_ns(rtems_clock_get_uptime_nanoseconds());
}

uint32_t tmtc_tm_dropped(void) { return tm_dropped; }
tc_ctx_t *tmtc_tc_ctx(void)    { return &tc_ctx; }

int tmtc_tm_sink(const uint8_t *packet, size_t len, void *arg)
{
    (void)arg;
    if (rtems_message_queue_send(tm_queue_id, packet, len) !=
        RTEMS_SUCCESSFUL) {
        tm_dropped = tm_dropped + 1u;
        return 1;
    }
    return 0;
}

void tmtc_lock(void)
{
    (void)rtems_semaphore_obtain(tc_lock_id, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
}

void tmtc_unlock(void)
{
    (void)rtems_semaphore_release(tc_lock_id);
}

void tmtc_dispatch_locked(const uint8_t *frame, size_t len)
{
    tc_handle(&tc_ctx, frame, len);
}

void tmtc_dispatch(const uint8_t *frame, size_t len)
{
    tmtc_lock();
    tc_handle(&tc_ctx, frame, len);
    tmtc_unlock();
}

/* ---- TM_TX ------------------------------------------------------------------ */

static rtems_task tm_tx_task(rtems_task_argument arg)
{
    uint8_t packet[PUS_MAX_PACKET];
    char line[LINE_MAX_CHARS];
    size_t size, i, n;

    (void)arg;
    for (;;) {
        if (rtems_message_queue_receive(tm_queue_id, packet, &size,
                                        RTEMS_WAIT, RTEMS_NO_TIMEOUT)
            != RTEMS_SUCCESSFUL || size > PUS_MAX_PACKET) {
            continue;
        }
        n = 0;
        line[n++] = 'T'; line[n++] = 'M'; line[n++] = ':';
        for (i = 0; i < size; i++) {
            line[n++] = HEX[packet[i] >> 4];
            line[n++] = HEX[packet[i] & 0x0Fu];
        }
        line[n++] = '\n';
        console_write(line, n);
    }
}

/* ---- TC_RX ------------------------------------------------------------------ */

static void process_line(const char *line, size_t len)
{
    uint8_t frame[PUS_MAX_PACKET];
    size_t n = 0, i;

    if (len < 3u || line[0] != 'T' || line[1] != 'C' || line[2] != ':') {
        return;
    }
    line += 3;
    len  -= 3u;
    if ((len % 2u) != 0u || len / 2u > sizeof(frame)) {
        tmtc_lock();
        tc_count_link_error(&tc_ctx);
        tmtc_unlock();
        return;
    }
    for (i = 0; i < len; i += 2u) {
        int hi = hex_value(line[i]);
        int lo = hex_value(line[i + 1u]);
        if (hi < 0 || lo < 0) {
            tmtc_lock();
            tc_count_link_error(&tc_ctx);
            tmtc_unlock();
            return;
        }
        frame[n++] = (uint8_t)((hi << 4) | lo);
    }
    tmtc_dispatch(frame, n);
}

static rtems_task tc_rx_task(rtems_task_argument arg)
{
    char chunk[64];
    char line[LINE_MAX_CHARS];
    size_t line_len = 0;
    int overflow = 0;

    (void)arg;
    for (;;) {
        ssize_t got = read(STDIN_FILENO, chunk, sizeof(chunk));
        ssize_t i;

        if (got < 0) {
            rtems_task_wake_after(10);
            continue;
        }
        if (got == 0) {
#ifdef __rtems__
            rtems_task_wake_after(10);
            continue;
#else
            rtems_shutdown_executive(0);   /* host: ground closed the link */
#endif
        }
        for (i = 0; i < got; i++) {
            char c = chunk[i];
            if (c == '\n' || c == '\r') {
                if (!overflow && line_len > 0u) {
                    process_line(line, line_len);
                }
                line_len = 0;
                overflow = 0;
            } else if (line_len < sizeof(line)) {
                line[line_len++] = c;
            } else {
                overflow = 1;
            }
        }
    }
}

/* ---- Start up ---------------------------------------------------------------- */

static void console_disable_echo(void)
{
    struct termios t;
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_lflag &= (tcflag_t)~(ECHO | ECHOE | ECHOK | ECHONL);
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
}

rtems_status_code tmtc_init(void)
{
    rtems_status_code sc;

    console_disable_echo();
    sc = rtems_message_queue_create(rtems_build_name('T', 'M', 'Q', '1'),
                                    TM_QUEUE_DEPTH, PUS_MAX_PACKET,
                                    RTEMS_FIFO, &tm_queue_id);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }
    sc = rtems_semaphore_create(rtems_build_name('T', 'C', 'L', 'K'), 1,
                                RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY |
                                RTEMS_INHERIT_PRIORITY, 0, &tc_lock_id);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }
    tc_init(&tc_ctx, APID_CMD, tmtc_tm_sink, NULL, tmtc_now);
    return RTEMS_SUCCESSFUL;
}

static rtems_status_code start_task(rtems_name name, rtems_task_priority prio,
                                    rtems_task_entry entry)
{
    rtems_id id;
    rtems_status_code sc = rtems_task_create(name, prio, TMTC_TASK_STACK,
                                             RTEMS_DEFAULT_MODES,
                                             RTEMS_FLOATING_POINT, &id);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }
    return rtems_task_start(id, entry, 0);
}

rtems_status_code tmtc_start_tasks(void)
{
    rtems_status_code sc = start_task(rtems_build_name('T', 'M', 'T', 'X'),
                                      TM_TX_PRIORITY, tm_tx_task);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }
    return start_task(rtems_build_name('T', 'C', 'R', 'X'), TC_RX_PRIORITY,
                      tc_rx_task);
}
