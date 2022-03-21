/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <logging/log.h>

#include <drivers/uart.h>
#include <usb/usb_device.h>

#include <openthread/thread.h>
//#include <coap_server_client_interface.h>
#include <net/coap_utils.h>
#include <net/openthread.h>
#include <net/socket.h>

#include <drivers/sensor.h>

#define MY_STACK_SIZE 2048
#define MY_PRIORITY 10

extern void process_sensor_data(void *, void *, void *);

K_THREAD_STACK_DEFINE(my_stack_area, MY_STACK_SIZE);
struct k_thread my_thread_data;

k_tid_t my_tid;

#define RESPONSE_POLL_PERIOD 100

static bool is_connected;

#define COAP_PORT 5683

static struct sockaddr_in6 unique_local_addr = {
	.sin6_family = AF_INET6,
	.sin6_port = htons(COAP_PORT),
	.sin6_addr.s6_addr = {0xfd, 0x4d, 0xea, 0xfc, 0x6f, 0x32, 0xc7, 0xcf, 0x00, 0x00, 0x00, 0xff, 0xfe, 0x00, 0xfc, 0x11},
	.sin6_scope_id = 0U
};

static const char *const temp_option[] = { "ambient", 0 };

LOG_MODULE_REGISTER(cli_sample, CONFIG_OT_SENSOR_HTS_LOG_LEVEL);

static void on_thread_state_changed(uint32_t flags, void *context);

void coap_client_utils_init()
{
	coap_init(AF_INET6, NULL);

	openthread_set_state_changed_cb(on_thread_state_changed);
	openthread_start(openthread_get_default_context());
}

static void on_thread_state_changed(uint32_t flags, void *context)
{
	struct openthread_context *ot_context = context;

	if (flags & OT_CHANGED_THREAD_ROLE) {
		switch (otThreadGetDeviceRole(ot_context->instance)) {
		case OT_DEVICE_ROLE_CHILD:
		case OT_DEVICE_ROLE_ROUTER:
		case OT_DEVICE_ROLE_LEADER:
			is_connected = true;
			k_wakeup(my_tid);
			break;

		case OT_DEVICE_ROLE_DISABLED:
		case OT_DEVICE_ROLE_DETACHED:
		default:
			is_connected = false;
			break;
		}
	}
}

void process_sensor_data(void* arg1, void* arg2, void* arg3) {
	const struct device *dev = device_get_binding("HTS221");
	struct sensor_value temp, hum;
	uint8_t payload[5] = {0};

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	while(1) {
		if (!is_connected) {
			k_sleep(K_FOREVER);
		}
		if (sensor_sample_fetch(dev) < 0) {
			LOG_ERR("Sensor sample update error");
			return;
		}

		if (sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp) < 0) {
			LOG_ERR("Cannot read HTS221 temperature channel");
			return;
		}

		if (sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &hum) < 0) {
			LOG_ERR("Cannot read HTS221 humidity channel");
			return;
		}

		/* display temperature */
		//LOG_INF("Temperature:%d %d C", (temp.val1), (temp.val2));
		payload[0] = temp.val1;
		payload[1] = temp.val2;
		payload[3] = hum.val1;
		payload[4] = hum.val2;

		/* display humidity */
		//LOG_INF("Relative Humidity:%d %d", (hum.val1), (hum.val2));
		LOG_INF("Sending coap");
		if (coap_send_request(COAP_METHOD_PUT,
			(const struct sockaddr *)&unique_local_addr,
			temp_option, payload, sizeof(payload), NULL) < 0) {
				LOG_ERR("Failed sending coap.");
		} else {
			LOG_INF("COAP send.");
		}
		k_msleep(10000);
	}
}

void main(void)
{
	coap_client_utils_init();
	my_tid = k_thread_create(&my_thread_data, my_stack_area,
                                 K_THREAD_STACK_SIZEOF(my_stack_area),
                                 process_sensor_data,
                                 NULL, NULL, NULL,
                                 MY_PRIORITY, 0, K_NO_WAIT);
#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_shell_uart), zephyr_cdc_acm_uart)
	int ret;
	const struct device *dev;
	uint32_t dtr = 0U;

	ret = usb_enable(NULL);
	if (ret != 0) {
		LOG_ERR("Failed to enable USB");
		return;
	}

	dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
	if (dev == NULL) {
		LOG_ERR("Failed to find specific UART device");
		return;
	}

	LOG_INF("Waiting for host to be ready to communicate");
	LOG_INF("Waiting for host to be ready to communicate");

	/* Data Terminal Ready - check if host is ready to communicate */
	while (!dtr) {
		ret = uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
		if (ret) {
			LOG_ERR("Failed to get Data Terminal Ready line state: %d",
				ret);
			continue;
		}
		k_msleep(100);
	}

	(void)uart_line_ctrl_set(dev, UART_LINE_CTRL_DCD, 1);
	(void)uart_line_ctrl_set(dev, UART_LINE_CTRL_DSR, 1);
#endif
}
