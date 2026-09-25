#pragma once
#include "config/storage.h"

enum class CalibrationStatus : uint8_t {
    Success,
    TimeoutNoMagnet,    // no magnet detected on the outer track within the wait window
    MultipleMagnets,    // a revolution came up short: more than one magnet on the track
    WrongTrack,         // the magnet is on another track, not the outer one
};

// Calibration runs in two phases, with a prompt in between (the menu owns the
// prompts). Both block, drive the LCD via menuMessage(), and leave the platter
// stopped.
//
// Phase 1, platter clear: spins and measures each sensor's resting level.
// Nothing is saved yet.
void calibrationSampleBaselines();

// Phase 2, one magnet on the start mark (outer track): spins and measures the
// threshold, the magnet's pole, the belt ratio and the bar start. On success
// saves everything to cfg and EEPROM; on failure cfg is untouched.
CalibrationStatus calibrationDetect(SavedConfig& cfg);

// Calib. StartPos, Auto: one magnet on the start mark and nothing else on the
// outer track (other tracks may stay full). Spins and sets the bar start from
// that magnet, using the saved calibration. Changes nothing else.
CalibrationStatus calibrationFindStart(const SavedConfig& cfg);
