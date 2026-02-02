/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _HARDWARE_PIO_H
#define _HARDWARE_PIO_H

#include "pico/assert.h"
#include "pico/types.h"
#include "hardware/platform_defs.h"
#include "hardware/regs/pio.h"
#include "hardware/pio_instructions.h"

// PICO_CONFIG: PARAM_ASSERTIONS_ENABLED_HARDWARE_PIO, Enable/disable assertions in the hardware_pio module, type=bool, default=0, group=hardware_pio
#ifndef PARAM_ASSERTIONS_ENABLED_HARDWARE_PIO
#ifdef PARAM_ASSERTIONS_ENABLED_PIO // backwards compatibility with SDK < 2.0.0
#define PARAM_ASSERTIONS_ENABLED_HARDWARE_PIO PARAM_ASSERTIONS_ENABLED_PIO
#else
#define PARAM_ASSERTIONS_ENABLED_HARDWARE_PIO 0
#endif
#endif

// PICO_CONFIG: PICO_PIO_VERSION, PIO hardware version, type=int, default=0 on RP2040 and 1 on RP2350, group=hardware_pio
#ifndef PICO_PIO_VERSION
#if PIO_GPIOBASE_BITS
#define PICO_PIO_VERSION 1
#else
#define PICO_PIO_VERSION 0
#endif
#endif

// PICO_CONFIG: PICO_PIO_CLKDIV_ROUND_NEAREST, True if floating point PIO clock divisors should be rounded to the nearest possible clock divisor rather than rounding down, type=bool, default=PICO_CLKDIV_ROUND_NEAREST, group=hardware_pio
#ifndef PICO_PIO_CLKDIV_ROUND_NEAREST
#define PICO_PIO_CLKDIV_ROUND_NEAREST PICO_CLKDIV_ROUND_NEAREST
#endif

/** \file hardware/pio.h
 *  \defgroup hardware_pio hardware_pio
 *
 * \brief Programmable I/O (PIO) API
 *
 * A programmable input/output block (PIO) is a versatile hardware interface which
 * can support a number of different IO standards. 
 * 
 * \if rp2040_specific
 * There are two PIO blocks in the RP2040.
 * \endif
 *
 * \if rp2350_specific
 * There are three PIO blocks in the RP2350
 * \endif
 *
 * Each PIO is programmable in the same sense as a processor: the four state machines independently
 * execute short, sequential programs, to manipulate GPIOs and transfer data. Unlike a general
 * purpose processor, PIO state machines are highly specialised for IO, with a focus on determinism,
 * precise timing, and close integration with fixed-function hardware. Each state machine is equipped
 * with:
 *  * Two 32-bit shift registers – either direction, any shift count
 *  * Two 32-bit scratch registers
 *  * 4×32 bit bus FIFO in each direction (TX/RX), reconfigurable as 8×32 in a single direction
 *  * Fractional clock divider (16 integer, 8 fractional bits)
 *  * Flexible GPIO mapping
 *  * DMA interface, sustained throughput up to 1 word per clock from system DMA
 *  * IRQ flag set/clear/status
 *
 * Full details of the PIO can be found in the appropriate RP-series datasheet. Note that there are
 * additional features in the RP2350 PIO implementation that mean care should be taken when writing PIO
 * code that needs to run on both the RP2040 and the RP2350.
 *
 * \anchor pio_sm_pins
 * \if rp2040_specific
 * On RP2040, pin numbers may always be specified from 0-31
 * \endif
 *
 * \if rp2350_specific
 * On RP2350A, pin numbers may always be specified from 0-31.
 *
 * On RP2350B, there are 48 pins but each PIO instance can only address 32 pins (the PIO
 * instance either addresses pins 0-31 or 16-47 based on \ref pio_set_gpio_base). The
 * `pio_sm_` methods that directly affect the hardware always take _real_ pin numbers in the full range, however:
 *
 * * If `PICO_PIO_USE_GPIO_BASE != 1` then the 5th bit of the pin number is ignored. This is done so
 *   that programs compiled for boards with RP2350A do not incur the extra overhead of dealing with higher pins that don't exist.
 *   Effectively these functions behave exactly like RP2040 in this case.
 *   Note that `PICO_PIO_USE_GPIO_BASE` is defaulted to 0 if `PICO_RP2350A` is 1
 * * If `PICO_PIO_USE_GPIO_BASE == 1` then the passed pin numbers are adjusted internally by subtracting
 *   the GPIO base to give a pin number in the range 0-31 from the PIO's perspective
 *
 * You can set `PARAM_ASSERTIONS_ENABLED_HARDWARE_PIO = 1` to enable parameter checking to debug pin (or other) issues with
 * hardware_pio methods.
 *
 * Note that pin masks follow the same rules as individual pins; bit N of a 32-bit or 64-bit mask always refers to pin N.
 * \endif
 */

#ifdef __cplusplus
extern "C" {
#endif

static_assert(PIO_SM0_SHIFTCTRL_FJOIN_RX_LSB == PIO_SM0_SHIFTCTRL_FJOIN_TX_LSB + 1, "");

/** \brief FIFO join states
 *  \ingroup hardware_pio
 */
enum pio_fifo_join {
    PIO_FIFO_JOIN_NONE = 0,    ///< TX FIFO length=4 is used for transmit, RX FIFO length=4 is used for receive
    PIO_FIFO_JOIN_TX = 1,      ///< TX FIFO length=8 is used for transmit, RX FIFO is disabled
    PIO_FIFO_JOIN_RX = 2,      ///< RX FIFO length=8 is used for receive, TX FIFO is disabled
#if PICO_PIO_VERSION > 0
    PIO_FIFO_JOIN_TXGET = 4,   ///< TX FIFO length=4 is used for transmit, RX FIFO is disabled; space is used for "get" instructions or processor writes
    PIO_FIFO_JOIN_TXPUT = 8,   ///< TX FIFO length=4 is used for transmit, RX FIFO is disabled; space is used for "put" instructions or processor reads
    PIO_FIFO_JOIN_PUTGET = 12, ///< TX FIFO length=4 is used for transmit, RX FIFO is disabled; space is used for "put"/"get" instructions with no processor access
#endif
};

/** \brief MOV status types
 *  \ingroup hardware_pio
 */
enum pio_mov_status_type {
    STATUS_TX_LESSTHAN = 0,
    STATUS_RX_LESSTHAN = 1,
#if PICO_PIO_VERSION > 0
    STATUS_IRQ_SET = 2
#endif
};

#if PICO_PIO_VERSION > 0
#ifndef PICO_PIO_USE_GPIO_BASE
// PICO_CONFIG: PICO_PIO_USE_GPIO_BASE, Enable code for handling more than 32 PIO pins, type=bool, default=true when supported and when the device has more than 32 pins, group=hardware_pio
#define PICO_PIO_USE_GPIO_BASE ((NUM_BANK0_GPIOS) > 32)
#endif
#endif

/** \brief PIO state machine configuration
 *  \defgroup sm_config sm_config
 *  \ingroup hardware_pio
 *
 * A PIO block needs to be configured, these functions provide helpers to set up configuration
 * structures. See \ref pio_sm_set_config
 *
 * \anchor sm_config_pins
 * \if rp2040_specific
 * On RP2040, pin numbers may always be specified from 0-31
 * \endif
 *
 * \if rp2350_specific
 * On RP2350A, pin numbers may always be specified from 0-31.
 *
 * On RP2350B, there are 48 pins but each PIO instance can only address 32 pins (the PIO
 * instance either addresses pins 0-31 or 16-47 based on \ref pio_set_gpio_base). The
 * `sm_config_` state machine configuration always take _real_ pin numbers in the full range, however:
 *
 * * If `PICO_PIO_USE_GPIO_BASE != 1` then the 5th bit of the pin number is ignored. This is done so
 *   that programs compiled for boards with RP2350A do not incur the extra overhead of dealing with higher pins that don't exist.
 *   Effectively these functions behave exactly like RP2040 in this case.
 *   Note that `PICO_PIO_USE_GPIO_BASE` is defaulted to 0 if `PICO_RP2350A` is 1
 * * If `PICO_PIO_USE_GPIO_BASE == 1` then the state machine configuration stores the actual pin numbers in the range 0-47.
 *   Of course in this scenario, it is possible to make an invalid configuration (one which uses pins in both the ranges
 *   0-15 and 32-47).
 *
 *   \ref pio_sm_set_config (or \ref pio_sm_init which calls it) attempts to apply the configuration to a particular PIO's state machine,
 *   and will return PICO_ERROR_BAD_ALIGNMENT if the configuration cannot be applied due to the above problem,
 *   or if the PIO's GPIO base (see \ref pio_set_gpio_base) does not allow access to the required pins.
 *
 *   To be clear, \ref pio_sm_set_config does not change the PIO's GPIO base for you; you must configre the PIO's
 *   GPIO base before calling the method, however you can use \ref pio_claim_free_sm_and_add_program_for_gpio_range
 *   to find/configure a PIO instance suitable for a partiular GPIO range.
 *
 * You can set `PARAM_ASSERTIONS_ENABLED_HARDWARE_PIO = 1` to enable parameter checking to debug pin (or other) issues with
 * hardware_pio methods.
 * \endif
 */

/** \brief PIO Configuration structure
 *  \ingroup sm_config
 *
 * This structure is an in-memory representation of the configuration that can be applied to a PIO
 * state machine later using pio_sm_set_config() or pio_sm_init().
 */
typedef struct {
    uint32_t clkdiv;
    uint32_t execctrl;
    uint32_t shiftctrl;
    uint32_t pinctrl;
#if PICO_PIO_USE_GPIO_BASE
#define PINHI_ALL_PINCTRL_LSBS ((1u << PIO_SM0_PINCTRL_IN_BASE_LSB) | (1u << PIO_SM0_PINCTRL_OUT_BASE_LSB) | \
                               (1u << PIO_SM0_PINCTRL_SET_BASE_LSB) | (1u << PIO_SM0_PINCTRL_SIDESET_BASE_LSB))
// note we put the out_special pin starting at bit 20
#define PINHI_EXECCTRL_LSB 20
static_assert( (1u << PINHI_EXECCTRL_LSB) > (PINHI_ALL_PINCTRL_LSBS * 0x1f), "");
#define PINHI_ALL_PIN_LSBS ((1u << PINHI_EXECCTRL_LSB) |(1u << PIO_SM0_PINCTRL_IN_BASE_LSB) | (1u << PIO_SM0_PINCTRL_OUT_BASE_LSB) | \
                               (1u << PIO_SM0_PINCTRL_SET_BASE_LSB) | (1u << PIO_SM0_PINCTRL_SIDESET_BASE_LSB))
    // each 5-bit field which would usually be used for the pin_base in pin_ctrl, is used for:
    // 0b11111 - corresponding field not specified
    // 0b00000 - pin is in range 0-15
    // 0b00001 - pin is in range 16-31
    // 0b00010 - pin is in range 32-47
    uint32_t pinhi;
#endif
} pio_sm_config;

static inline void check_pio_pin_param(uint pin) {
#if !PICO_PIO_USE_GPIO_BASE
    invalid_params_if(HARDWARE_PIO, pin >= 32);
#else
    // pin base allows us to move up 16 pins at a time
    invalid_params_if(HARDWARE_PIO, pin >= ((NUM_BANK0_GPIOS + 15u)&~15u));
#endif
}

/*! \brief Set the base of the 'out' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'out' pins can overlap with the 'in', 'set' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param out_base First pin to set as output. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 */
static inline void sm_config_set_out_pin_base(pio_sm_config *c, uint out_base) {
    check_pio_pin_param(out_base);
    c->pinctrl = (c->pinctrl & ~PIO_SM0_PINCTRL_OUT_BASE_BITS) |
                 ((out_base & 31) << PIO_SM0_PINCTRL_OUT_BASE_LSB);
#if PICO_PIO_USE_GPIO_BASE
    c->pinhi = (c->pinhi & ~(31u << PIO_SM0_PINCTRL_OUT_BASE_LSB)) |
                    ((out_base >> 4) << PIO_SM0_PINCTRL_OUT_BASE_LSB);
#endif
}

/*! \brief Set the number of 'out' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'out' pins can overlap with the 'in', 'set' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param out_count 0-32 Number of pins to set.
 */
static inline void sm_config_set_out_pin_count(pio_sm_config *c, uint out_count) {
    valid_params_if(HARDWARE_PIO, out_count <= 32);
    c->pinctrl = (c->pinctrl & ~PIO_SM0_PINCTRL_OUT_COUNT_BITS) |
                 (out_count << PIO_SM0_PINCTRL_OUT_COUNT_LSB);
}

/*! \brief Set the 'out' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'out' pins can overlap with the 'in', 'set' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param out_base First pin to set as output. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 * \param out_count 0-32 Number of pins to set.
 */
static inline void sm_config_set_out_pins(pio_sm_config *c, uint out_base, uint out_count) {
    sm_config_set_out_pin_base(c, out_base);
    sm_config_set_out_pin_count(c, out_count);
}

/*! \brief Set the base of the 'set' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'set' pins can overlap with the 'in', 'out' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param set_base First pin to use as 'set'. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 */
static inline void sm_config_set_set_pin_base(pio_sm_config *c, uint set_base) {
    check_pio_pin_param(set_base);
    c->pinctrl = (c->pinctrl & ~PIO_SM0_PINCTRL_SET_BASE_BITS) |
                 ((set_base & 31) << PIO_SM0_PINCTRL_SET_BASE_LSB);
#if PICO_PIO_USE_GPIO_BASE
    c->pinhi = (c->pinhi & ~(31u << PIO_SM0_PINCTRL_SET_BASE_LSB)) |
                    ((set_base >> 4) << PIO_SM0_PINCTRL_SET_BASE_LSB);
#endif
}

/*! \brief Set the count of 'set' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'set' pins can overlap with the 'in', 'out' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param set_count 0-5 Number of pins to set.
 */
static inline void sm_config_set_set_pin_count(pio_sm_config *c, uint set_count) {
    valid_params_if(HARDWARE_PIO, set_count <= 5);
    c->pinctrl = (c->pinctrl & ~PIO_SM0_PINCTRL_SET_COUNT_BITS) |
                 (set_count << PIO_SM0_PINCTRL_SET_COUNT_LSB);
}

/*! \brief Set the 'set' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'set' pins can overlap with the 'in', 'out' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param set_base First pin to use as 'set'. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 * \param set_count 0-5 Number of pins to set.
 */
static inline void sm_config_set_set_pins(pio_sm_config *c, uint set_base, uint set_count) {
    sm_config_set_set_pin_base(c, set_base);
    sm_config_set_set_pin_count(c, set_count);
}

/*! \brief Set the base of the 'in' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'in' pins can overlap with the 'out', 'set' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param in_base First pin to use as input. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 */
static inline void sm_config_set_in_pin_base(pio_sm_config *c, uint in_base) {
    check_pio_pin_param(in_base);
    c->pinctrl = (c->pinctrl & ~PIO_SM0_PINCTRL_IN_BASE_BITS) |
                 ((in_base & 31) << PIO_SM0_PINCTRL_IN_BASE_LSB);
#if PICO_PIO_USE_GPIO_BASE
    c->pinhi = (c->pinhi & ~(31u << PIO_SM0_PINCTRL_IN_BASE_LSB)) |
                    ((in_base >> 4) << PIO_SM0_PINCTRL_IN_BASE_LSB);
#endif
}

/*! \brief Set the base for the 'in' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'in' pins can overlap with the 'out', 'set' and 'sideset' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param in_base First pin to use as input. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 */
static inline void sm_config_set_in_pins(pio_sm_config *c, uint in_base) {
    sm_config_set_in_pin_base(c, in_base);
}

/*! \brief Set the count of 'in' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * When reading pins using the IN pin mapping, this many (low) bits will be read, with the rest taking
 * the value zero.
 *
 * \if rp2040_specific
 * RP2040 does not have the ability to mask unused input pins, so the in_count must be 32
 * \endif
 *
 * \param c Pointer to the configuration structure to modify
 * \param in_count 1-32 The number of pins to include when reading via the IN pin mapping
 */
static inline void sm_config_set_in_pin_count(pio_sm_config *c, uint in_count) {
#if PICO_PIO_VERSION == 0
    // can't be changed from 32 on PIO v0
    ((void)c);
    valid_params_if(HARDWARE_PIO, in_count == 32);
#else
    valid_params_if(HARDWARE_PIO, in_count && in_count <= 32);
    c->shiftctrl = (c->shiftctrl & ~PIO_SM0_SHIFTCTRL_IN_COUNT_BITS) |
                   ((in_count & 0x1fu) << PIO_SM0_SHIFTCTRL_IN_COUNT_LSB);
#endif
}

/*! \brief Set the base of the 'sideset' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * 'sideset' pins can overlap with the 'in', 'out' and 'set' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param sideset_base First pin to use for 'side set'. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 */
static inline void sm_config_set_sideset_pin_base(pio_sm_config *c, uint sideset_base) {
    check_pio_pin_param(sideset_base);
    c->pinctrl = (c->pinctrl & ~PIO_SM0_PINCTRL_SIDESET_BASE_BITS) |
                 ((sideset_base & 31) << PIO_SM0_PINCTRL_SIDESET_BASE_LSB);
#if PICO_PIO_USE_GPIO_BASE
    c->pinhi = (c->pinhi & ~(31u << PIO_SM0_PINCTRL_SIDESET_BASE_LSB)) |
                    ((sideset_base >> 4) << PIO_SM0_PINCTRL_SIDESET_BASE_LSB);
#endif
}

/*! \brief Set the 'sideset' pins in a state machine configuration
 *  \ingroup sm_config
 *
 * This method is identical to \ref sm_config_set_sideset_pin_base, and is provided
 * for backwards compatibility
 *
 * 'sideset' pins can overlap with the 'in', 'out' and 'set' pins
 *
 * \param c Pointer to the configuration structure to modify
 * \param sideset_base First pin to use for 'side set'. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 */
static inline void sm_config_set_sideset_pins(pio_sm_config *c, uint sideset_base) {
    sm_config_set_sideset_pin_base(c, sideset_base);
}

/*! \brief Set the 'sideset' options in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param bit_count Number of bits to steal from delay field in the instruction for use of side set (max 5)
 * \param optional True if the topmost side set bit is used as a flag for whether to apply side set on that instruction
 * \param pindirs True if the side set affects pin directions rather than values
 */
static inline void sm_config_set_sideset(pio_sm_config *c, uint bit_count, bool optional, bool pindirs) {
    valid_params_if(HARDWARE_PIO, bit_count <= 5);
    valid_params_if(HARDWARE_PIO, !optional || bit_count >= 1);
    c->pinctrl = (c->pinctrl & ~PIO_SM0_PINCTRL_SIDESET_COUNT_BITS) |
                 (bit_count << PIO_SM0_PINCTRL_SIDESET_COUNT_LSB);
    c->execctrl = (c->execctrl & ~(PIO_SM0_EXECCTRL_SIDE_EN_BITS | PIO_SM0_EXECCTRL_SIDE_PINDIR_BITS)) |
                  (bool_to_bit(optional) << PIO_SM0_EXECCTRL_SIDE_EN_LSB) |
                  (bool_to_bit(pindirs) << PIO_SM0_EXECCTRL_SIDE_PINDIR_LSB);
}

/*! \brief Set the state machine clock divider (from integer and fractional parts - 16:8) in a state machine configuration
 *  \ingroup sm_config
 *
 * The clock divider can slow the state machine's execution to some rate below
 * the system clock frequency, by enabling the state machine on some cycles
 * but not on others, in a regular pattern. This can be used to generate e.g.
 * a given UART baud rate. See the datasheet for further detail.
 *
 * \param c Pointer to the configuration structure to modify
 * \param div_int Integer part of the divisor
 * \param div_frac8 Fractional part in 1/256ths
 * \sa sm_config_set_clkdiv()
 */
static inline void sm_config_set_clkdiv_int_frac8(pio_sm_config *c, uint32_t div_int, uint8_t div_frac8) {
    static_assert(REG_FIELD_WIDTH(PIO_SM0_CLKDIV_INT) == 16, "");
    invalid_params_if(HARDWARE_PIO, div_int >> 16);
    invalid_params_if(HARDWARE_PIO, div_int == 0 && div_frac8 != 0);
    static_assert(REG_FIELD_WIDTH(PIO_SM0_CLKDIV_FRAC) == 8, "");
    c->clkdiv =
            (((uint)div_frac8) << PIO_SM0_CLKDIV_FRAC_LSB) |
            (((uint)div_int) << PIO_SM0_CLKDIV_INT_LSB);
}

// backwards compatibility
static inline void sm_config_set_clkdiv_int_frac(pio_sm_config *c, uint16_t div_int, uint8_t div_frac8) {
    sm_config_set_clkdiv_int_frac8(c, div_int, div_frac8);
}

static inline void pio_calculate_clkdiv8_from_float(float div, uint32_t *div_int, uint8_t *div_frac8) {
    valid_params_if(HARDWARE_PIO, div >= 1 && div <= 65536);
    const int frac_bit_count = REG_FIELD_WIDTH(PIO_SM0_CLKDIV_FRAC);
#if PICO_PIO_CLKDIV_ROUND_NEAREST
    div += 0.5f / (1 << frac_bit_count); // round to the nearest 1/256
#endif
    *div_int = (uint16_t)div;
    // not a strictly necessary check, but if this changes, then this method should
    // probably no longer be used in favor of one with a larger fraction
    static_assert(REG_FIELD_WIDTH(PIO_SM0_CLKDIV_FRAC) == 8, "");
    if (*div_int == 0) {
        *div_frac8 = 0;
    } else {
        *div_frac8 = (uint8_t)((div - (float)*div_int) * (1u << frac_bit_count));
    }
}

// backwards compatibility
static inline void pio_calculate_clkdiv_from_float(float div, uint16_t *div_int16, uint8_t *div_frac8) {
    uint32_t div_int;
    pio_calculate_clkdiv8_from_float(div, &div_int, div_frac8);
    *div_int16 = (uint16_t) div_int;
}

/*! \brief Set the state machine clock divider (from a floating point value) in a state machine configuration
 *  \ingroup sm_config
 *
 * The clock divider slows the state machine's execution by masking the
 * system clock on some cycles, in a repeating pattern, so that the state
 * machine does not advance. Effectively this produces a slower clock for the
 * state machine to run from, which can be used to generate e.g. a particular
 * UART baud rate. See the datasheet for further detail.
 *
 * \param c Pointer to the configuration structure to modify
 * \param div The fractional divisor to be set. 1 for full speed. An integer clock divisor of n
 *  will cause the state machine to run 1 cycle in every n.
 *  Note that for small n, the jitter introduced by a fractional divider (e.g. 2.5) may be unacceptable
 *  although it will depend on the use case.
 */
static inline void sm_config_set_clkdiv(pio_sm_config *c, float div) {
    uint32_t div_int;
    uint8_t div_frac8;
    pio_calculate_clkdiv8_from_float(div, &div_int, &div_frac8);
    sm_config_set_clkdiv_int_frac8(c, div_int, div_frac8);
}

/*! \brief Set the wrap addresses in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param wrap_target the instruction memory address to wrap to
 * \param wrap        the instruction memory address after which to set the program counter to wrap_target
 *                    if the instruction does not itself update the program_counter
 */
static inline void sm_config_set_wrap(pio_sm_config *c, uint wrap_target, uint wrap) {
    valid_params_if(HARDWARE_PIO, wrap < PIO_INSTRUCTION_COUNT);
    valid_params_if(HARDWARE_PIO, wrap_target < PIO_INSTRUCTION_COUNT);
    c->execctrl = (c->execctrl & ~(PIO_SM0_EXECCTRL_WRAP_TOP_BITS | PIO_SM0_EXECCTRL_WRAP_BOTTOM_BITS)) |
                  (wrap_target << PIO_SM0_EXECCTRL_WRAP_BOTTOM_LSB) |
                  (wrap << PIO_SM0_EXECCTRL_WRAP_TOP_LSB);
}

/*! \brief Set the 'jmp' pin in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param pin The raw GPIO pin number to use as the source for a `jmp pin` instruction. See \ref sm_config_pins "sm_config_ pins" for more detail on pin arguments
 */
static inline void sm_config_set_jmp_pin(pio_sm_config *c, uint pin) {
    check_pio_pin_param(pin);
    c->execctrl = (c->execctrl & ~PIO_SM0_EXECCTRL_JMP_PIN_BITS) |
                  ((pin & 31) << PIO_SM0_EXECCTRL_JMP_PIN_LSB);
#if PICO_PIO_USE_GPIO_BASE
    c->pinhi = (c->pinhi & ~(31u << 20)) |
               ((pin >> 4) << 20);
#endif
}

/*! \brief Setup 'in' shifting parameters in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param shift_right true to shift ISR to right, false to shift ISR to left
 * \param autopush whether autopush is enabled
 * \param push_threshold threshold in bits to shift in before auto/conditional re-pushing of the ISR
 */
static inline void sm_config_set_in_shift(pio_sm_config *c, bool shift_right, bool autopush, uint push_threshold) {
    valid_params_if(HARDWARE_PIO, push_threshold <= 32);
    c->shiftctrl = (c->shiftctrl &
                    ~(PIO_SM0_SHIFTCTRL_IN_SHIFTDIR_BITS |
                      PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS |
                      PIO_SM0_SHIFTCTRL_PUSH_THRESH_BITS)) |
                   (bool_to_bit(shift_right) << PIO_SM0_SHIFTCTRL_IN_SHIFTDIR_LSB) |
                   (bool_to_bit(autopush) << PIO_SM0_SHIFTCTRL_AUTOPUSH_LSB) |
                   ((push_threshold & 0x1fu) << PIO_SM0_SHIFTCTRL_PUSH_THRESH_LSB);
}

/*! \brief Setup 'out' shifting parameters in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param shift_right true to shift OSR to right, false to shift OSR to left
 * \param autopull whether autopull is enabled
 * \param pull_threshold threshold in bits to shift out before auto/conditional re-pulling of the OSR
 */
static inline void sm_config_set_out_shift(pio_sm_config *c, bool shift_right, bool autopull, uint pull_threshold) {
    valid_params_if(HARDWARE_PIO, pull_threshold <= 32);
    c->shiftctrl = (c->shiftctrl &
                    ~(PIO_SM0_SHIFTCTRL_OUT_SHIFTDIR_BITS |
                      PIO_SM0_SHIFTCTRL_AUTOPULL_BITS |
                      PIO_SM0_SHIFTCTRL_PULL_THRESH_BITS)) |
                   (bool_to_bit(shift_right) << PIO_SM0_SHIFTCTRL_OUT_SHIFTDIR_LSB) |
                   (bool_to_bit(autopull) << PIO_SM0_SHIFTCTRL_AUTOPULL_LSB) |
                   ((pull_threshold & 0x1fu) << PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB);
}

/*! \brief Setup the FIFO joining in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param join Specifies the join type. See \ref pio_fifo_join
 */
static inline void sm_config_set_fifo_join(pio_sm_config *c, enum pio_fifo_join join) {
    valid_params_if(HARDWARE_PIO, join == PIO_FIFO_JOIN_NONE || join == PIO_FIFO_JOIN_TX || join == PIO_FIFO_JOIN_RX
#if PICO_PIO_VERSION > 0
        || join == PIO_FIFO_JOIN_TXPUT || join == PIO_FIFO_JOIN_TXGET || join == PIO_FIFO_JOIN_PUTGET
#endif
    );
#if PICO_PIO_VERSION == 0
    c->shiftctrl = (c->shiftctrl & (uint)~(PIO_SM0_SHIFTCTRL_FJOIN_TX_BITS | PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS)) |
                   (((uint)join) << PIO_SM0_SHIFTCTRL_FJOIN_TX_LSB);
#else
    c->shiftctrl = (c->shiftctrl & (uint)~(PIO_SM0_SHIFTCTRL_FJOIN_TX_BITS | PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS |
                                           PIO_SM0_SHIFTCTRL_FJOIN_RX_PUT_BITS | PIO_SM0_SHIFTCTRL_FJOIN_RX_GET_BITS)) |
                   (((uint)(join & 3)) << PIO_SM0_SHIFTCTRL_FJOIN_TX_LSB) |
                   (((uint)(join >> 2)) << PIO_SM0_SHIFTCTRL_FJOIN_RX_GET_LSB);
#endif
}

/*! \brief Set special 'out' operations in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param sticky to enable 'sticky' output (i.e. re-asserting most recent OUT/SET pin values on subsequent cycles)
 * \param has_enable_pin true to enable auxiliary OUT enable pin
 * \param enable_bit_index Data bit index for auxiliary OUT enable.
*/
static inline void sm_config_set_out_special(pio_sm_config *c, bool sticky, bool has_enable_pin, uint enable_bit_index) {
    c->execctrl = (c->execctrl &
                   (uint)~(PIO_SM0_EXECCTRL_OUT_STICKY_BITS | PIO_SM0_EXECCTRL_INLINE_OUT_EN_BITS |
                     PIO_SM0_EXECCTRL_OUT_EN_SEL_BITS)) |
                  (bool_to_bit(sticky) << PIO_SM0_EXECCTRL_OUT_STICKY_LSB) |
                  (bool_to_bit(has_enable_pin) << PIO_SM0_EXECCTRL_INLINE_OUT_EN_LSB) |
                  ((enable_bit_index << PIO_SM0_EXECCTRL_OUT_EN_SEL_LSB) & PIO_SM0_EXECCTRL_OUT_EN_SEL_BITS);
}

/*! \brief Set source for 'mov status' in a state machine configuration
 *  \ingroup sm_config
 *
 * \param c Pointer to the configuration structure to modify
 * \param status_sel the status operation selector. See \ref pio_mov_status_type
 * \param status_n parameter for the mov status operation (currently a bit count)
 */
static inline void sm_config_set_mov_status(pio_sm_config *c, enum pio_mov_status_type status_sel, uint status_n) {
    valid_params_if(HARDWARE_PIO,
                    status_sel == STATUS_TX_LESSTHAN || status_sel == STATUS_RX_LESSTHAN
#if PICO_PIO_VERSION > 0
                    || status_sel == STATUS_IRQ_SET
#endif
    );
    c->execctrl = (c->execctrl
                  & ~(PIO_SM0_EXECCTRL_STATUS_SEL_BITS | PIO_SM0_EXECCTRL_STATUS_N_BITS))
                  | ((((uint)status_sel) << PIO_SM0_EXECCTRL_STATUS_SEL_LSB) & PIO_SM0_EXECCTRL_STATUS_SEL_BITS)
                  | ((status_n << PIO_SM0_EXECCTRL_STATUS_N_LSB) & PIO_SM0_EXECCTRL_STATUS_N_BITS);
}

/*! \brief  Get the default state machine configuration
 *  \ingroup sm_config
 *
 * Setting | Default
 * --------|--------
 * Out Pins | 32 starting at 0
 * Set Pins | 0 starting at 0
 * In Pins | 32 starting at 0
 * Side Set Pins (base) | 0
 * Side Set | disabled
 * Wrap | wrap=31, wrap_to=0
 * In Shift | shift_direction=right, autopush=false, push_threshold=32
 * Out Shift | shift_direction=right, autopull=false, pull_threshold=32
 * Jmp Pin | 0
 * Out Special | sticky=false, has_enable_pin=false, enable_pin_index=0
 * Mov Status | status_sel=STATUS_TX_LESSTHAN, n=0
 *
 * \return the default state machine configuration which can then be modified.
 */
static inline pio_sm_config pio_get_default_sm_config(void) {
    pio_sm_config c = {};
#if PICO_PIO_USE_GPIO_BASE
    c.pinhi = -1;
#endif
    sm_config_set_clkdiv_int_frac8(&c, 1, 0);
    sm_config_set_wrap(&c, 0, 31);
    sm_config_set_in_shift(&c, true, false, 32);
    sm_config_set_out_shift(&c, true, false, 32);
    return c;
}

typedef struct pio_program {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin; // required instruction memory origin or -1
    uint8_t pio_version;
#if PICO_PIO_VERSION > 0
    uint8_t used_gpio_ranges; // bitmap with one bit per 16 pins
#endif
} pio_program_t;

/*! \brief PIO interrupt source numbers for pio related IRQs
 * \ingroup hardware_pio
 */
typedef enum pio_interrupt_source {
    pis_interrupt0 = PIO_INTR_SM0_LSB,                        ///< PIO interrupt 0 is raised
    pis_interrupt1 = PIO_INTR_SM1_LSB,                        ///< PIO interrupt 1 is raised
    pis_interrupt2 = PIO_INTR_SM2_LSB,                        ///< PIO interrupt 2 is raised
    pis_interrupt3 = PIO_INTR_SM3_LSB,                        ///< PIO interrupt 3 is raised
#if PICO_PIO_VERSION > 0
    pis_interrupt4 = PIO_INTR_SM4_LSB,                        ///< PIO interrupt 4 is raised
    pis_interrupt5 = PIO_INTR_SM5_LSB,                        ///< PIO interrupt 5 is raised
    pis_interrupt6 = PIO_INTR_SM6_LSB,                        ///< PIO interrupt 6 is raised
    pis_interrupt7 = PIO_INTR_SM7_LSB,                        ///< PIO interrupt 7 is raised
#endif
    pis_sm0_tx_fifo_not_full = PIO_INTR_SM0_TXNFULL_LSB,      ///< State machine 0 TX FIFO is not full
    pis_sm1_tx_fifo_not_full = PIO_INTR_SM1_TXNFULL_LSB,      ///< State machine 1 TX FIFO is not full
    pis_sm2_tx_fifo_not_full = PIO_INTR_SM2_TXNFULL_LSB,      ///< State machine 2 TX FIFO is not full
    pis_sm3_tx_fifo_not_full = PIO_INTR_SM3_TXNFULL_LSB,      ///< State machine 3 TX FIFO is not full
    pis_sm0_rx_fifo_not_empty = PIO_INTR_SM0_RXNEMPTY_LSB,    ///< State machine 0 RX FIFO is not empty
    pis_sm1_rx_fifo_not_empty = PIO_INTR_SM1_RXNEMPTY_LSB,    ///< State machine 1 RX FIFO is not empty
    pis_sm2_rx_fifo_not_empty = PIO_INTR_SM2_RXNEMPTY_LSB,    ///< State machine 2 RX FIFO is not empty
    pis_sm3_rx_fifo_not_empty = PIO_INTR_SM3_RXNEMPTY_LSB,    ///< State machine 3 RX FIFO is not empty
} pio_interrupt_source_t;

#ifdef __cplusplus
}
#endif

#endif // _PIO_H_
