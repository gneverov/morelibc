// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

// Test program for pioasm C implementation
// Compile and run:
// cmake -B build -DPIOASM_BUILD_TESTS=1
// cmake --build build
// ./build/pioasm_test 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pioasm.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST_START(name) \
    do { \
        test_count++; \
        printf("Test %d: %s... ", test_count, name); \
    } while(0)

#define TEST_PASS() \
    do { \
        pass_count++; \
        printf("PASS\n"); \
    } while(0)

#define TEST_FAIL(msg) \
    do { \
        printf("FAIL: %s\n", msg); \
    } while(0)

// Test: trivial program
static void test_trivial(void) {
    TEST_START("trivial program");

    const char *source =
        ".program trivial\n"
        "out pins, 1\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 1) {
        TEST_FAIL("expected 1 instruction");
        return;
    }

    // OUT pins, 1 should encode as: opcode 3, dest 0 (pins), count 1
    // 011 00000 000 00001 = 0x6001
    uint16_t expected = 0x6001;
    if (parser.instructions[0] != expected) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 0x%04X, got 0x%04X",
                 expected, parser.instructions[0]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: simple JMP
static void test_jmp(void) {
    TEST_START("jmp instruction");

    const char *source =
        ".program test_jmp\n"
        "loop:\n"
        "    nop\n"
        "    jmp loop\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 2) {
        TEST_FAIL("expected 2 instructions");
        return;
    }

    // JMP unconditional to address 0: opcode 0, cond 0, addr 0
    // 000 00000 000 00000 = 0x0000
    if ((parser.instructions[1] & 0xE01F) != 0x0000) {
        char msg[64];
        snprintf(msg, sizeof(msg), "jmp encoding wrong: 0x%04X",
                 parser.instructions[1]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: JMP conditions
static void test_jmp_conditions(void) {
    TEST_START("jmp conditions");

    const char *source =
        ".program test_cond\n"
        "start:\n"
        "    jmp start       ; unconditional\n"
        "    jmp !x start    ; x == 0\n"
        "    jmp x-- start   ; x != 0, post-dec\n"
        "    jmp !y start    ; y == 0\n"
        "    jmp y-- start   ; y != 0, post-dec\n"
        "    jmp x!=y start  ; x != y\n"
        "    jmp pin start   ; pin high\n"
        "    jmp !osre start ; osre == 0\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 8) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 8 instructions, got %d", parser.program.length);
        TEST_FAIL(msg);
        return;
    }

    // Check conditions are encoded correctly (bits 7-5)
    // All jump to address 0
    uint16_t expected_conditions[] = {
        0x0000,  // unconditional (000)
        0x0020,  // !x (001)
        0x0040,  // x-- (010)
        0x0060,  // !y (011)
        0x0080,  // y-- (100)
        0x00A0,  // x!=y (101)
        0x00C0,  // pin (110)
        0x00E0,  // !osre (111)
    };

    for (int i = 0; i < 8; i++) {
        if ((parser.instructions[i] & 0x00E0) != expected_conditions[i]) {
            char msg[64];
            snprintf(msg, sizeof(msg), "condition %d wrong: got 0x%04X, expected cond 0x%04X",
                     i, parser.instructions[i], expected_conditions[i]);
            TEST_FAIL(msg);
            return;
        }
    }

    TEST_PASS();
}

// Test: WAIT instruction
static void test_wait(void) {
    TEST_START("wait instruction");

    const char *source =
        ".program test_wait\n"
        "    wait 1 gpio 5\n"
        "    wait 0 pin 3\n"
        "    wait 1 irq 2\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 3) {
        TEST_FAIL("expected 3 instructions");
        return;
    }

    // WAIT 1 GPIO 5: opcode 1, pol 1, src 0 (gpio), idx 5
    // 001 00000 1 00 00101 = 0x2085
    if ((parser.instructions[0] & 0xE0FF) != 0x2085) {
        char msg[64];
        snprintf(msg, sizeof(msg), "wait gpio wrong: 0x%04X", parser.instructions[0]);
        TEST_FAIL(msg);
        return;
    }

    // WAIT 0 PIN 3: opcode 1, pol 0, src 1 (pin), idx 3
    // 001 00000 0 01 00011 = 0x2023
    if ((parser.instructions[1] & 0xE0FF) != 0x2023) {
        char msg[64];
        snprintf(msg, sizeof(msg), "wait pin wrong: 0x%04X", parser.instructions[1]);
        TEST_FAIL(msg);
        return;
    }

    // WAIT 1 IRQ 2: opcode 1, pol 1, src 2 (irq), idx 2
    // 001 00000 1 10 00010 = 0x20C2
    if ((parser.instructions[2] & 0xE0FF) != 0x20C2) {
        char msg[64];
        snprintf(msg, sizeof(msg), "wait irq wrong: 0x%04X", parser.instructions[2]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: IN/OUT instructions
static void test_in_out(void) {
    TEST_START("in/out instructions");

    const char *source =
        ".program test_io\n"
        "    in pins, 8\n"
        "    out pins, 16\n"
        "    in x, 32\n"
        "    out y, 1\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 4) {
        TEST_FAIL("expected 4 instructions");
        return;
    }

    // IN pins, 8: opcode 2, src 0 (pins), count 8
    // 010 00000 000 01000 = 0x4008
    if ((parser.instructions[0] & 0xE0FF) != 0x4008) {
        char msg[64];
        snprintf(msg, sizeof(msg), "in pins wrong: 0x%04X", parser.instructions[0]);
        TEST_FAIL(msg);
        return;
    }

    // OUT pins, 16: opcode 3, dest 0 (pins), count 16
    // 011 00000 000 10000 = 0x6010
    if ((parser.instructions[1] & 0xE0FF) != 0x6010) {
        char msg[64];
        snprintf(msg, sizeof(msg), "out pins wrong: 0x%04X", parser.instructions[1]);
        TEST_FAIL(msg);
        return;
    }

    // IN x, 32: opcode 2, src 1 (x), count 0 (represents 32)
    // 010 00000 001 00000 = 0x4020
    if ((parser.instructions[2] & 0xE0FF) != 0x4020) {
        char msg[64];
        snprintf(msg, sizeof(msg), "in x wrong: 0x%04X", parser.instructions[2]);
        TEST_FAIL(msg);
        return;
    }

    // OUT y, 1: opcode 3, dest 2 (y), count 1
    // 011 00000 010 00001 = 0x6041
    if ((parser.instructions[3] & 0xE0FF) != 0x6041) {
        char msg[64];
        snprintf(msg, sizeof(msg), "out y wrong: 0x%04X", parser.instructions[3]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: PUSH/PULL instructions
static void test_push_pull(void) {
    TEST_START("push/pull instructions");

    const char *source =
        ".program test_pp\n"
        "    push\n"
        "    pull\n"
        "    push iffull noblock\n"
        "    pull ifempty block\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 4) {
        TEST_FAIL("expected 4 instructions");
        return;
    }

    // PUSH block: 100 DDDDD 0 F B 00000 (F=iffull, B=block)
    // block=1, iffull=0: 100 00000 0 0 1 00000 = 0x8020
    if ((parser.instructions[0] & 0xE0FF) != 0x8020) {
        char msg[64];
        snprintf(msg, sizeof(msg), "push wrong: 0x%04X", parser.instructions[0]);
        TEST_FAIL(msg);
        return;
    }

    // PULL block: 100 DDDDD 1 E B 00000 (E=ifempty, B=block)
    // block=1, ifempty=0: 100 00000 1 0 1 00000 = 0x80A0
    if ((parser.instructions[1] & 0xE0FF) != 0x80A0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "pull wrong: 0x%04X", parser.instructions[1]);
        TEST_FAIL(msg);
        return;
    }

    // PUSH iffull noblock: block=0, iffull=1
    // 100 00000 0 1 0 00000 = 0x8040
    if ((parser.instructions[2] & 0xE0FF) != 0x8040) {
        char msg[64];
        snprintf(msg, sizeof(msg), "push iffull noblock wrong: 0x%04X", parser.instructions[2]);
        TEST_FAIL(msg);
        return;
    }

    // PULL ifempty block: block=1, ifempty=1
    // 100 00000 1 1 1 00000 = 0x80E0
    if ((parser.instructions[3] & 0xE0FF) != 0x80E0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "pull ifempty block wrong: 0x%04X", parser.instructions[3]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: MOV instruction
static void test_mov(void) {
    TEST_START("mov instruction");

    const char *source =
        ".program test_mov\n"
        "    mov x, y\n"
        "    mov y, !x\n"
        "    mov pins, ::y\n"
        "    mov isr, null\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 4) {
        TEST_FAIL("expected 4 instructions");
        return;
    }

    // MOV x, y: opcode 5, dest 1 (x), op 0 (none), src 2 (y)
    // 101 00000 001 00 010 = 0xA022
    if ((parser.instructions[0] & 0xE0FF) != 0xA022) {
        char msg[64];
        snprintf(msg, sizeof(msg), "mov x,y wrong: 0x%04X", parser.instructions[0]);
        TEST_FAIL(msg);
        return;
    }

    // MOV y, !x: opcode 5, dest 2 (y), op 1 (invert), src 1 (x)
    // 101 00000 010 01 001 = 0xA049
    if ((parser.instructions[1] & 0xE0FF) != 0xA049) {
        char msg[64];
        snprintf(msg, sizeof(msg), "mov y,!x wrong: 0x%04X", parser.instructions[1]);
        TEST_FAIL(msg);
        return;
    }

    // MOV pins, ::y: opcode 5, dest 0 (pins), op 2 (reverse), src 2 (y)
    // 101 00000 000 10 010 = 0xA012
    if ((parser.instructions[2] & 0xE0FF) != 0xA012) {
        char msg[64];
        snprintf(msg, sizeof(msg), "mov pins,::y wrong: 0x%04X", parser.instructions[2]);
        TEST_FAIL(msg);
        return;
    }

    // MOV isr, null: opcode 5, dest 6 (isr), op 0 (none), src 3 (null)
    // 101 00000 110 00 011 = 0xA0C3
    if ((parser.instructions[3] & 0xE0FF) != 0xA0C3) {
        char msg[64];
        snprintf(msg, sizeof(msg), "mov isr,null wrong: 0x%04X", parser.instructions[3]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: SET instruction
static void test_set(void) {
    TEST_START("set instruction");

    const char *source =
        ".program test_set\n"
        "    set pins, 0\n"
        "    set x, 31\n"
        "    set y, 15\n"
        "    set pindirs, 1\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 4) {
        TEST_FAIL("expected 4 instructions");
        return;
    }

    // SET pins, 0: opcode 7, dest 0 (pins), value 0
    // 111 00000 000 00000 = 0xE000
    if ((parser.instructions[0] & 0xE0FF) != 0xE000) {
        char msg[64];
        snprintf(msg, sizeof(msg), "set pins,0 wrong: 0x%04X", parser.instructions[0]);
        TEST_FAIL(msg);
        return;
    }

    // SET x, 31: opcode 7, dest 1 (x), value 31
    // 111 00000 001 11111 = 0xE03F
    if ((parser.instructions[1] & 0xE0FF) != 0xE03F) {
        char msg[64];
        snprintf(msg, sizeof(msg), "set x,31 wrong: 0x%04X", parser.instructions[1]);
        TEST_FAIL(msg);
        return;
    }

    // SET y, 15: opcode 7, dest 2 (y), value 15
    // 111 00000 010 01111 = 0xE04F
    if ((parser.instructions[2] & 0xE0FF) != 0xE04F) {
        char msg[64];
        snprintf(msg, sizeof(msg), "set y,15 wrong: 0x%04X", parser.instructions[2]);
        TEST_FAIL(msg);
        return;
    }

    // SET pindirs, 1: opcode 7, dest 4 (pindirs), value 1
    // 111 00000 100 00001 = 0xE081
    if ((parser.instructions[3] & 0xE0FF) != 0xE081) {
        char msg[64];
        snprintf(msg, sizeof(msg), "set pindirs,1 wrong: 0x%04X", parser.instructions[3]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: IRQ instruction
static void test_irq(void) {
    TEST_START("irq instruction");

    const char *source =
        ".program test_irq\n"
        "    irq 0\n"
        "    irq clear 1\n"
        "    irq wait 2\n"
        "    irq set 3 rel\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 4) {
        TEST_FAIL("expected 4 instructions");
        return;
    }

    // IRQ set 0: opcode 6, mode 0 (set), rel 0, idx 0
    // 110 00000 00 0 00 000 = 0xC000
    if ((parser.instructions[0] & 0xE0FF) != 0xC000) {
        char msg[64];
        snprintf(msg, sizeof(msg), "irq 0 wrong: 0x%04X", parser.instructions[0]);
        TEST_FAIL(msg);
        return;
    }

    // IRQ clear 1: opcode 6, mode 2 (clear), rel 0, idx 1
    // 110 00000 01 0 00 001 = 0xC041
    if ((parser.instructions[1] & 0xE0FF) != 0xC041) {
        char msg[64];
        snprintf(msg, sizeof(msg), "irq clear 1 wrong: 0x%04X", parser.instructions[1]);
        TEST_FAIL(msg);
        return;
    }

    // IRQ wait 2: opcode 6, mode 1 (set_wait), rel 0, idx 2
    // 110 00000 00 1 00 010 = 0xC022
    if ((parser.instructions[2] & 0xE0FF) != 0xC022) {
        char msg[64];
        snprintf(msg, sizeof(msg), "irq wait 2 wrong: 0x%04X", parser.instructions[2]);
        TEST_FAIL(msg);
        return;
    }

    // IRQ set 3 rel: opcode 6, mode 0 (set), rel 1, idx 3
    // 110 00000 00 0 10 011 = 0xC013
    if ((parser.instructions[3] & 0xE0FF) != 0xC013) {
        char msg[64];
        snprintf(msg, sizeof(msg), "irq set 3 rel wrong: 0x%04X", parser.instructions[3]);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: defines and expressions
static void test_defines(void) {
    TEST_START("defines and expressions");

    const char *source =
        ".program test_def\n"
        ".define VAL 5\n"
        ".define SHIFTED (VAL << 1)\n"
        ".define SUM (3 + 4)\n"
        "    set x, VAL\n"
        "    set y, SHIFTED\n"
        "    set pins, SUM\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 3) {
        TEST_FAIL("expected 3 instructions");
        return;
    }

    // SET x, 5
    if ((parser.instructions[0] & 0x001F) != 5) {
        char msg[64];
        snprintf(msg, sizeof(msg), "set x, VAL wrong: value=%d",
                 parser.instructions[0] & 0x1F);
        TEST_FAIL(msg);
        return;
    }

    // SET y, 10 (5 << 1)
    if ((parser.instructions[1] & 0x001F) != 10) {
        char msg[64];
        snprintf(msg, sizeof(msg), "set y, SHIFTED wrong: value=%d",
                 parser.instructions[1] & 0x1F);
        TEST_FAIL(msg);
        return;
    }

    // SET pins, 7 (3 + 4)
    if ((parser.instructions[2] & 0x001F) != 7) {
        char msg[64];
        snprintf(msg, sizeof(msg), "set pins, SUM wrong: value=%d",
                 parser.instructions[2] & 0x1F);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: side-set and delay
static void test_sideset_delay(void) {
    TEST_START("side-set and delay");

    const char *source =
        ".program test_ss\n"
        ".side_set 1\n"
        "    nop side 0 [3]\n"
        "    nop side 1 [2]\n"
        "    nop [15]\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 3) {
        TEST_FAIL("expected 3 instructions");
        return;
    }

    // Note: sideset_bits is now internal to sm_config, not directly accessible
    // We verify the sideset behavior through instruction encoding

    // NOP side 0 [3]: delay_sideset = (0 << 4) | 3 = 0x03
    // bits 12-8 = 00011
    if (((parser.instructions[0] >> 8) & 0x1F) != 0x03) {
        char msg[64];
        snprintf(msg, sizeof(msg), "nop side 0 [3] wrong delay/sideset: 0x%02X",
                 (parser.instructions[0] >> 8) & 0x1F);
        TEST_FAIL(msg);
        return;
    }

    // NOP side 1 [2]: delay_sideset = (1 << 4) | 2 = 0x12
    if (((parser.instructions[1] >> 8) & 0x1F) != 0x12) {
        char msg[64];
        snprintf(msg, sizeof(msg), "nop side 1 [2] wrong delay/sideset: 0x%02X",
                 (parser.instructions[1] >> 8) & 0x1F);
        TEST_FAIL(msg);
        return;
    }

    // NOP [15]: delay only (no sideset) = 15
    if (((parser.instructions[2] >> 8) & 0x0F) != 15) {
        char msg[64];
        snprintf(msg, sizeof(msg), "nop [15] wrong delay: 0x%02X",
                 (parser.instructions[2] >> 8) & 0x1F);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: wrap directives
static void test_wrap(void) {
    TEST_START("wrap directives");

    const char *source =
        ".program test_wrap\n"
        "    nop\n"
        ".wrap_target\n"
        "    nop\n"
        "    nop\n"
        ".wrap\n"
        "    nop\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.wrap_target != 1) {
        char msg[64];
        snprintf(msg, sizeof(msg), "wrap_target should be 1, got %d", parser.wrap_target);
        TEST_FAIL(msg);
        return;
    }

    if (parser.wrap != 2) {
        char msg[64];
        snprintf(msg, sizeof(msg), "wrap should be 2, got %d", parser.wrap);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: ws2812-like program (partial)
static void test_ws2812_like(void) {
    TEST_START("ws2812-like program");

    const char *source =
        ".program ws2812_test\n"
        ".side_set 1\n"
        ".define T1 3\n"
        ".define T2 3\n"
        ".define T3 4\n"
        ".wrap_target\n"
        "bitloop:\n"
        "    out x, 1       side 0 [T3 - 1]\n"
        "    jmp !x do_zero side 1 [T1 - 1]\n"
        "do_one:\n"
        "    jmp bitloop    side 1 [T2 - 1]\n"
        "do_zero:\n"
        "    nop            side 0 [T2 - 1]\n"
        ".wrap\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        printf("\n  Error at line %d: %s\n", parser.error_line, pioasm_error_str(err));
        TEST_FAIL("assembly failed");
        return;
    }

    if (parser.program.length != 4) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 4 instructions, got %d", parser.program.length);
        TEST_FAIL(msg);
        return;
    }

    if (parser.wrap_target != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "wrap_target should be 0, got %d", parser.wrap_target);
        TEST_FAIL(msg);
        return;
    }

    if (parser.wrap != 3) {
        char msg[64];
        snprintf(msg, sizeof(msg), "wrap should be 3, got %d", parser.wrap);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: comments
static void test_comments(void) {
    TEST_START("comments");

    const char *source =
        ".program test_comments\n"
        "; This is a semicolon comment\n"
        "// This is a C++ style comment\n"
        "    nop  ; inline comment\n"
        "    nop  // another inline comment\n"
        "/* This is a\n"
        "   multi-line\n"
        "   comment */\n"
        "    nop\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 3) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 3 instructions, got %d", parser.program.length);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: public symbols
static void test_public_symbols(void) {
    TEST_START("public symbols");

    const char *source =
        ".program test_public\n"
        ".define public MY_CONST 42\n"
        ".define private_val 10\n"
        "public my_label:\n"
        "    nop\n"
        "private_label:\n"
        "    nop\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    // Find MY_CONST
    int num_public = 0;
    bool found_const = false;
    for (int i = 0; i < parser.symbol_count; i++) {
        if (!parser.symbols[i].is_public) {
            continue;
        }
        num_public++;
        if (strcmp(parser.symbols[i].name, "MY_CONST") == 0) {
            if (parser.symbols[i].value != 42) {
                TEST_FAIL("MY_CONST value wrong");
                return;
            }
            found_const = true;
        }
    }

    // Should have 2 public symbols: MY_CONST and my_label
    if (num_public != 2) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 2 public symbols, got %d", num_public);
        TEST_FAIL(msg);
        return;
    }

    if (!found_const) {
        TEST_FAIL("MY_CONST not found in public symbols");
        return;
    }

    TEST_PASS();
}

// Test: forward references
static void test_forward_references(void) {
    TEST_START("forward references");

    const char *source =
        ".program test_fwd\n"
        "    jmp end\n"
        "    nop\n"
        "    nop\n"
        "end:\n"
        "    nop\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 4) {
        TEST_FAIL("expected 4 instructions");
        return;
    }

    // JMP should target address 3
    if ((parser.instructions[0] & 0x001F) != 3) {
        char msg[64];
        snprintf(msg, sizeof(msg), "jmp target wrong: expected 3, got %d",
                 parser.instructions[0] & 0x1F);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: program name storage
static void test_program_name(void) {
    TEST_START("program name");

    const char *source =
        ".program my_test_program\n"
        "nop\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (strcmp(parser.name, "my_test_program") != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected 'my_test_program', got '%s'", parser.name);
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: .lang_opt directive (ignored)
static void test_lang_opt(void) {
    TEST_START("lang_opt directive");

    const char *source =
        ".program test\n"
        ".lang_opt python import rp2\n"
        ".lang_opt python class Test(rp2.PIO)\n"
        "nop\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_OK) {
        TEST_FAIL(pioasm_error_str(err));
        return;
    }

    if (parser.program.length != 1) {
        TEST_FAIL("expected 1 instruction");
        return;
    }

    TEST_PASS();
}

// Test: error handling - missing program
static void test_error_missing_program(void) {
    TEST_START("error: missing program");

    const char *source =
        "nop\n";  // No .program directive

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_ERR_MISSING_PROGRAM) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected MISSING_PROGRAM error, got %s",
                 pioasm_error_str(err));
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

// Test: error handling - undefined symbol
static void test_error_undefined_symbol(void) {
    TEST_START("error: undefined symbol");

    const char *source =
        ".program test\n"
        "jmp undefined_label\n";

    pioasm_parser_t parser;
    pioasm_error_t err = pioasm_assemble(source, &parser);

    if (err != PIOASM_ERR_UNDEFINED_SYMBOL) {
        char msg[64];
        snprintf(msg, sizeof(msg), "expected UNDEFINED_SYMBOL error, got %s",
                 pioasm_error_str(err));
        TEST_FAIL(msg);
        return;
    }

    TEST_PASS();
}

int main(void) {
    printf("PIO Assembler C Implementation Tests\n");
    printf("=====================================\n\n");

    test_trivial();
    test_jmp();
    test_jmp_conditions();
    test_wait();
    test_in_out();
    test_push_pull();
    test_mov();
    test_set();
    test_irq();
    test_defines();
    test_sideset_delay();
    test_wrap();
    test_ws2812_like();
    test_comments();
    test_public_symbols();
    test_forward_references();
    test_program_name();
    test_lang_opt();
    test_error_missing_program();
    test_error_undefined_symbol();

    printf("\n=====================================\n");
    printf("Results: %d/%d tests passed\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}
