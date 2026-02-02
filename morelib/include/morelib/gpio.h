// SPDX-FileCopyrightText: 2026 Gregory Neverov
// SPDX-License-Identifier: MIT

/*
 * This header provides a Linux-compatible GPIO character device API
 * based on the Linux kernel v2 GPIO userspace ABI.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Constants */
#define GPIO_MAX_NAME_SIZE 16
#define GPIO_V2_LINES_MAX 8
#define GPIO_V2_LINE_NUM_ATTRS_MAX 2

/* GPIO v2 Line Flags */
enum gpio_v2_line_flag {
    GPIO_V2_LINE_FLAG_USED              = 1U << 0,  /* Line is in use */
    GPIO_V2_LINE_FLAG_ACTIVE_LOW        = 1U << 1,  /* Active-low logic */
    GPIO_V2_LINE_FLAG_INPUT             = 1U << 2,  /* Direction: input */
    GPIO_V2_LINE_FLAG_OUTPUT            = 1U << 3,  /* Direction: output */
    GPIO_V2_LINE_FLAG_EDGE_RISING       = 1U << 4,  /* Detect rising edge */
    GPIO_V2_LINE_FLAG_EDGE_FALLING      = 1U << 5,  /* Detect falling edge */
    GPIO_V2_LINE_FLAG_OPEN_DRAIN        = 1U << 6,  /* Open drain output */
    GPIO_V2_LINE_FLAG_OPEN_SOURCE       = 1U << 7,  /* Open source output */
    GPIO_V2_LINE_FLAG_BIAS_PULL_UP      = 1U << 8,  /* Pull-up enabled */
    GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN    = 1U << 9,  /* Pull-down enabled */
    GPIO_V2_LINE_FLAG_BIAS_DISABLED     = 1U << 10, /* Bias disabled */
    GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME = 1U << 11, /* Use CLOCK_REALTIME for events */
    GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE   = 1U << 12, /* Use HTE for timestamps (unsupported) */
};

/* GPIO v2 Line Attribute ID */
enum gpio_v2_line_attr_id {
    GPIO_V2_LINE_ATTR_ID_FLAGS          = 1,  /* Line flags */
    GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES  = 2,  /* Initial output values */
    GPIO_V2_LINE_ATTR_ID_DEBOUNCE       = 3,  /* Debounce period in microseconds */
};

/* GPIO v2 Line Event ID */
enum gpio_v2_line_event_id {
    GPIO_V2_LINE_EVENT_RISING_EDGE      = 1,  /* Rising edge detected */
    GPIO_V2_LINE_EVENT_FALLING_EDGE     = 2,  /* Falling edge detected */
};

/* GPIO v2 Line Info Changed Type */
enum gpio_v2_line_changed_type {
    GPIO_V2_LINE_CHANGED_REQUESTED      = 1,  /* Line requested */
    GPIO_V2_LINE_CHANGED_RELEASED       = 2,  /* Line released */
    GPIO_V2_LINE_CHANGED_CONFIG         = 3,  /* Line configuration changed */
};

/* GPIO Chip Information */
struct gpiochip_info {
    char name[GPIO_MAX_NAME_SIZE];   /* Chip identifier (e.g., "rp2040-gpio") */
    char label[GPIO_MAX_NAME_SIZE];  /* Chip description (e.g., "RP2040 GPIO") */
    uint32_t lines;                  /* Total number of GPIO lines */
};

/* GPIO v2 Line Attribute */
struct gpio_v2_line_attribute {
    uint32_t id;         /* Attribute ID (gpio_v2_line_attr_id) */
    union {
        uint32_t flags;     /* Line flags (when id = FLAGS) */
        uint32_t values;    /* Output values bitmap (when id = OUTPUT_VALUES) */
        uint32_t debounce_period_us; /* Debounce period (when id = DEBOUNCE) */
    };
};

/* GPIO v2 Line Config Attribute */
struct gpio_v2_line_config_attribute {
    struct gpio_v2_line_attribute attr;  /* Attribute data */
    uint32_t mask;                       /* Bitmap of lines this applies to */
};

/* GPIO v2 Line Configuration */
struct gpio_v2_line_config {
    uint32_t flags;     /* Default flags for all lines */
    uint32_t num_attrs; /* Number of attribute overrides */
    struct gpio_v2_line_config_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
};

/* GPIO v2 Line Information */
struct gpio_v2_line_info {
    char name[GPIO_MAX_NAME_SIZE];      /* Line name (e.g., "GPIO25") */
    char consumer[GPIO_MAX_NAME_SIZE];  /* Consumer label (who requested it) */
    uint16_t offset;                    /* Line offset on this chip */
    uint16_t num_attrs;                 /* Number of attributes */
    uint32_t flags;                     /* Line flags */
    struct gpio_v2_line_attribute attrs[GPIO_V2_LINE_NUM_ATTRS_MAX];
};

/* GPIO v2 Line Information Changed Event */
struct gpio_v2_line_info_changed {
    struct gpio_v2_line_info info;  /* Updated line information */
    uint64_t timestamp_ns;          /* When the change occurred */
    uint8_t event_type;             /* Change type (gpio_v2_line_changed_type) */
};

/* GPIO v2 Line Request */
struct gpio_v2_line_request {
    uint8_t offsets[GPIO_V2_LINES_MAX]; /* Line offsets to request */
    char consumer[GPIO_MAX_NAME_SIZE];  /* Consumer label */
    struct gpio_v2_line_config config;  /* Line configuration */
    uint16_t num_lines;                 /* Number of lines requested */
    uint16_t event_buffer_size;         /* Suggested event buffer size */
    int32_t fd;                         /* OUTPUT: returned file descriptor */
};

/* GPIO v2 Line Values */
struct gpio_v2_line_values {
    uint32_t bits;  /* Bitmap of line values (1=high, 0=low) */
    uint32_t mask;  /* Bitmap of which lines to get/set */
};

/* GPIO v2 Line Event */
struct gpio_v2_line_event {
    uint64_t timestamp_ns;  /* Event timestamp (nanoseconds) */
    uint16_t id;            /* Event ID (gpio_v2_line_event_id) */
    uint16_t offset;        /* Line offset that triggered event */
    uint32_t seqno;         /* Global event sequence number */
    uint32_t line_seqno;    /* Per-line event sequence number */
};

/*
 * IOCTL Commands
 */

/* Chip device IOCTLs (on /dev/gpiochipX file descriptor) */
#define GPIO_IOCTL_BASE 0xB4

// Get the publicly available information for a chip.
// param: struct gpiochip_info *
#define GPIO_GET_CHIPINFO_IOCTL             (GPIO_IOCTL_BASE + 0x01)

// Get the publicly available information for a line.
// param: struct gpio_v2_line_info *
#define GPIO_V2_GET_LINEINFO_IOCTL          (GPIO_IOCTL_BASE + 0x05)

// Enable watching a line for changes to its request state and configuration
// param: struct gpio_v2_line_info *
#define GPIO_V2_GET_LINEINFO_WATCH_IOCTL    (GPIO_IOCTL_BASE + 0x06)

// Request a line or lines from the kernel.
// param: struct gpio_v2_line_request *
#define GPIO_V2_GET_LINE_IOCTL              (GPIO_IOCTL_BASE + 0x07)

// Disable watching a line for changes to its requested state and configuration information.
// param: uint32_t *
#define GPIO_GET_LINEINFO_UNWATCH_IOCTL     (GPIO_IOCTL_BASE + 0x0C)

// Update the configuration of previously requested lines.
// param: struct gpio_v2_line_config *
#define GPIO_V2_LINE_SET_CONFIG_IOCTL       (GPIO_IOCTL_BASE + 0x0D)

// Get the values of requested lines.
// param: struct gpio_v2_line_values *
#define GPIO_V2_LINE_GET_VALUES_IOCTL       (GPIO_IOCTL_BASE + 0x0E)

// Set the values of requested output lines.
// param: struct gpio_v2_line_values *
#define GPIO_V2_LINE_SET_VALUES_IOCTL       (GPIO_IOCTL_BASE + 0x0F)

#ifdef __cplusplus
}
#endif
