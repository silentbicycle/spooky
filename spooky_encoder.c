/* 
 * Copyright (c) 2014 Scott Vokes <vokes.s@gmail.com>
 *  
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *  
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>
#include "spooky_encoder.h"

typedef enum {
    TX_NONE,                    /* no message */
    TX_SHARP,                   /* header: sharp transitions */
    TX_LONG,                    /* header: slow transitions */
    TX_LENGTH,                  /* header: length */
    TX_CHKSUM,                  /* header: checksum */
    TX_PAYLOAD,                 /* message */
} tx_mode;

#define HEADER_SHARP_TRANSITIONS 8
#define HEADER_LONG_TRANSITIONS 4

#if 0
#include <stdio.h>
#define LOG(...) printf("e: " __VA_ARGS__)
#else
#define LOG(...)
#endif

static uint8_t calc_chksum(uint8_t *buf, size_t length);
static enum spooky_encoder_step_res encode_bit(uint8_t bit, uint8_t index);

/* Initialize an encoder. */
enum spooky_encoder_init_res
spooky_encoder_init(struct spooky_encoder *enc,
                    uint8_t *buffer, uint8_t buffer_size, uint8_t tx_rate) {
    if ((enc == NULL) || (buffer == NULL)) {
        return SPOOKY_ENCODER_INIT_ERROR_NULL;
    }
    if ((buffer_size == 0) || (tx_rate == 0)) {
        return SPOOKY_ENCODER_INIT_ERROR_BAD_ARGUMENT;
    }

    memset(enc, 0, sizeof(*enc));
    enc->buffer = buffer;
    enc->buffer_size = buffer_size;
    enc->tx_rate = tx_rate;
    enc->mode = TX_NONE;
    LOG("initialized %p with buffer %p (%u bytes), rate %u\n",
        (void*)enc, (void*)buffer, buffer_size, tx_rate);
    return SPOOKY_ENCODER_INIT_OK;
}

/* Enqueue a new outgoing message, which will be copied into the
 * encoder's internal buffer. */
enum spooky_encoder_enqueue_res
spooky_encoder_enqueue(struct spooky_encoder *enc,
                       uint8_t *input, uint8_t input_size) {
    if (enc->mode != TX_NONE) return SPOOKY_ENCODER_ENQUEUE_ERROR_FULL;
    enc->mode = TX_SHARP;
    if (input_size > enc->buffer_size) {
        return SPOOKY_ENCODER_ENQUEUE_ERROR_SIZE;
    }
    memcpy(enc->buffer, input, input_size);
    enc->input_size = input_size;
    enc->index = 0;
    LOG("enqueued buffer %p (%d bytes)\n", input, input_size);
    return SPOOKY_ENCODER_ENQUEUE_OK;
}

enum spooky_encoder_clear_res
spooky_encoder_clear(struct spooky_encoder *enc) {
    if (enc == NULL) return SPOOKY_ENCODER_CLEAR_ERROR_NULL;
    if (enc->mode != TX_NONE) {
        enc->mode = TX_NONE;
    }
    return SPOOKY_ENCODER_CLEAR_OK;
}

#define LOW SPOOKY_ENCODER_STEP_OK_LOW
#define HIGH SPOOKY_ENCODER_STEP_OK_HIGH

/* Step the current encoding.
 * Returns whether the signal should stay as-is (OK),
 * transition low or high (OK_LOW, OK_HIGH), or if the TX is complete. */
enum spooky_encoder_step_res
spooky_encoder_step(struct spooky_encoder *enc) {
    enum spooky_encoder_step_res res = SPOOKY_ENCODER_STEP_ERROR_NULL;
    if (enc == NULL) return res;

    enc->ticks++;
    if ((enc->ticks % enc->tx_rate) != 0)
        return SPOOKY_ENCODER_STEP_OK;
    enc->ticks = 0;

    LOG("step, mod %u\n", enc->mode);

    switch (enc->mode) {
    case TX_NONE:
        res = SPOOKY_ENCODER_STEP_OK_DONE;
        break;
    case TX_SHARP:                 /* send sharp transitions */
        res = encode_bit(0x01, enc->index);
        enc->index++;
        if (enc->index == 2*HEADER_SHARP_TRANSITIONS) {
            enc->mode = TX_LONG;
            enc->index = 0;
        }
        break;
    case TX_LONG:
    {
        uint8_t bit = 0x55 & (1 << (7 - (enc->index/2)));
        res = encode_bit(bit, enc->index);
        enc->index++;
        if (enc->index == 4*HEADER_LONG_TRANSITIONS) {
            enc->mode = TX_LENGTH;
            enc->index = 0;
            LOG("length is 0x%02x\n", enc->input_size);
        }
        break;
    }
    case TX_LENGTH:
    {
        uint8_t bit = enc->input_size & (1 << (7 - (enc->index / 2)));
        res = encode_bit(bit, enc->index);
        enc->index++;
        if (enc->index == 2*8) {
            enc->mode = TX_CHKSUM;
            enc->index = 0;
            enc->chksum = calc_chksum(enc->buffer, enc->input_size);
            LOG("checksum is 0x%02x\n", enc->chksum);
        }
        break;
    }
    case TX_CHKSUM:
    {
        uint8_t bit = enc->chksum & (1 << (7 - (enc->index/2)));
        res = encode_bit(bit, enc->index);
        enc->index++;
        if (enc->index == 2*8) {
            enc->mode = TX_PAYLOAD;
            enc->index = 0;
        }
        break;
    }
    case TX_PAYLOAD:
    {
        uint8_t byte_idx = enc->index / 16;
        uint8_t bit_idx = (enc->index % 16) / 2;
        uint8_t byte = enc->buffer[byte_idx];
        LOG("sending byte 0x%02x bit %d\n", byte, 7 - bit_idx);
        uint8_t bit = byte & (1 << (7 - (bit_idx)));
        res = encode_bit(bit, enc->index);
        enc->index++;
        if (enc->index == 8*2*enc->input_size) {
            LOG("msg done!\n");
            enc->mode = TX_NONE;
        }
        break;
    }
    }
    return res;
}

static uint8_t calc_chksum(uint8_t *buf, size_t length) {
    uint8_t res = 0;
    for (int i=0; i<length; i++) res += buf[i];
    return ~res;
}

static enum spooky_encoder_step_res encode_bit(uint8_t bit, uint8_t index) {
    if ((index & 0x01) == 0) {  /* prepare for bit edge */
        return bit ? LOW : HIGH;
    } else {                    /* actual bit edge */
        return bit ? HIGH : LOW;
    }
}
