// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include "morelib/pipe.h"
#include "morelib/ring.h"

#include "FreeRTOS.h"
#include "semphr.h"


struct pipe_file {
    struct poll_file base;
    struct pipe *pipe;
    int closed;
};

struct pipe {
    SemaphoreHandle_t mutex;
    int ref_count;
    ring_t ring;
    struct pipe_file files[2];
    StaticSemaphore_t mutex_buffer;
};

static void pipe_lock(struct pipe *pipe) {
    xSemaphoreTake(pipe->mutex, portMAX_DELAY);
}

static void pipe_unlock(struct pipe *pipe) {
    xSemaphoreGive(pipe->mutex);
}

static int pipe_close(void *ctx) {
    struct pipe_file *file = ctx;
    struct pipe *pipe = file->pipe;
    pipe_lock(pipe);
    file->closed++;
    int ref_count = --pipe->ref_count;
    pipe_unlock(pipe);
    if (ref_count == 0) {
        vSemaphoreDelete(pipe->mutex);
        ring_free(&pipe->ring);
        free(pipe);
    }
    return 0;
}


static int pipe_read(void *ctx, void *buffer, size_t size) {
    struct pipe_file *file = ctx;
    struct pipe *pipe = file->pipe;
    ring_t *ring = &pipe->ring;
    assert(!pipe->files[0].closed);

    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        pipe_lock(pipe);
        ret = ring_read(ring, buffer, size);
        if (!ret && size && !pipe->files[1].closed) {
            errno = EAGAIN;
            ret = -1;
        }
        if (ring_write_count(ring) >= (ring->size / 4)) {
            poll_file_notify(&pipe->files[1].base, 0, POLLOUT | POLLWRNORM);
        }
        if (!ring_read_count(ring)) {
            poll_file_notify(&pipe->files[0].base, POLLIN | POLLRDNORM, 0);
        }
        pipe_unlock(pipe);
    }
    while (POLL_CHECK(ret, &file->base, POLLIN, &xTicksToWait));
    return ret;
}

static int pipe_write(void *ctx, const void *buffer, size_t size) {
    struct pipe_file *file = ctx;
    struct pipe *pipe = file->pipe;
    ring_t *ring = &pipe->ring;
    assert(!pipe->files[1].closed);

    TickType_t xTicksToWait = portMAX_DELAY;
    int ret;
    do {
        pipe_lock(pipe);
        if (pipe->files[0].closed) {
            errno = EPIPE;
            ret = -1;
        }
        else {
            ret = ring_write(ring, buffer, size);
            if (ret) {
                poll_file_notify(&pipe->files[0].base, 0, POLLIN | POLLRDNORM);
            }
            else if (size) {
                errno = EAGAIN;
                ret = -1;
            }
            if (!ring_write_count(ring)) {
                poll_file_notify(&pipe->files[1].base, POLLOUT | POLLWRNORM, 0);
            }
        } 
        pipe_unlock(pipe);
    }
    while (POLL_CHECK(ret, &file->base, POLLOUT, &xTicksToWait));
    return ret;
}

static const struct vfs_file_vtable pipe_vtable = {
    .close = pipe_close,
    .pollable = 1,
    .read = pipe_read,
    .write = pipe_write,
};

int pipe_pair(struct poll_file *pipes[2]) {
    struct pipe *pipe = malloc(sizeof(struct pipe));
    if (!pipe) {
        return -1;
    }
    if (!ring_alloc(&pipe->ring, 9)) {
        free(pipe);
        return -1;
    }
    pipe->mutex = xSemaphoreCreateMutexStatic(&pipe->mutex_buffer);
    pipe->ref_count = 0;
    for (int i = 0; i < 2; i++) {
        struct pipe_file *file = &pipe->files[i];
        poll_file_init(&file->base, &pipe_vtable, O_RDONLY + i, i ? (POLLOUT | POLLWRNORM) : 0);
        file->pipe = pipe;
        file->closed = 0;
        pipe->ref_count++;
        pipes[i] = &file->base;
    }
    return 0;
}

int pipe(int fildes[2]) {
    struct poll_file *pipes[2]; 
    if (pipe_pair(pipes) < 0) {
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        fildes[i] = poll_file_fd(pipes[i]);
        poll_file_release(pipes[i]);
    }
    int ret = 0;
    if ((fildes[0] < 0) || (fildes[1] < 0)) {
        close(fildes[0]);
        close(fildes[1]);
        ret = -1;
    }
    return ret;
}
