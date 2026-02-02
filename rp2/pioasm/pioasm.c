// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "pioasm_internal.h"
#include <string.h>
#include <stdlib.h>

// Error message strings
static const char *error_strings[] = {
    [PIOASM_OK] = "OK",
    [PIOASM_ERR_SYNTAX] = "Syntax error",
    [PIOASM_ERR_UNDEFINED_SYMBOL] = "Undefined symbol",
    [PIOASM_ERR_DUPLICATE_SYMBOL] = "Duplicate symbol",
    [PIOASM_ERR_TOO_MANY_INSTRUCTIONS] = "Too many instructions",
    [PIOASM_ERR_TOO_MANY_SYMBOLS] = "Too many symbols",
    [PIOASM_ERR_INVALID_VALUE] = "Invalid value",
    [PIOASM_ERR_VALUE_OUT_OF_RANGE] = "Value out of range",
    [PIOASM_ERR_INVALID_INSTRUCTION] = "Invalid instruction",
    [PIOASM_ERR_INVALID_OPERAND] = "Invalid operand",
    [PIOASM_ERR_INVALID_CONDITION] = "Invalid condition",
    [PIOASM_ERR_INVALID_DIRECTIVE] = "Invalid directive",
    [PIOASM_ERR_MISSING_PROGRAM] = "Missing .program directive",
    [PIOASM_ERR_CIRCULAR_REFERENCE] = "Circular reference",
    [PIOASM_ERR_PIO_VERSION] = "Feature requires newer PIO version",
    [PIOASM_ERR_SIDESET_TOO_WIDE] = "Side-set value too wide",
    [PIOASM_ERR_DELAY_TOO_LARGE] = "Delay value too large",
};

// Initialize parser state and allocate dynamic arrays
static pioasm_error_t parser_init(pioasm_parser_t *p) {
    memset(p, 0, sizeof(*p));

    p->program.instructions = p->instructions;
    p->program.origin = -1;
    p->wrap_target = -1;
    p->wrap = -1;
    p->error = PIOASM_OK;

    // Initialize sm_config with SDK defaults
    p->sm_config = pio_get_default_sm_config();

    // Set default shift configuration (right shift, no auto, threshold 32)
    // Default threshold of 32 is encoded as 0 in hardware
    sm_config_set_in_shift(&p->sm_config, true, false, 0);
    sm_config_set_out_shift(&p->sm_config, true, false, 0);

    // Set default clock divider (1.0 = no division)
    sm_config_set_clkdiv_int_frac(&p->sm_config, 1, 0);

    return PIOASM_OK;
}

// Free parser dynamic arrays
static void parser_cleanup(pioasm_parser_t *p) {
}

// Main assembly function
pioasm_error_t pioasm_assemble(const char *source, pioasm_parser_t *p) {
    if (!source || !p) {
        return PIOASM_ERR_SYNTAX;
    }

    // Initialize parser (allocates dynamic arrays)
    pioasm_error_t err = parser_init(p);
    if (err != PIOASM_OK) {
        return err;
    }

    // Initialize lexer
    pioasm_lexer_init(p, source);

    // Parse the source
    err = pioasm_parse(p);

    // Copy results even if there was an error (partial results may be useful)
    // copy_result(p, result);

    // Cleanup
    parser_cleanup(p);

    return err;
}

// Get error string
const char *pioasm_error_str(pioasm_error_t error) {
    if (error < 0 || error > PIOASM_ERR_DELAY_TOO_LARGE) {
        return "Unknown error";
    }
    return error_strings[error];
}
