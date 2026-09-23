#pragma once
#include <stdint.h>
#include "sequencer/scale.h"
#include "config.h"

constexpr uint16_t EEPROM_MAGIC   = 0xBEEF;
constexpr uint8_t  EEPROM_VERSION = 9;
constexpr int      EEPROM_ADDRESS = 0;

// One side of a magnet: Layer A plays the normal pole, Layer B the reversed
// one. Both hold the same fields; they are symmetric on purpose, so either
// side can play any voice.
enum class LayerMode : uint8_t {
    On,
    Off,
    SameAsA,   // Layer B only: reversed magnets play exactly like normal ones
};

constexpr uint8_t LAYER_A     = 0;
constexpr uint8_t LAYER_B     = 1;
constexpr uint8_t NUM_LAYERS  = 2;
constexpr uint8_t LAYER_CHANNEL_AUTO = 0;   // follow the voice's channel

struct LayerCfg {
    LayerMode mode;
    uint8_t   voice;          // VoiceId
    uint8_t   channel;        // LAYER_CHANNEL_AUTO, or a MIDI channel 1-16
    int8_t    octaveOffset;   // added to the master octave
    uint8_t   level;          // percent, 0-100
};

struct SavedConfig {
    uint16_t magic;
    uint8_t  version;
    RootNote root;
    Scale    scale;
    uint8_t  octave;
    float    volume;
    float    rpm;
    bool     muted;
    int8_t   sensorShift;   // root note offset across sensors, default 0
    bool     calibrated;      // false until calibration has run
    uint16_t hallThreshold;   // ADC deviation to trigger a note
    uint16_t hallBaseline[NUM_HALL_SENSORS];  // ADC value at rest, per sensor
    float    rpmCorrection;   // actual_rpm / commanded_rpm measured during calibration
    bool     playWelcomeTune;   // play scale preview on boot
    uint8_t  lcdTimeout;        // backlight timeout in seconds; 0=always off, 255=always on
    uint8_t  beatsPerRev;       // beats per platter revolution; BPM = |rpm| * this
    uint8_t  auxFn;             // which parameter the aux knob is bound to
    uint8_t  pitchStepDiv;      // one aux step = 1/this of a semitone
    int8_t   magnetPolarity;    // +1 or -1: which way a real hit deviates
    // Added in version 8. New fields go at the END so an older layout is a
    // prefix of this one and storageLoad() can migrate it instead of wiping
    // calibration.
    uint8_t  voiceV8;           // UNUSED since version 9 (moved into
                                // layer[LAYER_A].voice); kept for the layout
    // Added in version 9.
    LayerCfg layer[NUM_LAYERS];
};

void storageLoad(SavedConfig& cfg);

// Writes everything except the fields the aux knob modulates live, which keep
// the values the menu last committed. Use this for ordinary saves (speed,
// volume, calibration results) -- it is safe to call at any time without
// worrying that in-flight performance modulation will be persisted.
void storageSave(const SavedConfig& cfg);

// The menu's save: adopts the live values of the aux-modulated fields as the
// new committed ones, then writes. Use this ONLY where the user deliberately
// set one of those values from a menu, not from the aux knob.
void storageCommit(const SavedConfig& cfg);

// Discards live aux modulation: copies the committed values of the
// aux-modulated fields back over the live ones. Writes nothing to EEPROM --
// there is nothing to write, since that drift was never saved in the first
// place. Does not touch the pitch offset, which lives in pitch.cpp.
void storageRevertLive(SavedConfig& cfg);

SavedConfig storageDefaults();
