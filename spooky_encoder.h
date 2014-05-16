#ifndef SPOOKY_ENCODE_H
#define SPOOKY_ENCODE_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Struct for the encoder. */
struct spooky_encoder {
    uint16_t index;
    uint8_t tx_rate;
    uint8_t buffer_size;
    uint8_t input_size;
    uint8_t ticks;
    uint8_t mode;
    uint8_t chksum;
    uint8_t *buffer;
};

enum spooky_encoder_init_res {
    SPOOKY_ENCODER_INIT_OK = 0,
    SPOOKY_ENCODER_INIT_ERROR_NULL = -1,
    SPOOKY_ENCODER_INIT_ERROR_BAD_ARGUMENT = -2,
};

enum spooky_encoder_enqueue_res {
    SPOOKY_ENCODER_ENQUEUE_OK = 0,
    SPOOKY_ENCODER_ENQUEUE_ERROR_SIZE = -1,
    SPOOKY_ENCODER_ENQUEUE_ERROR_FULL = -2,
};

enum spooky_encoder_clear_res {
    SPOOKY_ENCODER_CLEAR_OK = 0,
    SPOOKY_ENCODER_CLEAR_ERROR_NULL = -1,
};

enum spooky_encoder_step_res {
    SPOOKY_ENCODER_STEP_OK = 0,
    SPOOKY_ENCODER_STEP_OK_LOW = 1,
    SPOOKY_ENCODER_STEP_OK_HIGH = 2,
    SPOOKY_ENCODER_STEP_OK_DONE = 3,
    SPOOKY_ENCODER_STEP_ERROR_EMPTY = -1,
    SPOOKY_ENCODER_STEP_ERROR_NULL = -2,
};

/* Initialize an encoder.
 * TX_RATE is the number of ticks per bit, which must be >= 2 to allow
 * transitions within a frame. A tick consists of calling 'step' once. */
enum spooky_encoder_init_res
spooky_encoder_init(struct spooky_encoder *enc,
    uint8_t *buffer, uint8_t buffer_size, uint8_t tx_rate);

/* Enqueue a new outgoing message, which will be copied into the
 * encoder's internal buffer. */
enum spooky_encoder_enqueue_res
spooky_encoder_enqueue(struct spooky_encoder *enc,
    uint8_t *input, uint8_t input_size);

/* Abort and clear the current transmission. */
enum spooky_encoder_clear_res
spooky_encoder_clear(struct spooky_encoder *enc);

/* Step the current encoding. Should be called periodically, at as
 * consistent an interval as the hardware will allow (ideally, once
 * every 50 microseconds).
 * 
 * Returns whether the signal should stay as-is (OK),
 * transition low or high (OK_LOW, OK_HIGH), or if the TX is complete. */
enum spooky_encoder_step_res
spooky_encoder_step(struct spooky_encoder *enc);


#endif
