// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "pioasm_internal.h"
#include "hardware/pio_instructions.h"
#include <string.h>
#include <stdlib.h>

typedef unsigned int uint;
// Forward declarations
static pioasm_error_t parse_line(pioasm_parser_t *p);
static pioasm_error_t parse_directive(pioasm_parser_t *p);
static pioasm_error_t parse_instruction(pioasm_parser_t *p);
static pioasm_error_t parse_expression(pioasm_parser_t *p, uint *result);
static pioasm_error_t parse_value(pioasm_parser_t *p, uint *result);

// Helper: set error
static pioasm_error_t set_error(pioasm_parser_t *p, pioasm_error_t err) {
    if (p->error == PIOASM_OK) {
        p->error = err;
        p->error_line = p->line;
    }
    return err;
}

// Helper: advance to next token
static pioasm_error_t advance(pioasm_parser_t *p) {
    pioasm_error_t err = pioasm_lexer_next(p);
    if (err != PIOASM_OK) {
        return set_error(p, err);
    }
    return PIOASM_OK;
}

// Helper: expect a specific token type
static pioasm_error_t expect(pioasm_parser_t *p, pioasm_token_type_t type) {
    if (p->token.type != type) {
        return set_error(p, PIOASM_ERR_SYNTAX);
    }
    return advance(p);
}

// Helper: check if current token is specific type (without consuming)
static bool check(pioasm_parser_t *p, pioasm_token_type_t type) {
    return p->token.type == type;
}

// Helper: consume token if it matches (returns true if consumed)
static bool match(pioasm_parser_t *p, pioasm_token_type_t type) {
    if (check(p, type)) {
        advance(p);
        return true;
    }
    return false;
}

// Helper: skip optional comma
static void skip_optional_comma(pioasm_parser_t *p) {
    match(p, TOK_COMMA);
}

// Symbol table functions
static pioasm_symbol_t *find_symbol(pioasm_parser_t *p, const char *name) {
    for (int i = 0; i < p->symbol_count; i++) {
        if (strncmp(p->symbols[i].name, name, PIOASM_MAX_SYMBOL_NAME) == 0) {
            return &p->symbols[i];
        }
    }
    return NULL;
}

static pioasm_error_t add_symbol(pioasm_parser_t *p, const char *name, uint value,
                                  bool is_label, bool is_public) {
    // Check for existing symbol (might be unresolved placeholder)
    pioasm_symbol_t *existing = find_symbol(p, name);
    if (existing) {
        if (existing->is_resolved) {
            // Already defined - duplicate error
            return set_error(p, PIOASM_ERR_DUPLICATE_SYMBOL);
        }
        // Placeholder from forward reference - resolve it
        existing->value = value;
        existing->is_label = is_label;
        existing->is_public = is_public;
        existing->is_resolved = true;
        existing->resolve_in_progress = false;
        return PIOASM_OK;
    }

    // Grow array if needed
    if (p->symbol_count >= PIOASM_MAX_SYMBOLS) {
        return set_error(p, PIOASM_ERR_TOO_MANY_SYMBOLS);
    }

    pioasm_symbol_t *sym = &p->symbols[p->symbol_count++];
    strncpy(sym->name, name, PIOASM_MAX_SYMBOL_NAME);
    sym->value = value;
    sym->is_label = is_label;
    sym->is_public = is_public;
    sym->is_resolved = true;
    sym->resolve_in_progress = false;

    return PIOASM_OK;
}

// Add unresolved forward reference
static pioasm_error_t add_unresolved(pioasm_parser_t *p, uint instr_idx,
                                      const char *name, bool is_relative) {
    // Grow unresolved array if needed
    if (p->unresolved_count >= PIOASM_MAX_SYMBOLS) {
        return set_error(p, PIOASM_ERR_TOO_MANY_SYMBOLS);
    }

    // Find or create placeholder symbol
    pioasm_symbol_t *sym = find_symbol(p, name);
    uint sym_index;

    if (sym) {
        sym_index = sym - p->symbols;  // Calculate index from pointer
    } else {
        // Grow symbols array if needed for placeholder
        if (p->symbol_count >= PIOASM_MAX_SYMBOLS) {
            return set_error(p, PIOASM_ERR_TOO_MANY_SYMBOLS);
        }

        // Add placeholder symbol (unresolved)
        sym_index = p->symbol_count;
        pioasm_symbol_t *new_sym = &p->symbols[p->symbol_count++];
        strncpy(new_sym->name, name, PIOASM_MAX_SYMBOL_NAME);
        new_sym->value = 0;
        new_sym->is_label = true;  // Assume it's a label for forward refs
        new_sym->is_public = false;
        new_sym->is_resolved = false;
        new_sym->resolve_in_progress = false;
    }

    pioasm_unresolved_t *ref = &p->unresolved[p->unresolved_count++];
    ref->instruction_index = instr_idx;
    ref->symbol_index = sym_index;
    ref->is_relative = is_relative;

    return PIOASM_OK;
}

// Operator precedence for expression parsing
static int get_precedence(pioasm_token_type_t type) {
    switch (type) {
        case TOK_OR:
        case TOK_XOR:
        case TOK_AND:
            return 1;
        case TOK_SHL:
        case TOK_SHR:
            return 2;
        case TOK_PLUS:
        case TOK_MINUS:
            return 3;
        case TOK_MULTIPLY:
        case TOK_DIVIDE:
            return 4;
        default:
            return 0;
    }
}

// Parse a primary expression (value, parenthesized expr, or unary op)
static pioasm_error_t parse_primary(pioasm_parser_t *p, uint *result) {
    pioasm_error_t err;

    // Unary minus
    if (check(p, TOK_MINUS)) {
        advance(p);
        err = parse_primary(p, result);
        if (err != PIOASM_OK) return err;
        *result = -*result;
        return PIOASM_OK;
    }

    // Bit reverse operator ::
    if (check(p, TOK_REVERSE)) {
        advance(p);
        err = parse_primary(p, result);
        if (err != PIOASM_OK) return err;
        // Reverse bits (32-bit)
        uint32_t v = *result;
        uint32_t r = 0;
        for (int i = 0; i < 32; i++) {
            r = (r << 1) | (v & 1);
            v >>= 1;
        }
        *result = r;
        return PIOASM_OK;
    }

    // Parenthesized expression
    if (check(p, TOK_LPAREN)) {
        advance(p);
        err = parse_expression(p, result);
        if (err != PIOASM_OK) return err;
        return expect(p, TOK_RPAREN);
    }

    // Integer literal
    if (check(p, TOK_INT)) {
        *result = p->token.uint_value;
        advance(p);
        return PIOASM_OK;
    }

    // Identifier (symbol reference)
    if (check(p, TOK_ID)) {
        pioasm_symbol_t *sym = find_symbol(p, p->token.str_value);
        if (sym) {
            *result = sym->value;
            advance(p);
            return PIOASM_OK;
        } else {
            // Unresolved symbol - this will be an error later
            // For now, return 0 and let forward reference handling deal with it
            *result = 0;
            advance(p);
            return PIOASM_OK;
        }
    }

    return set_error(p, PIOASM_ERR_SYNTAX);
}

// Parse expression with precedence climbing
static pioasm_error_t parse_expression_prec(pioasm_parser_t *p, uint *result, int min_prec) {
    pioasm_error_t err = parse_primary(p, result);
    if (err != PIOASM_OK) return err;

    while (get_precedence(p->token.type) >= min_prec) {
        pioasm_token_type_t op = p->token.type;
        int prec = get_precedence(op);
        advance(p);

        uint rhs;
        err = parse_expression_prec(p, &rhs, prec + 1);
        if (err != PIOASM_OK) return err;

        switch (op) {
            case TOK_PLUS:     *result = *result + rhs; break;
            case TOK_MINUS:    *result = *result - rhs; break;
            case TOK_MULTIPLY: *result = *result * rhs; break;
            case TOK_DIVIDE:
                if (rhs == 0) return set_error(p, PIOASM_ERR_INVALID_VALUE);
                *result = *result / rhs;
                break;
            case TOK_AND:      *result = *result & rhs; break;
            case TOK_OR:       *result = *result | rhs; break;
            case TOK_XOR:      *result = *result ^ rhs; break;
            case TOK_SHL:      *result = *result << rhs; break;
            case TOK_SHR:      *result = *result >> rhs; break;
            default: break;
        }
    }

    return PIOASM_OK;
}

// Parse expression
static pioasm_error_t parse_expression(pioasm_parser_t *p, uint *result) {
    return parse_expression_prec(p, result, 1);
}

// Parse value (more limited: int, id, or parenthesized expression)
static pioasm_error_t parse_value(pioasm_parser_t *p, uint *result) {
    if (check(p, TOK_INT)) {
        *result = p->token.uint_value;
        advance(p);
        return PIOASM_OK;
    }
    if (check(p, TOK_ID)) {
        pioasm_symbol_t *sym = find_symbol(p, p->token.str_value);
        if (sym) {
            *result = sym->value;
        } else {
            *result = 0;  // Will be resolved later
        }
        advance(p);
        return PIOASM_OK;
    }
    if (check(p, TOK_LPAREN)) {
        advance(p);
        pioasm_error_t err = parse_expression(p, result);
        if (err != PIOASM_OK) return err;
        return expect(p, TOK_RPAREN);
    }
    return set_error(p, PIOASM_ERR_SYNTAX);
}

// Parse value that might be a symbol reference (returns symbol name if unresolved)
static pioasm_error_t parse_value_or_symbol(pioasm_parser_t *p, uint *result,
                                            char *symbol_name, bool *is_symbol) {
    *is_symbol = false;

    if (check(p, TOK_INT)) {
        *result = p->token.uint_value;
        advance(p);
        return PIOASM_OK;
    }
    if (check(p, TOK_ID)) {
        pioasm_symbol_t *sym = find_symbol(p, p->token.str_value);
        if (sym) {
            *result = sym->value;
        } else {
            // Unresolved - save symbol name
            strncpy(symbol_name, p->token.str_value, PIOASM_MAX_SYMBOL_NAME);
            *is_symbol = true;
            *result = 0;
        }
        advance(p);
        return PIOASM_OK;
    }
    if (check(p, TOK_LPAREN)) {
        advance(p);
        pioasm_error_t err = parse_expression(p, result);
        if (err != PIOASM_OK) return err;
        return expect(p, TOK_RPAREN);
    }
    return set_error(p, PIOASM_ERR_SYNTAX);
}

// Encode delay and sideset using SDK functions
static uint encode_delay_sideset(pioasm_parser_t *p, uint delay, uint sideset, bool has_sideset) {
    uint sideset_bits = p->sideset_bits;
    if (p->sideset_opt && sideset_bits > 0) {
        sideset_bits++;  // Extra bit for optional sideset
    }

    uint delay_bits = 5 - sideset_bits;
    uint delay_max = (1 << delay_bits) - 1;
    uint sideset_max = sideset_bits > 0 ? ((1 << p->sideset_bits) - 1) : 0;

    // Validate delay
    if (delay > delay_max) {
        set_error(p, PIOASM_ERR_DELAY_TOO_LARGE);
        delay = delay_max;
    }

    uint result = pio_encode_delay(delay);

    if (has_sideset) {
        if (sideset > sideset_max) {
            set_error(p, PIOASM_ERR_SIDESET_TOO_WIDE);
            sideset = sideset_max;
        }
        if (p->sideset_opt) {
            result |= pio_encode_sideset_opt(p->sideset_bits, sideset);
        } else if (p->sideset_bits > 0) {
            result |= pio_encode_sideset(p->sideset_bits, sideset);
        }
    }

    return result;
}

// Parse delay and/or side-set
static pioasm_error_t parse_delay_sideset(pioasm_parser_t *p, uint *delay_sideset) {
    uint delay = 0;
    uint sideset;
    bool has_sideset = false;

    // Can have side-set and/or delay in any order
    while (check(p, TOK_SIDE) || check(p, TOK_LBRACKET)) {
        if (match(p, TOK_SIDE)) {
            pioasm_error_t err = parse_value(p, &sideset);
            if (err != PIOASM_OK) return err;
            has_sideset = true;
        } else if (match(p, TOK_LBRACKET)) {
            pioasm_error_t err = parse_expression(p, &delay);
            if (err != PIOASM_OK) return err;
            pioasm_error_t err2 = expect(p, TOK_RBRACKET);
            if (err2 != PIOASM_OK) return err2;
        }
    }

    *delay_sideset = encode_delay_sideset(p, delay, sideset, has_sideset);
    return PIOASM_OK;
}

// Add instruction to program
static pioasm_error_t add_instruction(pioasm_parser_t *p, uint instr) {
    if (p->program.length >= PIOASM_MAX_INSTRUCTIONS) {
        return set_error(p, PIOASM_ERR_TOO_MANY_INSTRUCTIONS);
    }
    p->instructions[p->program.length++] = instr;
    return PIOASM_OK;
}

// Parse JMP condition
static pioasm_error_t parse_jmp_condition(pioasm_parser_t *p, pioasm_condition_t *cond) {
    *cond = COND_ALWAYS;

    if (match(p, TOK_NOT)) {
        if (match(p, TOK_X)) {
            *cond = COND_X_ZERO;
        } else if (match(p, TOK_Y)) {
            *cond = COND_Y_ZERO;
        } else if (match(p, TOK_OSRE)) {
            *cond = COND_OSRE_ZERO;
        } else {
            return set_error(p, PIOASM_ERR_INVALID_CONDITION);
        }
        return PIOASM_OK;
    }

    if (check(p, TOK_X)) {
        advance(p);
        if (match(p, TOK_POST_DEC)) {
            *cond = COND_X_NONZERO_DEC;
        } else if (match(p, TOK_NOT_EQUAL)) {
            if (!match(p, TOK_Y)) {
                return set_error(p, PIOASM_ERR_INVALID_CONDITION);
            }
            *cond = COND_X_NE_Y;
        } else {
            return set_error(p, PIOASM_ERR_INVALID_CONDITION);
        }
        return PIOASM_OK;
    }

    if (check(p, TOK_Y)) {
        advance(p);
        if (!match(p, TOK_POST_DEC)) {
            return set_error(p, PIOASM_ERR_INVALID_CONDITION);
        }
        *cond = COND_Y_NONZERO_DEC;
        return PIOASM_OK;
    }

    if (match(p, TOK_PIN)) {
        *cond = COND_PIN;
        return PIOASM_OK;
    }

    // No condition - unconditional jump
    return PIOASM_OK;
}

// Parse JMP instruction
static pioasm_error_t parse_jmp(pioasm_parser_t *p) {
    pioasm_condition_t cond;
    pioasm_error_t err = parse_jmp_condition(p, &cond);
    if (err != PIOASM_OK) return err;

    skip_optional_comma(p);

    // Parse target (label or expression)
    char symbol_name[PIOASM_MAX_SYMBOL_NAME];
    bool is_symbol;
    uint target;

    err = parse_value_or_symbol(p, &target, symbol_name, &is_symbol);
    if (err != PIOASM_OK) return err;

    // Parse delay/sideset
    uint delay_sideset;
    err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    // Encode JMP using SDK functions based on condition
    uint instr = _pio_encode_instr_and_args(pio_instr_bits_jmp, cond, target);
    instr |= delay_sideset;

    err = add_instruction(p, instr);
    if (err != PIOASM_OK) return err;

    // Add forward reference if symbol was unresolved
    if (is_symbol) {
        err = add_unresolved(p, p->program.length - 1, symbol_name, false);
        if (err != PIOASM_OK) return err;
    }

    return PIOASM_OK;
}

// Parse WAIT source
static pioasm_error_t parse_wait_source(pioasm_parser_t *p, pioasm_wait_src_t *src, uint *index) {
    if (match(p, TOK_GPIO)) {
        skip_optional_comma(p);
        *src = WAIT_GPIO;
        return parse_value(p, index);
    }

    if (match(p, TOK_PIN)) {
        skip_optional_comma(p);
        *src = WAIT_PIN;
        return parse_value(p, index);
    }

    if (match(p, TOK_JMPPIN)) {
        if (p->program.pio_version < 1) {
            return set_error(p, PIOASM_ERR_PIO_VERSION);
        }
        *src = WAIT_JMPPIN;
        *index = 0;
        if (match(p, TOK_PLUS)) {
            return parse_value(p, index);
        }
        return PIOASM_OK;
    }

    if (match(p, TOK_IRQ)) {
        *src = WAIT_IRQ;
        uint irq_type = 0;

        // Check for prev/next
        if (match(p, TOK_PREV)) {
            if (p->program.pio_version < 1) {
                return set_error(p, PIOASM_ERR_PIO_VERSION);
            }
            irq_type = 1;
        } else if (match(p, TOK_NEXT)) {
            if (p->program.pio_version < 1) {
                return set_error(p, PIOASM_ERR_PIO_VERSION);
            }
            irq_type = 3;
        }

        skip_optional_comma(p);
        pioasm_error_t err = parse_value(p, index);
        if (err != PIOASM_OK) return err;
        if (*index > 7) {
            return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
        }

        // Check for rel (only if not prev/next)
        if (irq_type == 0 && match(p, TOK_REL)) {
            irq_type = 2;
        }
        *index |= irq_type << 3;

        return PIOASM_OK;
    }

    return set_error(p, PIOASM_ERR_INVALID_OPERAND);
}

// Parse WAIT instruction
static pioasm_error_t parse_wait(pioasm_parser_t *p) {
    uint polarity = 4;

    // Polarity is optional (defaults to 1)
    if (check(p, TOK_INT)) {
        polarity = p->token.uint_value ? 4 : 0;
        advance(p);
    }

    pioasm_wait_src_t src;
    uint index;
    pioasm_error_t err = parse_wait_source(p, &src, &index);
    if (err != PIOASM_OK) return err;

    // Parse delay/sideset
    uint delay_sideset;
    err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    // Encode WAIT using SDK functions
    uint instr = _pio_encode_instr_and_args(pio_instr_bits_wait, polarity | src, index);
    instr |= delay_sideset;

    return add_instruction(p, instr);
}

// Parse IN/OUT source/destination using SDK's enum pio_src_dest
static pioasm_error_t parse_in_out_target(pioasm_parser_t *p, enum pio_src_dest *target, bool is_in) {
    if (match(p, TOK_PINS)) {
        *target = pio_pins;
        return PIOASM_OK;
    }
    if (match(p, TOK_X)) {
        *target = pio_x;
        return PIOASM_OK;
    }
    if (match(p, TOK_Y)) {
        *target = pio_y;
        return PIOASM_OK;
    }
    if (match(p, TOK_NULL)) {
        *target = pio_null;
        return PIOASM_OK;
    }
    if (match(p, TOK_PINDIRS)) {
        *target = pio_pindirs;
        return PIOASM_OK;
    }
    if (match(p, TOK_ISR)) {
        *target = pio_isr;
        return PIOASM_OK;
    }
    if (is_in) {
        if (match(p, TOK_OSR)) {
            *target = pio_osr;
            return PIOASM_OK;
        }
        if (match(p, TOK_STATUS)) {
            *target = pio_status;
            return PIOASM_OK;
        }
    } else {
        if (match(p, TOK_PC)) {
            *target = pio_pc;
            return PIOASM_OK;
        }
        if (match(p, TOK_EXEC)) {
            *target = pio_exec_out;
            return PIOASM_OK;
        }
    }
    return set_error(p, PIOASM_ERR_INVALID_OPERAND);
}

// Parse IN instruction
static pioasm_error_t parse_in(pioasm_parser_t *p) {
    enum pio_src_dest src;
    pioasm_error_t err = parse_in_out_target(p, &src, true);
    if (err != PIOASM_OK) return err;

    skip_optional_comma(p);

    uint bit_count;
    err = parse_value(p, &bit_count);
    if (err != PIOASM_OK) return err;

    if (bit_count < 1 || bit_count > 32) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    // Parse delay/sideset
    uint delay_sideset;
    err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = pio_encode_in(src, bit_count) | delay_sideset;
    return add_instruction(p, instr);
}

// Parse OUT instruction
static pioasm_error_t parse_out(pioasm_parser_t *p) {
    enum pio_src_dest dest;
    pioasm_error_t err = parse_in_out_target(p, &dest, false);
    if (err != PIOASM_OK) return err;

    skip_optional_comma(p);

    uint bit_count;
    err = parse_value(p, &bit_count);
    if (err != PIOASM_OK) return err;

    if (bit_count < 1 || bit_count > 32) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    // Parse delay/sideset
    uint delay_sideset;
    err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = pio_encode_out(dest, bit_count) | delay_sideset;
    return add_instruction(p, instr);
}

// Parse PUSH instruction
static pioasm_error_t parse_push(pioasm_parser_t *p) {
    bool if_full = match(p, TOK_IFFULL);
    bool blocking = true;

    if (match(p, TOK_BLOCK)) {
        blocking = true;
    } else if (match(p, TOK_NOBLOCK)) {
        blocking = false;
    }

    // Parse delay/sideset
    uint delay_sideset;
    pioasm_error_t err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = pio_encode_push(if_full, blocking) | delay_sideset;
    return add_instruction(p, instr);
}

// Parse PULL instruction
static pioasm_error_t parse_pull(pioasm_parser_t *p) {
    bool if_empty = match(p, TOK_IFEMPTY);
    bool blocking = true;

    if (match(p, TOK_BLOCK)) {
        blocking = true;
    } else if (match(p, TOK_NOBLOCK)) {
        blocking = false;
    }

    // Parse delay/sideset
    uint delay_sideset;
    pioasm_error_t err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = pio_encode_pull(if_empty, blocking) | delay_sideset;
    return add_instruction(p, instr);
}

// Parse MOV source/destination
// Returns enum pio_src_dest for standard values, or MOV_FIFO_* for RP2350 FIFO
static pioasm_error_t parse_mov_target(pioasm_parser_t *p, uint *target, bool is_src) {
    if (match(p, TOK_PINS)) {
        *target = pio_pins;
        return PIOASM_OK;
    }
    if (match(p, TOK_X)) {
        *target = pio_x;
        return PIOASM_OK;
    }
    if (match(p, TOK_Y)) {
        *target = pio_y;
        return PIOASM_OK;
    }
    if (match(p, TOK_ISR)) {
        *target = pio_isr;
        return PIOASM_OK;
    }
    if (match(p, TOK_OSR)) {
        *target = pio_osr;
        return PIOASM_OK;
    }

    if (is_src) {
        if (match(p, TOK_NULL)) {
            *target = pio_null;
            return PIOASM_OK;
        }
        if (match(p, TOK_STATUS)) {
            *target = pio_status;
            return PIOASM_OK;
        }
    } else {
        if (match(p, TOK_EXEC)) {
            *target = pio_exec_mov;
            return PIOASM_OK;
        }
        if (match(p, TOK_PC)) {
            *target = pio_pc;
            return PIOASM_OK;
        }
        if (match(p, TOK_PINDIRS)) {
            if (p->program.pio_version < 1) {
                return set_error(p, PIOASM_ERR_PIO_VERSION);
            }
            *target = pio_pindirs;
            return PIOASM_OK;
        }
    }

    // RP2350 rxfifo[y] and rxfifo[n]
    // if (match(p, TOK_RXFIFO)) {
    //     if (p->program.pio_version < 1) {
    //         return set_error(p, PIOASM_ERR_PIO_VERSION);
    //     }
    //     pioasm_error_t err = expect(p, TOK_LBRACKET);
    //     if (err != PIOASM_OK) return err;

    //     if (match(p, TOK_Y)) {
    //         *target = MOV_FIFO_Y;
    //     } else {
    //         uint idx;
    //         err = parse_value(p, &idx);
    //         if (err != PIOASM_OK) return err;
    //         *target = MOV_FIFO_IDX + idx;
    //     }

    //     return expect(p, TOK_RBRACKET);
    // }

    return set_error(p, PIOASM_ERR_INVALID_OPERAND);
}

// Parse MOV operation
static pioasm_error_t parse_mov_op(pioasm_parser_t *p, pioasm_mov_op_t *op) {
    *op = MOV_OP_NONE;

    if (match(p, TOK_NOT)) {
        *op = MOV_OP_INVERT;
        return PIOASM_OK;
    }
    if (match(p, TOK_REVERSE)) {
        *op = MOV_OP_REVERSE;
        return PIOASM_OK;
    }

    return PIOASM_OK;
}

// Parse MOV instruction
static pioasm_error_t parse_mov(pioasm_parser_t *p) {
    uint dest;
    pioasm_error_t err = parse_mov_target(p, &dest, false);
    if (err != PIOASM_OK) return err;

    skip_optional_comma(p);

    pioasm_mov_op_t op;
    err = parse_mov_op(p, &op);
    if (err != PIOASM_OK) return err;

    uint src;
    err = parse_mov_target(p, &src, true);
    if (err != PIOASM_OK) return err;

    // Parse delay/sideset
    uint delay_sideset;
    err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = _pio_encode_instr_and_src_dest(pio_instr_bits_mov, dest, ((op & 3u) << 3u) | (src & 7u));
    instr |= delay_sideset;

    return add_instruction(p, instr);
}

// Parse IRQ instruction
static pioasm_error_t parse_irq(pioasm_parser_t *p) {
    pioasm_irq_t mode = IRQ_SET;
    uint irq_type = 0;  // 0=normal, 1=prev, 2=rel, 3=next

    // Check for prev/next first
    if (match(p, TOK_PREV)) {
        if (p->program.pio_version < 1) {
            return set_error(p, PIOASM_ERR_PIO_VERSION);
        }
        irq_type = 1;
    } else if (match(p, TOK_NEXT)) {
        if (p->program.pio_version < 1) {
            return set_error(p, PIOASM_ERR_PIO_VERSION);
        }
        irq_type = 3;
    }

    // Parse mode
    if (match(p, TOK_CLEAR)) {
        mode = IRQ_CLEAR;
    } else if (match(p, TOK_WAIT)) {
        mode = IRQ_SET_WAIT;
    } else if (match(p, TOK_NOWAIT) || match(p, TOK_SET)) {
        mode = IRQ_SET;
    }

    // Parse IRQ number
    uint irq_num;
    pioasm_error_t err = parse_value(p, &irq_num);
    if (err != PIOASM_OK) return err;

    if (irq_num > 7) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    // Check for rel (only if not prev/next)
    if (irq_type == 0 && match(p, TOK_REL)) {
        irq_type = 2;
    }

    // Parse delay/sideset
    uint delay_sideset;
    err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = _pio_encode_instr_and_args(pio_instr_bits_irq, mode, (irq_type << 3) | irq_num);
    instr |= delay_sideset;

    return add_instruction(p, instr);
}

// Parse SET target
static pioasm_error_t parse_set_target(pioasm_parser_t *p, enum pio_src_dest *target) {
    if (match(p, TOK_PINS)) {
        *target = pio_pins;
        return PIOASM_OK;
    }
    if (match(p, TOK_X)) {
        *target = pio_x;
        return PIOASM_OK;
    }
    if (match(p, TOK_Y)) {
        *target = pio_y;
        return PIOASM_OK;
    }
    if (match(p, TOK_PINDIRS)) {
        *target = pio_pindirs;
        return PIOASM_OK;
    }
    return set_error(p, PIOASM_ERR_INVALID_OPERAND);
}

// Parse SET instruction
static pioasm_error_t parse_set(pioasm_parser_t *p) {
    enum pio_src_dest dest;
    pioasm_error_t err = parse_set_target(p, &dest);
    if (err != PIOASM_OK) return err;

    skip_optional_comma(p);

    uint value;
    err = parse_value(p, &value);
    if (err != PIOASM_OK) return err;

    if (value > 31) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    // Parse delay/sideset
    uint delay_sideset;
    err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = pio_encode_set(dest, value) | delay_sideset;
    return add_instruction(p, instr);
}

// Parse NOP instruction
static pioasm_error_t parse_nop(pioasm_parser_t *p) {
    // Parse delay/sideset
    uint delay_sideset;
    pioasm_error_t err = parse_delay_sideset(p, &delay_sideset);
    if (err != PIOASM_OK) return err;

    uint instr = pio_encode_nop() | delay_sideset;
    return add_instruction(p, instr);
}

// Parse .word directive
static pioasm_error_t parse_word(pioasm_parser_t *p) {
    uint value;
    pioasm_error_t err = parse_value(p, &value);
    if (err != PIOASM_OK) return err;

    if (value > 0xffff) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    return add_instruction(p, value);
}

// Parse instruction
static pioasm_error_t parse_instruction(pioasm_parser_t *p) {
    if (!p->has_program) {
        return set_error(p, PIOASM_ERR_MISSING_PROGRAM);
    }

    switch (p->token.type) {
        case TOK_JMP:  advance(p); return parse_jmp(p);
        case TOK_WAIT: advance(p); return parse_wait(p);
        case TOK_IN:   advance(p); return parse_in(p);
        case TOK_OUT:  advance(p); return parse_out(p);
        case TOK_PUSH: advance(p); return parse_push(p);
        case TOK_PULL: advance(p); return parse_pull(p);
        case TOK_MOV:  advance(p); return parse_mov(p);
        case TOK_IRQ:  advance(p); return parse_irq(p);
        case TOK_SET:  advance(p); return parse_set(p);
        case TOK_NOP:  advance(p); return parse_nop(p);
        case TOK_WORD: advance(p); return parse_word(p);
        default:
            return set_error(p, PIOASM_ERR_INVALID_INSTRUCTION);
    }
}

// Parse .program directive
static pioasm_error_t parse_program_directive(pioasm_parser_t *p) {
    if (!check(p, TOK_ID)) {
        return set_error(p, PIOASM_ERR_SYNTAX);
    }
    // Store the program name
    strncpy(p->name, p->token.str_value, PIOASM_MAX_SYMBOL_NAME);
    advance(p);
    p->has_program = true;
    return PIOASM_OK;
}

// Parse .lang_opt directive (ignored)
static pioasm_error_t parse_lang_opt_directive(pioasm_parser_t *p) {
    // Skip to end of line at the character level (don't tokenize)
    while (*p->pos && *p->pos != '\n') {
        p->pos++;
    }
    // Re-scan the next token (will be NEWLINE or EOF)
    return pioasm_lexer_next(p);
}

// Parse .define directive
static pioasm_error_t parse_define_directive(pioasm_parser_t *p) {
    bool is_public = false;

    // Check for public modifier
    if (match(p, TOK_PUBLIC) || match(p, TOK_MULTIPLY)) {
        is_public = true;
    }

    if (!check(p, TOK_ID)) {
        return set_error(p, PIOASM_ERR_SYNTAX);
    }

    char name[PIOASM_MAX_SYMBOL_NAME];
    strncpy(name, p->token.str_value, PIOASM_MAX_SYMBOL_NAME);
    advance(p);

    uint value;
    pioasm_error_t err = parse_expression(p, &value);
    if (err != PIOASM_OK) return err;

    return add_symbol(p, name, value, false, is_public);
}

// Parse .origin directive
static pioasm_error_t parse_origin_directive(pioasm_parser_t *p) {
    uint value;
    pioasm_error_t err = parse_value(p, &value);
    if (err != PIOASM_OK) return err;

    if (value >= PIOASM_MAX_INSTRUCTIONS) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    p->program.origin = value;
    return PIOASM_OK;
}

// Parse .side_set directive
static pioasm_error_t parse_sideset_directive(pioasm_parser_t *p) {
    uint bits;
    pioasm_error_t err = parse_value(p, &bits);
    if (err != PIOASM_OK) return err;

    if (bits > 5) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    p->sideset_bits = bits;
    p->sideset_opt = false;
    bool sideset_pindirs = false;

    // Check for optional and pindirs
    while (check(p, TOK_OPTIONAL) || check(p, TOK_PINDIRS)) {
        if (match(p, TOK_OPTIONAL)) {
            p->sideset_opt = true;
        }
        if (match(p, TOK_PINDIRS)) {
            sideset_pindirs = true;
        }
    }

    // Configure sm_config with sideset settings
    // Total bits = sideset_bits + 1 if optional (for enable bit)
    uint sideset_bit_count = p->sideset_bits;
    if (p->sideset_opt && sideset_bit_count > 0) {
        sideset_bit_count++;
    }
    sm_config_set_sideset(&p->sm_config, sideset_bit_count, p->sideset_opt, sideset_pindirs);

    return PIOASM_OK;
}

// Parse .wrap_target directive
static pioasm_error_t parse_wrap_target_directive(pioasm_parser_t *p) {
    p->wrap_target = p->program.length;
    return PIOASM_OK;
}

// Parse .wrap directive
static pioasm_error_t parse_wrap_directive(pioasm_parser_t *p) {
    p->wrap = p->program.length > 0 ? p->program.length - 1 : 0;
    return PIOASM_OK;
}

// Parse .pio_version directive
static pioasm_error_t parse_pio_version_directive(pioasm_parser_t *p) {
    if (match(p, TOK_RP2040)) {
        p->program.pio_version = 0;
    } else if (match(p, TOK_RP2350)) {
        p->program.pio_version = 1;
    } else {
        uint version;
        pioasm_error_t err = parse_value(p, &version);
        if (err != PIOASM_OK) return err;
        p->program.pio_version = version;
    }
    return PIOASM_OK;
}

// Parse .clock_div directive
static pioasm_error_t parse_clock_div_directive(pioasm_parser_t *p) {
    if (check(p, TOK_FLOAT)) {
        float fval = p->token.float_value;
        advance(p);
        sm_config_set_clkdiv(&p->sm_config, fval);
    } else {
        uint val;
        pioasm_error_t err = parse_value(p, &val);
        if (err != PIOASM_OK) return err;
        sm_config_set_clkdiv_int_frac8(&p->sm_config, val, 0);
    }

    return PIOASM_OK;
}

// Parse .fifo directive
static pioasm_error_t parse_fifo_directive(pioasm_parser_t *p) {
    enum pio_fifo_join fifo_join;

    if (match(p, TOK_TXRX)) {
        fifo_join = PIO_FIFO_JOIN_NONE;
    } else if (match(p, TOK_TX)) {
        fifo_join = PIO_FIFO_JOIN_TX;
    } else if (match(p, TOK_RX)) {
        fifo_join = PIO_FIFO_JOIN_RX;
    } else if (match(p, TOK_TXGET)) {
        if (p->program.pio_version < 1) return set_error(p, PIOASM_ERR_PIO_VERSION);
#if PICO_PIO_VERSION > 0
        fifo_join = PIO_FIFO_JOIN_TXGET;
#else
        return set_error(p, PIOASM_ERR_PIO_VERSION);
#endif
    } else if (match(p, TOK_TXPUT)) {
        if (p->program.pio_version < 1) return set_error(p, PIOASM_ERR_PIO_VERSION);
#if PICO_PIO_VERSION > 0
        fifo_join = PIO_FIFO_JOIN_TXPUT;
#else
        return set_error(p, PIOASM_ERR_PIO_VERSION);
#endif
    } else if (match(p, TOK_PUTGET)) {
        if (p->program.pio_version < 1) return set_error(p, PIOASM_ERR_PIO_VERSION);
#if PICO_PIO_VERSION > 0
        fifo_join = PIO_FIFO_JOIN_PUTGET;
#else
        return set_error(p, PIOASM_ERR_PIO_VERSION);
#endif
    } else {
        return set_error(p, PIOASM_ERR_SYNTAX);
    }

    sm_config_set_fifo_join(&p->sm_config, fifo_join);
    return PIOASM_OK;
}

// Parse .mov_status directive
static pioasm_error_t parse_mov_status_directive(pioasm_parser_t *p) {
    enum pio_mov_status_type status_type;
    uint status_n;

    if (match(p, TOK_TXFIFO)) {
        if (!match(p, TOK_LESSTHAN)) {
            return set_error(p, PIOASM_ERR_SYNTAX);
        }
        pioasm_error_t err = parse_value(p, &status_n);
        if (err != PIOASM_OK) return err;
        status_type = STATUS_TX_LESSTHAN;
    } else if (match(p, TOK_RXFIFO)) {
        if (!match(p, TOK_LESSTHAN)) {
            return set_error(p, PIOASM_ERR_SYNTAX);
        }
        pioasm_error_t err = parse_value(p, &status_n);
        if (err != PIOASM_OK) return err;
        status_type = STATUS_RX_LESSTHAN;
    } else if (match(p, TOK_IRQ)) {
        // IRQ-based mov_status is RP2350 only
#if PICO_PIO_VERSION > 0
        uint irq_param = 0;

        if (match(p, TOK_NEXT)) {
            irq_param = 2;
        } else if (match(p, TOK_PREV)) {
            irq_param = 1;
        }

        if (!match(p, TOK_SET)) {
            return set_error(p, PIOASM_ERR_SYNTAX);
        }

        pioasm_error_t err = parse_value(p, &status_n);
        if (err != PIOASM_OK) return err;

        // For RP2350, use STATUS_IRQ_SET with combined value
        status_type = STATUS_IRQ_SET;
        status_n = (irq_param << 3) | (status_n & 0x7);
#else
        return set_error(p, PIOASM_ERR_PIO_VERSION);
#endif
    } else {
        return set_error(p, PIOASM_ERR_SYNTAX);
    }

    sm_config_set_mov_status(&p->sm_config, status_type, status_n);
    return PIOASM_OK;
}

// Parse shift direction
static bool parse_direction(pioasm_parser_t *p) {
    if (match(p, TOK_LEFT)) {
        return false;
    }
    if (match(p, TOK_RIGHT)) {
        return true;
    }
    // Default is right
    return true;
}

// Parse auto push/pull setting
static bool parse_autopush(pioasm_parser_t *p) {
    if (match(p, TOK_AUTO)) {
        return true;
    }
    if (match(p, TOK_MANUAL)) {
        return false;
    }
    return false;
}

// Parse .in directive
static pioasm_error_t parse_dot_in_directive(pioasm_parser_t *p) {
    uint count;
    pioasm_error_t err = parse_value(p, &count);
    if (err != PIOASM_OK) return err;

    bool shift_right = parse_direction(p);
    bool autopush = parse_autopush(p);

    uint threshold = 32;  // Default threshold
    if (check(p, TOK_INT) || check(p, TOK_ID) || check(p, TOK_LPAREN)) {
        err = parse_value(p, &threshold);
        if (err != PIOASM_OK) return err;
    }

    // Set shift config (threshold of 32 is encoded as 0 in hardware)
    sm_config_set_in_shift(&p->sm_config, shift_right, autopush, threshold & 0x1f);

    if ((p->program.pio_version == 0) && (threshold < 32)) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }
#if PICO_PIO_VERSION > 0
    sm_config_set_in_pin_count(&p->sm_config, count);
#endif

    return PIOASM_OK;
}

// Parse .out directive
static pioasm_error_t parse_dot_out_directive(pioasm_parser_t *p) {
    uint count;
    pioasm_error_t err = parse_value(p, &count);
    if (err != PIOASM_OK) return err;

    bool shift_right = parse_direction(p);
    bool autopull = parse_autopush(p);

    uint threshold = 32;  // Default threshold
    if (check(p, TOK_INT) || check(p, TOK_ID) || check(p, TOK_LPAREN)) {
        err = parse_value(p, &threshold);
        if (err != PIOASM_OK) return err;
    }

    // Set shift config (threshold of 32 is encoded as 0 in hardware)
    sm_config_set_out_shift(&p->sm_config, shift_right, autopull, threshold & 0x1f);
    sm_config_set_out_pin_count(&p->sm_config, count);
    return PIOASM_OK;
}

// Parse .set directive (for pin count)
static pioasm_error_t parse_dot_set_directive(pioasm_parser_t *p) {
    uint count;
    pioasm_error_t err = parse_value(p, &count);
    if (err != PIOASM_OK) return err;

    if (count > 5) {
        return set_error(p, PIOASM_ERR_VALUE_OUT_OF_RANGE);
    }

    sm_config_set_set_pin_count(&p->sm_config, count);
    return PIOASM_OK;
}

// Parse directive
static pioasm_error_t parse_directive(pioasm_parser_t *p) {
    switch (p->token.type) {
        case TOK_PROGRAM:     advance(p); return parse_program_directive(p);
        case TOK_DEFINE:      advance(p); return parse_define_directive(p);
        case TOK_ORIGIN:      advance(p); return parse_origin_directive(p);
        case TOK_SIDE_SET:    advance(p); return parse_sideset_directive(p);
        case TOK_WRAP_TARGET: advance(p); return parse_wrap_target_directive(p);
        case TOK_WRAP:        advance(p); return parse_wrap_directive(p);
        case TOK_PIO_VERSION: advance(p); return parse_pio_version_directive(p);
        case TOK_CLOCK_DIV:   advance(p); return parse_clock_div_directive(p);
        case TOK_FIFO:        advance(p); return parse_fifo_directive(p);
        case TOK_MOV_STATUS:  advance(p); return parse_mov_status_directive(p);
        case TOK_DOT_IN:      advance(p); return parse_dot_in_directive(p);
        case TOK_DOT_OUT:     advance(p); return parse_dot_out_directive(p);
        case TOK_DOT_SET:     advance(p); return parse_dot_set_directive(p);
        case TOK_LANG_OPT:    advance(p); return parse_lang_opt_directive(p);
        case TOK_WORD:        return parse_instruction(p);  // .word is an instruction
        default:
            return set_error(p, PIOASM_ERR_INVALID_DIRECTIVE);
    }
}

// Parse label
static pioasm_error_t parse_label(pioasm_parser_t *p) {
    if (!p->has_program) {
        return set_error(p, PIOASM_ERR_MISSING_PROGRAM);
    }

    bool is_public = false;
    if (match(p, TOK_PUBLIC) || match(p, TOK_MULTIPLY)) {
        is_public = true;
    }

    if (!check(p, TOK_ID)) {
        return set_error(p, PIOASM_ERR_SYNTAX);
    }

    char name[PIOASM_MAX_SYMBOL_NAME];
    strncpy(name, p->token.str_value, PIOASM_MAX_SYMBOL_NAME);
    advance(p);

    // Expect colon after label name
    pioasm_error_t err = expect(p, TOK_COLON);
    if (err != PIOASM_OK) return err;

    // Add label with current instruction address
    return add_symbol(p, name, p->program.length, true, is_public);
}

// Check if current token starts an instruction
static bool is_instruction_token(pioasm_token_type_t type) {
    switch (type) {
        case TOK_JMP:
        case TOK_WAIT:
        case TOK_IN:
        case TOK_OUT:
        case TOK_PUSH:
        case TOK_PULL:
        case TOK_MOV:
        case TOK_IRQ:
        case TOK_SET:
        case TOK_NOP:
        case TOK_WORD:
            return true;
        default:
            return false;
    }
}

// Check if current token starts a directive
static bool is_directive_token(pioasm_token_type_t type) {
    switch (type) {
        case TOK_PROGRAM:
        case TOK_DEFINE:
        case TOK_ORIGIN:
        case TOK_SIDE_SET:
        case TOK_WRAP_TARGET:
        case TOK_WRAP:
        case TOK_PIO_VERSION:
        case TOK_CLOCK_DIV:
        case TOK_FIFO:
        case TOK_MOV_STATUS:
        case TOK_DOT_IN:
        case TOK_DOT_OUT:
        case TOK_DOT_SET:
        case TOK_LANG_OPT:
            return true;
        default:
            return false;
    }
}

// Parse one line
static pioasm_error_t parse_line(pioasm_parser_t *p) {
    // Skip empty lines
    while (match(p, TOK_NEWLINE)) {
        // empty
    }

    if (check(p, TOK_EOF)) {
        return PIOASM_OK;
    }

    // Check for directive
    if (is_directive_token(p->token.type)) {
        return parse_directive(p);
    }

    // Check for label (identifier followed by colon)
    if (check(p, TOK_ID) || check(p, TOK_PUBLIC) || check(p, TOK_MULTIPLY)) {
        // Look ahead to see if this is a label
        const char *save_pos = p->pos;
        pioasm_token_t save_token = p->token;
        uint16_t save_line = p->line;

        // Skip public/*
        if (check(p, TOK_PUBLIC) || check(p, TOK_MULTIPLY)) {
            advance(p);
        }

        if (check(p, TOK_ID)) {
            char name[PIOASM_MAX_SYMBOL_NAME];
            strncpy(name, p->token.str_value, PIOASM_MAX_SYMBOL_NAME);
            advance(p);

            if (check(p, TOK_COLON)) {
                // It's a label - restore and parse properly
                p->pos = save_pos;
                p->token = save_token;
                p->line = save_line;

                pioasm_error_t err = parse_label(p);
                if (err != PIOASM_OK) return err;

                // Check for instruction after label
                if (is_instruction_token(p->token.type)) {
                    return parse_instruction(p);
                }
                return PIOASM_OK;
            }
        }

        // Not a label - restore
        p->pos = save_pos;
        p->token = save_token;
        p->line = save_line;
    }

    // Check for instruction
    if (is_instruction_token(p->token.type)) {
        return parse_instruction(p);
    }

    // Unknown token
    return set_error(p, PIOASM_ERR_SYNTAX);
}

// Resolve forward references
static pioasm_error_t resolve_forward_references(pioasm_parser_t *p) {
    for (int i = 0; i < p->unresolved_count; i++) {
        pioasm_unresolved_t *ref = &p->unresolved[i];
        pioasm_symbol_t *sym = &p->symbols[ref->symbol_index];  // Direct index lookup

        if (!sym->is_resolved) {
            return set_error(p, PIOASM_ERR_UNDEFINED_SYMBOL);
        }

        // Update instruction with resolved value
        uint16_t instr = p->instructions[ref->instruction_index];

        // For JMP, the address is in bits 4-0
        // Clear old address and set new one
        instr = (instr & 0xFFE0) | (sym->value & 0x1F);
        p->instructions[ref->instruction_index] = instr;
    }

    return PIOASM_OK;
}

// Main parse function
pioasm_error_t pioasm_parse(pioasm_parser_t *p) {
    // Get first token
    pioasm_error_t err = advance(p);
    if (err != PIOASM_OK) return err;

    // Parse all lines
    while (!check(p, TOK_EOF) && p->error == PIOASM_OK) {
        err = parse_line(p);
        if (err != PIOASM_OK) {
            return err;
        }

        // Consume newline if present
        while (match(p, TOK_NEWLINE)) {
            // empty
        }
    }

    // Resolve forward references
    if (p->error == PIOASM_OK) {
        err = resolve_forward_references(p);
        if (err != PIOASM_OK) return err;
    }

    // Set default wrap if not specified
    if (p->wrap_target < 0) {
        p->wrap_target = 0;
    }
    if (p->wrap < 0 && p->program.length > 0) {
        p->wrap = p->program.length - 1;
    }

    return p->error;
}
