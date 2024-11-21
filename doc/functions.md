# Morelibc
## Function list
Morelibc seeks to augment Picolibc with more functionality from the standard C/POSIX interfaces. Here is a list of which additional functions from these standards are implemented by Morelibc. Functions marked with 🟢 are implemented. Functions marked with 🔴 are not implemented. Some functions will never make sense to implement and they come with a note explaining the rationale. Other non-implemented functions may be implemented in the future.


## dirent.h
| Function | Status | Notes |
| - | - | - |
| `alphasort` | 🔴 | |
| `closedir` | 🟢 | |
| `dirfd` | 🔴 | |
| `fdopendir` | 🟢 | |
| `opendir` | 🟢 | |
| `readdir` | 🟢 | |
| `rewinddir` | 🟢 | |
| `scandir` | 🔴 | |
| `seekdir` | 🔴 | |
| `telldir` | 🔴 | |

## dlfcn.h
| Function | Status | Notes |
| - | - | - |
| `dladdr` | 🔴 | |
| `dlclose` | 🟢 | |
| `dlerror` | 🟢 | |
| `dlopen` | 🟢 | |
| `dlsym` | 🟢 | |

## fcntl.h
| Function | Status | Notes |
| - | - | - |
| `creat` | 🔴 | |
| `fcntl` | 🟢 | Only supports `F_GETFL`/`F_SETFL`. |
| `open` | 🟢 | |
| `openat` | 🔴 | |
| `rename` | 🟢 | |
| `renameat` | 🔴 | |

## poll.h
| Function | Status | Notes |
| - | - | - |
| `poll` | 🟢 | |
| `ppoll` | 🔴 | No signal mask. |

## sys/ioctl.h
| Function | Status | Notes |
| - | - | - |
| `ioctl` | 🟢 | Not POSIX. |

## sys/mman.h
| Function | Status | Notes |
| - | - | - |
| `mmap` | 🟢 | |
| `mprotect` | 🔴 | |
| `munmap` | 🟢 | No-op. All memory mappings are static. |

## sys/random.h
| Function | Status | Notes |
| - | - | - |
| `getrandom` | 🟢 | Implemented by port. Not POSIX. |

## sys/select.h
| Function | Status | Notes |
| - | - | - |
| `pselect` | 🔴 | No signal mask. |
| `select` | 🔴 | |

## sys/stat.h
| Function | Status | Notes |
| - | - | - |
| `chmod` | 🔴 | |
| `fchmod` | 🔴 | |
| `fchmodat` | 🔴 | |
| `fstat` | 🟢 | |
| `fstatat` | 🔴 | |
| `futimens` | 🔴 | |
| `lstat` | 🔴 | No symbolic links. |
| `mkdir` | 🟢 | |
| `mkdirat` | 🔴 | |
| `mkfifo` | 🔴 | |
| `mkfifoat` | 🔴 | |
| `mknod` | 🔴 | |
| `mknodat` | 🔴 | |
| `stat` | 🟢 | |
| `umask` | 🔴 | |
| `utimensat` | 🔴 | |

## sys/statvfs.h
| Function | Status | Notes |
| - | - | - |
| `fstatvfs` | 🟢 | |
| `statvfs` | 🟢 | |

## sys/time.h
| Function | Status | Notes |
| - | - | - |
| `gettimeofday` | 🟢 | Implemented by port. Obsolete. |
| `settimeofday` | 🟢 | Implemented by port. Not POSIX. |
| `utimes` | 🔴 | |

## sys/times.h
| Function | Status | Notes |
| - | - | - |
| `times` | 🟢 | Implemented by port. |

## sys/utsname.h
| Function | Status | Notes |
| - | - | - |
| `uname` | 🟢 | |

## termios.h
| Function | Status | Notes |
| - | - | - |
| `cfgetispeed` | 🟢 | |
| `cfgetospeed` | 🟢 | |
| `cfsetispeed` | 🟢 | |
| `cfsetospeed` | 🟢 | |
| `tcdrain` | 🟢 | |
| `tcflow` | 🟢 | |
| `tcflush` | 🟢 | |
| `tcgetattr` | 🟢 | |
| `tcgetsid` | 🔴 | |
| `tcgetwinsize` | 🔴 | |
| `tcsendbreak` | 🟢 | |
| `tcsetattr` | 🟢 | |
| `tcsetwinsize` | 🔴 | |

## time.h
| Function | Status | Notes |
| - | - | - |
| `asctime` | 🟢 | Implemented by Picolibc. Obsolete. |
| `clock` | 🟢 | Implemented by Picolibc. |
| `clock_getres` | 🔴 | |
| `clock_gettime` | 🔴 | |
| `clock_nanosleep` | 🔴 | |
| `clock_settime` | 🔴 | |
| `ctime` | 🟢 | Implemented by Picolibc. Obsolete. |
| `difftime` | 🟢 | Implemented by Picolibc. |
| `getdate` | 🔴 | | 
| `gmtime` | 🟢 | Implemented by Picolibc. |
| `gmtime_r` | 🟢 | Implemented by Picolibc. |
| `localtime` | 🟢 | Implemented by Picolibc. |
| `localtime_r` | 🟢 | Implemented by Picolibc. |
| `mktime` | 🟢 | Implemented by Picolibc. |
| `nanosleep` | 🟢 | |
| `strftime` | 🟢 | Implemented by Picolibc. |
| `strftime_l` | 🟢 | Implemented by Picolibc. |
| `strptime` | 🟢 | Implemented by Picolibc. |
| `time` | 🟢 | Implemented by Picolibc. |
| `timer_create` | 🔴 | |
| `timer_delete` | 🔴 | |
| `timer_getoverrun` | 🔴 | |
| `timer_gettime` | 🔴 | |
| `timer_settime` | 🔴 | |
| `timespec_get` | 🔴 | |
| `tzset` | 🟢 | Implemented by Picolibc. |

## unistd.h
| Function | Status | Notes |
| - | - | - |
| `access` | 🔴 | No users or groups. |
| `alarm`  | 🟢 | |
| `chdir` | 🟢 | |
| `chown` | 🔴 | No users or groups. |
| `close` | 🟢 | |
| `confstr` | 🔴 | No conf. |
| `crypt` | 🔴 | |
| `dup` | 🟢 | |
| `dup2` | 🟢 | |
| `dup3` | 🔴 | |
| `_exit` | 🟢 | |
| `execl`<br>`execle`<br>`execlp`<br>`execv`<br>`execve`<br>`execvp`| 🔴 | No multiple processes. |
| `faccessat` | 🔴 | No users or groups. |
| `fchdir` | 🔴 | |
| `fchown` | 🔴 | No users or groups. |
| `fchownat` | 🔴 | No users or groups. |
| `fexecve` | 🔴 | No multiple processes. |
| `fork` | 🔴 | No multiple processes. |
| `fpathconf` | 🔴 | No conf. |
| `fsync` | 🟢 | |
| `ftruncate` | 🟢 | |
| `getcwd` | 🟢 | |
| `getentropy` | 🟢 | |
| `getegid`<br>`geteuid`<br>`getgid`<br>`getuid`<br>`setegid`<br>`seteuid`<br>`setgid`<br>`setuid` | 🔴 | No users or groups. |
| `getgroups` | 🔴 | No users or groups. |
| `gethostid` | 🔴 | |
| `gethostname` | 🟢 | Returns `HOSTNAME` environment variable. |
| `getlogin` | 🔴 | No users or groups. |
| `getopt` |  🔴 | No opt. |
| `getpgid`<br>`getpgrp`<br>`getsid`<br>`setpgid`<br>`setsid` | 🔴 | No multiple processes. |
| `getpid` | 🟢 | Returns 0. |
| `getppid` | 🔴 | |
| `isatty` | 🟢 | |
| `lchown` | 🔴 | No symbolic links. |
| `link` | 🔴 | No hard links. |
| `linkat` | 🔴 | No hard links. |
| `lockf` | 🔴 | No file locks. |
| `lseek` | 🟢 | |
| `nice` | 🔴 | |
| `pathconf` | 🔴 | No conf. |
| `pause` | 🔴 | |
| `pipe` | 🟢 | |
| `pipe2` | 🔴 | |
| `posix_close` | 🔴 | |
| `pread`<br>`pwrite` | 🟢 | |
| `read` | 🟢 | |
| `readlink`<br>`readlinkat` | 🔴 | No symbolic links. |
| `rmdir` | 🟢 | |
| `sleep` | 🟢 | |
| `swab` | 🟢 | Implemented by Picolibc. |
| `symlink`<br>`symlinkat` | 🔴 | No symbolic links. |
| `sync` | 🟢 | |
| `sysconf` | 🔴 | No conf. |
| `tcgetpgrp`<br>`tcsetpgrp` | 🔴 | No multiple processes. |
| `truncate` | 🟢 | |
| `ttyname` | 🔴 | |
| `unlink` | 🟢 | |
| `unlinkat` | 🔴 | | 
| `write` | 🟢 | |
