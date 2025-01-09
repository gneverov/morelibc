# Morelibc

## Terminals
Morelibc adopts something similar to POSIX terminal [model](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap11.html). Since in Morelibc there is only one process, there is only one controlling terminal for the whole system and no useful concept of process groups. The controlling terminal is set by calling `open` on a TTY device without the `O_NOCTTY` flags specified. This is typically done early in the startup sequence.

Morelibc defines the [termios](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/termios.h.html) structure, however TTY drivers won't actually implement most of the options. The most useful option is `ISIG` which controls whether the driver raises a signal when, for example, Ctrl-C is pressed.

### Pseudo terminals
In addition to real terminals for serial devices like UARTs and USB CDC, pseudo terminals are also possible in Morelibc for terminal emulation. However unlike in POSIX, there is no defined pseudo terminal (PTY) abstraction. This is because there is only one process, so both sides of a pseudo terminal can communicate directly, and so can be implemented as a regular TTY driver.

There are two kinds of pseudo terminals: telnet and the terminal multiplexer.

### Telnet
Telnet can be used to connect to the micro-controller over a network. To run the telnet server:
```
#include "morelib/telnet.h"

// Socket address for the telnet server to listen on.
struct sockaddr_in address = {
    .sin_family = AF_INET,
    .sin_port = htons(23),
    .sin_addr = { .s_addr = htonl(INADDR_ANY) },
};

// Start the telnet server/
struct telnet_server server;
telnet_server_init(&server, (struct sockaddr *)&address, sizeof(address), telnet_accept, NULL);

// Callback for when a new telnet session is accepted.
static void telnet_accept(void *arg, int fd) {
    const char msg[] = "Hello world\r\n";
    write(fd, msg, sizeof(msg));
    close(fd);
}
```
Once a telnet session is accepted, it is up to the application what to do with it. In this example, it just sends back "Hello world" and closes the connection. A real application would provide some sort of user interface, or connect the session to stdio using the terminal multiplexer.

### Terminal multiplexer
Sometimes it is useful to have multiple ways to access the terminal on a micro-controller. For example, over serial UART or USB CDC depending on what is convenient or accessible. The terminal multiplexer allows multiple terminal devices to be multiplexed into a single device.

```
// Open the terminal multiplexer device as the controlling terminal.
int mux_fd = open("/dev/tmux", O_RDWR);

// Open the UART terminal device and add it to the multiplexer via an ioctl.
int s_fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY);
ioctl(mux_fd, TMUX_ADD, s_fd);
close(s_fd);

// Open the USB terminal device and add it to the multiplexer via an ioctl.
int usb_fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
ioctl(mux_fd, TMUX_ADD, usb_fd);
close(usb_fd);
```
The a single read/write operation on the multiplexer will be forwarded to all the individual terminals attached to the multiplexer.
