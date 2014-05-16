spooky: OOK Manchester encoding, decoding, and dynamic clock recovery.
For when you need action at a distance.

This is a library for microcontroller projects (Arduinos, ATtinys, PICs,
etc.) that need to transmit digital data using cheap, short-range radio.
Better radio parts usually do more signal management for you, but tend
to be more expensive per circuit and/or surface-mount only.

If you're making a sensor network for a home automation project (for
example), this will encode and decode the raw radio data with
[Manchester coding] to help distinguish from radio noise, and help the
receiver to automatically detect the signal's baud rate
("[clock recovery]").

[Manchester coding]: http://en.wikipedia.org/wiki/Manchester_code
[clock recovery]: http://en.wikipedia.org/wiki/Clock_recovery

To use the encoder, initialize a `spooky_encoder` struct with a buffer,
then enqueue outgoing messages as needed. Call `spooky_encoder_step` at
a regular interval (use a timer interrupt) and set the data line
connected to the transmitter accordingly.

To use the decoder, initialize a `spooky_decoder` struct with a working
buffer and a 'data received' callback, then check the current state of
the receiver's data line periodically (again, use a timer interrupt) and
pass the low/high state to `spooky_decoder_step`. Oversampling will help
to compensate for small amounts of variability in timing -- several data
points per transition in the signal is best.

For further usage details, see `spooky_decoder.h` and
`spooky_encoder.h`.

To build the tests, run `make test_spooky`.

## `example/` contains two example projects, for Arduinos

The example projects should use amateur-band, ASK/OOK radio transmitters
and receivers such as the following:

+ Transmitter: https://www.sparkfun.com/products/10534
+ Receiver: https://www.sparkfun.com/products/10532
+ Transmitter, receiver pair: http://www.seeedstudio.com/depot/315Mhz-RF-link-kit-p-76.html?cPath=19_22

Check the data sheets for the parts you get, of course.

### tx: Transmit the state of (up to) four switches.

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

### rx: Receive the switch state, light up four LEDs when the message arrives.

Connect Arduino pin 8 to the digital output pin on the radio receiver.
Power/ground the receiver as indicated by its data sheet.

Connect Arduino pins 10, 11, 12, and 13 to four LEDs, each of which has
a resistor connecting it to ground.

When the Arduino receives a radio message from the corresponding
transmitter circuit, it will briefly light up the LEDs to match the
closed switches.
