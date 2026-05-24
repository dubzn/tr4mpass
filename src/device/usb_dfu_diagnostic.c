/* usb_dfu_diagnostic.c -- safe DFU/USB diagnostics, no exploit delivery */

#include <stdio.h>
#include <string.h>

#include "device/usb_dfu.h"
#include "device/usb_dfu_diagnostic.h"
#include "util/log.h"

#define DFU_GETSTATUS_LEN 6
#define DIAG_TIMEOUT_MS   1000

static const char *speed_name(int speed)
{
    switch (speed) {
    case LIBUSB_SPEED_LOW:       return "low";
    case LIBUSB_SPEED_FULL:      return "full";
    case LIBUSB_SPEED_HIGH:      return "high";
    case LIBUSB_SPEED_SUPER:     return "super";
    case LIBUSB_SPEED_SUPER_PLUS:return "super+";
    default:                     return "unknown";
    }
}

static void print_string_descriptor(libusb_device_handle *handle,
                                    const char *label, uint8_t index)
{
    unsigned char buf[DFU_SERIAL_MAX];
    int ret;

    if (index == 0) {
        printf("  %-18s (none)\n", label);
        return;
    }

    memset(buf, 0, sizeof(buf));
    ret = libusb_get_string_descriptor_ascii(handle, index, buf, sizeof(buf));
    if (ret < 0) {
        printf("  %-18s index %u: %s\n", label, (unsigned)index,
               libusb_strerror(ret));
        return;
    }

    printf("  %-18s index %u: %s\n", label, (unsigned)index, buf);
}

static void print_endpoint(const struct libusb_endpoint_descriptor *ep)
{
    printf("      endpoint 0x%02X attrs=0x%02X max_packet=%u interval=%u\n",
           ep->bEndpointAddress,
           ep->bmAttributes,
           (unsigned)ep->wMaxPacketSize,
           (unsigned)ep->bInterval);
}

static void print_interface(const struct libusb_interface_descriptor *alt)
{
    int i;

    printf("    interface %u alt=%u class=0x%02X subclass=0x%02X "
           "protocol=0x%02X endpoints=%u\n",
           (unsigned)alt->bInterfaceNumber,
           (unsigned)alt->bAlternateSetting,
           (unsigned)alt->bInterfaceClass,
           (unsigned)alt->bInterfaceSubClass,
           (unsigned)alt->bInterfaceProtocol,
           (unsigned)alt->bNumEndpoints);

    for (i = 0; i < alt->bNumEndpoints; i++)
        print_endpoint(&alt->endpoint[i]);
}

static void print_config(libusb_device *usb_dev)
{
    struct libusb_config_descriptor *cfg = NULL;
    int ret;
    int i;
    int j;

    ret = libusb_get_active_config_descriptor(usb_dev, &cfg);
    if (ret != LIBUSB_SUCCESS) {
        printf("  active config desc: %s\n", libusb_strerror(ret));
        return;
    }

    printf("  config value:      %u\n", (unsigned)cfg->bConfigurationValue);
    printf("  attributes:        0x%02X\n", (unsigned)cfg->bmAttributes);
    printf("  max power:         %u mA\n", (unsigned)cfg->MaxPower * 2U);
    printf("  interfaces:        %u\n", (unsigned)cfg->bNumInterfaces);

    for (i = 0; i < cfg->bNumInterfaces; i++) {
        const struct libusb_interface *iface = &cfg->interface[i];
        for (j = 0; j < iface->num_altsetting; j++)
            print_interface(&iface->altsetting[j]);
    }

    libusb_free_config_descriptor(cfg);
}

static void print_dfu_getstatus(libusb_device_handle *handle)
{
    unsigned char status[DFU_GETSTATUS_LEN] = {0};
    int ret;

    ret = libusb_control_transfer(handle,
                                  LIBUSB_ENDPOINT_IN |
                                  LIBUSB_REQUEST_TYPE_CLASS |
                                  LIBUSB_RECIPIENT_INTERFACE,
                                  DFU_GETSTATUS,
                                  0,
                                  0,
                                  status,
                                  sizeof(status),
                                  DIAG_TIMEOUT_MS);

    if (ret < 0) {
        printf("  DFU_GETSTATUS:     %s (%d)\n", libusb_strerror(ret), ret);
        return;
    }

    printf("  DFU_GETSTATUS:     %d byte(s)", ret);
    if (ret == DFU_GETSTATUS_LEN) {
        unsigned poll_timeout = (unsigned)status[1] |
                                ((unsigned)status[2] << 8) |
                                ((unsigned)status[3] << 16);
        printf(" status=0x%02X state=0x%02X poll_timeout=%u ms iString=%u",
               (unsigned)status[0],
               (unsigned)status[4],
               poll_timeout,
               (unsigned)status[5]);
    }
    printf("\n");
}

int usb_dfu_print_diagnostic(const device_info_t *dev)
{
    struct libusb_device_descriptor desc;
    struct libusb_version const *ver;
    libusb_device *usb_dev;
    int active_config = -1;
    int ret;

    if (!dev) {
        log_error("usb_dfu_print_diagnostic: NULL device");
        return -1;
    }

    printf("\n--- Safe USB/DFU Diagnostic ---\n");
    ver = libusb_get_version();
    printf("  libusb:            %u.%u.%u.%u%s%s\n",
           (unsigned)ver->major,
           (unsigned)ver->minor,
           (unsigned)ver->micro,
           (unsigned)ver->nano,
           ver->rc ? "-" : "",
           ver->rc ? ver->rc : "");

    if (!dev->is_dfu_mode || !dev->usb) {
        printf("  DFU handle:        unavailable\n");
        printf("  note:              diagnostics stop before bypass/exploit code\n");
        printf("-------------------------------\n\n");
        return 0;
    }

    usb_dev = libusb_get_device(dev->usb);
    ret = libusb_get_device_descriptor(usb_dev, &desc);
    if (ret != LIBUSB_SUCCESS) {
        printf("  descriptor:        %s\n", libusb_strerror(ret));
        printf("-------------------------------\n\n");
        return -1;
    }

    printf("  bus/address:       %u/%u\n",
           (unsigned)libusb_get_bus_number(usb_dev),
           (unsigned)libusb_get_device_address(usb_dev));
    printf("  speed:             %s\n", speed_name(libusb_get_device_speed(usb_dev)));
    printf("  vid:pid:           %04X:%04X\n",
           (unsigned)desc.idVendor, (unsigned)desc.idProduct);
    printf("  bcdUSB/device:     0x%04X / 0x%04X\n",
           (unsigned)desc.bcdUSB, (unsigned)desc.bcdDevice);
    printf("  class/sub/proto:   0x%02X / 0x%02X / 0x%02X\n",
           (unsigned)desc.bDeviceClass,
           (unsigned)desc.bDeviceSubClass,
           (unsigned)desc.bDeviceProtocol);
    printf("  max packet EP0:    %u\n", (unsigned)desc.bMaxPacketSize0);

    ret = libusb_get_configuration(dev->usb, &active_config);
    printf("  active config:     %s",
           ret == LIBUSB_SUCCESS ? "" : libusb_strerror(ret));
    if (ret == LIBUSB_SUCCESS)
        printf("%d", active_config);
    printf("\n");

#ifndef __APPLE__
    ret = libusb_kernel_driver_active(dev->usb, 0);
    if (ret >= 0)
        printf("  kernel driver if0: %s\n", ret ? "active" : "inactive");
    else
        printf("  kernel driver if0: %s\n", libusb_strerror(ret));
#endif

    print_string_descriptor(dev->usb, "manufacturer", desc.iManufacturer);
    print_string_descriptor(dev->usb, "product", desc.iProduct);
    print_string_descriptor(dev->usb, "serial", desc.iSerialNumber);
    print_config(usb_dev);
    print_dfu_getstatus(dev->usb);

    printf("  bypass/exploit:    not executed\n");
    printf("-------------------------------\n\n");
    return 0;
}
