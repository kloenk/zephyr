/*
 * Copyright (c) 2026 Fiona Behrens
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT raspberrypi_usb_reset

#include "pico/bootrom.h"
#include "zephyr/sys/reboot.h"

#include <zephyr/init.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/drivers/usb/udc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbd_picotool, CONFIG_USBD_PICOTOOL_LOG_LEVEL);

// VENDOR sub-class for the reset interface
#define RESET_INTERFACE_SUBCLASS 0x00
// VENDOR protocol for the reset interface
#define RESET_INTERFACE_PROTOCOL 0x01

// CONTROL requests:

// reset to BOOTSEL
#define RESET_REQUEST_BOOTSEL 0x01
// regular flash boot
#define RESET_REQUEST_FLASH   0x02

struct usbd_picotool_reset_desc {
	struct usb_if_descriptor if0;

	struct usb_desc_header nil_desc;
};

struct usbd_picotool_reset_config {
	/* Pointer to the interface descriptor node or NULL */
	struct usbd_desc_node *const if_desc_data;
	/* Pointer to the class interface descriptor */
	struct usbd_picotool_reset_desc *const desc;
	const struct usb_desc_header **const usb_desc;
};

static void *usbd_picotool_reset_get_desc(struct usbd_class_data *c_data, const enum usbd_speed)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct usbd_picotool_reset_config *config = dev->config;

	return config->usb_desc;
}

static int usbd_picotool_control_to_dev(struct usbd_class_data *const c_data,
	const struct usb_setup_packet *const setup,
	const struct net_buf *const buf)
{
	const struct device *dev = usbd_class_get_private(c_data);
	int ret;

	LOG_DBG("got setup package: %d", setup->bRequest);

	if (setup->bRequest == RESET_REQUEST_BOOTSEL) {
		rom_reset_usb_boot_extra(-1, (setup->wValue & 0x3), false);
	}

	if (setup->bRequest == RESET_REQUEST_FLASH) {
		sys_reboot(SYS_REBOOT_COLD);
	}

	return 0;
}

static int usbd_picotool_reset_init(struct usbd_class_data *const c_data)
{
	struct usbd_context *uds_ctx = usbd_class_get_ctx(c_data);
	const struct device *dev = usbd_class_get_private(c_data);
	const struct usbd_picotool_reset_config *config = dev->config;
	struct usbd_picotool_reset_desc *desc = config->desc;

	if (config->if_desc_data != NULL && desc->if0.iInterface == 0) {
		if (usbd_add_descriptor(uds_ctx, config->if_desc_data)) {
			LOG_ERR("Failed to add interface string descriptor");
		} else {
			desc->if0.iInterface = usbd_str_desc_get_idx(config->if_desc_data);
		}
	}

	return 0;
}

struct usbd_class_api usbd_picotool_reset_api = {
	.init = usbd_picotool_reset_init,
	.control_to_dev = usbd_picotool_control_to_dev,
	.get_desc = usbd_picotool_reset_get_desc,
};

static int usbd_picotool_reset_dev_init(const struct device *dev)
{
	return 0;
}

#define PICOTOOL_RESET_DEFINE_LABEL_DESC(n)                                                        \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, label), ( \
	USBD_DESC_STRING_DEFINE(usbd_picotool_reset_if_desc_data_##n, \
	DT_INST_PROP(n, label), \
	USBD_DUT_STRING_INTERFACE); \
	))

#define PICOTOOL_RESET_DEFINE_LABEL(n)                                                             \
	IF_ENABLED(DT_INST_NODE_HAS_PROP(n, label), ( \
	  .if_desc_data = &usbd_picotool_reset_if_desc_data_##n, \
	))

#define PICOTOOL_RESET_DEFINE_DESCRIPTOR(n)                                                        \
	static struct usbd_picotool_reset_desc usbd_picotool_reset_desc_##n = {                    \
		.if0 =                                                                             \
			{                                                                          \
				.bLength = sizeof(struct usb_if_descriptor),                       \
				.bDescriptorType = USB_DESC_INTERFACE,                             \
				.bInterfaceNumber = 0,                                             \
				.bAlternateSetting = 0,                                            \
				.bNumEndpoints = 0,                                                \
				.bInterfaceClass = USB_BCC_VENDOR,                                 \
				.bInterfaceSubClass = RESET_INTERFACE_SUBCLASS,                    \
				.bInterfaceProtocol = RESET_INTERFACE_PROTOCOL,                    \
			},                                                                         \
                                                                                                   \
		.nil_desc =                                                                        \
			{                                                                          \
				.bLength = 0,                                                      \
				.bDescriptorType = 0,                                              \
			},                                                                         \
	};                                                                                         \
                                                                                                   \
	const static struct usb_desc_header *usbd_picotool_reset_usb_desc_##n[] = {                \
		(struct usb_desc_header *)&usbd_picotool_reset_desc_##n.if0,                       \
		(struct usb_desc_header *)&usbd_picotool_reset_desc_##n.nil_desc,                  \
	}

#define USBD_PICOTOL_RESET_DT_DEVICE_DEFINE(n)                                                     \
	BUILD_ASSERT(DT_INST_ON_BUS(n, usb),                                                       \
		     "node " DT_NODE_PATH(                                                         \
			     DT_DRV_INST(n)) " is not assigned to a USB device controller");       \
                                                                                                   \
	PICOTOOL_RESET_DEFINE_DESCRIPTOR(n);                                                       \
                                                                                                   \
	USBD_DEFINE_CLASS(usbd_picotool_reset_##n, &usbd_picotool_reset_api,                       \
			  (void *)DEVICE_DT_GET(DT_DRV_INST(n)), NULL);                            \
                                                                                                   \
	PICOTOOL_RESET_DEFINE_LABEL_DESC(n);                                                       \
                                                                                                   \
	static const struct usbd_picotool_reset_config reset_config_##n = {                        \
		.desc = &usbd_picotool_reset_desc_##n,                                             \
		.usb_desc = usbd_picotool_reset_usb_desc_##n,                                      \
		PICOTOOL_RESET_DEFINE_LABEL(n)};                                                   \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, usbd_picotool_reset_dev_init, NULL, NULL, &reset_config_##n, POST_KERNEL,               \
			      CONFIG_USBD_PICOTOOL_RESET_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(USBD_PICOTOL_RESET_DT_DEVICE_DEFINE);
