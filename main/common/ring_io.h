/* Nonblocking transport access while capture masks interrupts. UART receive
 * must read hardware directly because its normal ISR cannot fill the driver
 * queue during a ring run. Ordinary commands retain the shared lease parser. */
#include "burst_serial.h"
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "soc/uart_struct.h"
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "hal/usb_serial_jtag_ll.h"
#endif
#if CONFIG_ESP_SDR_SDIO_ENABLED
#include "sdio_link.h"   /* polled on the HAL, works with interrupts masked */
#endif
static bool ring_input_available(void) {
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if(burst_serial_port()==BURST_SERIAL_SDIO)return sdio_link_rx_available();
#endif
    if(burst_serial_port()==BURST_SERIAL_UART)return uart_ll_get_rxfifo_len(&UART0)>0;
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    return usb_serial_jtag_ll_rxfifo_data_available();
#else
    return false;
#endif
}
static int ring_read_byte(uint8_t *b) {
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if(burst_serial_port()==BURST_SERIAL_SDIO)return sdio_link_read(b,1);
#endif
    if(burst_serial_port()==BURST_SERIAL_UART){
        if(!uart_ll_get_rxfifo_len(&UART0))return 0;
        uart_ll_read_rxfifo(&UART0,b,1);return 1;
    }
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    return usb_serial_jtag_ll_read_rxfifo(b,1);
#else
    return 0;
#endif
}
static int ring_write(const uint8_t *p,unsigned n) {
#if CONFIG_ESP_SDR_SDIO_ENABLED
    if(burst_serial_port()==BURST_SERIAL_SDIO){int w=sdio_link_write(p,n);sdio_link_poll();return w;}
#endif
    if(burst_serial_port()==BURST_SERIAL_UART)return uart_tx_chars(UART_NUM_0,(const char *)p,n);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    if(!usb_serial_jtag_ll_txfifo_writable())return 0;
    int written=usb_serial_jtag_ll_write_txfifo(p,n);
    usb_serial_jtag_ll_txfifo_flush();return written;
#else
    return 0;
#endif
}
