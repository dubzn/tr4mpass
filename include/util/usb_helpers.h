#ifndef USB_HELPERS_H
#define USB_HELPERS_H

#include <stdint.h>
#include <libusb.h>

/*
 * Perform a USB control transfer with a data stage.
 * Returns number of bytes transferred on success, negative libusb error code
 * on failure.
 */
int usb_ctrl_transfer(libusb_device_handle *dev,
                      uint8_t bmRequestType,
                      uint8_t bRequest,
                      uint16_t wValue,
                      uint16_t wIndex,
                      unsigned char *data,
                      uint16_t wLength,
                      unsigned int timeout);

/*
 * Perform a USB control transfer with no data stage (wLength=0).
 * Returns 0 on success, negative libusb error code on failure.
 */
int usb_ctrl_transfer_no_data(libusb_device_handle *dev,
                              uint8_t bmRequestType,
                              uint8_t bRequest,
                              uint16_t wValue,
                              uint16_t wIndex,
                              unsigned int timeout);

/*
 * Print a human-readable description of a libusb error code to stderr.
 */
void usb_print_error(int libusb_error);

/*
 * Register the libusb context used for async event handling (call from
 * usb_dfu_init so handle_events matches the device context).
 */
void usb_helpers_set_event_ctx(libusb_context *ctx);

/*
 * Submit a control transfer, cancel after abort_timeout_ms, return bytes
 * actually transferred (gaster-style async DNLOAD for checkm8 setup).
 * xfer_timeout_ms is the libusb transfer timeout.
 */
int usb_ctrl_transfer_async_abort(libusb_device_handle *dev,
                                  uint8_t bmRequestType,
                                  uint8_t bRequest,
                                  uint16_t wValue,
                                  uint16_t wIndex,
                                  unsigned char *data,
                                  uint16_t wLength,
                                  unsigned int xfer_timeout_ms,
                                  unsigned int abort_timeout_ms);

#endif /* USB_HELPERS_H */
