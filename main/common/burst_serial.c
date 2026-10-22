/* The burst protocol is shared by native USB Serial/JTAG and UART0. */
#include "burst_serial.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "hal/usb_serial_jtag_ll.h"
#endif
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32S2
#include "esp_private/usb_console.h"
#endif
#if CONFIG_ESP_SDR_SDIO_ENABLED
#include "sdio_link.h"
#endif

#ifndef CONFIG_ESP_SDR_UART_BAUD
#define CONFIG_ESP_SDR_UART_BAUD 2000000
#endif

#define COMMAND_SIZE 128
static burst_serial_port_t active_port = BURST_SERIAL_USB;
static unsigned next_port;
static unsigned uart_baud = CONFIG_ESP_SDR_UART_BAUD;
static struct {
    char line[COMMAND_SIZE];
    size_t used;
    bool overflow;
    int64_t last_byte;
} input[BURST_SERIAL_COUNT];
static int64_t last_activity;   /* last command or payload; idle polling stays tight for a while after it */

/* Transport commands are consumed here for every receiver backend. The ACK
 * is completely transmitted at the old rate before changing UART0. The rate
 * is session-only; every boot starts with CONFIG_ESP_SDR_UART_BAUD. */
static bool baud_command(const char *line) {
    if (strcmp(line, "BAUD?") && strcmp(line, "BAUD") && strncmp(line, "BAUD ", 5)) return false;
#if CONFIG_ESP_SDR_UART_ENABLED
    if (active_port == BURST_SERIAL_UART) {
        char response[32];
        if (!strcmp(line, "BAUD?")) {
            int length = snprintf(response, sizeof(response), "BAUD %u\n", uart_baud);
            burst_serial_send(response, length);
            return true;
        }
        unsigned baud = !strcmp(line, "BAUD 1000000") ? 1000000 :
                        !strcmp(line, "BAUD 2000000") ? 2000000 : 0;
        if (!baud) {
            burst_serial_send("ERR baud_args\n", sizeof("ERR baud_args\n")-1);
            return true;
        }
        int length = snprintf(response, sizeof(response), "OK BAUD %u\n", baud);
        if (!burst_serial_send(response, length) ||
            uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(1000)) != ESP_OK ||
            uart_set_baudrate(UART_NUM_0, baud) != ESP_OK) {
            return true;
        }
        uart_baud = baud;
        uart_flush_input(UART_NUM_0);
        memset(&input[BURST_SERIAL_UART], 0, sizeof(input[BURST_SERIAL_UART]));
        return true;
    }
#endif
    burst_serial_send("ERR baud_transport\n", sizeof("ERR baud_transport\n")-1);
    return true;
}

#if CONFIG_IDF_TARGET_ESP32S2
static void usb_ready(void *arg) { (void)arg; }
#endif
void burst_serial_init(void) {
#if CONFIG_IDF_TARGET_ESP32S2
    ESP_ERROR_CHECK(esp_usb_console_set_cb(usb_ready,usb_ready,NULL));
#endif
#if CONFIG_ESP_SDR_UART_ENABLED
    uart_baud = CONFIG_ESP_SDR_UART_BAUD;
    const uart_config_t config = {
        .baud_rate = uart_baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, CONFIG_ESP_SDR_UART_TX_PIN,
                                CONFIG_ESP_SDR_UART_RX_PIN,
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* Interrupt-driven RX keeps uploads buffered while the command is parsed.
     * TX uses the FIFO directly so all waits have a bounded deadline. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 8192, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_flush_input(UART_NUM_0));
#endif
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if (sdio_link_init()) active_port = BURST_SERIAL_SDIO;
#endif
}

burst_serial_port_t burst_serial_port(void) { return active_port; }
unsigned burst_serial_baud(void) {
    return active_port == BURST_SERIAL_UART ? uart_baud : 0;
}

static int read_port(burst_serial_port_t port, void *buffer, size_t size) {
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if (port == BURST_SERIAL_SDIO) return sdio_link_read(buffer, size);
#endif
    if (port == BURST_SERIAL_UART) {
#if CONFIG_ESP_SDR_UART_ENABLED
        return uart_read_bytes(UART_NUM_0, buffer, size, 0);
#else
        return 0;
#endif
    }
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    return usb_serial_jtag_ll_read_rxfifo(buffer, size > 64 ? 64 : size);
#elif CONFIG_IDF_TARGET_ESP32S2
    return esp_usb_console_read_buf(buffer,size);
#else
    return 0;
#endif
}

bool burst_serial_stop_requested(void) {
    char c; bool stopped=false;
    for(unsigned j=0;j<128 && read_port(active_port,&c,1)==1;j++) {
        stopped=true;
        if(c=='\n') break;
    }
    return stopped;
}

/* Helpers for long-running commands that must keep receiving while they
 * send (continuous capture, streaming). Active port only. */

/* Non-blocking: returns the number of bytes the hardware FIFO accepted. */
int IRAM_ATTR burst_serial_try_send(const void *data, size_t size) {
    const uint8_t *p = data;
    size_t done = 0;
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if (active_port == BURST_SERIAL_SDIO) return sdio_link_write(p, size);
#endif
    if (active_port == BURST_SERIAL_UART) {
#if CONFIG_ESP_SDR_UART_ENABLED
        int sent = uart_tx_chars(UART_NUM_0, (const char *)p, size > 128 ? 128 : size);
        return sent < 0 ? 0 : sent;
#else
        return 0;
#endif
    }
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    while (done < size && usb_serial_jtag_ll_txfifo_writable()) {
        size_t chunk = size - done > 64 ? 64 : size - done;
        int sent = usb_serial_jtag_ll_write_txfifo(p + done, chunk);
        usb_serial_jtag_ll_txfifo_flush();
        if (sent <= 0) break;
        done += sent;
    }
#endif
    return (int)done;
}

/* Interrupt-driven native-USB transmit from a caller-owned byte ring (single
 * producer = main loop, single consumer = ISR). Keeps the 64-byte USB FIFO
 * refilled while the CPU runs DSP code. UART callers poll try_send instead. */
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "esp_intr_alloc.h"
#include "soc/interrupts.h"
static intr_handle_t ring_isr;
static const uint8_t *ring_buf;
static unsigned ring_size;
static volatile unsigned *ring_head, *ring_tail;
static void IRAM_ATTR ring_fill(void) {
    unsigned head = *ring_head, tail = *ring_tail;
    if (head == tail || !usb_serial_jtag_ll_txfifo_writable()) return;
    unsigned count = head - tail, offset = tail % ring_size;
    if (count > 64) count = 64;
    if (count > ring_size - offset) count = ring_size - offset;
    int sent = usb_serial_jtag_ll_write_txfifo(ring_buf + offset, count);
    usb_serial_jtag_ll_txfifo_flush();
    *ring_tail = tail + (unsigned)sent;
}
static void IRAM_ATTR ring_tx_isr(void *arg) {
    (void)arg;
    uint32_t status = usb_serial_jtag_ll_get_intsts_mask();
    if (!(status & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY)) return;
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    ring_fill();
    if (*ring_head == *ring_tail) usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
}
#endif
bool burst_serial_ring_start(const uint8_t *buf, unsigned size, volatile unsigned *head, volatile unsigned *tail) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (active_port != BURST_SERIAL_USB) return false;
    ring_buf = buf; ring_size = size; ring_head = head; ring_tail = tail;
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    if (esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, ESP_INTR_FLAG_IRAM, ring_tx_isr, NULL, &ring_isr) != ESP_OK) {
        ring_isr = NULL;
        return false;
    }
    return true;
#else
    (void)buf; (void)size; (void)head; (void)tail;
    return false;
#endif
}
void IRAM_ATTR burst_serial_ring_kick(void) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (!ring_isr) return;
    /* The IN-empty status is an event: prime the FIFO here, then let the ISR
     * continue once the host has taken the packet. */
    unsigned irq = portSET_INTERRUPT_MASK_FROM_ISR();
    ring_fill();
    if (*ring_head != *ring_tail) usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);
#endif
}
void burst_serial_ring_stop(void) {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (!ring_isr) return;
    usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    esp_intr_free(ring_isr);
    ring_isr = NULL;
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
#endif
}

/* Non-blocking line reader for long-running commands: true once a complete
 * line is in buf (NUL-terminated, carriage returns dropped). */
bool burst_serial_poll_run_line(char *buf, size_t capacity) {
    static char acc[COMMAND_SIZE];
    static size_t used;
    char ch;
    for (unsigned k = 0; k < 64; ++k) {
        if (read_port(active_port, &ch, 1) != 1) return false;
        if (ch == '\r') continue;
        if (ch != '\n') { if (used < sizeof(acc) - 1) acc[used++] = ch; continue; }
        size_t n = used < capacity - 1 ? used : capacity - 1;
        memcpy(buf, acc, n);
        buf[n] = '\0';
        used = 0;
        return true;
    }
    return false;
}

/* Discard the rest of a stop request and any partial command line. */
void burst_serial_drain_stop(void) {
    char ch;
    int64_t end = esp_timer_get_time() + 20000;
    while (esp_timer_get_time() < end) {
        if (read_port(active_port, &ch, 1) == 1) end = esp_timer_get_time() + 20000;
        else break;
    }
    memset(&input[active_port], 0, sizeof(input[active_port]));
}

int burst_serial_read_raw(void *buf, size_t size) {
    return read_port(active_port, buf, size);
}

void burst_serial_hold(bool on) {
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if (active_port == BURST_SERIAL_SDIO) sdio_link_hold(on);
#else
    (void)on;
#endif
}

/* Idle step of the command loop. The SDIO link has no interrupt handler, so
 * keep polling while output is still queued or shortly after traffic (the
 * next command of a burst then waits microseconds, not a tick); otherwise
 * sleep a tick as before. */
void burst_serial_idle(void) {
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if (active_port == BURST_SERIAL_SDIO) {
        sdio_link_poll();
        if (sdio_link_tx_pending() || esp_timer_get_time() - last_activity < 200000) { taskYIELD(); return; }
    }
#endif
    vTaskDelay(1);
}

int burst_serial_poll_line(char *line, size_t capacity) {
#if CONFIG_ESP_SDR_SDIO_ENABLED
    sdio_link_poll();
#endif
    for (unsigned j = 0; j < BURST_SERIAL_COUNT; ++j) {
        unsigned port = (next_port + j) % BURST_SERIAL_COUNT;
        int64_t now = esp_timer_get_time();
        if (now - input[port].last_byte > 3000000) {
            input[port].used = 0;
            input[port].overflow = false;
        }
        /* Bound polling so an unterminated command cannot starve the peer. */
        for (unsigned k = 0; k < COMMAND_SIZE; ++k) {
            char ch;
            if (read_port(port, &ch, 1) != 1) break;
            input[port].last_byte = now;
            if (ch == '\r') continue;
            if (ch != '\n') {
                if (input[port].used < COMMAND_SIZE - 1)
                    input[port].line[input[port].used++] = ch;
                else input[port].overflow = true;
                continue;
            }
            size_t used = input[port].used;
            bool overflow = input[port].overflow;
            input[port].used = 0;
            input[port].overflow = false;
            if (!used && !overflow) continue;
            active_port = port;
            next_port = (port + 1) % BURST_SERIAL_COUNT;
            last_activity = now;
            if (overflow || used >= capacity) return -1;
            memcpy(line, input[port].line, used);
            line[used] = '\0';
            if (baud_command(line)) return 0;
            return 1;
        }
    }
    return 0;
}

static int64_t transfer_deadline(size_t size) {
    /* 8N1 needs ten wire bits per byte. Leave two seconds for host scheduling,
     * including full frames at a deliberately reduced UART baud rate. */
    int64_t timeout = active_port == BURST_SERIAL_UART
        ? 2000000 + (int64_t)size * 10000000 / uart_baud
        : 3000000;
    return esp_timer_get_time() + timeout;
}

bool IRAM_ATTR burst_serial_send(const void *data, size_t size) {
    const uint8_t *p = data;
    size_t original = size;
    int64_t deadline = transfer_deadline(size);
    last_activity = esp_timer_get_time();
    while (size) {
        if (esp_timer_get_time() >= deadline) return false;
        int sent;
#if CONFIG_ESP_SDR_SDIO_ENABLED
        if (active_port == BURST_SERIAL_SDIO) {
            sent = sdio_link_write(p, size);
            /* Buffers come back as soon as the host has read them: keep
             * polling instead of sleeping a tick. The deadline bounds it. */
            if (!sent) { sdio_link_poll(); taskYIELD(); }
        } else
#endif
        if (active_port == BURST_SERIAL_UART) {
            sent = uart_tx_chars(UART_NUM_0, (const char *)p, size > 128 ? 128 : size);
            if (sent < 0) return false;
            if (!sent) vTaskDelay(1);
        } else {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
            if (!usb_serial_jtag_ll_txfifo_writable()) continue;
            sent = usb_serial_jtag_ll_write_txfifo(p, size > 64 ? 64 : size);
            usb_serial_jtag_ll_txfifo_flush();
#elif CONFIG_IDF_TARGET_ESP32S2

            sent=esp_usb_console_write_buf((const char *)p,size>64?64:size);
            if(sent<0)return false;
            if(!sent)taskYIELD();
            else esp_usb_console_flush();
#else
            return false;
#endif
        }
        p += sent;
        size -= sent;
    }
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if (active_port == BURST_SERIAL_SDIO) { sdio_link_flush(); return true; }
#endif
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if (active_port == BURST_SERIAL_USB && original && original % 64 == 0) {
        while (!usb_serial_jtag_ll_txfifo_writable())
            if (esp_timer_get_time() >= deadline) return false;
        usb_serial_jtag_ll_txfifo_flush();
    }
#else
    (void)original;
#endif
    return true;
}
