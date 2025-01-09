// SPDX-FileCopyrightText: 2025 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <poll.h>

#define EPOLLIN POLLIN
#define EPOLLPRI POLLPRI
#define EPOLLOUT POLLOUT
#define EPOLLRDNORM POLLRDNORM
#define EPOLLRDBAND POLLRDBAND
#define EPOLLWRNORM POLLWRNORM
#define EPOLLWRBAND POLLWRBAND
#define EPOLLERR POLLERR
#define EPOLLHUP POLLHUP
#define EPOLLONESHOT 0x40000000
#define EPOLLET 0x80000000

/* Valid opcodes ( "op" parameter ) to issue to epoll_ctl().  */
#define EPOLL_CTL_ADD 1	                /* Add a file descriptor to the interface.  */
#define EPOLL_CTL_DEL 2	                /* Remove a file descriptor from the interface.  */
#define EPOLL_CTL_MOD 3	                /* Change file descriptor epoll_event structure.  */


union epoll_data {
    void *ptr;
    int fd;
    uint32_t u32;
};

typedef union epoll_data  epoll_data_t;

struct epoll_event {
    uint32_t events;                    /* Epoll events */
    epoll_data_t data;                  /* User data variable */
};

int epoll_create(int size);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
