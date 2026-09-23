#include "storage.h"
#include <EEPROM.h>

// ---------------------------------------------------------------------------
// Committed vs live configuration
//
// The aux knob modulates settings during performance by writing straight into
// the live SavedConfig, which is what the sequencer reads. That drift is
// deliberate, and it must never reach EEPROM: a session should start from what
// was dialled in via the menu, not from wherever the knob happened to be left.
//
// So storage keeps its own copy of what EEPROM holds. An ordinary storageSave()
// -- a speed change, a volume change, calibration results -- writes everything
// EXCEPT the live-modulated fields, which keep their committed values.
// storageCommit() is the menu's version: it adopts the live values as the new
// committed ones first.
//
// The alternative was to have the save routine hunt for exceptions at each call
// site, which hides the asymmetry and silently breaks whenever a new aux target
// is added.
// ---------------------------------------------------------------------------

static SavedConfig committed;

// The fields the aux knob can modulate. THIS IS THE ONE PLACE THAT LIST LIVES:
// adding an aux target in menu.cpp's auxApplyDelta() means adding it here too,
// or that target will silently start persisting.
static void copyLiveModulatedFields(SavedConfig& dst, const SavedConfig& src) {
    dst.root        = src.root;
    dst.scale       = src.scale;
    dst.octave      = src.octave;
    dst.sensorShift = src.sensorShift;
}

SavedConfig storageDefaults() {
    // Written field by field rather than as an aggregate initialiser: one
    // member is now an array, and positional init of a struct this long was
    // already a silent-breakage risk every time a field was added.
    SavedConfig c{};
    c.magic           = EEPROM_MAGIC;
    c.version         = EEPROM_VERSION;
    c.root            = DEFAULT_ROOT;
    c.scale           = DEFAULT_SCALE;
    c.octave          = DEFAULT_OCTAVE;
    c.volume          = DEFAULT_VOLUME;
    c.rpm             = DEFAULT_RPM;
    c.muted           = false;
    c.sensorShift     = DEFAULT_SENSOR_SHIFT;
    c.calibrated      = false;
    c.hallThreshold   = HALL_THRESHOLD_DEFAULT;
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        c.hallBaseline[i] = HALL_BASELINE_DEFAULT;
    }
    c.rpmCorrection   = 1.0f;
    c.playWelcomeTune = DEFAULT_PLAY_WELCOME_TUNE;
    c.lcdTimeout      = DEFAULT_LCD_TIMEOUT;
    c.beatsPerRev     = DEFAULT_BEATS_PER_REV;
    c.auxFn           = DEFAULT_AUX_FN;
    c.pitchStepDiv    = DEFAULT_PITCH_STEP_DIV;
    c.magnetPolarity  = DEFAULT_MAGNET_POLARITY;
    return c;
}

void storageLoad(SavedConfig& cfg) {
    EEPROM.get(EEPROM_ADDRESS, cfg);
    if (cfg.magic != EEPROM_MAGIC || cfg.version != EEPROM_VERSION) {
        cfg = storageDefaults();
        committed = cfg;
        storageSave(cfg);
        return;
    }
    committed = cfg;
}

void storageSave(const SavedConfig& cfg) {
    SavedConfig out = cfg;
    copyLiveModulatedFields(out, committed);   // aux drift stays out of EEPROM
    EEPROM.put(EEPROM_ADDRESS, out);
    committed = out;
}

void storageRevertLive(SavedConfig& cfg) {
    copyLiveModulatedFields(cfg, committed);
}

void storageCommit(const SavedConfig& cfg) {
    copyLiveModulatedFields(committed, cfg);   // the menu set these deliberately
    storageSave(cfg);
}
