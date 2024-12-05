// SPDX-FileCopyrightText: 2023 Gregory Neverov
// SPDX-License-Identifier: MIT

/**
 * Multiplexes the TinyUSB CDC callbacks.
 */

#include "tusb.h"
#if CFG_TUD_CDC
#include "tinyusb/cdc_device_cb.h"
#include "tinyusb/tusb_lock.h"

static tud_cdc_cb_t tud_cdc_cbs[CFG_TUD_CDC];
static void *tud_cdc_cb_contexts[CFG_TUD_CDC];

/**
 * Sets the callback for a CDC interface.
 * 
 * Args:
 * itf: CDC inteface number
 * cb: callback
 * context: context argument of callback
 */
void tud_cdc_set_cb(uint8_t itf, tud_cdc_cb_t cb, void *context) {
    assert(itf < CFG_TUD_CDC);
    tud_lock();
    tud_cdc_cbs[itf] = cb;
    tud_cdc_cb_contexts[itf] = context;
    tud_unlock();
}

/**
 * Clears the ballback for a CDC interface.
 * 
 * Args:
 * itf: CDC interface number
 */
void tud_cdc_clear_cb(uint8_t itf) {
    assert(itf < CFG_TUD_CDC);
    tud_lock();
    tud_cdc_cbs[itf] = NULL;
    tud_cdc_cb_contexts[itf] = NULL;
    tud_unlock();
}


// TinyUSB static callback functions
static void tud_cdc_call_cb(uint8_t itf, tud_cdc_cb_type_t cb_type, tud_cdc_cb_args_t *cb_args) {
    assert(itf < CFG_TUD_CDC);
    tud_lock();
    if (tud_cdc_cbs[itf]) {
        tud_cdc_cbs[itf](tud_cdc_cb_contexts[itf], cb_type, cb_args);
    }
    tud_unlock();
}

__attribute__((visibility("hidden")))
void tud_cdc_rx_cb(uint8_t itf) {
    tud_cdc_call_cb(itf, TUD_CDC_RX, NULL);
}

__attribute__((visibility("hidden")))
void tud_cdc_rx_wanted_cb(uint8_t itf, char wanted_char) {
    tud_cdc_cb_args_t args = {
        .rx_wanted = { wanted_char }
    };
    tud_cdc_call_cb(itf, TUD_CDC_RX_WANTED, &args);
}

__attribute__((visibility("hidden")))
void tud_cdc_tx_complete_cb(uint8_t itf) {
    tud_cdc_call_cb(itf, TUD_CDC_TX_COMPLETE, NULL);
}

__attribute__((visibility("hidden")))
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    tud_cdc_cb_args_t args = {
        .line_state = { dtr, rts }
    };
    tud_cdc_call_cb(itf, TUD_CDC_LINE_STATE, &args);
}

__attribute__((visibility("hidden")))
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding) {
    tud_cdc_cb_args_t args = {
        .line_coding = { p_line_coding }
    };
    tud_cdc_call_cb(itf, TUD_CDC_LINE_CODING, &args);
}

__attribute__((visibility("hidden")))
void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms) {
    tud_cdc_cb_args_t args = {
        .send_break = { duration_ms }
    };
    tud_cdc_call_cb(itf, TUD_CDC_SEND_BREAK, &args);
}
#endif
