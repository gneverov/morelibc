// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include <stdatomic.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define TLS_INDEX_SYS 0
#define TLS_INDEX_APP 1


static_assert(TLS_INDEX_APP < configNUM_THREAD_LOCAL_STORAGE_POINTERS);

enum thread_interrupt_state {
    TASK_INTERRUPT_SET = 0x1,
    TASK_INTERRUPT_CAN_ABORT = 0x2,
};

typedef struct thread {
    struct thread *next;
    int ref_count;
    TaskHandle_t handle;
    UBaseType_t id;
    enum thread_interrupt_state state;

    TaskFunction_t entry;
    void *param;
    SemaphoreHandle_t joiner;
} thread_t;

// Thread creation
thread_t *thread_create(TaskFunction_t pxTaskCode, const char *pcName, const configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters, UBaseType_t uxPriority);

thread_t *thread_createStatic(TaskFunction_t pxTaskCode, const char *pcName, const configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters, UBaseType_t uxPriority, StackType_t *puxStackBuffer, StaticTask_t *pxTaskBuffer);


// Thread interruption
int thread_enable_interrupt(void);

void thread_disable_interrupt(void);

void thread_interrupt(thread_t *thread);

int thread_check_interrupted(void);


// Thread join
int thread_join(thread_t *thread, TickType_t timeout);

void thread_exit(void);

// Thread reference management
thread_t *thread_attach(thread_t *thread);

void thread_detach(thread_t *thread);


// Thread lookup
thread_t *thread_current(void);

static inline int thread_equal(thread_t *thread1, thread_t *thread2) {
    return thread1 == thread2;
}

bool thread_iterate(thread_t **pthread);

thread_t *thread_lookup(UBaseType_t id);


// Thread control
TaskHandle_t thread_suspend(thread_t *thread);

void thread_resume(TaskHandle_t handle);


// Task utilities
static inline StackType_t *task_pxTopOfStack(TaskHandle_t handle) {
    // assert(eTaskGetState(handle) == eSuspended);
    return *(StackType_t **)handle;
}


#if 0
// C11 threads linrary

// Mutual exclusion
typedef struct {
    SemaphoreHandle_t mutex;
    StaticSemaphore_t buffer;
    uint8_t type;
} mtx_t;

/**
 * Initializes a mutex, or recursive mutex if `recursive` is true.
 * Calls xSemaphoreCreateMutex (or xSemaphoreCreateRecursiveMutex for recursive).
 */
BaseType_t mtx_init(mtx_t *mutex, uint8_t type);

/**
 * Blocks the current thread until the mutex is locked.
 * Returns the value of xSemaphoreTake (or xSemaphoreTakeRecursive).
 */
BaseType_t mtx_lock(mtx_t *mutex);

/**
 * Blocks the current thread until the mutex is locked or until the timeout has been reached.
 * Returns the value of xSemaphoreTake (or xSemaphoreTakeRecursive).
 */
BaseType_t mtx_timedlock(mtx_t *mutex, TickType_t timeout);

/**
 * Tries to lock the mutex without blocking. Returns immediately if the mutex is already locked.
 * Returns the value of xSemaphoreTake (or xSemaphoreTakeRecursive).
 */
BaseType_t mtx_trylock(mtx_t *mutex);

/**
 * Unlocks the mutex.
 * Returns the value of xSemaphoreGive (or xSemaphoreGiveRecursive).
 */
BaseType_t mtx_unlock(mtx_t *mutex);

/**
 * Destroys the mutex.
 * Calls vSemaphoreDelete.
 */
void mtx_destroy(mtx_t *mutex);


// Call once
#define ONCE_FLAG_INIT ATOMIC_FLAG_INIT

typedef atomic_flag once_flag;

/**
 * Calls a function exactly once, even if invoked from several threads.
 */
void call_once(once_flag* flag, void (*func)(void));


// Condition variables
typedef struct {
    struct cnd_waiter *waiters;
} cnd_t;

/**
 * Initializes a condition variable.
 */
BaseType_t cnd_init(cnd_t *cond);

/**
 * Unblocks one thread that currently waits on the condition variable. If no threads are blocked, does nothing.
 * Calls xTaskNotifyGive and returns pdPASS.
 */
BaseType_t cnd_signal(cnd_t *cond);

/**
 * Unblocks all threads that are blocked on the condition variable at the time of the call. If no threads are blocked, the function does nothing.
 * Calls xTaskNotifyGive and returns pdPASS.
 */
BaseType_t cnd_broadcast(cnd_t *cond);

/**
 * Atomically unlocks the mutex and blocks on the condition variable until the thread is signalled by cnd_signal or cnd_broadcast, or until a spurious wake-up occurs. The mutex is locked again before the function returns.
 * Returns the value of ulTaskNotifyTake.
 */
BaseType_t cnd_wait(cnd_t *cond, mtx_t *mutex);

/**
 * Atomically unlocks the mutex and blocks on the condition variable until the thread is signalled by cnd_signal or cnd_broadcast, or until the timeout has been reached, or until a spurious wake-up occurs. The mutex is locked again before the function returns.
 * Returns the value of ulTaskNotifyTake.
 */
BaseType_t cnd_timedwait(cnd_t *cond, mtx_t *mutex, TickType_t timeout);

/**
 * Destroys a condition variable.
 */
void cnd_destroy(cnd_t *cond);


// Thread-local storage
typedef int tss_t;

#define TSS_DTOR_ITERATIONS 0

typedef void (*tss_dtor_t)(void*);

int tss_create(tss_t* tss_key, tss_dtor_t destructor);

void *tss_get(tss_t tss_key);

int tss_set(tss_t tss_id, void *val);

void tss_delete(tss_t tss_id);
#endif