#pragma once
#include <stdint.h>
#include "sequencer/scale.h"

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

// -------------------------------------------------------------------------
// Aux function knob
// -------------------------------------------------------------------------
// Which parameter the aux knob modulates. Index into the Fn list in menu.cpp.
// This binding is saved; the VALUES it modulates are live performance state and
// deliberately are not.
constexpr uint8_t   DEFAULT_AUX_FN       = 0;   // Octave

// Pitch step size, stored as a divisor of one semitone: 1 = a half step,
// 2 = a quarter tone, and so on. Anything above 1 is microtonal -- exact on the
// internal synth, and carried over MIDI as a note number plus pitch bend.
constexpr uint8_t   DEFAULT_PITCH_STEP_DIV = 1;

// -------------------------------------------------------------------------
// Hall sensors
// -------------------------------------------------------------------------
constexpr uint8_t   HALL_ADC_BITS           = 12;   // ADC resolution for hall sensors
// A1301 ratiometric sensor, 5V supply, voltage divider R1=3.3k/R2=6.8k -> Teensy 3.3V ADC
// Divider ratio: 6.8/10.1 = 0.673. Rest voltage: 2.5V * 0.673 = 1.68V -> ADC ~980 counts.
// Typical swing with magnet at operating distance: ~760-1200 counts.
// Default threshold: 200 counts deviation from baseline.
// Replaced by calibration measurement once calibration has run.
constexpr uint16_t  HALL_THRESHOLD_DEFAULT  = 200;

// Re-arm level: deviation must fall back BELOW this before a sensor that has
// fired is allowed to fire again. This is not simple hysteresis on the
// threshold -- it has to be close to baseline, because one magnet pass is not
// one clean bump. A disc magnet's return flux has the opposite sign to its face
// field, and hallUpdate() compares ABSOLUTE deviation, so a single pass can
// cross the threshold two or three times (fringe, face, fringe) and produce the
// double notes in todo.md. Requiring a return to near-baseline means the dip
// between those lobes no longer re-arms the sensor.
//
// It replaces the old HALL_HYSTERESIS, which was subtracted from the threshold
// and went negative for any calibrated threshold below 100 -- that latched the
// sensor on permanently (flagged in the 2026-06-10 audit).
constexpr uint16_t  HALL_REARM_LEVEL        = 40;

// Minimum ms between triggers on the same sensor. Long enough to cover one
// magnet pass, far shorter than the gap between legitimate triggers: even at
// MAX_RPM with four magnets on a track those are ~125 ms apart.
constexpr uint16_t  HALL_DEBOUNCE_MS        = 80;
// Pre-calibration resting value, applied to every sensor until calibration
// measures them individually. The old 980 came from a divider that was never
// built (3.3k/6.8k); the assembled boards use 7.5k/15k and actually rest
// between 1947 and 2098, so 980 sat ~1070 counts below every reading and made
// all eight look permanently triggered on a fresh EEPROM.
//
// This is only ever a starting point. The eight rest levels differ by enough
// that one shared number cannot serve them -- see hallSetCalibration().
constexpr uint16_t  HALL_BASELINE_DEFAULT   = 2050;

// Which way a passing magnet pushes the sensor output: +1 or -1. Only that
// direction fires a note, which is what keeps a magnet's opposite-signed fringe
// lobes from each firing one of their own. Calibration measures this from the
// sign of the peak it sees; the menu can override it.
constexpr int8_t    DEFAULT_MAGNET_POLARITY = 1;

// -------------------------------------------------------------------------
// Tempo / BPM
// -------------------------------------------------------------------------
// 1 revolution = 1 bar (4/4) by default. Now configurable from the menu and
// stored per-config as cfg.beatsPerRev; this is only the factory default.
constexpr uint8_t   DEFAULT_BEATS_PER_REV = 4;
// BPM = |RPM| * cfg.beatsPerRev

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
