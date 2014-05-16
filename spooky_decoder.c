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
#include "spooky_decoder.h"

typedef enum {
    RX_HEADER,                  /* 0xFF55 header for clock discovery */
    RX_LENGTH,                  /* length byte */
    RX_CHKSUM,                  /* sum-and-invert checksum byte */
    RX_PAYLOAD,                 /* payload */
} rx_mode;

#define RING_BUF_SZ_BITS 4
#define RING_BUF_SZ (1 << RING_BUF_SZ_BITS)
#define RING_BUF_MASK (RING_BUF_SZ - 1)

/* How many short transitions are required? The first few may be garbled. */
#define SHORT_TRANSITIONS 8

#define MAX_POSSIBLE_DELAY ((uint8_t)-1)

#define DEBUG 0
#if DEBUG
#include <stdio.h>
#define LOG(...) printf("d: " __VA_ARGS__)
static char *st_names[] = {"HEADER", "LENGTH", "CHKSUM", "PAYLOAD",};
#else
#define LOG(...)
#endif

/* Callback for when a complete byte has been received.
 * Returns whether the entire message payload is complete. */
typedef int (byte_cb)(struct spooky_decoder *dec);

static void reset_decoder(struct spooky_decoder *dec);
static int sink_bit(struct spooky_decoder *dec, bool bit);
static uint8_t checksum(uint8_t *buf, size_t size);
static void append_to_ring_buffer(struct spooky_decoder *dec,
    uint8_t offset);

/* Initialize a spooky decoder. */
enum spooky_decoder_init_res
spooky_decoder_init(struct spooky_decoder *dec,
                    uint8_t *output_buffer, size_t buffer_size,
                    spooky_decoder_cb *cb, void *udata) {
    if ((dec == NULL) || (output_buffer == NULL) || (cb == NULL)) {
        LOG("init error: null pointer given\n");
        return SPOOKY_DECODER_INIT_ERROR_NULL;
    }
    if ((buffer_size < SPOOKY_DECODER_MIN_BUFFER_SIZE)
        || (buffer_size > SPOOKY_DECODER_MAX_BUFFER_SIZE)) {
        LOG("init error: bad buffer size\n");
        return SPOOKY_DECODER_INIT_ERROR_BAD_ARGUMENT;
    }

    memset(dec, 0, sizeof(*dec));
    reset_decoder(dec);
    dec->last = 0xFF;           /* neither true nor false */

    dec->buffer = output_buffer;
    dec->buffer_size = buffer_size;
    memset(dec->buffer, 0, buffer_size);

    dec->cb = cb;
    dec->cb_udata = udata;

    LOG("initialized decoder %p with buffer %p (%zu bytes)\n",
        (void*)dec, (void*)output_buffer, buffer_size);
    return SPOOKY_DECODER_INIT_OK;
}

/* States. */
typedef int (step_state)(struct spooky_decoder *dec, bool bit);
static step_state step_header;
static step_state step_length;
static step_state step_chksum;
static step_state step_payload;
#define STATE(NAME) \
    static int NAME(struct spooky_decoder *dec, bool bit)

/* Step the decoder, with a new bit of input.
 * If a complete message has been received, the callback
 * passed to spooky_decoder_init will be called with it. */
enum spooky_decoder_step_res
spooky_decoder_step(struct spooky_decoder *dec, bool bit) {
    enum spooky_decoder_step_res res = SPOOKY_DECODER_STEP_ERROR_NULL;
    if (dec == NULL) {
        LOG("step error: NULL decoder\n");
        return res;
    }
    LOG("dec mode %s, index %d = %20d\n",
        st_names[dec->mode], dec->index, bit);
    dec->ticks++;

    switch (dec->mode) {
    case RX_HEADER: (void)step_header(dec, bit); break;
    case RX_LENGTH: (void)step_length(dec, bit); break;
    case RX_CHKSUM: (void)step_chksum(dec, bit); break;
    case RX_PAYLOAD:
        if (step_payload(dec, bit)) return SPOOKY_DECODER_STEP_DONE;
        break;
    }

    return SPOOKY_DECODER_STEP_OK;
}

/* Is a == (b +/- b/4)? */
static bool approx_eq(int a, int b) {
    /* This is pretty tolerant, but checksumming will also filter. */
    int tol = (b < 4 ? 1 : b / 4);
    if (DEBUG > 1) {
        LOG("%u >= %u and %u <= %u (tol %u, b %u)\n",
            a, b - tol, a, b + tol, tol, b);
    }
    return abs(a - b) <= tol;
}

static void dump_ring_buffer(struct spooky_decoder *dec) {
#if DEBUG
    printf("[");
    uint8_t *buf = dec->buffer;
    for (int i=0; i<RING_BUF_SZ; i++) {
        if (i == (dec->index & RING_BUF_MASK)) printf("*(%d) ", i);
        printf("%d%s, ", buf[i], (i == (dec->index & RING_BUF_MASK)) ? "*" : "");
    }
    printf("]\n");
#endif
}

/* Save the most recent tick count in the ring buffer. */
static void append_to_ring_buffer(struct spooky_decoder *dec,
    uint8_t offset) {
    uint8_t *buf = dec->buffer;

    /* First edge is preceeded by max possible delay. */
    buf[dec->index & RING_BUF_MASK] = (dec->index == 0
        ? MAX_POSSIBLE_DELAY : dec->ticks - offset);
    if (DEBUG) dump_ring_buffer(dec);
    dec->index++;
}

STATE(step_header) {
    if (bit != dec->last) {     /* edge detected */
        append_to_ring_buffer(dec, 0);
        dec->ticks = 0;

        uint16_t total = 0;     /* total for average */
        uint16_t long_count = 0;
        uint16_t avg = 0;

        /* For the cells in the ring buffer, look for some that are
         * approx. even, followed by 8 that are approx. 2x the
         * average of the first 8. */
        uint8_t *buf = dec->buffer;
        for (int i=0; i<RING_BUF_SZ; i++) {
            uint8_t idx = (dec->index + i) & RING_BUF_MASK;
            uint8_t val = buf[idx];
            if (val == MAX_POSSIBLE_DELAY) { break; }

            if (i < RING_BUF_SZ - 8) {
                total += val;
                if (i == RING_BUF_SZ - 8 - 1) {
                    avg = total / (RING_BUF_SZ - 8);
                }
            } else if (avg > 0) {
                if (approx_eq(val, 2*avg)) { long_count++; }
            }
        }

        LOG(" ====> count %d, avg %d\n", long_count, avg);
        if (long_count == 8) {
            LOG("\n\n");
            LOG("Switching to LENGTH state, avg %u\n", avg);
            dec->mode = RX_LENGTH;
            dec->ticks = 0;
            dec->interval = avg;
        }
    dec->last = bit;
    }
    return 0;
}

static bool longer_than_tolerance_allows(uint16_t t, uint16_t i) {
    uint16_t max = i + (i / 4);
    if (DEBUG > 1) { LOG("? %u > %u (%u)\n", t, max, i); }
    return t > max;
}

/* Sink a bit, and call the callback if appropriate. */
static int sink_bit_with_cb(struct spooky_decoder *dec, bool bit,
        byte_cb *cb, bool save_ticks) {
    int res = 0;
    LOG("sink_bit, interval %u, ticks %u, bit %u, pre_ticks %u, last %d, accum 0x%02x\n",
        dec->interval, dec->ticks, bit, dec->pre_ticks, dec->last, dec->bit_accum);

    if (bit == dec->last) {
        if (longer_than_tolerance_allows(dec->ticks - dec->pre_ticks, 2*dec->interval)) {
            LOG("### error in data stream (too long w/out transition), resetting\n");
            reset_decoder(dec);
            return res;
        } else {
            return res;     /* no edge, yet */
        }
    }
    if (DEBUG > 1) { LOG("TRANSITION, %d => %d\n", dec->last, bit); }
    dec->last = bit;

    if (approx_eq(dec->ticks, dec->interval) && dec->pre_ticks == 0) { /* setup edge */
        if (save_ticks) {
            append_to_ring_buffer(dec, 0);
            dec->pre_ticks = dec->ticks;
        }
    } else if (approx_eq(dec->ticks, 2*dec->interval)) { /* actual edge */
        if (save_ticks) { append_to_ring_buffer(dec, dec->pre_ticks); }
        dec->pre_ticks = 0;
        dec->ticks = 0;
        if (sink_bit(dec, bit)) {
            res = cb(dec);      /* call state-specific callback */
            dec->bit_accum = 0x00;
        }
    }
    return res;
}

static int length_byte_cb(struct spooky_decoder *dec) {
    dec->payload_length = dec->bit_accum;
    LOG("got length of 0x%02x\n", dec->payload_length);
    if (dec->payload_length > dec->buffer_size) {
        LOG("input too large for buffer, aborting\n");
        reset_decoder(dec);
    } else if (dec->payload_length == 0) {
        LOG("length of 0, aborting\n");
        reset_decoder(dec);
    } else {
        dec->mode = RX_CHKSUM;
    }
    return 0;
}

static int chksum_byte_cb(struct spooky_decoder *dec) {
    dec->chksum = dec->bit_accum;
    LOG("got checksum of 0x%02x\n", dec->chksum);
    dec->index = 0;
    dec->mode = RX_PAYLOAD;
    return 0;
}

static int payload_byte_cb(struct spooky_decoder *dec) {
    uint8_t byte = dec->bit_accum;
    LOG("got byte: 0x%02x\n", byte);
    dec->buffer[dec->index] = byte;
    dec->index++;
    LOG("index: %u of %u\n", dec->index, dec->payload_length);
    if (dec->index == dec->payload_length) {
        uint8_t cs = checksum(dec->buffer, dec->payload_length);
        LOG("expected 0x%02x, got 0x%02x\n", cs, dec->chksum);
        if (cs == dec->chksum) {
            LOG("success! got %d bytes\n", dec->index);
            dec->cb(dec->buffer, dec->index, dec->cb_udata);
        } else {
            LOG("checksum failure, expected 0x%02x, got 0x%02x\n",
                dec->chksum, cs);
        }
        reset_decoder(dec);
        dec->index = 0;
        /* It could reset the buffer here, but setting the index to
         * 0 will add a MAX_POSSIBLE_DELAY value to the ring buffer,
         * preventing false matches anyway. */
        return 1;
    }
    return 0;
}

/* Read a length byte. */
STATE(step_length) { return sink_bit_with_cb(dec, bit, length_byte_cb, true); }

/* Read a checksum byte. */
STATE(step_chksum) { return sink_bit_with_cb(dec, bit, chksum_byte_cb, true); }

/* Read the data payload. */
STATE(step_payload) { return sink_bit_with_cb(dec, bit, payload_byte_cb, false); }

static void reset_decoder(struct spooky_decoder *dec) {
    dec->mode = RX_HEADER;
    dec->ticks = 0;
    dec->bit_index = 0x80;
    dec->interval = 0;
    dec->bit_accum = 0x00;
    dec->payload_length = 0x00;
    dec->pre_ticks = 0;
    /* Note: Intentionally not resetting the buffer or dec->last here,
     * so that a signal preceded by a false header won't be missed. */
}

/* Sink a bit into the accumulator.
 * Returns whether a byte was completed. */
static int sink_bit(struct spooky_decoder *dec, bool bit) {
    if (bit == 0) {
        ; // no-op
    } else if (bit == 1) {
        dec->bit_accum |= dec->bit_index;
    }
    
    dec->bit_index >>= 1;
    if (dec->bit_index == 0x00) {
        dec->bit_index = 0x80;
        return 1;
    }
    return 0;
}

/* 8-bit sum-and-invert checksum */
static uint8_t checksum(uint8_t *buf, size_t size) {
    uint8_t res = 0;
    for (int i=0; i<size; i++) { res += buf[i]; }
    return ~res;
}
