spooky: a library for OOK Manchester encoding, decoding, and dynamic clock recovery.

For usage, see `spooky_decoder.h` and `spooky_encoder.h`.

To build the tests, run `make test_spooky`.

The API is somewhat influenced by the target device only having 512 bytes of RAM.

# `example/` contains two example projects, for Arduinos

The example projects should use the following radio transmitter and
receiver pair, or something similar:

+ Transmitter: https://www.sparkfun.com/products/10534
+ Receiver: https://www.sparkfun.com/products/10532

## tx: Transmit the state of (up to) four switches.

Connect Arduino pins 8, 9, 10, and 11 to switches, with pulldown
resistors on the Arduino side (so they don't float when the switch is
open), and the other side to VCC.

Connect Arduino pin 12 to a momentary switch (button), with a pulldown
resistor so it doesn't float, and the other side to VCC.

Connect Arduino pin 13 to the radio transmitter data pin. Power/ground the
transmitter as indicated by its data sheet.

When the Arduino is powered and the button is pressed, it will transmit
the current state of the switches to the receiver approximately every 3
seconds.

## rx: Receive the switch state, light up four LEDs when the message arrives.

Connect Arduino pin 8 to the digital output pin on the radio receiver.
Power/ground the receiver as indicated by its data sheet.

Connect Arduino pins 10, 11, 12, and 13 to four LEDs, each of which has
a resistor connecting it to ground.

When the Arduino receives a radio message from the corresponding
transmitter circuit, it will briefly light up the LEDs to match the
closed switches.
