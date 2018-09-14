/*
 * vfs301/vfs300 fingerprint reader driver
 * https://github.com/andree182/vfs301
 *
 * Copyright (c) 2011-2012 Andrej Krutak <dev@andree.sk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#define FP_COMPONENT "vfs301"

#include "drivers_api.h"
#include "vfs301_proto.h"

/************************** GENERIC STUFF *************************************/

/* Callback of asynchronous sleep */
static void async_sleep_cb(void *data)
{
	fpi_ssm *ssm = data;

	fpi_ssm_next_state(ssm);
}

/* Submit asynchronous sleep */
static void async_sleep(unsigned int msec, fpi_ssm *ssm)
{
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	fpi_timeout *timeout;

	/* Add timeout */
	timeout = fpi_timeout_add(msec, async_sleep_cb, ssm);

	if (timeout == NULL) {
		/* Failed to add timeout */
		fp_err("failed to add timeout");
		fpi_imgdev_session_error(dev, -ETIME);
		fpi_ssm_mark_failed(ssm, -ETIME);
	}
}

static int submit_image(fpi_ssm *ssm)
{
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	vfs301_dev_t *vdev = FP_INSTANCE_DATA(FP_DEV(dev));
	int height;
	struct fp_img *img;

#if 0
	/* XXX: This is probably handled by libfprint automagically? */
	if (vdev->scanline_count < 20) {
		fpi_ssm_jump_to_state(ssm, M_REQUEST_PRINT);
		return 0;
	}
#endif

	img = fpi_img_new(VFS301_FP_OUTPUT_WIDTH * vdev->scanline_count);
	if (img == NULL)
		return 0;

	vfs301_extract_image(vdev, img->data, &height);

	/* TODO: how to detect flip? should the resulting image be
	 * oriented so that it is equal e.g. to a fingerprint on a paper,
	 * or to the finger when I look at it?) */
	img->flags = FP_IMG_COLORS_INVERTED | FP_IMG_V_FLIPPED;

	img->width = VFS301_FP_OUTPUT_WIDTH;
	img->height = height;

	img = fpi_img_resize(img, img->height * img->width);
	fpi_imgdev_image_captured(dev, img);

	return 1;
}

/* Loop ssm states */
enum
{
	/* Step 0 - Scan finger */
	M_REQUEST_PRINT,
	M_WAIT_PRINT,
	M_CHECK_PRINT,
	M_READ_PRINT_START,
	M_READ_PRINT_WAIT,
	M_READ_PRINT_POLL,
	M_SUBMIT_PRINT,

	/* Number of states */
	M_LOOP_NUM_STATES,
};

/* Exec loop sequential state machine */
static void m_loop_state(fpi_ssm *ssm)
{
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	vfs301_dev_t *vdev = FP_INSTANCE_DATA(FP_DEV(dev));

	switch (fpi_ssm_get_cur_state(ssm)) {
	case M_REQUEST_PRINT:
		vfs301_proto_request_fingerprint(fpi_imgdev_get_usb_dev(dev), vdev);
		fpi_ssm_next_state(ssm);
		break;

	case M_WAIT_PRINT:
		/* Wait fingerprint scanning */
		async_sleep(200, ssm);
		break;

	case M_CHECK_PRINT:
		if (!vfs301_proto_peek_event(fpi_imgdev_get_usb_dev(dev), vdev))
			fpi_ssm_jump_to_state(ssm, M_WAIT_PRINT);
		else
			fpi_ssm_next_state(ssm);
		break;

	case M_READ_PRINT_START:
		fpi_imgdev_report_finger_status(dev, TRUE);
		vfs301_proto_process_event_start(fpi_imgdev_get_usb_dev(dev), vdev);
		fpi_ssm_next_state(ssm);
		break;

	case M_READ_PRINT_WAIT:
		/* Wait fingerprint scanning */
		async_sleep(200, ssm);
		break;

	case M_READ_PRINT_POLL:
		{
		int rv = vfs301_proto_process_event_poll(fpi_imgdev_get_usb_dev(dev), vdev);
		g_assert(rv != VFS301_FAILURE);
		if (rv == VFS301_ONGOING)
			fpi_ssm_jump_to_state(ssm, M_READ_PRINT_WAIT);
		else
			fpi_ssm_next_state(ssm);
		}
		break;

	case M_SUBMIT_PRINT:
		if (submit_image(ssm)) {
			fpi_ssm_mark_completed(ssm);
			/* NOTE: finger off is expected only after submitting image... */
			fpi_imgdev_report_finger_status(dev, FALSE);
		} else {
			fpi_ssm_jump_to_state(ssm, M_REQUEST_PRINT);
		}
		break;
	}
}

/* Complete loop sequential state machine */
static void m_loop_complete(fpi_ssm *ssm)
{
	/* Free sequential state machine */
	fpi_ssm_free(ssm);
}

/* Exec init sequential state machine */
static void m_init_state(fpi_ssm *ssm)
{
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	vfs301_dev_t *vdev = FP_INSTANCE_DATA(FP_DEV(dev));

	g_assert(fpi_ssm_get_cur_state(ssm) == 0);

	vfs301_proto_init(fpi_imgdev_get_usb_dev(dev), vdev);

	fpi_ssm_mark_completed(ssm);
}

/* Complete init sequential state machine */
static void m_init_complete(fpi_ssm *ssm)
{
	struct fp_img_dev *dev = fpi_ssm_get_user_data(ssm);
	fpi_ssm *ssm_loop;

	if (!fpi_ssm_get_error(ssm)) {
		/* Notify activate complete */
		fpi_imgdev_activate_complete(dev, 0);

		/* Start loop ssm */
		ssm_loop = fpi_ssm_new(FP_DEV(dev), m_loop_state, M_LOOP_NUM_STATES);
		fpi_ssm_set_user_data(ssm_loop, dev);
		fpi_ssm_start(ssm_loop, m_loop_complete);
	}

	/* Free sequential state machine */
	fpi_ssm_free(ssm);
}

/* Activate device */
static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	fpi_ssm *ssm;

	/* Start init ssm */
	ssm = fpi_ssm_new(FP_DEV(dev), m_init_state, 1);
	fpi_ssm_set_user_data(ssm, dev);
	fpi_ssm_start(ssm, m_init_complete);

	return 0;
}

/* Deactivate device */
static void dev_deactivate(struct fp_img_dev *dev)
{
	vfs301_dev_t *vdev;

	vdev = FP_INSTANCE_DATA(FP_DEV(dev));
	vfs301_proto_deinit(fpi_imgdev_get_usb_dev(dev), vdev);
	fpi_imgdev_deactivate_complete(dev);
}

static int dev_open(struct fp_img_dev *dev, unsigned long driver_data)
{
	vfs301_dev_t *vdev = NULL;
	int r;

	/* Claim usb interface */
	r = libusb_claim_interface(fpi_imgdev_get_usb_dev(dev), 0);
	if (r < 0) {
		/* Interface not claimed, return error */
		fp_err("could not claim interface 0: %s", libusb_error_name(r));
		return r;
	}

	/* Initialize private structure */
	vdev = g_malloc0(sizeof(vfs301_dev_t));
	fp_dev_set_instance_data(FP_DEV(dev), vdev);

	vdev->scanline_buf = malloc(0);
	vdev->scanline_count = 0;

	/* Notify open complete */
	fpi_imgdev_open_complete(dev, 0);

	return 0;
}

static void dev_close(struct fp_img_dev *dev)
{
	vfs301_dev_t *vdev;

	/* Release private structure */
	vdev = FP_INSTANCE_DATA(FP_DEV(dev));
	free(vdev->scanline_buf);
	g_free(vdev);

	/* Release usb interface */
	libusb_release_interface(fpi_imgdev_get_usb_dev(dev), 0);

	/* Notify close complete */
	fpi_imgdev_close_complete(dev);
}

/* Usb id table of device */
static const struct usb_id id_table[] =
{
	{ .vendor = 0x138a, .product = 0x0005 /* vfs301 */ },
	{ .vendor = 0x138a, .product = 0x0008 /* vfs300 */ },
	{ 0, 0, 0, },
};

/* Device driver definition */
struct fp_img_driver vfs301_driver =
{
	/* Driver specification */
	.driver =
	{
		.id = VFS301_ID,
		.name = FP_COMPONENT,
		.full_name = "Validity VFS301",
		.id_table = id_table,
		.scan_type = FP_SCAN_TYPE_SWIPE,
	},

	/* Image specification */
	.flags = 0,
	.img_width = VFS301_FP_WIDTH,
	.img_height = -1,
	.bz3_threshold = 24,

	/* Routine specification */
	.open = dev_open,
	.close = dev_close,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};
