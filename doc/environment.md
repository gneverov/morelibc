# Morelibc
## Environment Variables
Morelibc keeps a list of environment variables that are stored in flash memory and are persistent across resets and reflashes of the firmware. They are useful for storing small pieces of information that are needed before or without a filesystem being mounted and that rarely change.

They are stored at a fixed offset in flash memory and the firmware image is careful not to overwrite this memory. This allows device settings to be maintained across firmware updates. Environment variables are access/updated via the usual `getenv`/`setenv` functions. They are not designed to be written frequently. If you have access to a filesystem, then storing settings in a file might be a better alternative.

Some examples of how an application might define and use environment variables.
| Variable | Notes | Example |
| - | - | - |
| `COUNTRY` | Country code for wifi | US |
| `HOSTNAME` | Host name used by `gethostname` |
| `ROOT` | How to mount root filesystem: *device* *fstype* [*flags*] | /dev/flash fatfs |
| `TTY` | Device to open for stdio streams| /dev/ttyUSB0 |
| `TZ` | Time zone used by `tzset`| PST8PDT |
