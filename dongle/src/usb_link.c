#include "usb_link.h"
#include "protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>

#include <string.h>

/*
 * Exactly one CDC-ACM instance must exist. The board's common devicetree
 * (boards/common/usb/cdc_acm_serial.dtsi) already declares one and points
 * zephyr,console at it; we deliberately do NOT add a second in an overlay.
 *
 * If this assert fires, something added another instance — the dongle would
 * enumerate as two COM ports and DEVICE_DT_GET_ONE would silently bind to
 * whichever happened to be first. That is precisely the trap the upstream
 * cdc_acm sample falls into on this board.
 */
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(zephyr_cdc_acm_uart) == 1,
	     "Expected exactly one CDC-ACM instance: the protocol owns the port");

static const struct device *const cdc_dev =
	DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

#define RX_RING_SIZE 512
#define TX_RING_SIZE 1024
#define CHUNK_SIZE   64

RING_BUF_DECLARE(rx_rb, RX_RING_SIZE);
RING_BUF_DECLARE(tx_rb, TX_RING_SIZE);

static struct proto_asm assembler;
static usb_link_line_fn line_handler;
static struct k_work rx_work;
static struct k_work_q *rx_queue;

/* Whole lines refused, either over-length or with no room in the ring. Read
 * back by the engine and reported in the handshake LOG line. */
static uint32_t tx_drops;

/* Serialises producers into tx_rb. The consumer is the ISR; ring_buf is safe
 * for one producer and one consumer, so all we need is to keep producers from
 * overlapping each other. Never taken in ISR context. */
static K_MUTEX_DEFINE(tx_lock);

/* ------------------------------------------------------------------------ */

static void on_assembled_line(char *line, void *user)
{
	ARG_UNUSED(user);

	if (line_handler != NULL) {
		line_handler(line);
	}
}

static void rx_work_handler(struct k_work *work)
{
	uint8_t buf[CHUNK_SIZE];
	uint32_t n;

	ARG_UNUSED(work);

	while ((n = ring_buf_get(&rx_rb, buf, sizeof(buf))) > 0) {
		proto_asm_feed(&assembler, buf, n, on_assembled_line, NULL);
	}
}

static void cdc_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
		if (uart_irq_rx_ready(dev)) {
			uint8_t buf[CHUNK_SIZE];
			int n = uart_fifo_read(dev, buf, sizeof(buf));

			if (n > 0) {
				/* Overflow drops bytes; the assembler resynchronises
				 * at the next terminator. */
				(void)ring_buf_put(&rx_rb, buf, (uint32_t)n);
				(void)k_work_submit_to_queue(rx_queue, &rx_work);
			}
		}

		if (uart_irq_tx_ready(dev)) {
			uint8_t *data;
			uint32_t claimed = ring_buf_get_claim(&tx_rb, &data, CHUNK_SIZE);

			if (claimed == 0) {
				(void)ring_buf_get_finish(&tx_rb, 0);
				uart_irq_tx_disable(dev);
			} else {
				int sent = uart_fifo_fill(dev, data, (int)claimed);

				/* Consume only what the FIFO actually took. */
				(void)ring_buf_get_finish(&tx_rb, sent > 0 ? (uint32_t)sent : 0);
			}
		}
	}
}

/* ------------------------------------------------------------------------ */

int usb_link_send(const char *line)
{
	size_t len = strlen(line);
	bool queued;

	if (len == 0 || len > PROTO_MAX_CONTENT) {
		tx_drops++;
		return -EMSGSIZE;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);

	/* All-or-nothing: a half-written line would corrupt the receiver's
	 * framing until the next terminator. */
	queued = ring_buf_space_get(&tx_rb) >= len + 1u;
	if (queued) {
		(void)ring_buf_put(&tx_rb, (const uint8_t *)line, (uint32_t)len);
		(void)ring_buf_put(&tx_rb, (const uint8_t *)"\n", 1u);
	} else {
		tx_drops++;
	}

	k_mutex_unlock(&tx_lock);

	uart_irq_tx_enable(cdc_dev);

	return queued ? 0 : -ENOSPC;
}

uint32_t usb_link_tx_drops(void)
{
	return tx_drops;
}

int usb_link_init(usb_link_line_fn on_line, struct k_work_q *workq)
{
	if (workq == NULL) {
		return -EINVAL;
	}
	if (!device_is_ready(cdc_dev)) {
		return -ENODEV;
	}

	line_handler = on_line;
	rx_queue = workq;
	proto_asm_init(&assembler);
	k_work_init(&rx_work, rx_work_handler);

	uart_irq_callback_set(cdc_dev, cdc_isr);
	/*
	 * Deliberately NOT gated on DTR. The app opens the port via Web Serial
	 * and never calls setSignals(), so DTR assertion is the browser's
	 * default rather than anything the protocol guarantees. Waiting on it
	 * yields a dongle that enumerates but never answers INFO.
	 */
	uart_irq_rx_enable(cdc_dev);

	return 0;
}
