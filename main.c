#include <stdio.h>
#include <stdint.h>

#include "mem.h"
#include "osapi.h"
#include "uart.h"
#include "user_interface.h"

#include "main.h"

/* This is put here to prevent compiler warning, but its rightful place should
 * be in 'ets_sys.h'.
 */
extern void ets_isr_unmask();

static os_timer_t os_timer = {0};

/* Buffer to be passed to interrupt handler. This is not used in this case, as
 * we read the FIFO buffer directly.
 */
static volatile uint8_t *rx_buffer;

/* Line buffer */
static char rx_line[255] = {0};

/* Position in the buffer */
static uint8_t rx_line_pos = 0;

/* Indicates whether the line is considered complete */
static uint8_t rx_line_done = 0;

/* Sends a byte to UART0
 */
static ICACHE_FLASH_ATTR void uart_byte_out(uint8_t byte)
{
    /* Wait until the buffered data drops below 128 bytes before sending out
     * the byte
     */
    while (1) {
	uint8_t fifo_tx_cnt = (READ_PERI_REG(UART_STATUS(UART0)) >> UART_TXFIFO_CNT_S) & UART_TXFIFO_CNT;
	if (fifo_tx_cnt < 128)
	    break;
    }
    WRITE_PERI_REG(UART_FIFO(UART0), byte);
}

/* Sends a string of bytes to UART0
 */
static ICACHE_FLASH_ATTR void uart_str_out(char *s)
{
    char *c = s;
    while (*c)
	uart_byte_out(*c++);
}

/* Handles interrupts for UART0, as set by ETS_UART_INTR_ATTACH(..)
 */
void uart0_rx_intr_handler(void *para)
{
    /* Since this handler handles all UART interrupts, detect which one is
     * triggered.
     */
    uint32_t uart_int_status = READ_PERI_REG(UART_INT_ST(UART0));

    /* If the RX FIFO is full, read the data. The 'full' threshold is set in
     * the UART_CONF1 register.
     */
    if (UART_RXFIFO_FULL_INT_ST == (uart_int_status & UART_RXFIFO_FULL_INT_ST)) {
	/* Read the number of bytes in the RX FIFO
	 */
	uint8_t fifo_rx_cnt = (READ_PERI_REG(UART_STATUS(UART0)) >> UART_RXFIFO_CNT_S) & UART_RXFIFO_CNT;

	/* Read the byte(s) and append them to the line, up to 255 characters
	 * per line
	 */
	uint8_t i;
	for (i = 0; i < fifo_rx_cnt; ++i) {
	    uint8_t ch = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
	    /* Output the byte for good measure
	     */
	    uart_byte_out(ch);
	    /* If the user presses Enter (0x0D), consider the line to be
	     * completed.
	     */
	    if (ch == 0x0D) {
		rx_line[rx_line_pos++] = 0;
		rx_line_done = 1;
	    } else if (rx_line_pos < 255)
		rx_line[rx_line_pos++] = ch;
	}
	/* Clear the interrupt flag to signal that the interrupt has been
	 * handled
	 */
	WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
    }
}

/* Handles the timer tick
 */
static void main_on_timer(void *arg)
{
    /* Process a completed line or a line longer than 255 characters
     */
    if (rx_line_done || rx_line_pos >= 255) {
	char *s0 = &rx_line[0], *s1 = "reset";

	/* Restart the system if the 'reset' command is received; otherwise,
	 * send the line to UART.
	 */
	while (*s0 || *s1)
	    if (*s0++ != *s1++)
		break;
	if (*--s0 == *--s1)
	    system_restart();
	else {
	    uart_str_out("\r\n> ");
	    uart_str_out(rx_line);
	    uart_str_out("\r\n");
	}
	rx_line_done = 0;
	rx_line_pos = 0;
	rx_line[0] = 0;
    }
}

/* User initialisation boilerplate
 */
void ICACHE_FLASH_ATTR user_init(void)
{
    /* Set the register value for the baud rate as 'CPU frequency divided by
     * the desired baud rate'
     */
    uint32_t uart_clkdiv = (uint32_t)(system_get_cpu_freq()*1000000 / 921600);
    WRITE_PERI_REG(UART_CLKDIV(UART0), ((uart_clkdiv & UART_CLKDIV_CNT) << UART_CLKDIV_S));

    /* Configure UART with 8-N-1 (i.e. 8 data bits, no parity, 1 stop bit)
     */
    WRITE_PERI_REG(UART_CONF0(UART0), ((0x1 & UART_STOP_BIT_NUM)  << UART_STOP_BIT_NUM_S) | ((0x3 & UART_BIT_NUM) << UART_BIT_NUM_S));
    
    /* Configure the FIFO 'full' threshold to 1 byte (i.e. the 'full FIFO'
     * interrupt will be triggered when the FIFO buffer contains 1 byte of
     * data)
     */
    WRITE_PERI_REG(UART_CONF1(UART0), ((0x1 & UART_RXFIFO_FULL_THRHD) << UART_RXFIFO_FULL_THRHD_S));
    /* Enable the 'full FIFO' interrupt
     */
    WRITE_PERI_REG(UART_INT_ENA(UART0), UART_RXFIFO_FULL_INT_ENA);

    /* Set the handler for UART interrupts and enable interrupt globally
     */
    ETS_UART_INTR_ATTACH(uart0_rx_intr_handler, rx_buffer);
    ETS_UART_INTR_ENABLE();

    /* Start the main loop timer
     */
    os_timer_disarm(&os_timer);
    os_timer_setfn(&os_timer, &main_on_timer, (void *)NULL);
    os_timer_arm(&os_timer, 100, 1);
}
