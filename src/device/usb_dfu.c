/* usb_dfu.c -- Apple DFU mode device detection and raw USB I/O (libusb) */

#include <unistd.h>

#include "device/usb_dfu.h"
#include "util/usb_helpers.h"
#include "util/log.h"

/* DFU control transfer direction flags */
#define DFU_REQUEST_OUT  (LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | \
                          LIBUSB_RECIPIENT_INTERFACE)
#define DFU_REQUEST_IN   (LIBUSB_ENDPOINT_IN  | LIBUSB_REQUEST_TYPE_CLASS | \
                          LIBUSB_RECIPIENT_INTERFACE)

/* Maximum chunk size for a single DFU transfer */
#define DFU_MAX_TRANSFER 0x800

/* Module-global libusb context */
static libusb_context *g_ctx = NULL;

int usb_dfu_init(void)
{
    int ret;
    if (g_ctx)
        return 0;
    ret = libusb_init(&g_ctx);
    if (ret != LIBUSB_SUCCESS) {
        log_error("libusb_init failed: %s", libusb_strerror(ret));
        return -1;
    }
#if LIBUSB_API_VERSION >= 0x01000106
    libusb_set_option(g_ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
#else
    libusb_set_debug(g_ctx, LIBUSB_LOG_LEVEL_WARNING);
#endif
    log_debug("libusb context initialized");
    usb_helpers_set_event_ctx(g_ctx);
    return 0;
}

void usb_dfu_cleanup(void)
{
    if (g_ctx) {
        usb_helpers_set_event_ctx(NULL);
        libusb_exit(g_ctx);
        g_ctx = NULL;
    }
}

int usb_dfu_find(libusb_device_handle **handle)
{
    libusb_device **devs = NULL;
    ssize_t count;
    ssize_t i;
    int found = 0;
    int ret;

    if (!handle)
        return -1;
    *handle = NULL;

    if (!g_ctx) {
        log_error("usb_dfu_find: libusb not initialized (call usb_dfu_init first)");
        return -1;
    }

    count = libusb_get_device_list(g_ctx, &devs);
    if (count < 0) {
        log_error("libusb_get_device_list failed: %s",
                  libusb_strerror((int)count));
        return -1;
    }

    for (i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        ret = libusb_get_device_descriptor(devs[i], &desc);
        if (ret != LIBUSB_SUCCESS)
            continue;

        if (desc.idVendor == APPLE_VID && desc.idProduct == DFU_PID) {
            ret = libusb_open(devs[i], handle);
            if (ret != LIBUSB_SUCCESS) {
                log_error("failed to open DFU device: %s",
                          libusb_strerror(ret));
                *handle = NULL;
                continue;
            }
            log_info("DFU device found (bus %d, addr %d)",
                     libusb_get_bus_number(devs[i]),
                     libusb_get_device_address(devs[i]));
            found = 1;
            break;
        }
    }

    libusb_free_device_list(devs, 1);

    if (!found) {
        log_debug("no Apple DFU device found");
        return -1;
    }

#ifndef __APPLE__
    /* Linux: release kernel driver so libusb can talk DFU directly. */
    if (libusb_kernel_driver_active(*handle, 0) == 1) {
        ret = libusb_detach_kernel_driver(*handle, 0);
        if (ret != LIBUSB_SUCCESS) {
            log_warn("failed to detach kernel driver: %s",
                     libusb_strerror(ret));
        }
    }
#endif

    /* Gaster sets configuration 1 before DFU I/O. */
    ret = libusb_set_configuration(*handle, 1);
    if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_BUSY) {
        log_debug("libusb_set_configuration(1): %s", libusb_strerror(ret));
    }

    /* Claim interface 0 (DFU interface) */
    ret = libusb_claim_interface(*handle, 0);
    if (ret != LIBUSB_SUCCESS) {
        log_error("failed to claim interface 0: %s", libusb_strerror(ret));
        log_info("On Linux: run as root, stop usbmuxd, or add udev rules for 05ac:1227");
        libusb_close(*handle);
        *handle = NULL;
        return -1;
    }

    return 0;
}

int usb_dfu_send(libusb_device_handle *handle, const void *data, size_t len)
{
    size_t sent = 0;
    uint16_t block_num = 0;
    int ret;

    if (!handle || (!data && len > 0))
        return -1;

    /* DFU DNLOAD: send in DFU_MAX_TRANSFER chunks; wValue is block number (DFU spec). */
    while (sent < len) {
        size_t chunk = len - sent;
        if (chunk > DFU_MAX_TRANSFER)
            chunk = DFU_MAX_TRANSFER;

        ret = usb_ctrl_transfer(handle, DFU_REQUEST_OUT, DFU_DNLOAD,
                                block_num, 0, (unsigned char *)data + sent,
                                (uint16_t)chunk, DFU_USB_TIMEOUT);
        if (ret < 0) {
            log_error("DFU DNLOAD failed at offset %zu: %s",
                      sent, libusb_strerror(ret));
            usb_print_error(ret);
            return -1;
        }

        sent += (size_t)ret;
        log_debug("DFU DNLOAD block %u: sent %zu / %zu bytes", (unsigned)block_num, sent, len);
        block_num++;
    }

    return 0;
}

int usb_dfu_recv(libusb_device_handle *handle, void *buf, size_t len,
                 size_t *actual)
{
    int ret;
    uint16_t xfer_len;

    if (!handle || !buf || !actual)
        return -1;

    *actual = 0;

    if (len > DFU_MAX_TRANSFER)
        xfer_len = DFU_MAX_TRANSFER;
    else
        xfer_len = (uint16_t)len;

    ret = usb_ctrl_transfer(handle, DFU_REQUEST_IN, DFU_UPLOAD,
                            0, 0, (unsigned char *)buf,
                            xfer_len, DFU_USB_TIMEOUT);
    if (ret < 0) {
        log_error("DFU UPLOAD failed: %s", libusb_strerror(ret));
        usb_print_error(ret);
        return -1;
    }

    *actual = (size_t)ret;
    log_debug("DFU UPLOAD: received %zu bytes", *actual);
    return 0;
}

void usb_dfu_close(libusb_device_handle *handle)
{
    if (!handle)
        return;

    libusb_release_interface(handle, 0);
    libusb_close(handle);
    log_debug("DFU device handle closed");
}

int usb_dfu_reset_and_reopen(libusb_device_handle **handle)
{
    unsigned elapsed_ms = 0;
    const unsigned poll_ms = 5;
    const unsigned max_wait_ms = 5000;

    if (!handle)
        return -1;

    if (*handle) {
        log_debug("usb_dfu_reset_and_reopen: bus reset...");
        libusb_reset_device(*handle);
        usb_dfu_close(*handle);
        *handle = NULL;
    }

    log_info("checkm8: waiting for USB re-enumeration...");
    while (elapsed_ms < max_wait_ms) {
        if (usb_dfu_find(handle) == 0) {
            log_info("checkm8: USB handle re-acquired (bus reset OK)");
            return 0;
        }
        usleep(poll_ms * 1000U);
        elapsed_ms += poll_ms;
    }

    log_error("usb_dfu_reset_and_reopen: device did not reappear within %u ms",
              max_wait_ms);
    return -1;
}
