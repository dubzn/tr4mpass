/* usb_dfu_info.c -- DFU descriptor parsing and libirecovery fallback */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libirecovery.h>

#include "device/usb_dfu.h"
#include "util/log.h"
#include "util/usb_helpers.h"

#ifdef TR4MPASS_IRECV_LEGACY
#define IRECV_INFO_HAS_CPID(info) ((info)->cpid != 0)
#define IRECV_INFO_HAS_ECID(info) ((info)->ecid != 0)
#else
#define IRECV_INFO_HAS_CPID(info) ((info)->have_cpid)
#define IRECV_INFO_HAS_ECID(info) ((info)->have_ecid)
#endif

#define DFU_SERIAL_INDEX 3
#define DFU_STRING_SCAN_MAX 8

static int parse_hex_field(const char *serial, const char *key, uint64_t *out)
{
    const char *p;
    char *endptr;

    if (!serial || !key || !out)
        return -1;
    p = strstr(serial, key);
    if (!p)
        return -1;
    endptr = NULL;
    *out = strtoull(p + strlen(key), &endptr, 16);
    if (!endptr || endptr == p + strlen(key)) {
        log_warn("parse_hex_field: garbage value for key '%s'", key);
        *out = 0;
    }
    return 0;
}

static int serial_has_dfu_identity(const char *serial)
{
    return serial && strstr(serial, "CPID:") && strstr(serial, "ECID:");
}

static int copy_descriptor_string(unsigned char *dst, size_t dst_len,
                                  const unsigned char *src, int src_len)
{
    size_t copy_len;

    if (!dst || dst_len == 0 || !src || src_len < 0)
        return -1;

    copy_len = (size_t)src_len;
    if (copy_len >= dst_len)
        copy_len = dst_len - 1;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return (int)copy_len;
}

static int read_string_descriptor_retry(libusb_device_handle *handle,
                                        uint8_t index,
                                        unsigned char *buf, size_t buf_len)
{
    unsigned char tmp[DFU_SERIAL_MAX];
    int attempt;
    int ret = LIBUSB_ERROR_INVALID_PARAM;

    if (!handle || !buf || buf_len == 0 || index == 0)
        return LIBUSB_ERROR_INVALID_PARAM;

    for (attempt = 0; attempt < 3; attempt++) {
        ret = libusb_get_string_descriptor_ascii(handle, index,
                                                 tmp, sizeof(tmp));
        if (ret >= 0)
            return copy_descriptor_string(buf, buf_len, tmp, ret);
        if (ret != LIBUSB_ERROR_PIPE && ret != LIBUSB_ERROR_TIMEOUT)
            break;
        if (attempt < 2) {
            log_warn("string descriptor %u read: %s (attempt %d/3, retrying)",
                     (unsigned)index, libusb_strerror(ret), attempt + 1);
            usleep(50000);
        }
    }

    return ret;
}

static int try_string_descriptor(libusb_device_handle *handle, uint8_t index,
                                 unsigned char *best, size_t best_len,
                                 int *best_ret, int *last_err)
{
    unsigned char candidate[DFU_SERIAL_MAX];
    int ret;

    ret = read_string_descriptor_retry(handle, index,
                                       candidate, sizeof(candidate));
    if (ret < 0) {
        if (last_err)
            *last_err = ret;
        log_debug("DFU string descriptor %u unavailable: %s",
                  (unsigned)index, libusb_strerror(ret));
        return 0;
    }

    log_debug("DFU string descriptor %u: %s", (unsigned)index, candidate);
    if (serial_has_dfu_identity((char *)candidate)) {
        copy_descriptor_string(best, best_len, candidate, ret);
        if (best_ret)
            *best_ret = ret;
        return 1;
    }
    if (best_ret && *best_ret < 0) {
        copy_descriptor_string(best, best_len, candidate, ret);
        *best_ret = ret;
    }
    return 0;
}

static int read_dfu_serial_via_libusb(libusb_device_handle *handle,
                                      unsigned char *buf, size_t buf_len)
{
    struct libusb_device_descriptor desc;
    int tried[256] = {0};
    int best_ret = -1;
    int last_err = LIBUSB_ERROR_NOT_FOUND;
    uint8_t serial_index = DFU_SERIAL_INDEX;
    libusb_device *dev;
    int ret;
    int i;

    if (!handle || !buf || buf_len == 0)
        return LIBUSB_ERROR_INVALID_PARAM;
    buf[0] = '\0';

    dev = libusb_get_device(handle);
    if (dev && libusb_get_device_descriptor(dev, &desc) == LIBUSB_SUCCESS) {
        log_debug("DFU string indexes: manufacturer=%u product=%u serial=%u",
                  (unsigned)desc.iManufacturer,
                  (unsigned)desc.iProduct,
                  (unsigned)desc.iSerialNumber);
        if (desc.iSerialNumber != 0)
            serial_index = desc.iSerialNumber;
    }

    ret = try_string_descriptor(handle, serial_index, buf, buf_len,
                                &best_ret, &last_err);
    tried[serial_index] = 1;
    if (ret == 1)
        return best_ret;

    if (!tried[DFU_SERIAL_INDEX]) {
        ret = try_string_descriptor(handle, DFU_SERIAL_INDEX, buf, buf_len,
                                    &best_ret, &last_err);
        tried[DFU_SERIAL_INDEX] = 1;
        if (ret == 1)
            return best_ret;
    }

    for (i = 1; i <= DFU_STRING_SCAN_MAX; i++) {
        if (tried[i])
            continue;
        ret = try_string_descriptor(handle, (uint8_t)i, buf, buf_len,
                                    &best_ret, &last_err);
        tried[i] = 1;
        if (ret == 1)
            return best_ret;
    }

    return (best_ret >= 0) ? best_ret : last_err;
}

static int read_dfu_info_via_irecovery(uint32_t *cpid, uint64_t *ecid,
                                       char *serial, size_t serial_len)
{
    irecv_client_t client = NULL;
    const struct irecv_device_info *info;
    irecv_error_t err;
    int found = 0;

    err = irecv_open_with_ecid(&client, 0);
    if (err != IRECV_E_SUCCESS) {
        log_warn("libirecovery DFU fallback open failed: %s",
                 irecv_strerror(err));
        return -1;
    }

    info = irecv_get_device_info(client);
    if (!info) {
        log_warn("libirecovery DFU fallback returned no device info");
        irecv_close(client);
        return -1;
    }
    if (cpid && IRECV_INFO_HAS_CPID(info)) {
        *cpid = info->cpid;
        found = 1;
        log_info("CPID via libirecovery: 0x%04X", *cpid);
    }
    if (ecid && IRECV_INFO_HAS_ECID(info)) {
        *ecid = info->ecid;
        found = 1;
        log_info("ECID via libirecovery: 0x%016" PRIX64, *ecid);
    }
    if (serial && serial_len > 0 &&
        info->serial_string && info->serial_string[0] != '\0') {
        snprintf(serial, serial_len, "%s", info->serial_string);
        found = 1;
        log_debug("DFU serial via libirecovery: %s", serial);
    }

    irecv_close(client);
    return found ? 0 : -1;
}

static int read_dfu_info_fallback(libusb_device_handle *handle,
                                  uint32_t *cpid, uint64_t *ecid,
                                  char *serial, size_t serial_len)
{
    int rc;
    int claim_rc;

    if (handle)
        libusb_release_interface(handle, 0);
    rc = read_dfu_info_via_irecovery(cpid, ecid, serial, serial_len);
    if (handle) {
        claim_rc = libusb_claim_interface(handle, 0);
        if (claim_rc != LIBUSB_SUCCESS)
            log_debug("DFU interface re-claim after fallback failed: %s",
                      libusb_strerror(claim_rc));
    }
    return rc;
}

int usb_dfu_read_info(libusb_device_handle *handle, uint32_t *cpid,
                      uint64_t *ecid, char *serial, size_t serial_len)
{
    unsigned char buf[DFU_SERIAL_MAX];
    char fallback_serial[DFU_SERIAL_MAX] = {0};
    uint64_t val;
    int ret;

    if (!handle)
        return -1;

    if (cpid) *cpid = 0;
    if (ecid) *ecid = 0;
    if (serial && serial_len > 0) serial[0] = '\0';

    ret = read_dfu_serial_via_libusb(handle, buf, sizeof(buf));
    if (ret < 0) {
        log_error("failed to read serial descriptor: %s",
                  libusb_strerror(ret));
        usb_print_error(ret);
        return read_dfu_info_fallback(handle, cpid, ecid, serial, serial_len);
    }

    log_debug("DFU serial string: %s", (char *)buf);
    if (serial && serial_len > 0)
        snprintf(serial, serial_len, "%s", (char *)buf);

    if (cpid && parse_hex_field((char *)buf, "CPID:", &val) == 0) {
        *cpid = (uint32_t)val;
        log_info("CPID: 0x%04X", *cpid);
    } else if (cpid) {
        log_warn("CPID field not found in serial string");
    }
    if (ecid && parse_hex_field((char *)buf, "ECID:", &val) == 0) {
        *ecid = val;
        log_info("ECID: 0x%016" PRIX64, *ecid);
    } else if (ecid) {
        log_warn("ECID field not found in serial string");
    }

    if ((cpid && *cpid == 0) || (ecid && *ecid == 0)) {
        if (read_dfu_info_fallback(handle, cpid, ecid, fallback_serial,
                                   sizeof(fallback_serial)) == 0) {
            if (fallback_serial[0] != '\0' && serial && serial_len > 0)
                snprintf(serial, serial_len, "%s", fallback_serial);
            return 0;
        }
    }

    if (cpid && *cpid == 0 &&
        strncmp((char *)buf, "Apple Mobile Device", 19) == 0) {
        log_warn("DFU device exposed only the generic product string.");
        log_info("Expected format: 'CPID:XXXX CPRV:XX BDID:XX ECID:XXXX ...'");
        log_info("On macOS, try: sudo ./tr4mpass --detect-only");
        log_info("Re-enter DFU if the screen is not completely black.");
    }

    return 0;
}
