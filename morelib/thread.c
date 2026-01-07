// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <malloc.h>
#include "morelib/thread.h"


// Global mutex for thread operations
static SemaphoreHandle_t thread_mutex;

static thread_t *thread_list;

__attribute__((constructor, visibility("hidden")))
void thread_init(void) {
    static StaticSemaphore_t buffer;
    thread_mutex = xSemaphoreCreateMutexStatic(&buffer);
}

#ifndef NDEBUG
static inline bool thread_check_locked(void) {
    return xSemaphoreGetMutexHolder(thread_mutex) == xTaskGetCurrentTaskHandle();
}
#endif

static inline void thread_lock(void) {
    xSemaphoreTake(thread_mutex, portMAX_DELAY);
}

static inline void thread_unlock(void) {
    assert(thread_check_locked());
    xSemaphoreGive(thread_mutex);
}

// Thread creation
static void thread_entry(void *pvParameters) {
    thread_t *thread = pvParameters;
    vTaskSetThreadLocalStoragePointer(NULL, TLS_INDEX_SYS, thread);
    vTaskSetThreadLocalStoragePointer(NULL, TLS_INDEX_APP, NULL);

    // Wait for creation thread to release lock
    thread_lock();
    thread_unlock();

    thread->entry(thread->param);

    thread_exit();
}

static thread_t *thread_alloc(TaskFunction_t pxTaskCode, void *pvParameters) {
    thread_t *thread = malloc(sizeof(thread_t));
    if (thread) {
        thread->next = NULL;
        thread->ref_count = 1;
        thread->handle = NULL;
        thread->id = -1;
        thread->state = 0;
        thread->entry = pxTaskCode;
        thread->param = pvParameters;
        thread->joiner = NULL;
    }
    return thread;
}

static void thread_initialize(thread_t *thread, TaskHandle_t handle) {
    thread->handle = handle;

    // Increment ref count for copy passed as thread entry parameter
    thread->ref_count++;

    // Get the new thread's ID
    TaskStatus_t thread_status;
    vTaskGetInfo(thread->handle, &thread_status, pdFALSE, eRunning);
    thread->id = thread_status.xTaskNumber;

    // Insert new thread in list of all threads
    thread->next = thread_list;
    thread->ref_count++;
    thread_list = thread;
}

// __attribute__((noreturn))
void thread_exit(void) {
    thread_t *thread = thread_current();

    // Remove thread from thread list
    thread_lock();
    thread->handle = NULL;
    thread_t **pthread = &thread_list;
    while (*pthread) {
        if (*pthread == thread) {
            --thread->ref_count;
            // The thread->next pointer is kept intact so that an iterator currently on this thread
            // can still advance to other threads. Consequently another ref count needs to be added 
            // to the next thread in the list.
            if (thread->next) {
                thread->next->ref_count++;
            }
            *pthread = thread->next;
            break;
        }
        pthread = &(*pthread)->next;
    }

    if (thread->joiner) {
        xSemaphoreGive(thread->joiner);
    }
    thread_unlock();

    // Release our reference to the thread
    thread_detach(thread);

    vTaskDelete(NULL);    
}

thread_t *thread_create(TaskFunction_t pxTaskCode, const char *pcName, const configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters, UBaseType_t uxPriority) {
    thread_t *thread = thread_alloc(pxTaskCode, pvParameters);
    if (!thread) {
        return NULL;
    }
    thread_lock();
    TaskHandle_t handle;
    if (xTaskCreate(thread_entry, pcName, MAX(configMINIMAL_STACK_SIZE, usStackDepth), thread, uxPriority, &handle) == pdPASS) {
        thread_initialize(thread, handle);
        thread_unlock();
    } else {
        thread_unlock();
        thread_detach(thread);
        thread = NULL;
    }
    return thread;
}

thread_t *thread_createStatic(TaskFunction_t pxTaskCode, const char *pcName, const configSTACK_DEPTH_TYPE usStackDepth, void *pvParameters, UBaseType_t uxPriority, StackType_t *puxStackBuffer, StaticTask_t *pxTaskBuffer) {
    thread_t *thread = thread_alloc(pxTaskCode, pvParameters);
    if (!thread) {
        return NULL;
    }
    thread_lock();
    TaskHandle_t handle = xTaskCreateStatic(thread_entry, pcName, MAX(configMINIMAL_STACK_SIZE, usStackDepth), thread, uxPriority, puxStackBuffer, pxTaskBuffer);
    if (handle) {
        thread_initialize(thread, handle);
        thread_unlock();
    } else {
        thread_unlock();
        thread_detach(thread);
        thread = NULL;
    }
    return thread;
}


int thread_enable_interrupt(void) {
    thread_t *thread = thread_current();
    if (!thread) {
        return 0;
    }
    thread_lock();
    if (thread->state & TASK_INTERRUPT_SET) {
        thread->state &= ~TASK_INTERRUPT_SET;
        thread_unlock();
        errno = EINTR;
        return -1;
    }
    thread->state |= TASK_INTERRUPT_CAN_ABORT;
    thread_unlock();
    return 0;
}

void thread_disable_interrupt(void) {
    thread_t *thread = thread_current();
    if (!thread) {
        return;
    }
    // Because thread interrupts are enabled, waiting for the thread mutex may be aborted.
    // Ignore aborts and keep retrying to acquire mutex.
    while (!xSemaphoreTake(thread_mutex, portMAX_DELAY)) {
        ;
    }
    thread->state &= ~TASK_INTERRUPT_CAN_ABORT;

    /* This code sets pxCurrentTCB->ucDelayAborted to pdFALSE. This flag is not useful for us
    because xTaskCheckForTimeOut does not distinguish between timeouts and interruptions. We use
    our own flag in TLS to record interruptions instead. */
    TimeOut_t xTimeOut;
    TickType_t xTicksToWait = portMAX_DELAY;
    xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait);
    thread_unlock();
}

void thread_interrupt(thread_t *thread) {
    thread_lock();
    thread->state |= TASK_INTERRUPT_SET;
    if ((thread->state & TASK_INTERRUPT_CAN_ABORT) && thread->handle) {
        while (xTaskAbortDelay(thread->handle) == pdFAIL) {
            // If xTaskAbortDelay fails, it means the target thread is in between calling thread_enable_interrupt and starting its blocking call.
            // Wait a minimal amount of time for the target thread to start blocking so that xTaskAbortDelay succeeds.
            // The target thread cannot disable thread interrupts because we hold the thread lock, so the target thread will eventually block trying to acquire the thread lock.
            vTaskDelay(1);
        }
    }
    thread_unlock();
}

int thread_check_interrupted(void) {
    thread_t *thread = thread_current();
    if (!thread) {
        return 0;
    }    
    thread_lock();
    if (thread->state & TASK_INTERRUPT_SET) {
        thread->state &= ~TASK_INTERRUPT_SET;
        thread_unlock();
        errno = EINTR;
        return -1;
    }
    thread_unlock();
    return 0;
}

thread_t *thread_current(void) {
    return pvTaskGetThreadLocalStoragePointer(NULL, TLS_INDEX_SYS);
}

thread_t *thread_attach(thread_t *thread) {
    thread_lock();
    assert(thread->ref_count > 0);
    thread->ref_count++;
    thread_unlock();
    return thread;
}

void thread_detach(thread_t *thread) {
    // assert(thread_check_locked());
    if (!thread) {
        return;
    }
    thread_lock();
    int ref_count = --thread->ref_count;
    thread_unlock();
    if (ref_count == 0) {
        assert(thread->handle == NULL);
        thread_detach(thread->next);
        if (thread->joiner) {
            vSemaphoreDelete(thread->joiner);
        }
        free(thread);
    }
}


int thread_join(thread_t *thread, TickType_t timeout) {
    if (thread->handle == xTaskGetCurrentTaskHandle()) {
        errno = EINVAL;
        return -1;
    }
    TimeOut_t xTimeOut;
    vTaskSetTimeOutState(&xTimeOut);
    while (!xTaskCheckForTimeOut(&xTimeOut, &timeout)) {
        thread_lock();
        if (!thread->handle) {
            thread_unlock();
            return 0;
        }
        if (!thread->joiner) {
            thread->joiner = xSemaphoreCreateBinary();
        }
        SemaphoreHandle_t joiner = thread->joiner;
        thread_unlock();

        if (thread_enable_interrupt()) {
            return -1;
        }
        if (xSemaphoreTake(joiner, timeout)) {
            xSemaphoreGive(joiner);
        }
        thread_disable_interrupt();
    }
    errno = ETIMEDOUT;
    return -1;
}

bool thread_iterate(thread_t **pthread) {
    thread_t *thread = *pthread;
    thread_lock();
    if (thread) {
        thread = thread->next;
    } else {
        thread = thread_list;
    }
    while (thread && !thread->handle) {
        thread = thread->next;
    }
    if (thread) {
        thread->ref_count++;
    }
    thread_unlock();
    *pthread = thread;
    return thread;
}

thread_t *thread_lookup(UBaseType_t id) {
    thread_t *ret = NULL;
    thread_t *thread = NULL;
    while (thread_iterate(&thread)) {
        if (thread->id == id) {
            ret = thread;
            break;
        }
        thread_detach(thread);
    }
    return ret;
}

TaskHandle_t thread_suspend(thread_t *thread) {
    thread_lock();
    TaskHandle_t handle = thread->handle;
    if (handle && (handle != xTaskGetCurrentTaskHandle())) {
        vTaskSuspend(handle);
    }
    thread_unlock();
    return handle;
}

void thread_resume(TaskHandle_t handle) {
    if (handle != xTaskGetCurrentTaskHandle()) {
        vTaskResume(handle);
    }
}


#if 0 
// C11 threads library

// Mutual exclusion
BaseType_t mtx_init(mtx_t *mutex, uint8_t type) {
    mutex->type = type;
    switch (type) {
        case queueQUEUE_TYPE_MUTEX:
            mutex->mutex = xSemaphoreCreateMutexStatic(&mutex->buffer);
            break;
        case queueQUEUE_TYPE_BINARY_SEMAPHORE:
            mutex->mutex = xSemaphoreCreateBinaryStatic(&mutex->buffer);
            xSemaphoreGive(mutex->mutex);
            break;
        case queueQUEUE_TYPE_RECURSIVE_MUTEX:
            mutex->mutex = xSemaphoreCreateRecursiveMutexStatic(&mutex->buffer);
            break;
        default:
            mutex->mutex = NULL;
            break;
    }
    return mutex->mutex ? pdTRUE : pdFALSE;
}

BaseType_t mtx_timedlock(mtx_t *mutex, TickType_t timeout) {
    BaseType_t ret;
    if (mutex->type == queueQUEUE_TYPE_RECURSIVE_MUTEX) {
        ret = xSemaphoreTakeRecursive(mutex->mutex, timeout);
    } else {
        ret = xSemaphoreTake(mutex->mutex, timeout);
    }
    return ret;
}

BaseType_t mtx_lock(mtx_t *mutex) {
    return mtx_timedlock(mutex, portMAX_DELAY);
}

BaseType_t mtx_trylock(mtx_t *mutex) {
    return mtx_timedlock(mutex, 0);
}

BaseType_t mtx_unlock(mtx_t *mutex) {
    BaseType_t ret;
    if (mutex->type == queueQUEUE_TYPE_RECURSIVE_MUTEX) {
        ret = xSemaphoreGiveRecursive(mutex->mutex);
    } else {
        ret = xSemaphoreGive(mutex->mutex);
    }
    return ret;
}

static BaseType_t mtx_unlock_recursive(mtx_t *mutex) {
    if (mtx_unlock(mutex) == pdFALSE) {
        return 0;
    }
    BaseType_t count = 1;
    if (mutex->type == queueQUEUE_TYPE_RECURSIVE_MUTEX) {
        while (xQueueGetMutexHolder(mutex->mutex) == xTaskGetCurrentTaskHandle()) {
            xSemaphoreGiveRecursive(mutex->mutex);
            count++;
        }
    } 
    return count;
}

void mtx_destroy(mtx_t *mutex) {
    if (mutex->mutex) {
        vSemaphoreDelete(mutex->mutex);
        mutex->mutex = NULL;
    }
}


// Call once
void call_once(once_flag* flag, void (*func)(void)) {
    if (!atomic_flag_test_and_set(flag)) {
        func();
    }
}


// Condition variables
struct cnd_waiter {
    TaskHandle_t task;
    struct cnd_waiter *next;
};

BaseType_t cnd_init(cnd_t *cond) {
    cond->waiters = NULL;
    return pdTRUE;
}

static BaseType_t cnd_signal_internal(cnd_t *cond, bool once) {
    // Find and mark an unsignaled waiter under critical section
    taskENTER_CRITICAL();
    if (cond->waiters != NULL) {
        // Search for the first waiter with non-NULL task (unsignaled)
        struct cnd_waiter *first = cond->waiters->next;
        struct cnd_waiter *current = first;

        do {
            if (current->task != NULL) {
                // Found an unsignaled waiter
                xTaskNotifyGive(current->task);
                current->task = NULL;  // Mark as signaled
                if (once) {
                    break;
                }
            }
            current = current->next;
        } while (current != first);
    }
    taskEXIT_CRITICAL();
    return pdTRUE;
}

BaseType_t cnd_signal(cnd_t *cond) {
    return cnd_signal_internal(cond, true);
}

BaseType_t cnd_broadcast(cnd_t *cond) {
    return cnd_signal_internal(cond, false);
}

BaseType_t cnd_timedwait(cnd_t *cond, mtx_t *mutex, TickType_t timeout) {
    struct cnd_waiter waiter;
    waiter.task = xTaskGetCurrentTaskHandle();
    ulTaskNotifyTake(pdTRUE, 0);  // Clear any old notification

    // Insert at tail of circular list under critical section
    taskENTER_CRITICAL();
    if (cond->waiters == NULL) {
        waiter.next = &waiter;  // Single node points to itself
    } else {
        waiter.next = cond->waiters->next;  // Point to old head
        cond->waiters->next = &waiter;      // Old tail points to new node
    }
    cond->waiters = &waiter;  // New node becomes tail
    taskEXIT_CRITICAL();

    BaseType_t count = mtx_unlock_recursive(mutex);
    uint32_t notified = ulTaskNotifyTake(pdTRUE, timeout);
    
    // Remove ourselves from the list under critical section
    // (whether we were signaled or timed out)
    taskENTER_CRITICAL();
    if (cond->waiters != NULL) {
        if (waiter.next == &waiter) {
            // We're the only waiter
            cond->waiters = NULL;
        } else {
            // Find the node that points to us
            struct cnd_waiter *prev = cond->waiters;
            while (prev->next != &waiter) {
                prev = prev->next;
            }

            // Remove us from the list
            prev->next = waiter.next;

            // If we were the tail, update the tail pointer
            if (cond->waiters == &waiter) {
                cond->waiters = prev;
            }
        }
    }
    taskEXIT_CRITICAL();

    while (count--) {
        mtx_lock(mutex);
    }
    return notified ? pdTRUE : pdFALSE;
}

BaseType_t cnd_wait(cnd_t *cond, mtx_t *mutex) {
    cnd_timedwait(cond, mutex, portMAX_DELAY);
    return pdTRUE;
}

void cnd_destroy(cnd_t *cond) {
    // Nothing to clean up - no dynamic allocation
    (void)cond;
}
#endif