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

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>

#include "../../spooky_decoder.h"

#define DELAY_USEC (50)

#define RX_PIN 0 /* "8" on Arduino */
#define LED_COUNT 4
#define LED_BASE 2
#define LED_MASK 0b00111100

/* LEDs are pins "10" (1 << 2) to "13" (1 << 5) */

static void rx_cb(uint8_t *data, uint8_t data_size, void *udata);
static void blinky_death(void);
static void clear_LEDs(void);
static void toggle_pin(uint8_t bit, uint8_t flag);

struct spooky_decoder dec;
#define DEC_BUF_SIZE 16
uint8_t dec_buf[DEC_BUF_SIZE];

static volatile int interrupt_flag = 0;

// timer compare-A interrupt
ISR(TIMER0_COMPA_vect) { interrupt_flag = 1; }

/* Configure interrupt to trigger every 50 usec. */
static void init_timer(void) {
    /* 'Clear Timer on Compare Match' mode, for when timer == OCR0A. */
    TCCR0A |= (0x02 << WGM00);

    TCCR0B |= (0x02 << CS00);   /* 1:8 clock prescalar */

    /* Number of ticks before interrupt is triggered */
    OCR0A = (int)(DELAY_USEC * 1e-6 * (F_CPU / 8));

    /* Timer/Counter0 Output Compare Match A Interrupt Enable */
    TIMSK0 |= (1 << OCIE0A);
}

static void init(void) {
    /* PORTB: RX pin is input, rest are outputs */
    PORTB = PORTB & ~(1 << RX_PIN); // ensure no pullup
    DDRB = LED_MASK;

    /* PORTD: pins are used for debugging only */
    DDRD = 0xFF;                /* all output */

    init_timer();

    #define PIN_LAST_BIT 2
    #define PIN_ACTIVE_INDICATOR 3
    #define PIN_ERROR 6
    #define PIN_MODE_CHANGE 7

    enum spooky_decoder_init_res res;
    res = spooky_decoder_init(&dec, dec_buf, DEC_BUF_SIZE, rx_cb, NULL);
    if (res != SPOOKY_DECODER_INIT_OK) blinky_death();

    sei(); // enable interrupts
}

static bool read_RX(void) { return (PINB & (1 << RX_PIN)) ? true : false; }

int main(void) {
    init();
    enum spooky_decoder_step_res res;

    static uint8_t last_mode = 0xFF;
    toggle_pin(PIN_ACTIVE_INDICATOR, 0);

    for (;;) {
        while (!interrupt_flag) {}
        interrupt_flag = 0;

        toggle_pin(PIN_ACTIVE_INDICATOR, 1);

        bool rx = read_RX();
        res = spooky_decoder_step(&dec, rx);
        if (res < 0) blinky_death();

        toggle_pin(PIN_LAST_BIT, rx);
        
        /* Indicate state transitions. */
        uint8_t mode = dec.mode;
        if (mode != last_mode) {
            toggle_pin(PIN_MODE_CHANGE, 1);
            if (mode < last_mode && last_mode != 3) toggle_pin(PIN_ERROR, 1);
        }
        last_mode = mode;

#define MODE_MASK 0x03
        // update mode bits
        PORTD = (PORTD & ~(MODE_MASK << 4)) | ((mode & MODE_MASK) << 4);

        toggle_pin(PIN_ACTIVE_INDICATOR, 0);
        toggle_pin(PIN_MODE_CHANGE, 0);
        toggle_pin(PIN_ERROR, 0);

        /* Got a full message */
        if (res == SPOOKY_DECODER_STEP_DONE) {
            // Keep 'em lit for long enough to notice
            _delay_ms(200);
            clear_LEDs();
        }
    }
}

/* Callback: We got a message! */
static void rx_cb(uint8_t *data, uint8_t data_size, void *udata) {
    if (data_size < 2) { return; }

    uint8_t device_id = data[0];
    (void)device_id;            /* unused */
    uint8_t b = data[1];        /* byte of payload */

    /* Set LEDs to match the bits in the payload. */
    for (int i=0; i<LED_COUNT; i++) {
        if ((b & (1 << i))) {
            PORTB |= (1 << (LED_BASE + i));
        } else {
            PORTB &= ~(1 << (LED_BASE + i));
        }

        /* Uncomment to always light one LED. */
        //if (i == LED_COUNT - 1) { PORTB |= (1 << (LED_BASE + i)); }
    }
}

/* Toggle the PORTD pins, used for debugging only. */
static void toggle_pin(uint8_t bit, uint8_t flag) {
    if (flag) {
        PORTD |= (1 << bit);
    } else {
        PORTD &= ~(1 << bit);
    }
}

static void clear_LEDs(void) {
    PORTB &= ~(LED_MASK);
}

static void blinky_death() {
    cli();                      /* disable interrupts */
    for (;;) {
        PORTB ^= LED_MASK; /* blink */
        _delay_ms(1000);
    }
}
