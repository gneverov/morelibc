// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#pragma once

#include "morelib/poll.h"
#include "rp2/fifo.h"

#include "hardware/pio.h"

#define PIO_PINS_MAX 32


struct rp2_pio_chip_info {
    uint8_t num_pios;
    uint8_t num_state_machines;
    uint8_t pio_version;
    uint8_t use_gpio_base;
};

struct rp2_pio_request {
    pio_program_t program;
    uint32_t pin_mask;
    uint16_t instructions[PIO_INSTRUCTION_COUNT];
};

struct rp2_pio_sm {
    struct poll_file base;
    PIO pio;
    pio_program_t program;
    uint loaded_offset;
    uint32_t pin_mask;
    uint sm;

    rp2_fifo_t rx_fifo;
    rp2_fifo_t tx_fifo;
    size_t threshold;
};

struct rp2_pio_sm_config {
    pio_sm_config config;
    uint initial_pc;
    uint wrap_target;
    uint wrap;
};

struct rp2_pio_fifo_config {
    size_t fifo_size;
    size_t threshold;
    bool tx;
    enum dma_channel_transfer_size dma_transfer_size;
    bool bswap;
};

struct rp2_pio_pin_config {
    uint32_t mask;
    uint32_t values;
    uint32_t dirs;
    uint32_t pull_ups;
    uint32_t pull_downs;
};

#define RP2_PIO_IOCTL_BASE 0x0300
// Get info about the PIO chip.
// param: struct rp2_pio_chip_info *
#define RP2_PIO_SM_INFO_IOCTL               (RP2_PIO_IOCTL_BASE + 0)

// Request a PIO state machine.
// param: const struct rp2_pio_request *
#define RP2_PIO_SM_OPEN_IOCTL               (RP2_PIO_IOCTL_BASE + 1)

// Configure the PIO state machine, reset it to initial state, and clear any fifos.
// param: const struct rp2_pio_sm_config *
#define RP2_PIO_SM_CONFIGURE_IOCTL          (RP2_PIO_IOCTL_BASE + 2)

// Configure the DMA fifos for reading or writing to the state machine.
// param: const struct rp2_pio_fifo_config *
#define RP2_PIO_SM_CONFIGURE_FIFO_IOCTL     (RP2_PIO_IOCTL_BASE + 3)

// Configure the pins used by the state machine (direction, value, pulls).
// param: const struct rp2_pio_pin_config *
#define RP2_PIO_SM_CONFIGURE_PINS_IOCTL     (RP2_PIO_IOCTL_BASE + 4)

// Enable or disable the state machine.
// param: int
#define RP2_PIO_SM_SET_ENABLED_IOCTL        (RP2_PIO_IOCTL_BASE + 5)

// Get the state machine's current program counter.
// param: uint *
#define RP2_PIO_SM_GET_PC_IOCTL             (RP2_PIO_IOCTL_BASE + 6)

// Execute a state machine instruction.
// param: uint
// returns: 0 if instruction stalled, 1 otherwise
#define RP2_PIO_SM_EXEC_IOCTL               (RP2_PIO_IOCTL_BASE + 7)

// Drain the tx fifo of the state machine.
// param: none
#define RP2_PIO_SM_DRAIN_IOCTL              (RP2_PIO_IOCTL_BASE + 8)

extern const struct dev_driver rp2_pio_drv;

void rp2_pio_sm_debug(int fd);

