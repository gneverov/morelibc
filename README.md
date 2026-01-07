# Morelibc
[Picolibc](https://github.com/picolibc/picolibc) is an excellent C runtime library for small or embedded systems. However sometimes we want *more* than what can be offered in Picolibc. This is where Morelibc comes in. Morelibc is an add-on to Picolibc that provides more of the standard C/POSIX programming interface which developers are accustomed to. Specifically Morelibc provides:
- Multi-threaded concurrency through integration with [FreeRTOS](https://www.freertos.org/) and including implementations of [`select`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pselect.html) and [`poll`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ppoll.html).
- A virtual file system (VFS) providing a Unix-like directory structure, the ability to mount additional storage devices, and implement additional file system types.
- A simple driver model for implementing hardware-specific drivers for storage (block) and communication (character) devices.
- Dynamic linking support (i.e., DLLs or shared libraries) to allow libraries to be built, distributed, and installed without recompiling or reinstalling the firmware.

## Supported platforms
- Raspberry Pi RP2040 and RP2350

In theory, support can be added for any platform that supports Picolibc and FreeRTOS.

## Integrations
This project also contains a number of optional components for integrating popular embedded system libraries with the Morelibc core.

- [FatFS](http://elm-chan.org/fsw/ff/): driver for FAT file system
- [littlefs](https://github.com/littlefs-project/littlefs): driver for littlefs file system
- [lwIP](https://savannah.nongnu.org/projects/lwip/): socket programming interface
- [TinyUSB](https://github.com/hathach/tinyusb): drivers for CDC (serial) and MSC (storage) devices (USB device-side only)
- [Mbed-TLS](https://github.com/Mbed-TLS/mbedtls): sockets with TLS support

## Building
To build the [minimal example](/example/):
```
morelibc/example$ cmake -B build
morelibc/example$ cmake --build build
```
This will download FreeRTOS and Pico SDK to be self-contained. An established project can use its existing copies of these projects, although particular versions may matter.

[MicroPythonRT](https://github.com/gneverov/micropythonrt) is an example of using Morelibc in a larger project.

## Documentation
- [Boot sequence, devices, and file systems](/doc/filesystem.md)
- [FreeRTOS, concurrency, and multi-threading](/doc/concurrency.md)
- [Terminals](/doc/terminals.md)
- [Environment variables](/doc/environment.md)
- [Function list](/doc/functions.md)
