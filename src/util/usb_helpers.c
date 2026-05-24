#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "util/usb_helpers.h"

/* Maximum retry attempts for transient USB errors (PIPE/STALL) */
#define USB_PIPE_MAX_RETRIES  3

/* Delay between retries in microseconds (50ms) */
#define USB_PIPE_RETRY_DELAY  50000

static void usb_async_completed_cb(struct libusb_transfer *transfer)
{
    *(int *)transfer->user_data = 1;
}

static libusb_context *g_usb_event_ctx;

void usb_helpers_set_event_ctx(libusb_context *ctx)
{
    g_usb_event_ctx = ctx;
}

/*
 * is_transient_usb_error -- Returns 1 if the libusb error code
 * represents a transient condition that may succeed on retry.
 * LIBUSB_ERROR_PIPE (-9) means the device STALLed the endpoint,
 * which is common during DFU operations and often clears on retry.
 */
static int is_transient_usb_error(int err)
{
    return (err == LIBUSB_ERROR_PIPE || err == LIBUSB_ERROR_TIMEOUT);
}

int usb_ctrl_transfer(libusb_device_handle *dev,
                      uint8_t bmRequestType,
                      uint8_t bRequest,
                      uint16_t wValue,
                      uint16_t wIndex,
                      unsigned char *data,
                      uint16_t wLength,
                      unsigned int timeout)
{
    if (!dev)
        return LIBUSB_ERROR_INVALID_PARAM;

    return libusb_control_transfer(dev, bmRequestType, bRequest,
                                   wValue, wIndex, data, wLength, timeout);
}

int usb_ctrl_transfer_no_data(libusb_device_handle *dev,
                              uint8_t bmRequestType,
                              uint8_t bRequest,
                              uint16_t wValue,
                              uint16_t wIndex,
                              unsigned int timeout)
{
    if (!dev)
        return LIBUSB_ERROR_INVALID_PARAM;

    return libusb_control_transfer(dev, bmRequestType, bRequest,
                                   wValue, wIndex, NULL, 0, timeout);
}

void usb_print_error(int libusb_error)
{
    fprintf(stderr, "[usb] error %d: %s\n",
            libusb_error, libusb_strerror((enum libusb_error)libusb_error));

    if (libusb_error == LIBUSB_ERROR_PIPE) {
        fprintf(stderr, "[usb] PIPE error: the device STALLed the transfer.\n"
                "[usb] Troubleshooting:\n"
                "[usb]   1. Re-enter DFU mode and try again\n"
                "[usb]   2. Try a different USB port (prefer direct, "
                "not a hub)\n"
                "[usb]   3. Try a different USB cable (original Apple "
                "cable recommended)\n"
                "[usb]   4. On WSL/Linux: ensure usbipd or usbmuxd is "
                "running\n"
                "[usb]   5. On macOS: native USB tends to be more "
                "reliable\n");
    }
}

int usb_ctrl_transfer_async_abort(libusb_device_handle *dev,
                                  uint8_t bmRequestType,
                                  uint8_t bRequest,
                                  uint16_t wValue,
                                  uint16_t wIndex,
                                  unsigned char *data,
                                  uint16_t wLength,
                                  unsigned int xfer_timeout_ms,
                                  unsigned int abort_timeout_ms)
{
    struct libusb_transfer *transfer;
    struct timeval tv;
    int completed = 0;
    unsigned char *buf;
    int ret;
    unsigned iter = 0;
    const unsigned max_iter = 64;

    if (!dev)
        return LIBUSB_ERROR_INVALID_PARAM;

    transfer = libusb_alloc_transfer(0);
    if (!transfer)
        return LIBUSB_ERROR_NO_MEM;

    buf = malloc(LIBUSB_CONTROL_SETUP_SIZE + wLength);
    if (!buf) {
        libusb_free_transfer(transfer);
        return LIBUSB_ERROR_NO_MEM;
    }

    if ((bmRequestType & LIBUSB_ENDPOINT_IN) == 0 && data && wLength > 0)
        memcpy(buf + LIBUSB_CONTROL_SETUP_SIZE, data, wLength);

    libusb_fill_control_setup(buf, bmRequestType, bRequest,
                              wValue, wIndex, wLength);
    libusb_fill_control_transfer(transfer, dev, buf, usb_async_completed_cb,
                                 &completed, xfer_timeout_ms);

    if (libusb_submit_transfer(transfer) != LIBUSB_SUCCESS) {
        free(buf);
        libusb_free_transfer(transfer);
        return LIBUSB_ERROR_IO;
    }

    /*
     * Gaster: cancel on every event-loop iteration while waiting up to
     * abort_timeout_ms.  Reset tv each pass so a zero timeval cannot block
     * forever on Linux.
     */
    while (completed == 0 && iter < max_iter) {
        unsigned wait_ms = abort_timeout_ms ? abort_timeout_ms : 1;

        tv.tv_sec  = (long)(wait_ms / 1000);
        tv.tv_usec = (long)((wait_ms % 1000) * 1000);

        ret = libusb_handle_events_timeout_completed(g_usb_event_ctx, &tv,
                                                     &completed);
        if (completed != 0)
            break;

        libusb_cancel_transfer(transfer);
        if (ret != LIBUSB_SUCCESS)
            break;
        iter++;
    }

    if (completed == 0) {
        libusb_cancel_transfer(transfer);
        tv.tv_sec = 0;
        tv.tv_usec = 5000;
        (void)libusb_handle_events_timeout_completed(g_usb_event_ctx, &tv,
                                                    &completed);
        free(buf);
        libusb_free_transfer(transfer);
        return LIBUSB_ERROR_TIMEOUT;
    }

    switch (transfer->status) {
    case LIBUSB_TRANSFER_STALL:
        ret = LIBUSB_ERROR_PIPE;
        break;
    case LIBUSB_TRANSFER_COMPLETED:
    case LIBUSB_TRANSFER_CANCELLED:
    case LIBUSB_TRANSFER_TIMED_OUT:
        ret = (int)transfer->actual_length;
        break;
    default:
        ret = LIBUSB_ERROR_IO;
        break;
    }

    free(buf);
    libusb_free_transfer(transfer);
    return ret;
}
