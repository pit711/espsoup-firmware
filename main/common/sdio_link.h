#pragma once
/* ESPsoup SDIO slave transport (ESP32-C6 behind an ESP32-P4 host).
 *
 * Polled on the SDIO slave HAL only, no interrupt handler: the ring capture
 * paths keep interrupts masked for the whole run and still need to send
 * frames and notice a stop request. Every call below is safe with
 * interrupts masked and non-blocking, except hold(false), which spins for
 * at most 5 ms until the collected chain is exposed. The host side is ESSL (esp_serial_slave_link);
 * sending runs in stream mode (the host reads any length of what is queued),
 * receiving uses SDIO_LINK_RX_BUF byte buffers. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SDIO_LINK_RX_BUF 512u   /* receive buffer size negotiated with the host */

bool sdio_link_init(void);
void sdio_link_poll(void);                       /* recycle finished TX, start queued, flush stale */
bool sdio_link_rx_available(void);
int sdio_link_read(void *buf, size_t n);         /* bytes copied, 0 if none */
int sdio_link_write(const void *p, size_t n);    /* bytes accepted, 0 if the link is full */
void sdio_link_flush(void);                      /* push the open buffer to the host */
unsigned sdio_link_tx_pending(void);             /* buffers queued or open, 0 when idle */
unsigned sdio_link_tx_slots(void);               /* 2 KB send buffers allocated (8..20) */
/* hold(true): collect everything written as one chain; hold(false) exposes
 * it to the host at once. The host then reads a whole frame while the caller
 * is busy (e.g. capturing) instead of one chain per poll. Best effort: when
 * the buffers run out while holding, the chain is exposed early. */
void sdio_link_hold(bool on);
