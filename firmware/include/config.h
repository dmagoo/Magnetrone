#pragma once
#include <stdint.h>
#include "sequencer/scale.h"

// -------------------------------------------------------------------------
// MIDI
// -------------------------------------------------------------------------
constexpr uint8_t   MIDI_CHANNEL         = 1;

// -------------------------------------------------------------------------
// Audio
// -------------------------------------------------------------------------
constexpr float     DEFAULT_VOLUME       = 0.5f;    // 0.0 - 1.0
constexpr bool      DEFAULT_PLAY_WELCOME_TUNE = true;
constexpr uint8_t   LCD_I2C_ADDRESS      = 0x27;

// -------------------------------------------------------------------------
// Sequencer defaults
// -------------------------------------------------------------------------
constexpr RootNote  DEFAULT_ROOT         = RootNote::C;
constexpr Scale     DEFAULT_SCALE        = Scale::Major;
constexpr uint8_t   DEFAULT_OCTAVE       = 4;       // middle C
constexpr uint8_t   NUM_HALL_SENSORS     = 8;
constexpr int8_t    DEFAULT_SENSOR_SHIFT = 0;   // which sensor index plays root note; configurable under Advanced
constexpr uint16_t  NOTE_DURATION_MS     = 250; // static note length (piano tap)

// -------------------------------------------------------------------------
// Hall sensors
// -------------------------------------------------------------------------
// 49E ratiometric sensor, 5V supply, voltage divider R1=1k/R2=2k -> Teensy 3.3V ADC
// Divider ratio: 2/3. Rest voltage: 2.5V * 0.667 = 1.667V -> ADC ~2069 counts.
// Swing: ~0.33V-3.0V after divider -> ADC ~413-3723 counts.
// Default threshold: 300 counts deviation from baseline (~0.22V).
// Replaced by calibration measurement once calibration has run.
constexpr uint16_t  HALL_THRESHOLD_DEFAULT  = 300;
constexpr uint16_t  HALL_HYSTERESIS         = 150;  // must return within this to reset trigger
constexpr uint16_t  HALL_DEBOUNCE_MS        = 20;   // minimum ms between triggers on the same sensor
constexpr uint16_t  HALL_BASELINE_DEFAULT   = 2069; // pre-calibration estimate

// -------------------------------------------------------------------------
// Tempo / BPM
// -------------------------------------------------------------------------
// 1 revolution = 1 bar (4/4). Configurable in Advanced menu (future).
constexpr uint8_t   BEATS_PER_REV        = 4;
// BPM = RPM * BEATS_PER_REV

// -------------------------------------------------------------------------
// Motion
// -------------------------------------------------------------------------
constexpr float     DEFAULT_RPM          = 45.0f;
constexpr float     MIN_RPM              = 10.0f;
constexpr float     MAX_RPM              = 120.0f;

constexpr uint16_t  MOTOR_STEPS_PER_REV  = 200;     // NEMA17 full steps
constexpr uint8_t   MICROSTEP_DIVISOR    = 16;       // A4988 1/16 microstepping
constexpr uint16_t  GEAR_RATIO           = 11;       // 220T driven / 20T drive (GT2)

// Steps per platter revolution
constexpr uint32_t  STEPS_PER_PLATTER_REV =
    (uint32_t)MOTOR_STEPS_PER_REV * MICROSTEP_DIVISOR * GEAR_RATIO;  // 35200

// Step period in microseconds for a given RPM:
//   period_us = 60,000,000 / (RPM * STEPS_PER_PLATTER_REV)
constexpr uint32_t rpmToStepPeriodUs(float rpm) {
    return (uint32_t)(60000000.0f / (rpm * STEPS_PER_PLATTER_REV));
}

// -------------------------------------------------------------------------
// UI
// -------------------------------------------------------------------------
constexpr uint16_t  MENU_TIMEOUT_MS      = 5000;    // return to status screen

// LCD backlight timeout in seconds.
// 0 = always off, 255 = always on, any other value = seconds of inactivity.
constexpr uint8_t   DEFAULT_LCD_TIMEOUT  = 5;

// Sentinel values for LCD timeout setting.
constexpr uint8_t   LCD_TIMEOUT_ALWAYS_OFF = 0;
constexpr uint8_t   LCD_TIMEOUT_ALWAYS_ON  = 255;
