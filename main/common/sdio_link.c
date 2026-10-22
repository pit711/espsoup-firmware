/* Polled SDIO slave on the HAL; see sdio_link.h. Modelled on
 * esp_driver_sdio/sdio_slave.c with the ISR replaced by sdio_link_poll(). */
#include "sdio_link.h"
#include <string.h>
#include "driver/gpio.h"
#include "esp_private/gpio.h"
#include "esp_heap_caps.h"
#include "esp_private/periph_ctrl.h"
#include "esp_timer.h"
#include "hal/sdio_slave_hal.h"
#include "hal/sdio_slave_ll.h"
#include "hal/sdio_slave_periph.h"

#define TX_SLOTS_MAX 20u
#define TX_SLOTS_MIN 8u
#define TX_BUF 2064u             /* 16 buffers hold a CAP16 frame (24 B header + 32760 B) while collecting.
                                  * 4092 (DMA maximum) measured the same: the host's inter-block gaps are per
                                  * 512-byte block, not per descriptor. */
#define RX_SLOTS 8u
#define TX_FLUSH_US 300          /* coalesce small writes for at most this long */

static sdio_slave_context_t hal;
static uint8_t *tx_buf[TX_SLOTS_MAX];
static bool tx_busy[TX_SLOTS_MAX];  /* queued to the HAL, not yet returned */
static unsigned tx_slots;           /* buffers actually allocated */
static unsigned tx_queued;
static int tx_open = -1;            /* slot collecting bytes, -1 none */
static unsigned tx_open_len;
static int64_t tx_open_t0;
static bool tx_hold;                /* collect a whole frame before exposing it (sdio_link_hold) */

static sdio_slave_ll_desc_t *rx_desc[RX_SLOTS];
static uint8_t *rx_buf[RX_SLOTS];
static sdio_slave_ll_desc_t *rx_ready[RX_SLOTS]; /* FIFO of filled descriptors */
static unsigned rx_head, rx_tail;                /* free-running */
static unsigned rx_off;                          /* consumed bytes of rx_ready[tail] */
static unsigned rx_loaded;                       /* descriptors owned by the HAL */

static void configure_pin(int pin, uint32_t func, bool pullup) {
    gpio_input_enable(pin);
    gpio_func_sel(pin, func);
    gpio_set_drive_capability(pin, 3);
    gpio_pulldown_dis(pin);
    if (pullup) gpio_pullup_en(pin);
}

bool sdio_link_init(void) {
    memset(&hal, 0, sizeof(hal));
    /* Stream mode: everything queued so far is exposed to the host as one
     * chain and read in any length. Packet mode showed one 2 KB buffer at a
     * time, and each handover cost a poll tick on both chips. */
    hal.sending_mode = SDIO_SLAVE_SEND_STREAM;
    hal.timing = SDIO_SLAVE_TIMING_PSEND_PSAMPLE;
    hal.no_highspeed = 0;
    hal.recv_buffer_size = SDIO_LINK_RX_BUF;
    /* Take what the DMA heap gives, down to TX_SLOTS_MIN. */
    for (tx_slots = 0; tx_slots < TX_SLOTS_MAX; tx_slots++) {
        tx_buf[tx_slots] = heap_caps_malloc(TX_BUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
        if (!tx_buf[tx_slots]) break;
    }
    if (tx_slots < TX_SLOTS_MIN) return false;
    hal.send_queue_size = tx_slots;
    hal.send_desc_queue.size = SDIO_SLAVE_SEND_DESC_SIZE * (tx_slots + 1);
    hal.send_desc_queue.data = heap_caps_malloc(hal.send_desc_queue.size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
    if (!hal.send_desc_queue.data) return false;
    for (unsigned j = 0; j < RX_SLOTS; j++) {
        rx_buf[j] = heap_caps_malloc(SDIO_LINK_RX_BUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
        rx_desc[j] = heap_caps_calloc(1, sizeof(sdio_slave_ll_desc_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
        if (!rx_buf[j] || !rx_desc[j]) return false;
    }
    sdio_slave_hal_init(&hal);

    const sdio_slave_slot_info_t *slot = &sdio_slave_slot_info[0];
    configure_pin(slot->clk_gpio, slot->func, false);
    configure_pin(slot->cmd_gpio, slot->func, true);
    configure_pin(slot->d0_gpio, slot->func, true);
    configure_pin(slot->d1_gpio, slot->func, true);
    configure_pin(slot->d2_gpio, slot->func, true);
    configure_pin(slot->d3_gpio, slot->func, true);
    PERIPH_RCC_ATOMIC() {
        sdio_slave_ll_enable_bus_clock(true);
        sdio_slave_ll_reset_register();
    }
    sdio_slave_hal_hw_init(&hal);
    sdio_slave_hal_send_reset_counter(&hal);
    sdio_slave_hal_recv_reset_counter(&hal);

    for (unsigned j = 0; j < RX_SLOTS; j++) {
        sdio_slave_hal_recv_init_desc(&hal, rx_desc[j], rx_buf[j]);
        sdio_slave_hal_load_buf(&hal, rx_desc[j]);
        rx_loaded++;
    }
    sdio_slave_hostint_t all = (sdio_slave_hostint_t)UINT32_MAX;
    sdio_slave_hal_hostint_clear(&hal, &all);
    sdio_slave_hal_send_start(&hal);
    sdio_slave_hal_recv_start(&hal);
    sdio_slave_hal_set_ioready(&hal, true);
    return true;
}

unsigned sdio_link_tx_slots(void) { return tx_slots; }

/* ---- transmit ----------------------------------------------------------- */
static int tx_free_slot(void) {
    for (unsigned j = 0; j < tx_slots; j++) if (!tx_busy[j] && (int)j != tx_open) return (int)j;
    return -1;
}

static void tx_queue_open(void) {
    if (tx_open < 0 || !tx_open_len) return;
    if (sdio_slave_hal_send_queue(&hal, tx_buf[tx_open], tx_open_len, (void *)(uintptr_t)(tx_open + 1)) == ESP_OK) {
        tx_busy[tx_open] = true;
        tx_queued++;
    }
    tx_open = -1;
    tx_open_len = 0;
}

/* Return finished buffers and, unless holding, expose what is queued as the
 * next chain. Result: ESP_OK exposed a chain, ESP_ERR_NOT_FOUND nothing
 * queued, ESP_ERR_INVALID_STATE the previous chain is still being read. */
static esp_err_t tx_recycle(void) {
    sdio_slave_hal_send_handle_isr_invoke(&hal);
    if (hal.send_state == STATE_SENDING && sdio_slave_hal_send_eof_happened(&hal)) {
        void *arg;
        uint32_t cnt;
        while (sdio_slave_hal_send_get_next_finished_arg(&hal, &arg, &cnt) == ESP_OK) {
            unsigned slot = (unsigned)(uintptr_t)arg - 1u;
            if (slot < tx_slots && tx_busy[slot]) { tx_busy[slot] = false; tx_queued--; }
        }
    }
    if (tx_hold) return ESP_ERR_INVALID_STATE;
    return sdio_slave_hal_send_new_packet_if_exist(&hal);
}

int sdio_link_write(const void *p, size_t n) {
    tx_recycle();
    if (!n) return 0;
    if (tx_open < 0) {
        tx_open = tx_free_slot();
        if (tx_open < 0) {
            /* Out of buffers while collecting. With a chain in flight the
             * host is reading it right now and its buffers come back within
             * a millisecond: keep collecting (the caller polls). Only when
             * nothing is in flight would waiting never end, so expose what
             * has been collected so far. */
            if (tx_hold && hal.send_state != STATE_SENDING) { tx_hold = false; tx_recycle(); }
            return 0;
        }
        tx_open_len = 0;
        tx_open_t0 = esp_timer_get_time();
    }
    size_t room = TX_BUF - tx_open_len;
    if (n > room) n = room;
    memcpy(tx_buf[tx_open] + tx_open_len, p, n);
    tx_open_len += n;
    if (tx_open_len == TX_BUF) { tx_queue_open(); tx_recycle(); }
    return (int)n;
}

void sdio_link_flush(void) {
    if (!tx_hold) tx_queue_open();   /* while collecting, the next write continues in the same buffer */
    tx_recycle();
}

void sdio_link_hold(bool on) {
    tx_hold = on;
    if (on) return;
    tx_queue_open();
    /* The caller stops polling after this (a capture runs with interrupts
     * masked), so make sure the collected chain is exposed before returning.
     * Only a previous, small chain can still be in flight. */
    int64_t deadline = esp_timer_get_time() + 5000;
    while (tx_recycle() == ESP_ERR_INVALID_STATE && esp_timer_get_time() < deadline) {}
}

unsigned sdio_link_tx_pending(void) { return tx_queued + (tx_open >= 0 && tx_open_len ? 1u : 0u); }

/* ---- receive ------------------------------------------------------------ */
static void rx_poll(void) {
    sdio_slave_hal_recv_done(&hal);
    unsigned done = 0;
    while (sdio_slave_hal_recv_has_next_item(&hal)) done++;
    while (done--) {
        sdio_slave_ll_desc_t *d = sdio_slave_hal_recv_unload_desc(&hal);
        if (!d) break;
        rx_ready[rx_head % RX_SLOTS] = d;
        rx_head++;
        rx_loaded--;
    }
}

bool sdio_link_rx_available(void) {
    rx_poll();
    return rx_head != rx_tail;
}

int sdio_link_read(void *buf, size_t n) {
    rx_poll();
    uint8_t *out = buf;
    size_t got = 0;
    while (got < n && rx_head != rx_tail) {
        sdio_slave_ll_desc_t *d = rx_ready[rx_tail % RX_SLOTS];
        size_t avail = d->length - rx_off, k = n - got < avail ? n - got : avail;
        memcpy(out + got, (const uint8_t *)d->buf + rx_off, k);
        got += k;
        rx_off += k;
        if (rx_off >= d->length) {
            rx_off = 0;
            rx_tail++;
            sdio_slave_hal_load_buf(&hal, d);  /* back to the host */
            rx_loaded++;
        }
    }
    return (int)got;
}

void sdio_link_poll(void) {
    if (tx_open >= 0 && tx_open_len && !tx_hold && esp_timer_get_time() - tx_open_t0 >= TX_FLUSH_US) tx_queue_open();
    tx_recycle();
    rx_poll();
}
