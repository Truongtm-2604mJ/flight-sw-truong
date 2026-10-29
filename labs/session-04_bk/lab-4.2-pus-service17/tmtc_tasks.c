/* Lab 4.2 - RTEMS TM/TC tasks and ground link. See tmtc_tasks.h. */
#include "tmtc_tasks.h"

#include <string.h>
#include <termios.h>
#include <unistd.h>

#define LINE_MAX_CHARS (3u + 2u * PUS_MAX_PACKET + 1u)

static rtems_id tm_queue_id;
static tc_ctx_t tc_ctx;
static volatile uint32_t tm_dropped; /* written under queue-full only */

/* ---- Small helpers ------------------------------------------------------ */

static const char HEX[] = "0123456789ABCDEF";

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    return -1;
}

/* Write all octets to the console. No stdio: tasks never call printf. */
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

/* ---- Public helpers ------------------------------------------------------ */

obt_t tmtc_now(void)
{
    return obt_from_ns(rtems_clock_get_uptime_nanoseconds());
}

uint32_t tmtc_tm_dropped(void)
{
    return tm_dropped;
}

tc_ctx_t *tmtc_tc_ctx(void)
{
    return &tc_ctx;
}

int tmtc_tm_sink(const uint8_t *packet, size_t len, void *arg)
{
    rtems_status_code sc;

    (void)arg;
    /* rtems_message_queue_send never blocks: if the queue is full it
     * returns RTEMS_TOO_MANY. The caller's timing is therefore bounded
     * even when the downlink is stalled. */
    sc = rtems_message_queue_send(tm_queue_id, packet, len);
    if (sc != RTEMS_SUCCESSFUL) {
        tm_dropped = tm_dropped + 1u;
        return 1;
    }
    return 0;
}

/* ---- TM_TX: downlink task ------------------------------------------------ */

static rtems_task tm_tx_task(rtems_task_argument arg)
{
    uint8_t packet[PUS_MAX_PACKET];
    char line[LINE_MAX_CHARS];
    size_t size;
    size_t i;
    size_t n;

    (void)arg;

    for (;;) {
        rtems_status_code sc = rtems_message_queue_receive(
            tm_queue_id, packet, &size, RTEMS_WAIT, RTEMS_NO_TIMEOUT);
        if (sc != RTEMS_SUCCESSFUL || size > PUS_MAX_PACKET) {
            continue;
        }

        n = 0;
        line[n++] = 'T'; line[n++] = 'M'; line[n++] = ':';
        for (i = 0; i < size; i++) {
            line[n++] = HEX[packet[i] >> 4];
            line[n++] = HEX[packet[i] & 0x0Fu];
        }
        line[n++] = '\n';

        /* One write per packet so lines from different sources cannot
         * interleave inside the console driver. */
        console_write(line, n);
    }
}

/* ---- TC_RX: command reception task --------------------------------------- */

/* Decode "TC:<hex>" and pass the frame to the handler. */
static void process_line(const char *line, size_t len)
{
    uint8_t frame[PUS_MAX_PACKET];
    size_t n = 0;
    size_t i;

    if (len < 3u || line[0] != 'T' || line[1] != 'C' || line[2] != ':') {
        return; /* not addressed to the command link: ignore silently */
    }

    line += 3;
    len  -= 3u;

    if ((len % 2u) != 0u || len / 2u > sizeof(frame)) {
        tc_count_link_error(&tc_ctx);
        return;
    }
    for (i = 0; i < len; i += 2u) {
        int hi = hex_value(line[i]);
        int lo = hex_value(line[i + 1u]);
        if (hi < 0 || lo < 0) {
            tc_count_link_error(&tc_ctx);
            return;
        }
        frame[n++] = (uint8_t)((hi << 4) | lo);
    }

    tc_handle(&tc_ctx, frame, n);
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
            rtems_task_wake_after(10); /* no data source: back off */
            continue;
        }
        if (got == 0) {
#ifdef __rtems__
            rtems_task_wake_after(10);
            continue;
#else
            /* Host simulation: the ground station closed the link. */
            rtems_shutdown_executive(0);
#endif
        }

        for (i = 0; i < got; i++) {
            char c = chunk[i];

            if (c == '\n' || c == '\r') {
                if (overflow) {
                    tc_count_link_error(&tc_ctx);
                } else if (line_len > 0u) {
                    process_line(line, line_len);
                }
                line_len = 0;
                overflow = 0;
            } else if (line_len < sizeof(line)) {
                line[line_len++] = c;
            } else {
                overflow = 1; /* discard up to the next line end */
            }
        }
    }
}

/* ---- Start up ------------------------------------------------------------ */

static void console_disable_echo(void)
{
    struct termios t;

    /* The console driver echoes input by default. Echoed "TC:" lines are
     * harmless (the ground ignores them) but they waste downlink. */
    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &t) == 0) {
        t.c_lflag &= (tcflag_t)~(ECHO | ECHOE | ECHOK | ECHONL);
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
}

rtems_status_code tmtc_init(void)
{
    rtems_status_code sc;

    console_disable_echo();

    sc = rtems_message_queue_create(
        rtems_build_name('T', 'M', 'Q', '1'),
        TM_QUEUE_DEPTH,
        PUS_MAX_PACKET,
        RTEMS_FIFO,
        &tm_queue_id);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }

    tc_init(&tc_ctx, APID_CMD, tmtc_tm_sink, NULL, tmtc_now);
    return RTEMS_SUCCESSFUL;
}

static rtems_status_code start_task(rtems_name name,
                                    rtems_task_priority priority,
                                    rtems_task_entry entry)
{
    rtems_id id;
    rtems_status_code sc;

    /* RTEMS_FLOATING_POINT is defensive: on LEON3 a libc call that
     * touches the FPU from a non-FP task is fatal error 38 (the Lab 3.3
     * finding). These two tasks only call read()/write(), but they are
     * the ones that go through libc, so they get the attribute. */
    sc = rtems_task_create(name, priority, TMTC_TASK_STACK,
                           RTEMS_DEFAULT_MODES, RTEMS_FLOATING_POINT, &id);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }
    return rtems_task_start(id, entry, 0);
}

rtems_status_code tmtc_start_tasks(void)
{
    rtems_status_code sc;

    sc = start_task(rtems_build_name('T', 'M', 'T', 'X'), TM_TX_PRIORITY,
                    tm_tx_task);
    if (sc != RTEMS_SUCCESSFUL) {
        return sc;
    }
    return start_task(rtems_build_name('T', 'C', 'R', 'X'), TC_RX_PRIORITY,
                      tc_rx_task);
}
