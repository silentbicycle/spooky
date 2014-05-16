#include "greatest.h"
#include "spooky_encoder.h"
#include "spooky_decoder.h"
#include <string.h>

typedef struct spooky_encoder spooky_encoder;
typedef struct spooky_decoder spooky_decoder;

// weak PRNG
static void set_TCSRNG_value(uint32_t new_value);
static void fill_buffer_with_noise(uint8_t *buf, size_t sz);

/* globals */
struct spooky_encoder enc;
struct spooky_decoder dec;
#define BUF_SZ 32
uint8_t buf[BUF_SZ];
#define RATE_MUL 2

/*********************************************************************
 * Encoder
 *********************************************************************/

static void enc_setup(void *unused) {
    memset(&enc, 0, sizeof(enc));
    memset(&buf, 0, BUF_SZ);
    (void)spooky_encoder_init(&enc, buf, BUF_SZ, 1);
}

TEST encoder_init_should_detect_bad_args() {
    enum spooky_encoder_init_res res;
    res = spooky_encoder_init(NULL, buf, BUF_SZ, 10);
    ASSERT_EQ(SPOOKY_ENCODER_INIT_ERROR_NULL, res);
    res = spooky_encoder_init(&enc, NULL, BUF_SZ, 10);
    ASSERT_EQ(SPOOKY_ENCODER_INIT_ERROR_NULL, res);
    res = spooky_encoder_init(&enc, buf, 0, 10);
    ASSERT_EQ(SPOOKY_ENCODER_INIT_ERROR_BAD_ARGUMENT, res);
    res = spooky_encoder_init(&enc, buf, BUF_SZ, 0);
    ASSERT_EQ(SPOOKY_ENCODER_INIT_ERROR_BAD_ARGUMENT, res);
    res = spooky_encoder_init(&enc, buf, BUF_SZ, 5);
    ASSERT_EQ(SPOOKY_ENCODER_INIT_OK, res);
    PASS();
}

TEST encoder_enqueue_should_accept_outgoing_input() {
    uint8_t input[10];
    for (int i = 0; i < 10; i++) { input[i] = (uint8_t)i; }
    enum spooky_encoder_enqueue_res eres;
    eres = spooky_encoder_enqueue(&enc, input, 10);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_OK, eres);
    PASS();
}

TEST encoder_enqueue_should_reject_excessively_large_messages() {
    uint8_t input[BUF_SZ + 1];
    for (int i = 0; i < BUF_SZ + 1; i++) { input[i] = (uint8_t)i; }
    enum spooky_encoder_enqueue_res eres;
    eres = spooky_encoder_enqueue(&enc, input, BUF_SZ + 1);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_ERROR_SIZE, eres);
    PASS();
}

TEST encoder_enqueue_should_reject_when_output_is_already_enqueued() {
    uint8_t input[11];
    for (int i = 0; i < 11; i++) { input[i] = (uint8_t)i; }
    enum spooky_encoder_enqueue_res eres;
    eres = spooky_encoder_enqueue(&enc, input, 8);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_OK, eres);
    eres = spooky_encoder_enqueue(&enc, input, 8);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_ERROR_FULL, eres);
    PASS();
}

TEST encoder_clear_should_abort_current_TX() {
    uint8_t input[10];
    for (int i = 0; i < 10; i++) { input[i] = (uint8_t)i; }
    enum spooky_encoder_enqueue_res eres;
    eres = spooky_encoder_enqueue(&enc, input, 10);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_OK, eres);

    // buffer in use, fail
    eres = spooky_encoder_enqueue(&enc, input, 10);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_ERROR_FULL, eres);

    // clear it
    ASSERT_EQ(SPOOKY_ENCODER_CLEAR_ERROR_NULL, spooky_encoder_clear(NULL));
    ASSERT_EQ(SPOOKY_ENCODER_CLEAR_OK, spooky_encoder_clear(&enc));

    // now it works
    eres = spooky_encoder_enqueue(&enc, input, 10);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_OK, eres);
    PASS();
}

// manchester coded high and low edges
#define EH SPOOKY_ENCODER_STEP_OK_LOW, SPOOKY_ENCODER_STEP_OK_HIGH
#define EL SPOOKY_ENCODER_STEP_OK_HIGH, SPOOKY_ENCODER_STEP_OK_LOW

uint8_t test_data[] = {0xaa, 0x00};
char expected[] = {
    // Header: 0xFF (16 quick transitions, mark start)
    EH, EH, EH, EH, EH, EH, EH, EH,
    // Header, 0x55 (8 long transitions, mark start of data)
    EL, EH, EL, EH, EL, EH, EL, EH,
    // Header: 0x02 (length)
    EL, EL, EL, EL, EL, EL, EH, EL,
    // Header: 0x55 (sum-and-invert checksum of payload)
    EL, EH, EL, EH, EL, EH, EL, EH,
    // Payload: 0xAA
    EH, EL, EH, EL, EH, EL, EH, EL,
    // Payload: 0x00
    EL, EL, EL, EL, EL, EL, EL, EL,
    SPOOKY_ENCODER_STEP_OK_DONE,
};

#undef EH
#undef EL

TEST encoder_step_should_emit_bits_with_header_footer_and_checksum() {
    enum spooky_encoder_init_res ires; 
    ires = spooky_encoder_init(&enc, buf, BUF_SZ, 1);
        
    enum spooky_encoder_enqueue_res eres;
    eres = spooky_encoder_enqueue(&enc, test_data, sizeof(test_data));
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_OK, eres);

    enum spooky_encoder_step_res sres;
    for (int i=0; i<sizeof(expected); i++) {
        sres = spooky_encoder_step(&enc);
        ASSERT_EQ(expected[i], sres);
    }    
    PASS();
}

TEST encoder_step_should_emit_bits_slower_with_longer_tx_rate() {
    enum spooky_encoder_init_res ires; 
    ires = spooky_encoder_init(&enc, buf, BUF_SZ, 10);
        
    enum spooky_encoder_enqueue_res eres;
    eres = spooky_encoder_enqueue(&enc, test_data, sizeof(test_data));
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_OK, eres);

    enum spooky_encoder_step_res sres;
    for (int i=0; i<sizeof(expected); i++) {
        for (int ticks=0; ticks<9; ticks++) {
            sres = spooky_encoder_step(&enc);
            ASSERT_EQ(SPOOKY_ENCODER_STEP_OK, sres);
        }
        sres = spooky_encoder_step(&enc);
        ASSERT_EQ(expected[i], sres);
    }    
    PASS();
}

SUITE(encoder) {
    printf("sizeof encoder: %zd\n", sizeof(spooky_encoder));
    RUN_TEST(encoder_init_should_detect_bad_args);
    SET_SETUP(enc_setup, NULL);
    RUN_TEST(encoder_enqueue_should_accept_outgoing_input);
    RUN_TEST(encoder_enqueue_should_reject_excessively_large_messages);
    RUN_TEST(encoder_enqueue_should_reject_when_output_is_already_enqueued);
    RUN_TEST(encoder_clear_should_abort_current_TX);
    RUN_TEST(encoder_step_should_emit_bits_with_header_footer_and_checksum);
    RUN_TEST(encoder_step_should_emit_bits_slower_with_longer_tx_rate);
}


/*********************************************************************
 * Decoder
 *********************************************************************/

#define OUTPUT_BUF_SZ 32

static int rate = 1;
static int called = 0;
static uint8_t output_buf[OUTPUT_BUF_SZ];
static size_t output_sz = 0;

void dec_cb(uint8_t *buf, uint8_t sz, void *udata) {
    int *c = (int *)udata;
    *c = 1;
    memcpy(output_buf, buf, sz);
    output_sz = sz;
}

TEST decoder_init_should_detect_bad_args() {
    enum spooky_decoder_init_res res;
    res = spooky_decoder_init(NULL, buf, BUF_SZ, dec_cb, NULL);
    ASSERT_EQ(SPOOKY_DECODER_INIT_ERROR_NULL, res);
    res = spooky_decoder_init(&dec, NULL, BUF_SZ, dec_cb, NULL);
    ASSERT_EQ(SPOOKY_DECODER_INIT_ERROR_NULL, res);
    res = spooky_decoder_init(&dec, buf, BUF_SZ, NULL, NULL);
    ASSERT_EQ(SPOOKY_DECODER_INIT_ERROR_NULL, res);
    res = spooky_decoder_init(&dec, buf,
        SPOOKY_DECODER_MIN_BUFFER_SIZE - 1, dec_cb, NULL);
    ASSERT_EQ(SPOOKY_DECODER_INIT_ERROR_BAD_ARGUMENT, res);
    PASS();
}

static void dec_setup(void *unused) {
    memset(&dec, 0, sizeof(dec));
    memset(&buf, 0, BUF_SZ);
    memset(&output_buf, 0, OUTPUT_BUF_SZ);
    output_sz = 0;
    called = 0;
    rate = 1;
    (void)spooky_decoder_init(&dec, buf, OUTPUT_BUF_SZ, dec_cb, (void *)&called);
}

TEST decoder_step_should_reject_noise() {
    enum spooky_decoder_step_res res;
    uint8_t junk[] = { 0xc6, 0xc3, 0x00, 0x52, 0x8d, 0xda, 0x0b, 0x92,
                       0x92, 0x17, 0x49, 0x18, 0xe5, 0x49, 0x4b, 0x27,
                       0xc9, 0x7b, 0xe7, 0xdd }; //echo "junk" | sha1sum

    for (int byte_i = 0; byte_i < sizeof(junk); byte_i++) {
        for (int bit_i = 0; bit_i < 8; bit_i++) {
            uint8_t byte = junk[byte_i];
            bool bit = byte & (1 << (7 - bit_i));
            res = spooky_decoder_step(&dec, bit);
            ASSERT_EQ(SPOOKY_DECODER_STEP_OK, res);
        }
    }
    ASSERT_EQ(0, called);
    PASS();
}

static int expect_byte(struct spooky_decoder *d, uint8_t b) {
    enum spooky_decoder_step_res res;
    for (int i=0; i<8; i++) {
        bool bit = b & (1 << (7 - i));
        for (int r=0; r < rate * RATE_MUL; r++) {
            res = spooky_decoder_step(d, !bit);
            if (res < 0) {
                printf("step error: %d\n", res);
                return 0;
            }
        }
        for (int r=0; r < rate * RATE_MUL; r++) {
            res = spooky_decoder_step(d, bit);
            if (res < 0) {
                printf("step error: %d\n", res);
                return 0;
            }
        }
    }
    return 1;
}

#define EB(B)                                   \
    if (GREATEST_IS_VERBOSE()) { printf(" -- expect_byte: 0x%02x\n", B); }  \
    if (!expect_byte(&dec, B)) { FAIL(); }

TEST decoder_step_should_return_received_buffer() {
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length 0b0000 0001
    EB(0x85); // checksum, 0b1000 0101
    EB(0x7a); // payload: 0x7a (arbitrary)

    ASSERT_EQ(1, called);
    ASSERT_EQ(1, output_sz);
    ASSERT_EQ(0x7a, output_buf[0]);
    
    PASS();
}

TEST decoder_step_should_reject_message_with_invalid_checksum() {
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length, 0b0000 0001
    EB(0x58); // incorrect checksum
    EB(0x7a); // payload: 0x7a (arbitrary), 0b0111 1010

    ASSERT_EQ(0, called);
    ASSERT_EQ(0, output_sz);
    
    PASS();
}

TEST decoder_step_should_reject_message_larger_than_buffer() {
    EB(0xFF);
    EB(0x55); // 0b0101 0101
    EB(OUTPUT_BUF_SZ + 1); // length
    uint8_t msg_buf[OUTPUT_BUF_SZ + 1];
    uint8_t chksum = 0;
    for (int i=0; i<OUTPUT_BUF_SZ + 1; i++) { msg_buf[i] = i; chksum += i; }
    EB(~chksum);
    for (int i=0; i<OUTPUT_BUF_SZ + 1; i++) { EB(msg_buf[i]); }

    ASSERT_EQ(0, called);
    ASSERT_EQ(0, output_sz);
    
    PASS();
}

TEST decoder_step_should_return_received_buffer_when_rate_is_multiple_of_steps_2() {
    rate = 2; // send each GPIO bit read 2x to test clock recovery
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length
    EB(0x85); // checksum, 0b1000 0101
    EB(0x7a); // payload: 0x7a (arbitrary)

    ASSERT_EQ(1, called);
    ASSERT_EQ(1, output_sz);
    ASSERT_EQ(0x7a, output_buf[0]);
    
    PASS();
}

TEST decoder_step_should_return_received_buffer_when_rate_is_multiple_of_steps_7() {
    rate = 7; // send each GPIO bit read 7x to test clock recovery
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length
    EB(0x85); // checksum, 0b1000 0101
    EB(0x7a); // payload: 0x7a (arbitrary)

    ASSERT_EQ(1, called);
    ASSERT_EQ(1, output_sz);
    ASSERT_EQ(0x7a, output_buf[0]);
    
    PASS();
}

TEST decode_buffer_when_preceded_by_false_header() {
    EB(0x0F); // 0b0000 1111
    EB(0x55); // 0b0101 0101
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length
    EB(0x85); // checksum, 0b1000 0101
    EB(0x7a); // payload: 0x7a (arbitrary)

    ASSERT_EQ(1, called);
    ASSERT_EQ(1, output_sz);
    ASSERT_EQ(0x7a, output_buf[0]);
    
    PASS();
}

TEST decode_buffer_when_preceded_by_noise(uint16_t size, uint8_t seed, uint8_t ticks) {
    uint8_t noise_buf[size];
    set_TCSRNG_value(seed);
    memset(noise_buf, 0, size);
    fill_buffer_with_noise(noise_buf, size);
    rate = ticks;

    for (int i=0; i<size; i++) {
        if (GREATEST_IS_VERBOSE()) printf("### junk byte 0x%02x\n", noise_buf[i]);
        EB(noise_buf[i]);
    }

    if (GREATEST_IS_VERBOSE()) printf("### actual content\n");
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length
    EB(0x85); // checksum, 0b1000 0101
    EB(0x7a); // payload: 0x7a (arbitrary)

    ASSERT_EQ(1, called);
    ASSERT_EQ(1, output_sz);
    ASSERT_EQ(0x7a, output_buf[0]);
    
    PASS();
}

TEST recover_from_noise() {
    /* This tests recovery -- the real data starts while the state machine
     * is falsely in the length state due to noise. */
    uint8_t junk[] = { 0xb0, 0x39, 0x8d, 0xca, 0xb6, 0xc6, 0x0d, 0x57, };
    for (int i=0; i<sizeof(junk); i++) { EB(junk[i]); }
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length
    EB(0x85); // checksum, 0b1000 0101
    EB(0x7a); // payload: 0x7a (arbitrary)

    ASSERT_EQ(1, called);
    ASSERT_EQ(1, output_sz);
    ASSERT_EQ(0x7a, output_buf[0]);
    
    PASS();
}

TEST recover_when_real_message_appears_during_false_payload_state() {
    SKIPm("the payload clobbers the ring buffer, loses recovery info.");

    /* This tests recovery -- the real data starts while the state machine
     * is falsely in the payload state due to noise. */
    uint8_t junk[] = { 0x9a, 0xdc, 0x68, 0x8c, 0x55, 0x01 };
    for (int i=0; i<sizeof(junk); i++) { EB(junk[i]); }
    EB(0xFF); // 0b1111 1111
    EB(0x55); // 0b0101 0101
    EB(0x01); // length
    EB(0x85); // checksum, 0b1000 0101
    EB(0x7a); // payload: 0x7a (arbitrary)

    ASSERT_EQ(1, called);
    ASSERT_EQ(1, output_sz);
    ASSERT_EQ(0x7a, output_buf[0]);
    
    PASS();
}
SUITE(decoder) {
    printf("sizeof decoder: %zd\n", sizeof(spooky_decoder));

    RUN_TEST(decoder_init_should_detect_bad_args);
    SET_SETUP(dec_setup, NULL);

    RUN_TEST(decoder_step_should_reject_noise);
    RUN_TEST(decoder_step_should_return_received_buffer);
    RUN_TEST(decoder_step_should_reject_message_larger_than_buffer);
    RUN_TEST(decoder_step_should_reject_message_with_invalid_checksum);
    RUN_TEST(decoder_step_should_return_received_buffer_when_rate_is_multiple_of_steps_2);
    RUN_TEST(decoder_step_should_return_received_buffer_when_rate_is_multiple_of_steps_7);
    RUN_TEST(decode_buffer_when_preceded_by_false_header);
    RUN_TEST(recover_from_noise);

    RUN_TEST(recover_when_real_message_appears_during_false_payload_state);

    RUN_TESTp(decode_buffer_when_preceded_by_noise, 8, 2, 1);

    // This was failing with a length of 0.
    RUN_TESTp(decode_buffer_when_preceded_by_noise, 15, 2, 3);

    // A couple regression tests
    RUN_TESTp(decode_buffer_when_preceded_by_noise, 1, 5, 1);
    RUN_TESTp(decode_buffer_when_preceded_by_noise, 2, 7, 1);
    RUN_TESTp(decode_buffer_when_preceded_by_noise, 2, 5, 2);
    RUN_TESTp(decode_buffer_when_preceded_by_noise, 2, 7, 7);
    RUN_TESTp(decode_buffer_when_preceded_by_noise, 15, 2, 3);

    // Fuzz the decoder
    for (int ticks=1; ticks<8; ticks++) {
        for (int size=1; size<16; size++) {
            for (int seed=0; seed<25; seed++) {
                if (GREATEST_FAILURE_ABORT()) return;
                if (GREATEST_IS_VERBOSE()) {
                    printf("size %d, seed %d, ticks %d:\n", size, seed, ticks);
                }
                RUN_TESTp(decode_buffer_when_preceded_by_noise, size, seed, ticks);
            }
        }
    }
}


/***************
 * Integration *
 ***************/

static uint32_t TCSRNG_value = (1L << 31) - 1;

static void set_TCSRNG_value(uint32_t new_value) { TCSRNG_value = new_value; }

static uint32_t totes_cryptographically_secure_random_number_generator() {
    static uint32_t mul = (1L << 31) - 19;
    static uint32_t inc = (1L << 31) - 61;

    TCSRNG_value = (TCSRNG_value * mul) + inc;
    return TCSRNG_value;
}

static void fill_buffer_with_noise(uint8_t *buf, size_t sz) {
    for (int i=0; i<sz; i++) {
        buf[i] = totes_cryptographically_secure_random_number_generator() % 0xFF;
        //printf("%02x ", buf[i]);
    }
    //printf("\n");
}

TEST data_should_tx_and_rx_intact(uint8_t size, uint32_t seed, uint8_t ticks) {
    uint8_t in_buf[size];
    uint8_t out_buf[size + 8];
    set_TCSRNG_value(seed);
    memset(out_buf, 0, size);
    fill_buffer_with_noise(in_buf, size);
    called = 0;

    enum spooky_encoder_init_res eires = spooky_encoder_init(&enc,
        buf, size, ticks);
    ASSERT_EQ(SPOOKY_ENCODER_INIT_OK, eires);

    enum spooky_decoder_init_res dires = spooky_decoder_init(&dec,
        out_buf, size + 8, dec_cb, (void *)&called);
    ASSERT_EQ(SPOOKY_DECODER_INIT_OK, dires);

    enum spooky_encoder_enqueue_res eres = spooky_encoder_enqueue(&enc,
        in_buf, size);
    ASSERT_EQ(SPOOKY_ENCODER_ENQUEUE_OK, eres);

    enum spooky_encoder_step_res esres;
    int timeout = 0;
    bool bit = false;
    for (timeout=0; timeout<1000; timeout++) {
        enum spooky_decoder_step_res dsres = SPOOKY_DECODER_STEP_OK;
        esres = spooky_encoder_step(&enc);
        ASSERT(esres >= 0);
        if (esres == SPOOKY_ENCODER_STEP_OK_LOW) {
            bit = false;
            //printf("0 ");
        } else if (esres == SPOOKY_ENCODER_STEP_OK_HIGH) {
            bit = true;
            //printf("1 ");
        } else {
            ASSERT(esres >= 0);
        }

        for (int i=0; i<RATE_MUL; i++) {
            dsres = spooky_decoder_step(&dec, bit);
            if (called) { break; }
            if (dsres < 0) { FAILm("decoder error"); }
        }
        if (called) { break; }
    }
    if (timeout == 1000) { FAILm("stuck in potentially infinite loop"); }

    ASSERT_EQ(1, called);

    for (int i=0; i<size;i++) {
        ASSERT_EQ(in_buf[i], out_buf[i]);
    }
    PASS();
}

SUITE(integration) {
    // regression tests
    RUN_TESTp(data_should_tx_and_rx_intact, 9, 1, 1);

    // fuzz testing
    for (int size=8; size<16; size++) {
        for (int ticks=1; ticks < 4; ticks++) {
            for (int seed=0; seed<50; seed++) {
                if (GREATEST_IS_VERBOSE()) {
                    printf("size %d, ticks %d, seed %d:\n",
                        size, ticks, seed);
                }
                RUN_TESTp(data_should_tx_and_rx_intact, size, seed, ticks);
            }
        }
    }
}

/* Add all the definitions that need to be in the test runner's main file. */
GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();      /* command-line arguments, initialization. */
    RUN_SUITE(encoder);
    RUN_SUITE(decoder);
    RUN_SUITE(integration);
    GREATEST_MAIN_END();        /* display results */
}
