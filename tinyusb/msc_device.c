// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "tusb.h"
#if CFG_TUD_MSC
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "morelib/mount.h"
#include "tinyusb/tusb_config.h"
#include "tinyusb/tusb_lock.h"


static struct {
    int fd;
    size_t ssize : 16;
    size_t eject : 1;
} tud_msc_disk;

/**
 * Attaches a block device as the backing storage of the MSC device.
 * 
 * Args:
 * lun: SCSI LUN (not used)
 * device: path to a block device (e.g., /dev/mtdblock1)
 * mountflags: flags as used in mount
 * 
 * Returns:
 * 0 on success, -1 on failure and sets errno
 */
int tud_msc_insert(uint8_t lun, const char *device, int mountflags) {
    int ret = -1;
    int fd = open(device, O_RDWR);
    if (fd < 0) {
        goto cleanup;
    }
    int ssize;
    if (ioctl(fd, BLKSSZGET, &ssize) < 0) {
        goto cleanup;
    }
    mountflags &= MS_RDONLY;
    if (ioctl(fd, BLKROSET, &mountflags) < 0) {
        goto cleanup;
    }

    tud_lock();
    if (tud_msc_disk.ssize == 0) {
        tud_msc_disk.fd = fd;
        tud_msc_disk.ssize = ssize;
        tud_msc_disk.eject = 0;
        fd = -1;
        ret = 0;
    } else {
        errno = EBUSY;
    }
    tud_unlock();

cleanup:
    if (fd >= 0) {
        close(fd);
    }
    return ret;
}

static void tud_msc_close(uint8_t lun) {
    if (tud_msc_disk.ssize != 0) {
        ioctl(tud_msc_disk.fd, BLKFLSBUF, NULL);
        close(tud_msc_disk.fd);
        tud_msc_disk.fd = 0;
        tud_msc_disk.ssize = 0;
        tud_msc_disk.eject = 0;
    }
}

/**
 * Detaches the backing storage of the MSC device.
 * 
 * Args:
 * lun: SCSI LUN (not used)
 * 
 * Returns:
 * 0 on success, -1 on failure and sets errno
 */
int tud_msc_eject(uint8_t lun) {
    tud_lock();
    if (tud_msc_disk.ssize != 0) {
        tud_msc_disk.eject = 1;
    }
    if (!tud_mounted()) {
        tud_msc_close(lun);
    }
    tud_unlock();
    return 0;
}

/**
 * Returns whether the MSC device has backing storage.
 */
int tud_msc_ready(uint8_t lun) {
    tud_lock();
    int ret = tud_msc_disk.ssize;
    tud_unlock();
    return !!ret;
}

// TinyUSB static callback functions

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
__attribute__((visibility("hidden")))
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    const tusb_config_t *c = tusb_config_get();
    memcpy(vendor_id, c->msc_vendor_id, 8);
    memcpy(product_id, c->msc_product_id, 16);
    memcpy(product_rev, c->msc_product_rev, 4);
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
__attribute__((visibility("hidden")))
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    // printf("test_unit_ready: fd=%d, ssize=%d, eject=%d\n", tud_msc_disk.fd, tud_msc_disk.ssize, tud_msc_disk.eject);
    tud_lock();
    if (tud_msc_disk.eject) {
        tud_msc_close(lun);
    }
    int ret = tud_msc_disk.ssize;
    tud_unlock();
    return ret;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
__attribute__((visibility("hidden")))
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    // printf("capacity: fd=%d, ssize=%d, eject=%d\n", tud_msc_disk.fd, tud_msc_disk.ssize, tud_msc_disk.eject);
    tud_lock();
    *block_count = 0;
    *block_size = 0;

    unsigned long size;
    if (ioctl(tud_msc_disk.fd, BLKGETSIZE, &size) >= 0) {
        *block_count = (size << 9) / tud_msc_disk.ssize;
        *block_size = tud_msc_disk.ssize;
    }
    tud_unlock();
}

// Callback invoked when received READ10 command.
__attribute__((visibility("hidden")))
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    // printf("read10: fd=%d, ssize=%d, eject=%d, lba=%ld, offset=%ld\n", tud_msc_disk.fd, tud_msc_disk.ssize, tud_msc_disk.eject, lba, offset);
    tud_lock();
    int ret = pread(tud_msc_disk.fd, buffer, bufsize, lba * tud_msc_disk.ssize + offset);
    tud_unlock();
    return ret;
}

// Callback invoked when received WRITE10 command.
__attribute__((visibility("hidden")))
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    // printf("write10: fd=%d, ssize=%d, eject=%d, lba=%ld, offset=%ld\n", tud_msc_disk.fd, tud_msc_disk.ssize, tud_msc_disk.eject, lba, offset);
    tud_lock();
    int ret = pwrite(tud_msc_disk.fd, buffer, bufsize, lba * tud_msc_disk.ssize + offset);
    tud_unlock();
    return ret;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
__attribute__((visibility("hidden")))
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    // printf("scsi: fd=%d, ssize=%d, eject=%d, cmd=%d\n", tud_msc_disk.fd, tud_msc_disk.ssize, tud_msc_disk.eject, scsi_cmd[0]);
    tud_lock();
    int32_t resplen = 0;
    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            ioctl(tud_msc_disk.fd, BLKFLSBUF, NULL);
            break;

        default:
            // Set Sense = Invalid Command Operation
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            // negative means error -> tinyusb could stall and/or response with failed status
            resplen = -1;
            break;
    }
    tud_unlock();
    return resplen;
}

// Invoked to check if device is writable as part of SCSI WRITE10
__attribute__((visibility("hidden")))
bool tud_msc_is_writable_cb(uint8_t lun) {
    // printf("is_writable: fd=%d, ssize=%d, eject=%d\n", tud_msc_disk.fd, tud_msc_disk.ssize, tud_msc_disk.eject);
    tud_lock();
    int ro = 0;
    ioctl(tud_msc_disk.fd, BLKROGET, &ro);
    tud_unlock();
    return !ro;
}

#endif
