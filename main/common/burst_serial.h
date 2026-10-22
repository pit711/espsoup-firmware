#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "sdkconfig.h"
typedef enum {
    BURST_SERIAL_USB, BURST_SERIAL_UART,
#if CONFIG_ESP_SDR_SDIO_ENABLED
    BURST_SERIAL_SDIO,   /* ESP32-C6 as SDIO slave of an ESP32-P4 (sdio_link.h) */
#endif
    BURST_SERIAL_COUNT
} burst_serial_port_t;

void burst_serial_init(void);
/* 1: complete line, -1: overlong line, 0: idle. Each port has its own parser.
 * A complete line selects the port for all subsequent replies and payloads.
 * BAUD? and BAUD <1000000|2000000> are consumed internally. Baud changes
 * last until reset; OK BAUD <rate> is sent at the previous rate. */
int burst_serial_poll_line(char *line, size_t capacity);
burst_serial_port_t burst_serial_port(void);
unsigned burst_serial_baud(void);
bool burst_serial_send(const void *data, size_t size);

/* Consume a stream stop request on the owning port only. */
bool burst_serial_stop_requested(void);

/* Helpers for long-running commands (active port only):
 * try_send: non-blocking, returns the bytes accepted by the hardware FIFO.
 * poll_run_line: non-blocking line reader, true once a complete line is in buf.
 * drain_stop: discard the rest of a stop request and any partial line.
 * ring_*: native USB only, an ISR drains a caller-owned byte ring (head/tail
 * are free-running byte counters); start returns false on other ports. */
#include <stdint.h>
int burst_serial_try_send(const void *data, size_t size);
bool burst_serial_poll_run_line(char *buf, size_t capacity);
void burst_serial_drain_stop(void);
bool burst_serial_ring_start(const uint8_t *buf, unsigned size, volatile unsigned *head, volatile unsigned *tail);
void burst_serial_ring_kick(void);
void burst_serial_ring_stop(void);
/* Raw non-blocking read from the active port (binary uploads such as OTA). */
int burst_serial_read_raw(void *buf, size_t size);
/* Idle step of the command loop: a tick of sleep, or a tight poll while the
 * SDIO link still has output queued or traffic was recent. */
void burst_serial_idle(void);
/* SDIO only (no-op elsewhere): hold(true) collects the following sends as one
 * frame, hold(false) hands it to the host in one piece. */
void burst_serial_hold(bool on);
