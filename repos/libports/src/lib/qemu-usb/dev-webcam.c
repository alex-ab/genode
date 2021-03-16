/*
 * QEMU USB webcam model
 *
 * Written by Alexander Boettcher
 *
 * Copyright (C) 2021 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

#include "hw/usb.h"
#include "desc.h"
#include "webcam-backend.h"

enum DeviceConfiguration {
	DEVICE_VC_INTERFACE_ID   = 0,
	DEVICE_VS_INTERFACE_ID   = 1,

	DEVICE_VS_FORMAT_INDEX   = 1,
	DEVICE_VS_FORMAT_BITS    = 24,

	DEVICE_VS_FRAME_INDEX    = 1,

	TERMINAL_ID_INPUT        = 1,
	TERMINAL_ID_OUTPUT       = 2,

	DEVICE_EP_ID             = 1,
	EP_MAX_PACKET_SIZE       = 512,
};

typedef struct USBWebcamState {
	USBDevice  dev;
	QEMUTimer *timer;
	USBPacket *delayed_packet;
	unsigned   bytes_frame;
	unsigned   bytes_payload;
	unsigned   frame_counter;
	uint8_t    frame_toggle_bit;
	bool       delay_packet;
	bool       capture;
	uint8_t    watchdog;
	uint8_t   *frame_pixel;
} USBWebcamState;

#define TYPE_USB_WEBCAM "usb-webcam"
#define USB_WEBCAM(obj) OBJECT_CHECK(USBWebcamState, (obj), TYPE_USB_WEBCAM)

#define U16(x) ((x) & 0xff), (((x) >> 8) & 0xff)
#define U24(x) U16(x), (((x) >> 16) & 0xff)
#define U32(x) U24(x), (((x) >> 24) & 0xff)

enum {
	STR_MANUFACTURER = 1,
	STR_PRODUCT,
	STR_SERIALNUMBER,
	STR_CONFIG_HIGH,
	STR_VIDEOCONTROL,
	STR_CAMERATERMINAL,
};

static const USBDescStrings desc_strings = {
	[STR_MANUFACTURER]   = "Genode",
	[STR_PRODUCT]        = "Genode USB WebCAM",
	[STR_SERIALNUMBER]   = "1",
	[STR_CONFIG_HIGH]    = "High speed config (usb 2.0)",
	[STR_VIDEOCONTROL]   = "Videocontrol",
	[STR_CAMERATERMINAL] = "Camera Sensor",
};

enum {
	USB_CLASS_VIDEO = 0xe,
	SC_VIDEO_CONTROL              = 1,
	SC_VIDEO_STREAMING            = 2,
	SC_VIDEO_INTERFACE_COLLECTION = 3,
};

enum {
	VC_HEADER          = 1,
	VC_INPUT_TERMINAL  = 2,
	VC_OUTPUT_TERMINAL = 3,

	VS_INPUT_HEADER        = 1,
	VS_FORMAT_UNCOMPRESSED = 4,
	VS_FRAME_UNCOMPRESSED  = 5,

	VS_FORMAT_FRAME_BASED = 0x10,
	VS_FRAME_FRAME_BASED  = 0x11,
};

enum {
	TT_STREAMING = 0x101,
	ITT_CAMERA   = 0x201,
};

enum {
	UV_SET_CUR = 0x01,
	UV_GET_CUR = 0x81,
	UV_GET_MIN = 0x82,
	UV_GET_MAX = 0x83,
	UV_GET_DEF = 0x87,
};

enum {
	VS_PROBE_CONTROL  = 0x1,
	VS_COMMIT_CONTROL = 0x2,
};

struct vs_probe_control {
	uint16_t bmHint;
	uint8_t  bFormatIndex;
	uint8_t  bFrameIndex;
	uint32_t dwFrameInterval;
	uint16_t wKeyFrameRate;
	uint16_t wPFrameRate;
	uint16_t wCompQuality;
	uint16_t wCompWindowSize;
	uint16_t wDelay;
	uint32_t dwMaxVideoFrameSize;
	uint32_t dwMaxPayLoadTransferSize;
	uint32_t dwClockFrequency;
	uint8_t  bmFramingInfo;
	uint8_t  bPreferedVersion;
	uint8_t  bMinVersion;
	uint8_t  bMaxVersion;
} QEMU_PACKED vs_probe_state = {
	.bFormatIndex             = DEVICE_VS_FORMAT_INDEX,
	.bFrameIndex              = DEVICE_VS_FRAME_INDEX,
	.bmFramingInfo            = 1,
};

enum {
	BFH_END_OF_FRAME  = 1U << 1,
	BFH_PRESENT_TIME  = 1U << 2,
	BFH_END_OF_HEADER = 1U << 7,
};

struct payload_header {
	uint8_t  length;
	uint8_t  bfh;
	uint32_t timestamp;
} QEMU_PACKED;

static struct bgr3_frame_desc {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint8_t  bDescriptorSubType;
	uint8_t  bFrameIndex;
	uint8_t  bmCapabilities;
	uint16_t wWidth;
	uint16_t wHeight;
	uint32_t dwMinBitRate;
	uint32_t dwMaxBitRate;
	uint32_t dwDefaultFrameInterval;
	uint8_t  bFrameIntervalType;
	uint32_t dwBytesPerLine;
	uint32_t dwFrameInterval;
} QEMU_PACKED format_frame_desc = {
	.bLength                = 30,    /* n=0 ->38, >0 = 26 + 4*n */
	.bDescriptorType        = USB_DT_CS_INTERFACE,
	.bDescriptorSubType     = VS_FRAME_FRAME_BASED,
	.bFrameIndex            = DEVICE_VS_FRAME_INDEX,
	.bmCapabilities         = 1 | 2, /* D0: Still image, D1: Fixed frame-rate */
	.bFrameIntervalType     = 1,     /*  n */
};

static const USBDescIface desc_iface_high [] = {
	{
		.bInterfaceNumber              = DEVICE_VC_INTERFACE_ID,
		.bInterfaceClass               = USB_CLASS_VIDEO,
		.bInterfaceSubClass            = SC_VIDEO_CONTROL,
		.bInterfaceProtocol            = 0, /* undefined */
		.iInterface                    = STR_VIDEOCONTROL,
		.ndesc                         = 3,
		.descs = (USBDescOther[]) {
			{
				/* Class-specific VC Interface Header Descriptor */
				.data = (uint8_t[]) {
					12 + 1,              /*  u8  bLength 12 + n */
					USB_DT_CS_INTERFACE, /*  u8  bDescriptorType */
					VC_HEADER,           /*  u8  bDescriptorSubType */
					U16(0x0110),         /* u16  bcdUVC */
					U16(13 + 15 + 9),    /* u16  wTotalLength */
					U32(1000000),        /* u32  dwClockFrequency - deprecated */
					0x01,                /*  u8  bInCollection */
					0x01                 /*  u8  baInterfaceNr(1 .. n) */
				}
			},
			{
				/* Camera Terminal Descriptor */
				.data = (uint8_t[]) {
					15 + 0,              /*  u8  bLength 15 + n */
					USB_DT_CS_INTERFACE, /*  u8  bDescriptorType */
					VC_INPUT_TERMINAL,   /*  u8  bDescriptorSubType */
					TERMINAL_ID_INPUT,   /*  u8  bTerminalID */
					U16(ITT_CAMERA),     /* u16  wTerminalType */
					0,                   /*  u8  bAssocTerminal */
					STR_CAMERATERMINAL,  /*  u8  iTerminal */
					0,                   /* u16  wObjectFocalLengthMin */
					0,                   /* u16  wObjectFocalLengthMax */
					0,                   /* u16  wOcularFocalLength */
					0                    /*  u8  bControlSize */
				}
			},
			{
				.data = (uint8_t[]) {
					9 + 0,               /*  u8  bLength 9 + n */
					USB_DT_CS_INTERFACE, /*  u8  bDescriptorType */
					VC_OUTPUT_TERMINAL,  /*  u8  bDescriptorSubType */
					TERMINAL_ID_OUTPUT,  /*  u8  bTerminalID */
					U16(TT_STREAMING),   /* u16  wTerminalType */
					0,                   /*  u8  bAssocTerminal */
					TERMINAL_ID_INPUT,   /*  u8  bSourceID (<- bTerminalID) */
					0                    /*  u8  iTerminal */
				}
			}
		}
	},
	{
		.bInterfaceNumber              = DEVICE_VS_INTERFACE_ID,
		.bInterfaceClass               = USB_CLASS_VIDEO,
		.bInterfaceSubClass            = SC_VIDEO_STREAMING,
		.bInterfaceProtocol            = 0, /* undefined */
		.ndesc                         = 3,
		.descs = (USBDescOther[]) {
			{
				/* Class-specific VS Interface Header Descriptor */
				.data = (uint8_t[]) {
					13 + 1,                    /*  u8  bLength 13 + p * n */
					USB_DT_CS_INTERFACE,       /*  u8  bDescriptorType */
					VS_INPUT_HEADER,           /*  u8  bDescriptorSubType */
					1,                         /*  u8  bNumFormats p */
					U16(14 + 27 + 30),         /* u16  wTotalLength */
					USB_DIR_IN | DEVICE_EP_ID, /*  u8  bEndpointAddress */
					0,                         /*  u8  bmInfo */
					TERMINAL_ID_OUTPUT,        /*  u8  bTerminalLink <- bTerminalID */
					1,                         /*  u8  bStillCaptureMethod */
					1,                         /*  u8  bTriggerSupport */
					0,                         /*  u8  bTriggerUsage */
					1,                         /*  u8  bControlSize n */
					0                          /*  u8  bmaControls (1...n) */
				}
			},
			{
				.data = (uint8_t[]) {
					28,                     /*  u8  bLength */
					USB_DT_CS_INTERFACE,    /*  u8  bDescriptorType */
					VS_FORMAT_FRAME_BASED,  /*  u8  bDescriptorSubType */
					DEVICE_VS_FORMAT_INDEX, /*  u8  bFormatIndex */
					1,                      /*  u8  bNumFrameDescriptors */
					0x7d, 0xeb, 0x36, 0xe4, /*  u8  guidFormat - BGR */
					0x4f, 0x52, 0xce, 0x11,
					0x9f, 0x53, 0x00, 0x20,
					0xaf, 0x0b, 0xa7, 0x70,
					DEVICE_VS_FORMAT_BITS,  /*  u8  bBitsPerPixel */
					DEVICE_VS_FRAME_INDEX,  /*  u8  bDefaultFrameIndex */
					0,                      /*  u8  bAspectRadioX */
					0,                      /*  u8  bAspectRadioY */
					0,                      /*  u8  bmInterlaceFlags */
					0,                      /*  u8  bCopyProtect */
					0                       /*  u8  bVariableSize */
				}
			},
			{
				.data = (uint8_t *)&format_frame_desc
			}
		},
		.bNumEndpoints                 = 1,
		.eps = (USBDescEndpoint[]) {
			{
				.bEndpointAddress      = USB_DIR_IN | DEVICE_EP_ID,
				.bmAttributes          = USB_ENDPOINT_XFER_BULK,
				.wMaxPacketSize        = EP_MAX_PACKET_SIZE,
				.bInterval             = 1,
			},
		}
	}
};

static struct USBDescIfaceAssoc desc_iface_group = {
	.bFirstInterface   = 0,
	.bInterfaceCount   = 2,
	.bFunctionClass    = USB_CLASS_VIDEO,
	.bFunctionSubClass = SC_VIDEO_INTERFACE_COLLECTION,
	.bFunctionProtocol = 0,
	.iFunction         = STR_VIDEOCONTROL,
	.nif               = ARRAY_SIZE(desc_iface_high),
	.ifs               = desc_iface_high,
};

static const USBDescDevice desc_device_high = {
	.bcdUSB                        = 0x0200,
	.bDeviceClass                  = 0xef, /* Miscellaneous Device Class */
	.bDeviceSubClass               = 0x02, /* common class */
	.bDeviceProtocol               = 0x01, /* Interface Association Descriptor */
	.bMaxPacketSize0               = 64,
	.bNumConfigurations            = 1,
	.confs = (USBDescConfig[]) {
		{
			.bNumInterfaces        = 2,
			.bConfigurationValue   = 1,
			.iConfiguration        = STR_CONFIG_HIGH,
			.bmAttributes          = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
			.nif                   = 0,
			.nif_groups            = 1,
			.if_groups             = &desc_iface_group,
		},
	},
};

static const USBDesc descriptor_webcam = {
	.id = {
		.idVendor          = 0x46f4, /* CRC16() of "QEMU" */
		.idProduct         = 0x0001,
		.bcdDevice         = 0,
		.iManufacturer     = STR_MANUFACTURER,
		.iProduct          = STR_PRODUCT,
		.iSerialNumber     = STR_SERIALNUMBER,
	},
	.high  = &desc_device_high,
	.str   = desc_strings,
};

static const VMStateDescription vmstate_usb_webcam = {
	.name = TYPE_USB_WEBCAM,
};

static Property webcam_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void webcam_start_timer(USBWebcamState * const state)
{
	int64_t const now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
	timer_mod(state->timer, now_ns + 100ull * format_frame_desc.dwFrameInterval);
}

static unsigned max_frame_size()
{
	return format_frame_desc.wWidth * format_frame_desc.wHeight *
	       DEVICE_VS_FORMAT_BITS / 8;
}

static void usb_webcam_handle_reset(USBDevice *dev)
{
	USBWebcamState *state = USB_WEBCAM(dev);

	if (!state)
		return;

	state->delayed_packet   = 0;
	state->bytes_frame      = 0;
	state->bytes_payload    = 0;
	state->frame_counter    = 0;
	state->frame_toggle_bit = 0;
	state->delay_packet     = false;
	state->capture          = false;
	state->watchdog         = 0;
}

static void usb_webcam_setup_packet(USBWebcamState * const state, USBPacket * const p)
{
	unsigned packet_size           = vs_probe_state.dwMaxPayLoadTransferSize;
	struct   payload_header header = { .length = 0, .bfh = 0 };
	bool     start_timer           = !state->bytes_frame;

	if (p->iov.size < packet_size)
		packet_size = p->iov.size;

	if (packet_size <= sizeof(header)) {
		p->status = USB_RET_STALL;
		return;
	}

	if (state->bytes_frame >= max_frame_size()) {
		p->status = USB_RET_STALL;
		return;
	}

	/* reset capture watchdog */
	if (state->watchdog) {
		state->watchdog = 0;
		start_timer     = true;
	}

	/* check for capture state change */
	if (!state->capture) {
		state->capture = true;
		start_timer    = true;
		capture_state_changed(state->capture);
	}

	if (start_timer)
		webcam_start_timer(state);

	/* payload header */
	if (!state->bytes_payload || state->bytes_payload >= vs_probe_state.dwMaxPayLoadTransferSize) {
		header.length    = sizeof(header);
		header.bfh       = BFH_END_OF_HEADER | BFH_PRESENT_TIME | state->frame_toggle_bit;
		header.timestamp = state->frame_counter;

		state->bytes_payload = 0;
	}

	/* frame end check */
	if (state->bytes_frame + packet_size - header.length >= max_frame_size()) {
		packet_size = header.length + max_frame_size() - state->bytes_frame;

		header.bfh  |= BFH_END_OF_FRAME;

		state->bytes_payload = 0;

		if (state->frame_toggle_bit)
			state->frame_toggle_bit = 0;
		else
			state->frame_toggle_bit = 1;

		state->frame_counter++;
		state->delay_packet = true;
	} else {
		state->bytes_payload += packet_size;
	}

	/* copy header data in */
	if (header.length)
		usb_packet_copy(p, &header, header.length);

	/* copy frame data in */
	usb_packet_copy(p, state->frame_pixel + state->bytes_frame,
	                packet_size - header.length);
	p->status = USB_RET_SUCCESS;

	if (state->delay_packet)
		state->bytes_frame = 0;
	else
		state->bytes_frame += packet_size - header.length;
}

static void webcam_timeout(void *opague)
{
	USBDevice      *dev   = (USBDevice *)opague;
	USBWebcamState *state = USB_WEBCAM(opague);

	if (!state->delayed_packet) {
		unsigned const fps = 10000000u / format_frame_desc.dwFrameInterval;
		/* capture off detection - after 2s or if in delay_packet state */
		if (state->delay_packet || (state->watchdog && state->watchdog >= fps * 2)) {
			state->capture      = false;
			state->delay_packet = false;
			capture_state_changed(state->capture);
		} else {
			state->watchdog ++;
			webcam_start_timer(state);
		}
		return;
	}

	USBPacket *p = state->delayed_packet;

	state->delayed_packet = 0;
	state->delay_packet   = false;

	/* request next frame pixel buffer */
	capture_next_frame(state->frame_pixel);

	/* continue usb transmission with new frame */
	usb_webcam_setup_packet(state, p);
	if (p->status == USB_RET_SUCCESS)
		usb_packet_complete(dev, p);
}

static void usb_webcam_realize(USBDevice *dev, Error **errp)
{
	USBWebcamState *state = USB_WEBCAM(dev);

	usb_desc_create_serial(dev);
	usb_desc_init(dev);

	/* change target speed, which was set by usb_desc_init to USB_SPEED_FULL */
	dev->speed     = USB_SPEED_HIGH;
	dev->speedmask = USB_SPEED_MASK_HIGH;
	/* sets dev->device because of dev->speed* changes */
	usb_desc_attach(dev);

	state->timer       = timer_new_ns(QEMU_CLOCK_VIRTUAL, webcam_timeout, dev);
	state->frame_pixel = g_malloc(format_frame_desc.wWidth *
	                              format_frame_desc.wHeight *
	                              DEVICE_VS_FORMAT_BITS / 8);
}

static void usb_webcam_handle_control(USBDevice * const dev,
                                      USBPacket * const p,
                                      int const request, int const value,
                                      int const index, int const length,
                                      uint8_t * const data)
{
	int const ret = usb_desc_handle_control(dev, p, request, value, index,
	                                        length, data);
	if (ret >= 0) {
		p->status = USB_RET_SUCCESS;
		/* got handled */
		return;
	}

	bool stall = false;

	switch (request) {
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
		if (length || (index != (USB_DIR_IN | DEVICE_EP_ID))) {
			stall = true;
			break;
		}

		/* release packets on feature == 0 endpoint clear request */
		if (!value) {
			USBWebcamState *state = USB_WEBCAM(dev);
			if (state && state->delayed_packet)
				state->delayed_packet = 0;
		} else
			stall = true;

		break;
	case ClassInterfaceRequest | UV_GET_DEF:
	case ClassInterfaceRequest | UV_GET_CUR:
	case ClassInterfaceRequest | UV_GET_MIN:
	case ClassInterfaceRequest | UV_GET_MAX:
	{
		if (value & 0xff) {
			stall = true;
			break;
		}
		unsigned const cs        = (value >> 8) & 0xff; /* control selector */
		unsigned const interface = index & 0xff;
		unsigned const entity    = (index >> 8) & 0xff;

		if (interface != DEVICE_VS_INTERFACE_ID) {
			stall = true;
			break;
		}

		if (cs == VS_PROBE_CONTROL && length >= sizeof(vs_probe_state)) {
			memcpy(data, &vs_probe_state, sizeof(vs_probe_state));
			p->actual_length = sizeof(vs_probe_state);
		} else
			stall = true;
		break;
	}
	case ClassInterfaceOutRequest | UV_SET_CUR:
	{
		if (value & 0xff) {
			stall = true;
			break;
		}

		unsigned const cs        = (value >> 8) & 0xff; /* control selector */
		unsigned const interface = index & 0xff;
		unsigned const entity    = (index >> 8) & 0xff;

		if (interface == DEVICE_VC_INTERFACE_ID) {
			stall = true;
			break;
		}

		if (interface != DEVICE_VS_INTERFACE_ID) {
			stall = true;
			break;
		}

		if ((cs == VS_PROBE_CONTROL  && length >= sizeof(vs_probe_state)) ||
		    (cs == VS_COMMIT_CONTROL && length >= sizeof(vs_probe_state))) {
			/* we don't negotiate anything, we keep our default configuration */
		} else
			stall = true;
		break;
	}
	default:
		stall = true;
		break;
	}

	if (stall) {
		qemu_printf("%s:%d unhandled request len=%d, request=%x, value=%x,"
		            " index=%x - stall\n", __func__, __LINE__,
		            length, request, value, index);

		p->status = USB_RET_STALL;
	} else
		p->status = USB_RET_SUCCESS;
}

static void usb_webcam_handle_data(USBDevice *dev, USBPacket *p)
{
	USBWebcamState *state = USB_WEBCAM(dev);

	switch (p->pid) {
	case USB_TOKEN_IN:
		if (!p->ep || p->ep->nr != DEVICE_EP_ID) {
			p->status = USB_RET_STALL;
			return;
		}
		break;
	default:
		p->status = USB_RET_STALL;
		return;
	}

	if (state->delay_packet) {
		p->status = USB_RET_ASYNC;

		state->delayed_packet = p;
		return;
	}

	usb_webcam_setup_packet(state, p);
}

static void usb_webcam_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass    *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->realize        = usb_webcam_realize;
    uc->product_desc   = desc_strings[STR_PRODUCT];
    uc->usb_desc       = &descriptor_webcam;
    uc->handle_reset   = usb_webcam_handle_reset;
    uc->handle_control = usb_webcam_handle_control;
    uc->handle_data    = usb_webcam_handle_data;

    dc->vmsd = &vmstate_usb_webcam;
    device_class_set_props(dc, webcam_properties);
}

static const TypeInfo webcam_info = {
    .name          = TYPE_USB_WEBCAM,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBWebcamState),
    .class_init    = usb_webcam_class_initfn,
};

static void usb_webcam_register_types(void)
{
	/* request host target configuration */
	struct webcam_config config;
	webcam_backend_config(&config);

	/* setup model configuration parameters */
	unsigned const frame_bpl     = config.width * DEVICE_VS_FORMAT_BITS / 8;
	unsigned const frame_bitrate = config.width * config.height *
	                               DEVICE_VS_FORMAT_BITS * config.fps;
	unsigned const frame_interval = 1000000000u / 100 / config.fps; /* in 100ns units */

	format_frame_desc.wWidth                  = config.width;
	format_frame_desc.wHeight                 = config.height;
	format_frame_desc.dwMinBitRate            = frame_bitrate;
	format_frame_desc.dwMaxBitRate            = frame_bitrate;
	format_frame_desc.dwDefaultFrameInterval  = frame_interval;
	format_frame_desc.dwBytesPerLine          = frame_bpl;
	format_frame_desc.dwFrameInterval         = frame_interval;

	vs_probe_state.dwFrameInterval          = frame_interval;
	vs_probe_state.dwMaxVideoFrameSize      = max_frame_size();
	vs_probe_state.dwMaxPayLoadTransferSize = max_frame_size() / 2;
	vs_probe_state.dwClockFrequency         = config.fps;

	/* register device */
	type_register_static(&webcam_info);
}

type_init(usb_webcam_register_types)
