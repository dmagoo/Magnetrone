#pragma once
#include "config/storage.h"

enum class CalibrationStatus : uint8_t {
    Success,
    TimeoutNoMagnet,    // no magnet detected within the wait window
    MultipleMagnets,    // measured RPM suggests more than one magnet
    Aborted             // user pressed Back
};

// Runs full calibration sequence, updates cfg in place, saves to EEPROM on success.
// Blocks until complete. LCD is updated throughout via menuMessage().
CalibrationStatus calibrationRun(SavedConfig& cfg);
