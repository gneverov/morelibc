// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

#include "pioasm_internal.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

// Keyword entry for lookup
typedef struct {
    const char *name;
    pioasm_token_type_t token;
} keyword_entry_t;

// Keywords sorted alphabetically for binary search (case-insensitive)
static const keyword_entry_t keywords[] = {
    {"auto", TOK_AUTO},
    {"block", TOK_BLOCK},
    {"clear", TOK_CLEAR},
    {"exec", TOK_EXEC},
    {"gpio", TOK_GPIO},
    {"ifempty", TOK_IFEMPTY},
    {"iffull", TOK_IFFULL},
    {"in", TOK_IN},
    {"irq", TOK_IRQ},
    {"isr", TOK_ISR},
    {"jmp", TOK_JMP},
    {"jmppin", TOK_JMPPIN},
    {"left", TOK_LEFT},
    {"manual", TOK_MANUAL},
    {"mov", TOK_MOV},
    {"next", TOK_NEXT},
    {"noblock", TOK_NOBLOCK},
    {"nop", TOK_NOP},
    {"nowait", TOK_NOWAIT},
    {"null", TOK_NULL},
    {"one", TOK_INT},      // Special: returns int value 1
    {"opt", TOK_OPTIONAL},
    {"optional", TOK_OPTIONAL},
    {"osr", TOK_OSR},
    {"osre", TOK_OSRE},
    {"out", TOK_OUT},
    {"pc", TOK_PC},
    {"pin", TOK_PIN},
    {"pindirs", TOK_PINDIRS},
    {"pins", TOK_PINS},
    {"prev", TOK_PREV},
    {"public", TOK_PUBLIC},
    {"pull", TOK_PULL},
    {"push", TOK_PUSH},
    {"putget", TOK_PUTGET},
    {"rel", TOK_REL},
    {"right", TOK_RIGHT},
    {"rp2040", TOK_RP2040},
    {"rp2350", TOK_RP2350},
    {"rx", TOK_RX},
    {"rxfifo", TOK_RXFIFO},
    {"set", TOK_SET},
    {"side", TOK_SIDE},
    {"side_set", TOK_SIDE},
    {"sideset", TOK_SIDE},
    {"status", TOK_STATUS},
    {"tx", TOK_TX},
    {"txfifo", TOK_TXFIFO},
    {"txget", TOK_TXGET},
    {"txput", TOK_TXPUT},
    {"txrx", TOK_TXRX},
    {"wait", TOK_WAIT},
    {"x", TOK_X},
    {"y", TOK_Y},
    {"zero", TOK_INT},     // Special: returns int value 0
};

#define NUM_KEYWORDS (sizeof(keywords) / sizeof(keywords[0]))

// Directive entries
typedef struct {
    const char *name;
    pioasm_token_type_t token;
} directive_entry_t;

static const directive_entry_t directives[] = {
    {"clock_div", TOK_CLOCK_DIV},
    {"define", TOK_DEFINE},
    {"fifo", TOK_FIFO},
    {"in", TOK_DOT_IN},
    {"lang_opt", TOK_LANG_OPT},
    {"mov_status", TOK_MOV_STATUS},
    {"origin", TOK_ORIGIN},
    {"out", TOK_DOT_OUT},
    {"pio_version", TOK_PIO_VERSION},
    {"program", TOK_PROGRAM},
    {"set", TOK_DOT_SET},
    {"side_set", TOK_SIDE_SET},
    {"word", TOK_WORD},
    {"wrap", TOK_WRAP},
    {"wrap_target", TOK_WRAP_TARGET},
};

#define NUM_DIRECTIVES (sizeof(directives) / sizeof(directives[0]))

// Binary search for keyword
static const keyword_entry_t *find_keyword(const char *name) {
    for (size_t i = 0; i < NUM_KEYWORDS; i++) {
        if (strncasecmp(name, keywords[i].name, PIOASM_MAX_SYMBOL_NAME) == 0) {
            return &keywords[i];
        }
    }
    return NULL;
}

// Find directive
static const directive_entry_t *find_directive(const char *name) {
    for (size_t i = 0; i < NUM_DIRECTIVES; i++) {
        if (strncasecmp(name, directives[i].name, PIOASM_MAX_SYMBOL_NAME) == 0) {
            return &directives[i];
        }
    }
    return NULL;
}

// Initialize lexer
void pioasm_lexer_init(pioasm_parser_t *p, const char *source) {
    p->source = source;
    p->pos = source;
    p->line = 1;
    p->token.type = TOK_EOF;
    memset(p->token.str_value, 0, sizeof(p->token.str_value));
}

// Check if at specific character (for lookahead)
bool pioasm_lexer_peek_char(pioasm_parser_t *p, char c) {
    return *p->pos == c;
}

// Skip whitespace and comments
static void skip_whitespace_and_comments(pioasm_parser_t *p) {
    while (*p->pos) {
        // Skip whitespace (not newlines)
        if (*p->pos == ' ' || *p->pos == '\t' || *p->pos == '\r') {
            p->pos++;
            continue;
        }

        // Skip single-line comments (;  or //)
        if (*p->pos == ';' || (*p->pos == '/' && p->pos[1] == '/')) {
            while (*p->pos && *p->pos != '\n') {
                p->pos++;
            }
            continue;
        }

        // Skip block comments /* ... */
        if (*p->pos == '/' && p->pos[1] == '*') {
            p->pos += 2;
            while (*p->pos && !(*p->pos == '*' && p->pos[1] == '/')) {
                if (*p->pos == '\n') p->line++;
                p->pos++;
            }
            if (*p->pos) p->pos += 2;  // Skip */
            continue;
        }

        break;
    }
}

// Scan a number (decimal, hex, or binary)
static pioasm_error_t scan_number(pioasm_parser_t *p) {
    unsigned long value = 0;

    // Check for hex or binary prefix
    if (*p->pos == '0' && (p->pos[1] == 'x' || p->pos[1] == 'X')) {
        // Hexadecimal
        p->pos += 2;
        while (isxdigit((unsigned char)*p->pos)) {
            int digit;
            if (*p->pos >= '0' && *p->pos <= '9') {
                digit = *p->pos - '0';
            } else if (*p->pos >= 'a' && *p->pos <= 'f') {
                digit = *p->pos - 'a' + 10;
            } else {
                digit = *p->pos - 'A' + 10;
            }
            value = value * 16 + digit;
            p->pos++;
        }
    } else if (*p->pos == '0' && (p->pos[1] == 'b' || p->pos[1] == 'B')) {
        // Binary
        p->pos += 2;
        while (*p->pos == '0' || *p->pos == '1') {
            value = value * 2 + (*p->pos - '0');
            p->pos++;
        }
    } else {
        // Decimal (possibly float)
        while (isdigit((unsigned char)*p->pos)) {
            value = value * 10 + (*p->pos - '0');
            p->pos++;
        }
        // Check for decimal point
        if (*p->pos == '.' && isdigit((unsigned char)p->pos[1])) {
            p->pos++;
            float frac = 0.1f;
            float fvalue = (float)value;
            while (isdigit((unsigned char)*p->pos)) {
                fvalue += frac * (*p->pos - '0');
                frac *= 0.1f;
                p->pos++;
            }
            p->token.type = TOK_FLOAT;
            p->token.float_value = fvalue;
            return PIOASM_OK;
        }
    }

    p->token.type = TOK_INT;
    p->token.uint_value = value;
    return PIOASM_OK;
}

static void scan_word(pioasm_parser_t *p) {
    size_t len = 0;
    while (isalnum((unsigned char)*p->pos) || *p->pos == '_') {
        if (len < PIOASM_MAX_SYMBOL_NAME) {
            p->token.str_value[len++] = *p->pos;
        }
        p->pos++;
    }
    if (len < PIOASM_MAX_SYMBOL_NAME) {
        p->token.str_value[len++] = '\0';
    }
}

// Scan an identifier or keyword
static pioasm_error_t scan_identifier(pioasm_parser_t *p) {
    scan_word(p);

    // Check if it's a keyword
    const keyword_entry_t *kw = find_keyword(p->token.str_value);
    if (kw) {
        p->token.type = kw->token;
        // Handle special keywords "one" and "zero"
        if (strncasecmp(p->token.str_value, "one", PIOASM_MAX_SYMBOL_NAME) == 0) {
            p->token.uint_value = 1;
        } else if (strncasecmp(p->token.str_value, "zero", PIOASM_MAX_SYMBOL_NAME) == 0) {
            p->token.uint_value = 0;
        }
    } else {
        p->token.type = TOK_ID;
    }

    return PIOASM_OK;
}

// Scan a directive (starts with .)
static pioasm_error_t scan_directive(pioasm_parser_t *p) {
    // Skip the dot
    p->pos++;
    scan_word(p);

    // Look up directive
    const directive_entry_t *dir = find_directive(p->token.str_value);
    if (dir) {
        p->token.type = dir->token;
    } else {
        // Unknown directive - treat as syntax error
        return PIOASM_ERR_INVALID_DIRECTIVE;
    }

    return PIOASM_OK;
}

// Get next token
pioasm_error_t pioasm_lexer_next(pioasm_parser_t *p) {
    skip_whitespace_and_comments(p);

    // Clear previous token string
    p->token.str_value[0] = '\0';

    // Check for end of input
    if (*p->pos == '\0') {
        p->token.type = TOK_EOF;
        return PIOASM_OK;
    }

    // Check for newline
    if (*p->pos == '\n') {
        p->token.type = TOK_NEWLINE;
        p->pos++;
        p->line++;
        return PIOASM_OK;
    }

    // Check for numbers
    if (isdigit((unsigned char)*p->pos)) {
        return scan_number(p);
    }

    // Check for directives
    if (*p->pos == '.') {
        return scan_directive(p);
    }

    // Check for identifiers/keywords
    if (isalpha((unsigned char)*p->pos) || *p->pos == '_') {
        return scan_identifier(p);
    }

    // Two-character operators
    if (*p->pos == ':' && p->pos[1] == ':') {
        p->token.type = TOK_REVERSE;
        p->pos += 2;
        return PIOASM_OK;
    }
    if (*p->pos == '-' && p->pos[1] == '-') {
        p->token.type = TOK_POST_DEC;
        p->pos += 2;
        return PIOASM_OK;
    }
    // Handle Unicode minus minus (−−)
    if ((unsigned char)*p->pos == 0xE2 && (unsigned char)p->pos[1] == 0x88 &&
        (unsigned char)p->pos[2] == 0x92 && (unsigned char)p->pos[3] == 0xE2 &&
        (unsigned char)p->pos[4] == 0x88 && (unsigned char)p->pos[5] == 0x92) {
        p->token.type = TOK_POST_DEC;
        p->pos += 6;
        return PIOASM_OK;
    }
    if (*p->pos == '!' && p->pos[1] == '=') {
        p->token.type = TOK_NOT_EQUAL;
        p->pos += 2;
        return PIOASM_OK;
    }
    if (*p->pos == '<' && p->pos[1] == '<') {
        p->token.type = TOK_SHL;
        p->pos += 2;
        return PIOASM_OK;
    }
    if (*p->pos == '>' && p->pos[1] == '>') {
        p->token.type = TOK_SHR;
        p->pos += 2;
        return PIOASM_OK;
    }

    // Single-character operators
    switch (*p->pos) {
        case ',': p->token.type = TOK_COMMA; p->pos++; return PIOASM_OK;
        case ':': p->token.type = TOK_COLON; p->pos++; return PIOASM_OK;
        case '(': p->token.type = TOK_LPAREN; p->pos++; return PIOASM_OK;
        case ')': p->token.type = TOK_RPAREN; p->pos++; return PIOASM_OK;
        case '[': p->token.type = TOK_LBRACKET; p->pos++; return PIOASM_OK;
        case ']': p->token.type = TOK_RBRACKET; p->pos++; return PIOASM_OK;
        case '+': p->token.type = TOK_PLUS; p->pos++; return PIOASM_OK;
        case '-': p->token.type = TOK_MINUS; p->pos++; return PIOASM_OK;
        case '*': p->token.type = TOK_MULTIPLY; p->pos++; return PIOASM_OK;
        case '/': p->token.type = TOK_DIVIDE; p->pos++; return PIOASM_OK;
        case '|': p->token.type = TOK_OR; p->pos++; return PIOASM_OK;
        case '&': p->token.type = TOK_AND; p->pos++; return PIOASM_OK;
        case '^': p->token.type = TOK_XOR; p->pos++; return PIOASM_OK;
        case '!': p->token.type = TOK_NOT; p->pos++; return PIOASM_OK;
        case '~': p->token.type = TOK_NOT; p->pos++; return PIOASM_OK;
        case '=': p->token.type = TOK_ASSIGN; p->pos++; return PIOASM_OK;
        case '<': p->token.type = TOK_LESSTHAN; p->pos++; return PIOASM_OK;
    }

    // Unknown character
    return PIOASM_ERR_SYNTAX;
}
