/*
 * Copyright (c) 2016 Nordic Semiconductor ASA
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/usb/usb_device.h>

#include <zephyr/net/buf.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/l2cap.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_raw.h>

#include <nrfx_timer.h>
#include "audio_sync_timer.h"

#define LOG_MODULE_NAME hci_uart
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

static const struct device *const hci_uart_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_c2h_uart));
static K_THREAD_STACK_DEFINE(tx_thread_stack, CONFIG_BT_HCI_TX_STACK_SIZE);
static struct k_thread tx_thread_data;
static K_FIFO_DEFINE(tx_queue);

/* RX in terms of bluetooth communication */
static K_FIFO_DEFINE(uart_tx_queue);

#define H4_CMD 0x01
#define H4_ACL 0x02
#define H4_SCO 0x03
#define H4_EVT 0x04
#define H4_ISO 0x05

/* Receiver states. */
#define ST_IDLE 0	/* Waiting for packet type. */
#define ST_HDR 1	/* Receiving packet header. */
#define ST_PAYLOAD 2	/* Receiving packet payload. */
#define ST_DISCARD 3	/* Dropping packet. */

/* Length of a discard/flush buffer.
 * This is sized to align with a BLE HCI packet:
 * 1 byte H:4 header + 32 bytes ACL/event data
 * Bigger values might overflow the stack since this is declared as a local
 * variable, smaller ones will force the caller to call into discard more
 * often.
 */
#define H4_DISCARD_LEN 33

static int h4_read(const struct device *uart, uint8_t *buf, size_t len)
{
	int rx = uart_fifo_read(uart, buf, len);

	LOG_DBG("read %d req %d", rx, len);

	return rx;
}

static bool valid_type(uint8_t type)
{
	return (type == H4_CMD) | (type == H4_ACL) | (type == H4_ISO);
}

/* Function expects that type is validated and only CMD, ISO or ACL will be used. */
static uint32_t get_len(const uint8_t *hdr_buf, uint8_t type)
{
	switch (type) {
	case H4_CMD:
		return ((const struct bt_hci_cmd_hdr *)hdr_buf)->param_len;
	case H4_ISO:
		return bt_iso_hdr_len(
			sys_le16_to_cpu(((const struct bt_hci_iso_hdr *)hdr_buf)->len));
	case H4_ACL:
		return sys_le16_to_cpu(((const struct bt_hci_acl_hdr *)hdr_buf)->len);
	default:
		LOG_ERR("Invalid type: %u", type);
		return 0;
	}
}

/* Function expects that type is validated and only CMD, ISO or ACL will be used. */
static int hdr_len(uint8_t type)
{
	switch (type) {
	case H4_CMD:
		return sizeof(struct bt_hci_cmd_hdr);
	case H4_ISO:
		return sizeof(struct bt_hci_iso_hdr);
	case H4_ACL:
		return sizeof(struct bt_hci_acl_hdr);
	default:
		LOG_ERR("Invalid type: %u", type);
		return 0;
	}
}

static void rx_isr(void)
{
	static struct net_buf *buf;
	static int remaining;
	static uint8_t state;
	static uint8_t type;
	static uint8_t hdr_buf[MAX(sizeof(struct bt_hci_cmd_hdr),
			sizeof(struct bt_hci_acl_hdr))];
	int read;

	do {
		switch (state) {
		case ST_IDLE:
			/* Get packet type */
			read = h4_read(hci_uart_dev, &type, sizeof(type));
			/* since we read in loop until no data is in the fifo,
			 * it is possible that read = 0.
			 */
			if (read) {
				if (valid_type(type)) {
					/* Get expected header size and switch
					 * to receiving header.
					 */
					remaining = hdr_len(type);
					state = ST_HDR;
				} else {
					LOG_WRN("Unknown header %d", type);
				}
			}
			break;
		case ST_HDR:
			read = h4_read(hci_uart_dev,
				       &hdr_buf[hdr_len(type) - remaining],
				       remaining);
			remaining -= read;
			if (remaining == 0) {
				/* Header received. Allocate buffer and get
				 * payload length. If allocation fails leave
				 * interrupt. On failed allocation state machine
				 * is reset.
				 */
				buf = bt_buf_get_tx(BT_BUF_H4, K_NO_WAIT,
						    &type, sizeof(type));
				if (!buf) {
					LOG_ERR("No available command buffers!");
					state = ST_IDLE;
					return;
				}

				remaining = get_len(hdr_buf, type);

				net_buf_add_mem(buf, hdr_buf, hdr_len(type));
				if (remaining > net_buf_tailroom(buf)) {
					LOG_ERR("Not enough space in buffer");
					net_buf_unref(buf);
					state = ST_DISCARD;
				} else {
					state = ST_PAYLOAD;
				}

			}
			break;
		case ST_PAYLOAD:
			read = h4_read(hci_uart_dev, net_buf_tail(buf),
				       remaining);
			buf->len += read;
			remaining -= read;
			if (remaining == 0) {
				/* Packet received */
				LOG_DBG("putting RX packet in queue.");
				net_buf_put(&tx_queue, buf);
				state = ST_IDLE;
			}
			break;
		case ST_DISCARD:
		{
			uint8_t discard[H4_DISCARD_LEN];
			size_t to_read = MIN(remaining, sizeof(discard));

			read = h4_read(hci_uart_dev, discard, to_read);
			remaining -= read;
			if (remaining == 0) {
				state = ST_IDLE;
			}

			break;

		}
		default:
			read = 0;
			__ASSERT_NO_MSG(0);
			break;

		}
	} while (read);
}

static void tx_isr(void)
{
	static struct net_buf *buf;
	int len;

	if (!buf) {
		buf = net_buf_get(&uart_tx_queue, K_NO_WAIT);
		if (!buf) {
			uart_irq_tx_disable(hci_uart_dev);
			return;
		}
	}

	len = uart_fifo_fill(hci_uart_dev, buf->data, buf->len);
	net_buf_pull(buf, len);
	if (!buf->len) {
		net_buf_unref(buf);
		buf = NULL;
	}
}

static void bt_uart_isr(const struct device *unused, void *user_data)
{
	ARG_UNUSED(unused);
	ARG_UNUSED(user_data);

	if (!(uart_irq_rx_ready(hci_uart_dev) ||
	      uart_irq_tx_ready(hci_uart_dev))) {
		LOG_DBG("spurious interrupt");
	}

	if (uart_irq_tx_ready(hci_uart_dev)) {
		tx_isr();
	}

	if (uart_irq_rx_ready(hci_uart_dev)) {
		rx_isr();
	}
}

static void tx_thread(void *p1, void *p2, void *p3)
{
	while (1) {
		struct net_buf *buf;
		int err;

		/* Wait until a buffer is available */
		buf = net_buf_get(&tx_queue, K_FOREVER);
		/* Pass buffer to the stack */
		err = bt_send(buf);
		if (err!=BT_HCI_ERR_SUCCESS) {
			if (err!=BT_HCI_ERR_EXT_HANDLED) {
			    LOG_ERR("Unable to send (err %d)", err);
            }
			net_buf_unref(buf);
		}

		/* Give other threads a chance to run if tx_queue keeps getting
		 * new data all the time.
		 */
		k_yield();
	}
}

static int h4_send(struct net_buf *buf)
{
	LOG_DBG("buf %p type %u len %u", buf, bt_buf_get_type(buf),
		    buf->len);

	net_buf_put(&uart_tx_queue, buf);
	uart_irq_tx_enable(hci_uart_dev);

	return 0;
}

#if defined(CONFIG_BT_CTLR_ASSERT_HANDLER)
void bt_ctlr_assert_handle(char *file, uint32_t line)
{
	uint32_t len = 0U, pos = 0U;

	/* Disable interrupts, this is unrecoverable */
	(void)irq_lock();

	uart_irq_rx_disable(hci_uart_dev);
	uart_irq_tx_disable(hci_uart_dev);

	if (file) {
		while (file[len] != '\0') {
			if (file[len] == '/') {
				pos = len + 1;
			}
			len++;
		}
		file += pos;
		len -= pos;
	}

	uart_poll_out(hci_uart_dev, H4_EVT);
	/* Vendor-Specific debug event */
	uart_poll_out(hci_uart_dev, 0xff);
	/* 0xAA + strlen + \0 + 32-bit line number */
	uart_poll_out(hci_uart_dev, 1 + len + 1 + 4);
	uart_poll_out(hci_uart_dev, 0xAA);

	if (len) {
		while (*file != '\0') {
			uart_poll_out(hci_uart_dev, *file);
			file++;
		}
		uart_poll_out(hci_uart_dev, 0x00);
	}

	uart_poll_out(hci_uart_dev, line >> 0 & 0xff);
	uart_poll_out(hci_uart_dev, line >> 8 & 0xff);
	uart_poll_out(hci_uart_dev, line >> 16 & 0xff);
	uart_poll_out(hci_uart_dev, line >> 24 & 0xff);

	while (1) {
	}
}
#endif /* CONFIG_BT_CTLR_ASSERT_HANDLER */

static int hci_uart_init(void)
{
	LOG_DBG("");

	if (IS_ENABLED(CONFIG_USB_CDC_ACM)) {
		if (usb_enable(NULL)) {
			LOG_ERR("Failed to enable USB");
			return -EINVAL;
		}
	}

	if (!device_is_ready(hci_uart_dev)) {
		LOG_ERR("HCI UART %s is not ready", hci_uart_dev->name);
		return -EINVAL;
	}

	uart_irq_rx_disable(hci_uart_dev);
	uart_irq_tx_disable(hci_uart_dev);

	uart_irq_callback_set(hci_uart_dev, bt_uart_isr);

	uart_irq_rx_enable(hci_uart_dev);

	return 0;
}

SYS_INIT(hci_uart_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

#ifdef CONFIG_AUDIO_SYNC_TIMER_USES_RTC
#define TIMESYNC_GPIO  DT_NODELABEL(timesync)

#if DT_NODE_HAS_STATUS(TIMESYNC_GPIO, okay)
static const struct gpio_dt_spec timesync_pin = GPIO_DT_SPEC_GET(TIMESYNC_GPIO, gpios);
#else
#error "No timesync gpio available!"
#endif

#define ALTERNATE_TOGGLE_GPIO DT_NODELABEL(alternate_toggle)
#if DT_NODE_HAS_STATUS(ALTERNATE_TOGGLE_GPIO, okay)
static const struct gpio_dt_spec alternate_toggle_pin = GPIO_DT_SPEC_GET(ALTERNATE_TOGGLE_GPIO, gpios);
#else
#warning "No alternate toggle gpio availabe!"
#endif

#define  HCI_CMD_ISO_TIMESYNC	(0x200)

struct hci_cmd_iso_timestamp_response {
    struct bt_hci_evt_cc_status cc;
    uint32_t timestamp;
} __packed;

uint8_t hci_cmd_iso_timesync_cb(struct net_buf *buf)
{
	struct net_buf *rsp;
    struct hci_cmd_iso_timestamp_response *response;

	LOG_INF("buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);
	LOG_INF("buf[0] = 0x%02x", buf->data[0]);

	rsp = bt_hci_cmd_complete_create(BT_OP(BT_OGF_VS, HCI_CMD_ISO_TIMESYNC), sizeof(*response));
	response = net_buf_add(rsp, sizeof(*response));
	response->cc.status = BT_HCI_ERR_SUCCESS;
    response->timestamp = audio_sync_timer_capture();

#if DT_NODE_HAS_STATUS(TIMESYNC_GPIO, okay)
	gpio_pin_toggle_dt( &timesync_pin );
#endif

	if (IS_ENABLED(CONFIG_BT_HCI_RAW_H4)) {
		net_buf_push_u8(rsp, H4_EVT);
	}

    h4_send( rsp );

	return BT_HCI_ERR_EXT_HANDLED;
}
#endif

#define TIME_TO_WAIT_MS 1000
#define SYNC_TOGGLE_TIMER_INSTANCE_NUMBER 2
static const nrfx_timer_t sync_toggle_timer_instance =
	NRFX_TIMER_INSTANCE(SYNC_TOGGLE_TIMER_INSTANCE_NUMBER);

static nrfx_timer_config_t cfg = {.frequency = NRFX_MHZ_TO_HZ(1UL),
				  .mode = NRF_TIMER_MODE_TIMER,
				  .bit_width = NRF_TIMER_BIT_WIDTH_32,
				  .interrupt_priority = NRFX_TIMER_DEFAULT_CONFIG_IRQ_PRIORITY,
				  .p_context = NULL};

static void sync_toggle_timer_isr_handler(nrf_timer_event_t event_type, void *p_context)
{
    ARG_UNUSED(p_context);
    if(event_type == NRF_TIMER_EVENT_COMPARE1)
    {
        char * p_msg = p_context;
        uint32_t remainder_us = nrf_timer_cc_get(NRF_TIMER2,
						 NRF_TIMER_CC_CHANNEL1);

#if DT_NODE_HAS_STATUS(ALTERNATE_TOGGLE_GPIO, okay)
        gpio_pin_toggle_dt( &alternate_toggle_pin );
#endif

        LOG_INF("%d\n", remainder_us );
        printf("Timer finished. Context passed to the handler: >%s<", p_msg);
    }
}

int main(void)
{
    // timer setup
    nrfx_err_t ret;
    ret = nrfx_timer_init(&sync_toggle_timer_instance, &cfg, sync_toggle_timer_isr_handler);
    if (ret - NRFX_ERROR_BASE_NUM) {
        LOG_ERR("nrfx timer init error: %d", ret);
        return -ENODEV;
    }
    uint32_t desired_ticks = nrfx_timer_ms_to_ticks(&sync_toggle_timer_instance, TIME_TO_WAIT_MS);
    nrfx_timer_compare(&sync_toggle_timer_instance, NRF_TIMER_CC_CHANNEL1, desired_ticks, true);
    IRQ_CONNECT(NRFX_IRQ_NUMBER_GET(NRF_TIMER_INST_GET(SYNC_TOGGLE_TIMER_INSTANCE_NUMBER)), IRQ_PRIO_LOWEST,
            NRFX_TIMER_INST_HANDLER_GET(SYNC_TOGGLE_TIMER_INSTANCE_NUMBER), 0, 0);
    nrfx_timer_enable(&sync_toggle_timer_instance);

	/* incoming events and data from the controller */
	static K_FIFO_DEFINE(rx_queue);
	int err;

	LOG_DBG("Start");
	__ASSERT(hci_uart_dev, "UART device is NULL");

	/* Enable the raw interface, this will in turn open the HCI driver */
	bt_enable_raw(&rx_queue);

	if (IS_ENABLED(CONFIG_BT_WAIT_NOP)) {
		/* Issue a Command Complete with NOP */
		int i;

		const struct {
			const uint8_t h4;
			const struct bt_hci_evt_hdr hdr;
			const struct bt_hci_evt_cmd_complete cc;
		} __packed cc_evt = {
			.h4 = H4_EVT,
			.hdr = {
				.evt = BT_HCI_EVT_CMD_COMPLETE,
				.len = sizeof(struct bt_hci_evt_cmd_complete),
			},
			.cc = {
				.ncmd = 1,
				.opcode = sys_cpu_to_le16(BT_OP_NOP),
			},
		};

		for (i = 0; i < sizeof(cc_evt); i++) {
			uart_poll_out(hci_uart_dev,
				      *(((const uint8_t *)&cc_evt)+i));
		}
	}

#if DT_NODE_HAS_STATUS(ALTERNATE_TOGGLE_GPIO, okay)
    gpio_pin_configure_dt(&alternate_toggle_pin, GPIO_OUTPUT_INACTIVE);
#endif

#ifdef CONFIG_AUDIO_SYNC_TIMER_USES_RTC
	/* Register iso_timesync command */
	static struct bt_hci_raw_cmd_ext cmd_list = {
	    .op = BT_OP(BT_OGF_VS, HCI_CMD_ISO_TIMESYNC),
		.min_len = 1,
		.func = hci_cmd_iso_timesync_cb
	};

#if DT_NODE_HAS_STATUS(TIMESYNC_GPIO, okay)
	gpio_pin_configure_dt(&timesync_pin, GPIO_OUTPUT_INACTIVE);
#endif

	bt_hci_raw_cmd_ext_register(&cmd_list, 1);
#endif

	/* Spawn the TX thread and start feeding commands and data to the
	 * controller
	 */
	k_thread_create(&tx_thread_data, tx_thread_stack,
			K_THREAD_STACK_SIZEOF(tx_thread_stack), tx_thread,
			NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&tx_thread_data, "HCI uart TX");

#if 0
    while (1) {
		uint8_t c = 'A';
		uart_fifo_fill(hci_uart_dev, &c, 1);
		uart_irq_tx_enable(hci_uart_dev);
		k_sleep( K_MSEC(100) );
	}
    return 0;
#endif

    while (1) {
		struct net_buf *buf;

		buf = net_buf_get(&rx_queue, K_FOREVER);
		err = h4_send(buf);
		if (err) {
			LOG_ERR("Failed to send");
		}
	}
	return 0;
}
