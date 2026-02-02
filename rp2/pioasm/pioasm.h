// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#ifndef PIOASM_H
#define PIOASM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "hardware/pio.h"
#include "hardware/pio_instructions.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration limits
#define PIOASM_MAX_INSTRUCTIONS 32
#define PIOASM_MAX_SYMBOLS 8
#define PIOASM_MAX_SYMBOL_NAME 16

// Error codes
typedef enum {
    PIOASM_OK = 0,
    PIOASM_ERR_SYNTAX,
    PIOASM_ERR_UNDEFINED_SYMBOL,
    PIOASM_ERR_DUPLICATE_SYMBOL,
    PIOASM_ERR_TOO_MANY_INSTRUCTIONS,
    PIOASM_ERR_TOO_MANY_SYMBOLS,
    PIOASM_ERR_INVALID_VALUE,
    PIOASM_ERR_VALUE_OUT_OF_RANGE,
    PIOASM_ERR_INVALID_INSTRUCTION,
    PIOASM_ERR_INVALID_OPERAND,
    PIOASM_ERR_INVALID_CONDITION,
    PIOASM_ERR_INVALID_DIRECTIVE,
    PIOASM_ERR_MISSING_PROGRAM,
    PIOASM_ERR_CIRCULAR_REFERENCE,
    PIOASM_ERR_PIO_VERSION,
    PIOASM_ERR_SIDESET_TOO_WIDE,
    PIOASM_ERR_DELAY_TOO_LARGE,
} pioasm_error_t;

// Token structure
typedef struct {
    int type;
    union {
        uint uint_value;
        float float_value;
        char str_value[PIOASM_MAX_SYMBOL_NAME];
    };
} pioasm_token_t;

// Symbol entry
typedef struct {
    char name[PIOASM_MAX_SYMBOL_NAME];
    uint value;
    uint is_label : 1;
    uint is_public : 1;
    uint is_resolved : 1;
    uint resolve_in_progress : 1;  // For circular reference detection
} pioasm_symbol_t;

// Unresolved reference (for forward references)
typedef struct {
    uint8_t instruction_index;
    uint8_t symbol_index;      // Index into symbols[], replaces 32-byte name
    uint8_t is_relative : 1;   // For JMP targets
} pioasm_unresolved_t;

// Parser state (allocated internally by pioasm_assemble)
typedef struct pioasm_parser {
    // Source tracking
    const char *source;
    const char *pos;
    uint16_t line;

    // Current token
    pioasm_token_t token;

    // Dynamic symbol table
    pioasm_symbol_t symbols[PIOASM_MAX_SYMBOLS];
    uint8_t symbol_count;

    // Instructions (raw, before encoding) - fixed size (hardware limit)
    uint16_t instructions[PIOASM_MAX_INSTRUCTIONS];

    // Dynamic unresolved forward references
    pioasm_unresolved_t unresolved[PIOASM_MAX_SYMBOLS];
    uint8_t unresolved_count;

    // Program configuration
    char name[PIOASM_MAX_SYMBOL_NAME];
    pio_program_t program;
    int8_t wrap_target;       // -1 if not set
    int8_t wrap;              // -1 if not set

    // State machine config (built up using SDK helper functions)
    pio_sm_config sm_config;

    // Sideset bits needed for instruction encoding
    uint8_t sideset_bits;
    uint8_t sideset_opt : 1;

    // Program state
    uint8_t has_program : 1;

    // Error state
    pioasm_error_t error;
    uint16_t error_line;
} pioasm_parser_t;

/**
 * Apply program load offset to wrap points in sm_config.
 * Call this after loading the program with pio_add_program().
 *
 * @param parser  Pointer to parser state
 * @param offset  Offset returned by pio_add_program()
 */
static inline void pioasm_apply_offset(pioasm_parser_t *parser, uint offset) {
    sm_config_set_wrap(&parser->sm_config,
                       offset + parser->wrap_target,
                       offset + parser->wrap);
}

/**
 * Assemble a PIO program from source text.
 *
 * @param source  Null-terminated PIO assembly source code
 * @param parser  Pointer to parser workspace, where results are also stored.
 *                Can be heap-allocated, static, or from a memory pool.
 * @return        PIOASM_OK on success, error code on failure
 *
 * Example with static allocation:
 *   static pioasm_parser_t parser;
 *   pioasm_assemble(&parser, source);
 *
 * Example with heap allocation:
 *   pioasm_parser_t *parser = malloc(sizeof(pioasm_parser_t));
 *   pioasm_assemble(parser, source);
 *   free(parser);
 */
pioasm_error_t pioasm_assemble(const char *source, pioasm_parser_t *parser);

/**
 * Get a human-readable error message for an error code.
 *
 * @param error  Error code from pioasm_assemble
 * @return       Static string describing the error
 */
const char *pioasm_error_str(pioasm_error_t error);

#ifdef __cplusplus
}
#endif

#endif // PIOASM_H
