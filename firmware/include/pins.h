#pragma once
// Pin assignments are defined and documented in docs/pins.md -- that is the source of truth.
// Edit pins.md first, then update the constants below to match.
// All pin numbers are GPIO numbers (Teensy 4.1 Arduino API).

// MIDI
constexpr int PIN_MIDI_RX       = 0;
constexpr int PIN_MIDI_TX       = 1;

// Stepper driver (A4988)
constexpr int PIN_DIR           = 2;
constexpr int PIN_STEP          = 3;
constexpr int PIN_SLEEP         = 4;    // stepper sleep - not yet implemented
constexpr int PIN_ENABLE        = 5;

// Menu encoder
constexpr int PIN_MENU_A        = 6;
constexpr int PIN_MENU_B        = 9;
constexpr int PIN_MENU_BTN      = 10;

// Speed encoder
constexpr int PIN_SPEED_A       = 24;
constexpr int PIN_SPEED_B       = 25;
constexpr int PIN_SPEED_BTN     = 26;  // press = stop/resume

// Volume encoder
constexpr int PIN_VOL_A         = 27;
constexpr int PIN_VOL_B         = 28;
constexpr int PIN_VOL_BTN       = 29;  // press = mute/unmute

// Aux encoder 1 (reserved)
constexpr int PIN_AUX_ENC_A     = 30;
constexpr int PIN_AUX_ENC_B     = 31;
constexpr int PIN_AUX_ENC_BTN   = 32;

// Aux encoder 2 (reserved)
constexpr int PIN_AUX_ENC2_A    = 33;
constexpr int PIN_AUX_ENC2_B    = 34;
constexpr int PIN_AUX_ENC2_BTN  = 35;

// Aux buttons (reserved)
constexpr int PIN_AUX_BTN_1     = 36;
constexpr int PIN_AUX_BTN_2     = 37;

// Hall effect sensors
constexpr int PIN_HALL_1        = 41;
constexpr int PIN_HALL_2        = 40;
constexpr int PIN_HALL_3        = 39;
constexpr int PIN_HALL_4        = 38;
constexpr int PIN_HALL_5        = 17;
constexpr int PIN_HALL_6        = 16;
constexpr int PIN_HALL_7        = 15;
constexpr int PIN_HALL_8        = 14;

// I2C - shared bus (LCD + audio shield SGTL5000)
constexpr int PIN_SDA           = 18;
constexpr int PIN_SCL           = 19;

// Audio shield I2S - managed by Teensy Audio Library, defined here for reference only.
// T4.1: MCLK=23, BCLK=21, LRCLK=20, DOUT=7, DIN(rx)=8
// Pins 7, 8, 20, 21, 23 are driven by the audio shield - do not connect or use.

// SD card SPI (reserved, not implemented)
// Pin 12: SPI MISO - do not use if external SD fitted
