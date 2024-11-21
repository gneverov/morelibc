# Morelibc
The `main` function of a Morelibc typically performs a common sequence of tasks to get the system up and running for the application.

## FreeRTOS start up
Typically you want to start running in the context of a FreeRTOS task as soon as possible. Before `main` is called the program's "init" or "constructor" functions are run outside of a FreeRTOS context. These functions can only perform basic initialization (e.g., initialize a mutex), but they cannot rely on the scheduler (e.g., take a mutex), and ideally should not allocate (e.g., call `malloc`), or not be inter-dependent. So by the time `main` is called we're using ready to start more complex initialization that depends on the scheduler (e.g., all VFS functions require the scheduler).
```
#include "FreeRTOS.h"
#include "task.h"

xTaskCreate(init_task, "init", configMINIMAL_STACK_SIZE, NULL, 3, NULL);
vTaskStartScheduler();
```
So it's possible the `main` function does nothing more than the above code to start the scheduler and further initialization is deferred to the `init_task` function.

## The device filesystem
Typically the first thing a program will want to do is mount the "devfs" filesystem which provides the `/dev` directory. Filesystems are mounted using the `mount` function.
```
#include "morelib/mount.h"

mount(NULL, "/dev", "devfs", 0, NULL);
```
The first argument is the name of the storage device to mount. devfs isn't a real storage filesystem so this is null.

The second argument is the mount point. Conventionally this is `/dev`.

The third argument is the type of the filesystem (i.e., devfs).

The fourth argument is flags we don't care about, and the fifth argument is extra data we don't use.

When this function completes, there will be a valid directory at `/dev`. There won't actually be a root directory at `/` yet, but this is fine, we'll mount something there later.

### The device directory
So where do the contents of `/dev` come from? Because devices in an embedded system aren't expected to be very dynamic, the contents of the device directory is hard-coded into the firmware. Your program needs to provide the array symbol `devfs_entries` which contains the entries for the device directory.
```
#include <sys/stat.h>
#include "morelib/devfs.h"

const struct devfs_entry devfs_entries[] = {
    { "/", S_IFDIR, 0 },

    { "/mem", S_IFCHR, DEV_MEM },
    { "/null", S_IFCHR, DEV_NULL },
    { "/zero", S_IFCHR, DEV_ZERO },
    { "/full", S_IFCHR, DEV_FULL },

    { "/mtd0", S_IFCHR, DEV_MTD0 },
    { "/mtd1", S_IFCHR, DEV_MTD1 },

    { "/mtdblock0", S_IFBLK, DEV_MTDBLK0 },
    { "/mtdblock1", S_IFBLK, DEV_MTDBLK1 },

    { "/ttyS0", S_IFCHR, DEV_TTYS0 },
    { "/ttyS1", S_IFCHR, DEV_TTYS1 },

    { "/ttyUSB0", S_IFCHR, DEV_TTYUSB0 },
};

const size_t devfs_num_entries = sizeof(devfs_entries) / sizeof(devfs_entries[0]);
```
The first entry for "/" is mandatory and is for the device directory itself. After that you can put whatever device names make sense for your application. The device directory structure follows the general idea of [device files](https://en.wikipedia.org/wiki/Device_file#Unix_and_Unix-like_systems) from Unix-like systems.

Each entry has 3 parameters:
1. The name of the device (e.g., "/null"). This name is relative to the mount point, so after it is mounted, this device will be accessible at `/dev/null`.
1. The "mode" of the file that will be returned if you call [stat](https://pubs.opengroup.org/onlinepubs/9799919799/functions/stat.html) on the device. `S_IFCHR` for character devices, `S_IFBLK` for block devices.
1. The device number is a 16-bit integer: the high 8 bits, called the major number, identify the device type, and the low 8 bits, called the minor number, identify the device instance (more or less). The idea of device numbers originated in the olden days of Unix and are a simple way to identify devices in a small system. Device numbers are statically assigned and the driver implementation should define constants for what its device number are (e.g., `DEV_NULL`).

### Pseudo devices
Morelibc itself defines drivers for basic pseudo-devices like `/dev/null`. You don't have to include these in your device directory if you don't want to. Most drivers however will be defined by the platform (e.g., RP2 for Raspberry Pi Picos).

### Raw memory devices
"MTD" stands for Memory Technology Device and is a term borrowed from Linux to mean the built-in flash chip of your microcontroller. Morelibc defines the interface for MTD drivers, but does not implement any as they are platform-specific. In this example there two MTD devices, `mtd0` and `mtd1`, because the flash chip has been partitioned into a firmware partition for flashing firmware and a storage partition for a filesystem,

### Block memory devices
MTD drivers expose the raw interface of the flash chip so the user has to be aware of erase operations, different block sizes, only accessing whole blocks, etc. Maybe all that work is not for you, in which case there are MTD block drivers. There is only one MTD block driver, which is implemented by Morelibc and wraps regular MTD drivers (i.e., `mtdblock0` wraps `mtd0`, etc.) The MTD block driver lets you read and write anywhere without worrying about the structure of the flash chip. It does this by keeping an in-memory cache of flash blocks and delays writing blocks back to the flash. So it comes with a cost, and its up to the user to determine if the block or raw interface is better for a particular application.

### Serial devices
Next in the list are the TTY devices. `ttyS?` are the UART peripherals of the microcontroller. This microcontroller has two UART devices named `ttyS0` and `ttyS1`. Similarly `ttyUSB?` are for USB CDC devices. TTY devices are opened by a program to provide an interface with a user or transfer data.

## Opening a serial connection
Now that we have access to all the microcontroller's devices, we can do some more things. Usually we will want to open a serial connection so we can monitor or interact with the program and create a filesystem to read or write data from.

To open a serial connection just open the corresponding TTY device file.
```
#include <fcntl.h>
#include <unistd.h>

int fd = open("/dev/ttyS0", O_RDWR, 0);
close(fd);
```
Opening a TTY automatically installs it on file descriptors 0, 1, and 2 for stdin, stdout, and stderr. So you can use the file descriptor returned from `open` directly if you want, or you can close it and use the serial connection through stdin/stdout. If you want to open a TTY without it automatically becoming stdin/stdout, add the `O_NOCTTY` flag to the `open` call.

## Setting up the filesystem
Morelibc doesn't have a root filesystem but instead supports a "forest" of individual mount points, one of which could be `/` (i.e., root). This allows the system to run without any filesystems as all. We've already added one mount point (`/dev`) for the device directory, and now we can mount a real filesystem at another mount point.

### Filesystem drivers
However before we can mount a filesystem, we have to tell Morelibc what filesystem types are available in the system. We do this by providing another array symbol `vfs_fss` containing the list of available filesystem drivers.
```
#include "morelib/devfs.h"
#include "morelib/fatfs.h"

const struct vfs_filesystem *vfs_fss[] = {
    &devfs_fs,
    &fatfs_fs,
};

const size_t vfs_num_fss = sizeof(vfs_fss) / sizeof(vfs_fss[0]);
```
In this list we have to at least define the devfs filesystem driver which we used to mount the device directory. Here we also add the FAT filesystem driver.

### Mounting a volume
Once we have a filesystem type defined, we use the `mount` function from before to actually mount a volume.
```
#include "morelib/mount.h"

mount("/dev/mtdblock1", "/", "fatfs", 0, NULL);
```
The first argument is the name of the storage device to mount. Here `dev/mtdblock1` is the second MTD block device.

The second argument is the mount point. Here we are mounting it as the root directory, but it could be mounted anywhere (e.g., `/mnt`, `/flash`).

The third argument is the filesystem type and must be one of the types defined in the `vfs_fss` array.

Once mounted, the volume is ready to use in file operations. For example, using `opendir`/`readdir` to read the contents of the volume.

### Formatting a volume
If this is the first time using a device, then it may not have a valid filesystem stored on it and needs to be formatted. Formatting a device is done with the `mkfs` function, which as similar arguments to `mount`.
```
mkfs("/dev/mtdblock1", "fatfs", NULL);
```
Once formatted, the volume can be mounted using `mount` as above.

## Note about device types
A flash storage device can be accessed either through the raw or the block interface. Which one to use depends on the application. The FAT filesystem implementation used here doesn't know about flash memory devices and so it cannot use the raw interface, but the block interface works just fine. On the other hand, the littlefs filesystem was specifically designed for flash memory, so it should use the raw interface instead of the block interface. To mount a littlefs filesystem on the same device, you would run:
```
mount("/dev/mtd1", "/", "littlefs", 0, NULL);
```
Trying to mount a filesystem on an incompatible device type will produce an error.
