#include "storage.h"
#include <EEPROM.h>
#include "audio/voice.h"

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
    for (uint8_t l = 0; l < NUM_LAYERS; l++) dst.layer[l].voice = src.layer[l].voice;
}

static void setLayerDefaults(SavedConfig& c) {
    c.layer[LAYER_A] = { LayerMode::On,      (uint8_t)VoiceId::Piano,
                         LAYER_CHANNEL_AUTO,  0, 100 };
    // Same as A by default, so the table plays the same whichever way up a
    // magnet sits. The rest applies once B is switched On.
    c.layer[LAYER_B] = { LayerMode::SameAsA, (uint8_t)VoiceId::Bass,
                         LAYER_CHANNEL_AUTO, -1, 100 };
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
    c.voiceV8         = 0;
    setLayerDefaults(c);
    return c;
}

void storageLoad(SavedConfig& cfg) {
    EEPROM.get(EEPROM_ADDRESS, cfg);

    // Older layouts are prefixes of this one: each version only appended
    // fields. Keep everything they saved, calibration included, and default
    // only what is new.
    if (cfg.magic == EEPROM_MAGIC && (cfg.version == 7 || cfg.version == 8)) {
        uint8_t oldVoice = (cfg.version == 8) ? cfg.voiceV8 : (uint8_t)VoiceId::Piano;
        cfg.version = EEPROM_VERSION;
        cfg.voiceV8 = 0;
        setLayerDefaults(cfg);
        if (oldVoice < VOICE_COUNT) cfg.layer[LAYER_A].voice = oldVoice;
        committed = cfg;
        storageSave(cfg);
        return;
    }

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
