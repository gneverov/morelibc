// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#ifndef PIOASM_INTERNAL_H
#define PIOASM_INTERNAL_H

#include "pioasm.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Token types
typedef enum {
    TOK_EOF = 0,
    TOK_NEWLINE,
    TOK_ID,
    TOK_INT,
    TOK_FLOAT,

    // Punctuation
    TOK_COMMA,
    TOK_COLON,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MULTIPLY,
    TOK_DIVIDE,
    TOK_OR,
    TOK_AND,
    TOK_XOR,
    TOK_SHL,
    TOK_SHR,
    TOK_NOT,
    TOK_REVERSE,      // ::
    TOK_POST_DEC,     // --
    TOK_NOT_EQUAL,    // !=
    TOK_ASSIGN,       // =
    TOK_LESSTHAN,     // <

    // Instructions
    TOK_JMP,
    TOK_WAIT,
    TOK_IN,
    TOK_OUT,
    TOK_PUSH,
    TOK_PULL,
    TOK_MOV,
    TOK_IRQ,
    TOK_SET,
    TOK_NOP,

    // Directives
    TOK_PROGRAM,
    TOK_WRAP_TARGET,
    TOK_WRAP,
    TOK_DEFINE,
    TOK_SIDE_SET,
    TOK_WORD,
    TOK_ORIGIN,
    TOK_PIO_VERSION,
    TOK_CLOCK_DIV,
    TOK_FIFO,
    TOK_MOV_STATUS,
    TOK_DOT_SET,
    TOK_DOT_OUT,
    TOK_DOT_IN,
    TOK_LANG_OPT,

    // Operands/keywords
    TOK_PIN,
    TOK_GPIO,
    TOK_OSRE,
    TOK_JMPPIN,
    TOK_PREV,
    TOK_NEXT,
    TOK_PINS,
    TOK_NULL,
    TOK_PINDIRS,
    TOK_BLOCK,
    TOK_NOBLOCK,
    TOK_IFEMPTY,
    TOK_IFFULL,
    TOK_NOWAIT,
    TOK_CLEAR,
    TOK_REL,
    TOK_X,
    TOK_Y,
    TOK_EXEC,
    TOK_PC,
    TOK_ISR,
    TOK_OSR,
    TOK_OPTIONAL,     // also "opt"
    TOK_SIDE,
    TOK_STATUS,
    TOK_PUBLIC,

    // PIO version tokens
    TOK_RP2040,
    TOK_RP2350,
    TOK_RXFIFO,
    TOK_TXFIFO,

    // FIFO config tokens
    TOK_TXRX,
    TOK_TX,
    TOK_RX,
    TOK_TXPUT,
    TOK_TXGET,
    TOK_PUTGET,

    // Shift direction tokens
    TOK_LEFT,
    TOK_RIGHT,
    TOK_AUTO,
    TOK_MANUAL,
} pioasm_token_type_t;

// Instruction types (bits 15-13 of instruction word)
typedef enum {
    INST_JMP = 0,
    INST_WAIT = 1,
    INST_IN = 2,
    INST_OUT = 3,
    INST_PUSH_PULL = 4,
    INST_MOV = 5,
    INST_IRQ = 6,
    INST_SET = 7,
} pioasm_inst_type_t;

// JMP conditions
typedef enum {
    COND_ALWAYS = 0,
    COND_X_ZERO = 1,
    COND_X_NONZERO_DEC = 2,
    COND_Y_ZERO = 3,
    COND_Y_NONZERO_DEC = 4,
    COND_X_NE_Y = 5,
    COND_PIN = 6,
    COND_OSRE_ZERO = 7,
} pioasm_condition_t;

// MOV source/destination extensions for RP2350
// (Standard values use SDK's enum pio_src_dest)
typedef enum {
    MOV_FIFO_Y = 8,   // RP2350 rxfifo[y]
    MOV_FIFO_IDX = 9, // RP2350 rxfifo[n]
} pioasm_mov_ext_t;

// MOV operations
typedef enum {
    MOV_OP_NONE = 0,
    MOV_OP_INVERT = 1,
    MOV_OP_REVERSE = 2,
} pioasm_mov_op_t;

// WAIT source
typedef enum {
    WAIT_GPIO = 0,
    WAIT_PIN = 1,
    WAIT_IRQ = 2,
    WAIT_JMPPIN = 3,  // RP2350 only
} pioasm_wait_src_t;

// IRQ modifiers
typedef enum {
    IRQ_SET = 0,
    IRQ_SET_WAIT = 1,
    IRQ_CLEAR = 2,
} pioasm_irq_t;

// Lexer functions
void pioasm_lexer_init(pioasm_parser_t *p, const char *source);
pioasm_error_t pioasm_lexer_next(pioasm_parser_t *p);
bool pioasm_lexer_peek_char(pioasm_parser_t *p, char c);

// Parser functions
pioasm_error_t pioasm_parse(pioasm_parser_t *p);

#ifdef __cplusplus
}
#endif

#endif // PIOASM_INTERNAL_H
