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

int usb_ctrl_transfer_diag(libusb_device_handle *dev,
                           uint8_t bmRequestType,
                           uint8_t bRequest,
                           uint16_t wValue,
                           uint16_t wIndex,
                           unsigned char *data,
                           uint16_t wLength,
                           unsigned int timeout_ms,
                           usb_ctrl_transfer_diag_t *diag)
{
    struct libusb_transfer *transfer;
    struct timeval tv;
    unsigned char *buf;
    int completed = 0;
    int cancelled = 0;
    int submit_ret;
    int ret;
    unsigned iter = 0;
    const unsigned max_iter = 100;

    if (diag)
        memset(diag, 0, sizeof(*diag));

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
                                 &completed, timeout_ms);

    submit_ret = libusb_submit_transfer(transfer);
    if (diag)
        diag->submit_ret = submit_ret;
    if (submit_ret != LIBUSB_SUCCESS) {
        free(buf);
        libusb_free_transfer(transfer);
        return submit_ret;
    }

    tv.tv_sec = (long)(timeout_ms / 1000);
    tv.tv_usec = (long)((timeout_ms % 1000) * 1000);
    ret = libusb_handle_events_timeout_completed(g_usb_event_ctx, &tv,
                                                 &completed);
    if (ret != LIBUSB_SUCCESS && completed == 0) {
        libusb_cancel_transfer(transfer);
        cancelled = 1;
    }

    if (completed == 0) {
        libusb_cancel_transfer(transfer);
        cancelled = 1;
    }

    while (completed == 0 && iter < max_iter) {
        tv.tv_sec = 0;
        tv.tv_usec = 2000;
        ret = libusb_handle_events_timeout_completed(g_usb_event_ctx, &tv,
                                                     &completed);
        if (ret != LIBUSB_SUCCESS)
            break;
        iter++;
    }

    if (diag) {
        diag->status = transfer->status;
        diag->actual_length = transfer->actual_length;
        diag->completed = completed;
        diag->cancelled = cancelled;
    }

    if (completed == 0) {
        ret = LIBUSB_ERROR_TIMEOUT;
    } else {
        switch (transfer->status) {
        case LIBUSB_TRANSFER_COMPLETED:
            ret = transfer->actual_length;
            break;
        case LIBUSB_TRANSFER_STALL:
            ret = LIBUSB_ERROR_PIPE;
            break;
        case LIBUSB_TRANSFER_TIMED_OUT:
            ret = LIBUSB_ERROR_TIMEOUT;
            break;
        case LIBUSB_TRANSFER_CANCELLED:
            ret = LIBUSB_ERROR_TIMEOUT;
            break;
        case LIBUSB_TRANSFER_NO_DEVICE:
            ret = LIBUSB_ERROR_NO_DEVICE;
            break;
        case LIBUSB_TRANSFER_OVERFLOW:
            ret = LIBUSB_ERROR_OVERFLOW;
            break;
        default:
            ret = LIBUSB_ERROR_IO;
            break;
        }
    }

    if (diag)
        diag->result = ret;

    free(buf);
    libusb_free_transfer(transfer);
    return ret;
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
    int cancelled = 0;
    unsigned char *buf;
    int ret;
    unsigned iter = 0;
    const unsigned max_iter = 100;

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

    /* Wait for the abort timeout */
    tv.tv_sec  = (long)(abort_timeout_ms / 1000);
    tv.tv_usec = (long)((abort_timeout_ms % 1000) * 1000);
    libusb_handle_events_timeout_completed(g_usb_event_ctx, &tv, &completed);

    /* If not completed, cancel it */
    if (completed == 0) {
        libusb_cancel_transfer(transfer);
        cancelled = 1;
    }

    /* Wait for the transfer to finish (either success or cancelled) */
    while (completed == 0 && iter < max_iter) {
        tv.tv_sec = 0;
        tv.tv_usec = 2000; /* 2 ms */
        ret = libusb_handle_events_timeout_completed(g_usb_event_ctx, &tv,
                                                     &completed);
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
#if defined(__linux__) && !defined(__APPLE__)
        if (cancelled) {
            /*
             * Linux xHCI may report the requested OUT length for a cancelled
             * DFU_DNLOAD even though only a few packets reached the device.
             * Keep returning 0 there so checkm8_stage_setup can apply its
             * explicit 128/64/0 sent-byte guesses.
             *
             * Do not force cancelled IN transfers to 0: the checkm8
             * leak/no-leak/stall probes are GET_DESCRIPTOR requests, and
             * treating every cancelled IN as zero makes stage 3 accept a
             * possibly wrong heap state.
             */
            if ((bmRequestType & LIBUSB_ENDPOINT_IN) == 0)
                ret = 0;
            else
                ret = (int)transfer->actual_length;
        } else {
            ret = (int)transfer->actual_length;
        }
#else
        (void)cancelled;
        ret = (int)transfer->actual_length;
#endif
        break;
    default:
        ret = LIBUSB_ERROR_IO;
        break;
    }

    free(buf);
    libusb_free_transfer(transfer);
    return ret;
}
