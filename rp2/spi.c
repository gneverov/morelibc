// SPDX-FileCopyrightText: 2024 Gregory Neverov
// SPDX-License-Identifier: MIT

/**
 * Module to synchronize access to an SPI bus.
 * 
 * Only one thread can perform operations on an SPI instance at the same time. Normally a regular 
 * mutex would be sufficient to synchronize this access, but a thread may defer SPI operations to
 * an ISR, and in FreeRTOS an ISR cannot release a mutex on behalf of the thread, so a more complex
 * interface is required that accounts for an ISR owning the SPI resource. A typical usage pattern 
 * is:
 * 
 * // Thread
 *   rp2_spi_take(spi, portMAX_DELAY);
 *   // start SPI transfer
 *   rp2_spi_take_to_isr(spi);
 *   // SPI transfer happening in background
 *   ...
 * 
 * // ISR
 *   // complete SPI transfer
 *   rp2_spi_give_from_isr(spi, pxHigherPriorityTaskWoken);
 * 
 * rp2_spi_take and rp2_spi_give work just like xSemaphoreTake and xSemaphoreGive. The new functions 
 * rp2_spi_take_to_isr transfers ownership to the interrupt context, and rp2_spi_give_from_isr 
 * releases ownership from the interrupt context. Ownership must be acquired by a thread before it 
 * can be transferred to an interrupt. An interrupt cannot acquire ownership directly.
 */

#include <errno.h>
#include <malloc.h>
#include <poll.h>

#include "hardware/gpio.h"

#include "rp2/fifo.h"
#include "rp2/spi.h"


/**
 * Array of ownership-aware SPI instances.
 */
struct rp2_spi rp2_spis[NUM_SPIS] = {
    { .inst = spi0, 0 },
    { .inst = spi1, 0 },
};

__attribute__((constructor, visibility("hidden")))
void rp2_spi_init(void) {
    for (int i = 0; i < NUM_SPIS; i++) {
        rp2_spis[i].mutex = xSemaphoreCreateMutexStatic(&rp2_spis[i].buffer);
    }
}

/**
 * Take ownership of an SPI bus.
 * 
 * Args:
 * spi: SPI bus to take
 * xBlockTime: time to wait to acquire bus
 * 
 * Returns:
 * true if bus was acquired, otherwise false
 */
BaseType_t rp2_spi_take(rp2_spi_t *spi, TickType_t xBlockTime) {
    TimeOut_t timeout;
    vTaskSetTimeOutState(&timeout);
    if (xSemaphoreTake(spi->mutex, xBlockTime) == pdFALSE) {
        return pdFALSE;
    }

    xTaskNotifyStateClear(NULL);
    for (;;) {
        BaseType_t timed_out = xTaskCheckForTimeOut(&timeout, &xBlockTime);
        taskENTER_CRITICAL();
        BaseType_t in_isr = spi->in_isr;
        spi->mutex_holder = in_isr && !timed_out ? xTaskGetCurrentTaskHandle() : NULL;
        taskEXIT_CRITICAL();
        if (!in_isr) {
            return pdTRUE;
        }
        if (timed_out) {
            return pdFALSE;
        }
        ulTaskNotifyTake(pdTRUE, xBlockTime);
    }
}

/**
 * Transfer ownership of an SPI bus to the interrupt context.
 * 
 * The calling thread must have already acquired ownership by calling rp2_spi_take. After calling 
 * this function, the calling thread no longer owns the bus and the bus is owned by the interrupt
 * context.
 * 
 * Args:
 * spi: SPI bus to transfer
 * 
 * Returns:
 * true
 */
BaseType_t rp2_spi_take_to_isr(rp2_spi_t *spi) {
    assert(xQueueGetMutexHolder(spi->mutex) == xTaskGetCurrentTaskHandle());
    assert(spi->mutex_holder == NULL);
    taskENTER_CRITICAL();
    spi->in_isr = 1;
    taskEXIT_CRITICAL();
    return xSemaphoreGive(spi->mutex);
}

/**
 * Give up ownership of an SPI bus from an ISR.
 * 
 * This function can only be called in an ISR and the bus must be owned by the interrupt context. 
 * After calling this function, the interrupt context no longer owns the bus.
 * 
 * Args:
 * spi: the SPI bus to give
 * pxHigherPriorityTaskWoken:
 */
void rp2_spi_give_from_isr(rp2_spi_t *spi, BaseType_t *pxHigherPriorityTaskWoken) {
    assert(spi->in_isr);
    UBaseType_t state = taskENTER_CRITICAL_FROM_ISR();
    spi->in_isr = 0;
    TaskHandle_t task = spi->mutex_holder;
    taskEXIT_CRITICAL_FROM_ISR(state);
    if (task) {
        vTaskNotifyGiveFromISR(task, pxHigherPriorityTaskWoken);
    }
}

/**
 * Give up ownership of an SPI bus.
 * 
 * The calling thread must have already acquired ownership by calling rp2_spi_take. After calling 
 * this function, the calling thread no longer owns the bus.
 * 
 * Args:
 * spi: the SPI bus to give
 * 
 * Returns:
 * true
 */
BaseType_t rp2_spi_give(rp2_spi_t *spi) {
    assert(xQueueGetMutexHolder(spi->mutex) == xTaskGetCurrentTaskHandle());
    return xSemaphoreGive(spi->mutex);
}
