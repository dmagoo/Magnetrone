#pragma once
// All pin numbers are GPIO numbers (Teensy 4.1 Arduino API).

// MIDI
constexpr int PIN_MIDI_RX       = 0;
constexpr int PIN_MIDI_TX       = 1;

// Stepper driver (TMC2209)
constexpr int PIN_DIR           = 2;
constexpr int PIN_DIR_TMP       = 6;    // used for testing as there was a broken teensy pin
constexpr int PIN_STEP          = 3;
constexpr int PIN_ENABLE        = 5;    // stepper_enable

// TMC2209 UART (Serial8)
constexpr int PIN_STEPPER_RX   = 34;
constexpr int PIN_STEPPER_TX   = 35;



// Menu encoder
constexpr int PIN_MENU_A        = 6;
constexpr int PIN_MENU_B        = 9;
constexpr int PIN_MENU_BTN      = 10;

// Speed encoder
constexpr int PIN_SPEED_A       = 24;
constexpr int PIN_SPEED_B       = 25;
constexpr int PIN_SPEED_BTN     = 37;  // press = stop/resume

// Volume encoderG
constexpr int PIN_VOL_A         = 29;
constexpr int PIN_VOL_B         = 27;
constexpr int PIN_VOL_BTN       = 28;  // press = mute/unmute

// Aux encoder 1 (reserved)
constexpr int PIN_AUX_ENC_A     = 30;
constexpr int PIN_AUX_ENC_B     = 31;
constexpr int PIN_AUX_ENC_BTN   = 32;

// Aux button (reserved)
constexpr int PIN_AUX_BTN_1     = 36;

// Hall effect sensors
constexpr int PIN_HALL_1        = 17;
constexpr int PIN_HALL_2        = 14;
constexpr int PIN_HALL_3        = 39;
constexpr int PIN_HALL_4        = 38;
constexpr int PIN_HALL_5        = 40;
constexpr int PIN_HALL_6        = 41;
constexpr int PIN_HALL_7        = 16;
constexpr int PIN_HALL_8        = 26;

// I2C - shared bus (LCD + audio shield SGTL5000)
constexpr int PIN_SDA           = 18;
constexpr int PIN_SCL           = 19;

// Audio shield I2S - managed by Teensy Audio Library, defined here for reference only.
// T4.1: MCLK=23, BCLK=21, LRCLK=20, DOUT=7, DIN(rx)=8
// Pins 7, 8, 20, 21, 23 are driven by the audio shield - do not connect or use.

// SD card SPI (reserved, not implemented)
// Pin 12: SPI MISO - do not use if external SD fitted
