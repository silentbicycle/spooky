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
#include <avr/delay.h>
#include <avr/interrupt.h>

#include "../../spooky_encoder.h"

/* How many microseconds to delay between edges.
 * The effective transmission rate is roughly:
 * 1e6_usec_per_sec / (2_edges * DELAY_USEC * 8_bits_per_byte)
 * So a DELAY_USEC of:
 *     900 -> ~ 70 bytes / sec.
 *     240 -> ~260 bytes / sec.
 *
 * The rate at which you can transmit will depend a great deal
 * on your transmitter and receiver.
 *
 * Note that as you change this, you may also need to change the
 * prescalar value in TCCR0B below, since OCR0A is 8-bit.
 */
#define DELAY_USEC 900
/* Other values derived from it */
#define DELAY_TICKS_PER_MSEC (1000L / DELAY_USEC)

/* The device ID, sent as a prefix to the payload. */
#define DEVICE_ID 0xED

/* How many seconds to hold the signal high before sending
 * the actual message. The button is debounced during this
 * time, and it helps get a hold on the signal. */
#define DEBOUNCE_MSEC 10
#define DEBOUNCE_COUNT (DEBOUNCE_MSEC * DELAY_TICKS_PER_MSEC)

/* How many seconds to wait between transmissions */
#define TIMEOUT_SECONDS 3
#define TIMEOUT (TIMEOUT_SECONDS * 1000 * DELAY_TICKS_PER_MSEC)

#define TX_PIN 5 /* "13" on Arduino */

typedef enum { M_BUTTON, M_TX, M_TIMEOUT, } mode_t;

static bool is_button_down(void);
static bool check_switch(uint8_t id);
static void set_TX(bool high);
static void enqueue_tx_message(void);
static void blinky_death(void);
static void check_button(void);
static void step_tx(void);
static void step_timeout(void);

struct spooky_encoder enc;
#define ENC_BUF_SIZE 8
uint8_t enc_buf[ENC_BUF_SIZE];

/* All are on PORTB / PINB */
/* B (digital pin 8 to 13)
 * C (analog input pins)
 * D (digital pins 0 to 7) */

static void init_timer(void) {
    /* 'Clear Timer on Compare Match' mode, for when timer == OCR0A. */
    TCCR0A |= (0x02 << WGM00);

    TCCR0B |= (0x03 << CS00);   /* 1:64 clock prescalar */

    /* Number of ticks before interrupt is triggered */
    OCR0A = (int)(DELAY_USEC * 1e-6 * (F_CPU / 64));

    /* Timer/Counter0 Output Compare Match A Interrupt Enable */
    TIMSK0 |= (1 << OCIE0A) ;
}

static volatile int interrupt_flag = 0;

// timer compare-A interrupt
ISR(TIMER0_COMPA_vect) { interrupt_flag++; }

static void init(void)
{
    init_timer();

    /* 4 switches are inputs */
    /* button is input */
    /* TX pin is output */
    DDRB = 0b11100000;

    enum spooky_encoder_init_res res;
    res = spooky_encoder_init(&enc, enc_buf, ENC_BUF_SIZE, 1);
    if (res != SPOOKY_ENCODER_INIT_OK) blinky_death();

    sei();   /* enable interrupts */
}

static mode_t mode = M_BUTTON;
static uint32_t timeout = 0;
static uint16_t button_debounce = 0;

int main(void) {
    init();

    for (;;) {
        while (!interrupt_flag) {}
        interrupt_flag = 0;

        switch (mode) {
        case M_BUTTON: check_button(); break;
        case M_TX: step_tx(); break;
        case M_TIMEOUT: step_timeout(); break;
        }
    }
}

static void check_button(void) {
    if (is_button_down()) {
        set_TX(true);
        if (button_debounce == DEBOUNCE_COUNT) {
            mode = M_TX;
            enqueue_tx_message();
            button_debounce = 0;
        } else {
            button_debounce++;
        }
    } else {
        button_debounce = 0;
        set_TX(false);
    }
}

static void step_tx(void) {
    enum spooky_encoder_step_res res;
    res = spooky_encoder_step(&enc);
    switch(res) {
    case SPOOKY_ENCODER_STEP_OK_DONE:
        timeout = TIMEOUT;
        mode = (timeout > 0 ? M_TIMEOUT : M_BUTTON);
        /* fall through */
    case SPOOKY_ENCODER_STEP_OK_LOW: 
        set_TX(false);
        break;
    case SPOOKY_ENCODER_STEP_OK_HIGH:
        set_TX(true);
        break;
    case SPOOKY_ENCODER_STEP_OK:
        /* leave as-is */
        break;
    default:
        blinky_death();
    }
}

static void step_timeout(void) {
    if (timeout == 0) {
        mode = M_BUTTON;
    } else {
        timeout--;
    }
}

static bool is_button_down(void) { return PINB & (1 << 4); }

static bool check_switch(uint8_t id) {
    if (id > 3) return false;
    return PINB & (1 << id);
}

static void set_TX(bool high) {
    if (high) {
        PORTB |= (1 << TX_PIN);
    } else {
        PORTB &= ~(1 << TX_PIN);
    }
}

static void enqueue_tx_message() {
    uint8_t payload[2];
    uint8_t switch_bits = 0x00;
    payload[0] = DEVICE_ID;
    for (int i=0; i<4; i++) {
        if (check_switch(i)) { switch_bits |= (1 << i); }
    }

    payload[1] = switch_bits;

    if (spooky_encoder_enqueue(&enc, payload, 2) != SPOOKY_ENCODER_ENQUEUE_OK) {
        blinky_death();
    }
}

static void blinky_death() {
    for (;;) {
        PORTB ^= 0xFF; /* blink */
        _delay_ms(1000);       
    }
}
