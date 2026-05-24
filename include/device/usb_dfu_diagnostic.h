#ifndef USB_DFU_DIAGNOSTIC_H
#define USB_DFU_DIAGNOSTIC_H

#include "device/device.h"

/*
 * Print a safe USB/DFU diagnostic report.
 *
 * This intentionally avoids exploit setup, DNLOAD/UPLOAD transfers, payloads,
 * resets, and bypass module execution.  It only inspects libusb descriptors and
 * performs a standard DFU_GETSTATUS request when a DFU handle is available.
 */
int usb_dfu_print_diagnostic(const device_info_t *dev);

#endif /* USB_DFU_DIAGNOSTIC_H */
