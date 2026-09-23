#pragma once
#include <stdint.h>
#include "sequencer/scale.h"
#include "config.h"

constexpr uint16_t EEPROM_MAGIC   = 0xBEEF;
constexpr uint8_t  EEPROM_VERSION = 4;
constexpr int      EEPROM_ADDRESS = 0;

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
    uint16_t hallBaseline;    // ADC value at rest (average across sensors)
    float    rpmCorrection;   // actual_rpm / commanded_rpm measured during calibration
    bool     playWelcomeTune;   // play scale preview on boot
    uint8_t  lcdTimeout;        // backlight timeout in seconds; 0=always off, 255=always on
    uint8_t  beatsPerRev;       // beats per platter revolution; BPM = |rpm| * this
};

void storageLoad(SavedConfig& cfg);
void storageSave(const SavedConfig& cfg);
SavedConfig storageDefaults();
