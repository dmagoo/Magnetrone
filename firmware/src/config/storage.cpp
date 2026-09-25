#include "storage.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <stddef.h>
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
// storageCommit() is the menu's version: it first adopts the live value of the
// one field the menu set as the new committed one.
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
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        dst.layer[l].voice = src.layer[l].voice;
        dst.layer[l].shift   = src.layer[l].shift;
        dst.layer[l].lowNote = src.layer[l].lowNote;
    }
}

// Defaults for the fields added in version 10 (Track Shift, Wrap, Low Note),
// on their own so migration can apply them to layers that kept their older
// settings. Layer B's shift and Low Note are bound to A's by default.
static void setV10Defaults(LayerCfg& lc, bool sameAsA) {
    lc.shift          = DEFAULT_TRACK_SHIFT;
    lc.wrap           = DEFAULT_TRACK_WRAP;
    lc.shiftSameAsA   = sameAsA;
    lc.lowNote        = (uint8_t)(DEFAULT_LOW_NOTE_OUTER ? LowNote::Outer : LowNote::Inner);
    lc.lowNoteSameAsA = sameAsA;
}

// Defaults for the fields added in version 11 (bar start).
static void setV11Defaults(SavedConfig& c) {
    c.barPhase      = 0;
    c.barPhaseValid = false;
    c.startCheck    = DEFAULT_START_CHECK;
}

static void setLayerDefaults(SavedConfig& c) {
    c.layer[LAYER_A] = { LayerMode::On,      (uint8_t)VoiceId::Piano,
                         LAYER_CHANNEL_AUTO,  0, 100 };
    // Same as A by default, so the table plays the same whichever way up a
    // magnet sits. The rest applies once B is switched On.
    c.layer[LAYER_B] = { LayerMode::SameAsA, (uint8_t)VoiceId::Bass,
                         LAYER_CHANNEL_AUTO, -1, 100 };
    // B's shift and Low Note follow A's by default even once B is On, so the
    // two layers move together (Piano over Bass) until B is given its own.
    setV10Defaults(c.layer[LAYER_A], false);
    setV10Defaults(c.layer[LAYER_B], true);
}

// Before version 10 there was one master shift, and it ran the other way:
// degree = sensor - shift. Now degree = sensor + shift, so the old value is
// converted to the one that plays the same notes.
static uint8_t migrateShift(int8_t old) {
    int s = constrain((int)old, 0, NUM_HALL_SENSORS - 1);
    return (uint8_t)((NUM_HALL_SENSORS - s) % NUM_HALL_SENSORS);
}

// Version 10 split the Track Shift Fn into Layer A Shift and Layer B Shift and
// added Layer A Low and Layer B Low after them. The old Track Shift index
// becomes Layer A Shift; everything after it moves down the list. The numbers
// are the AuxFn order in menu.cpp.
static uint8_t migrateAuxFn(uint8_t old) {
    const uint8_t OLD_TRACK_SHIFT = 3, OLD_COUNT = 8, INSERTED = 3;
    if (old >= OLD_COUNT) return DEFAULT_AUX_FN;
    return (old > OLD_TRACK_SHIFT) ? (uint8_t)(old + INSERTED) : old;
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
    c.sensorShiftV9   = 0;
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
    setV11Defaults(c);
    return c;
}

void storageLoad(SavedConfig& cfg) {
    EEPROM.get(EEPROM_ADDRESS, cfg);

    // Older layouts are prefixes of this one: each version only appended
    // fields. Keep everything they saved, calibration included, and default
    // only what is new. Version 9 is the exception: LayerCfg grew, so its
    // layer[] is read back with the old stride.
    if (cfg.magic == EEPROM_MAGIC && cfg.version >= 7 && cfg.version <= 9) {
        uint8_t shift = migrateShift(cfg.sensorShiftV9);
        if (cfg.version == 9) {
            LayerCfgV9 old[NUM_LAYERS];
            EEPROM.get(EEPROM_ADDRESS + (int)offsetof(SavedConfig, layer), old);
            for (uint8_t l = 0; l < NUM_LAYERS; l++) {
                cfg.layer[l] = { old[l].mode, old[l].voice, old[l].channel,
                                 old[l].octaveOffset, old[l].level };
            }
            setV10Defaults(cfg.layer[LAYER_A], false);
            setV10Defaults(cfg.layer[LAYER_B], true);
        } else {
            uint8_t oldVoice = (cfg.version == 8) ? cfg.voiceV8 : (uint8_t)VoiceId::Piano;
            setLayerDefaults(cfg);
            if (oldVoice < VOICE_COUNT) cfg.layer[LAYER_A].voice = oldVoice;
        }
        // The old master shift goes into both layers.
        for (uint8_t l = 0; l < NUM_LAYERS; l++) cfg.layer[l].shift = shift;
        cfg.auxFn         = migrateAuxFn(cfg.auxFn);
        cfg.version       = EEPROM_VERSION;
        cfg.voiceV8       = 0;
        cfg.sensorShiftV9 = 0;
        setV11Defaults(cfg);
        committed = cfg;
        storageSave(cfg);
        return;
    }

    // Version 10 is a straight prefix: only the bar start fields are new.
    if (cfg.magic == EEPROM_MAGIC && cfg.version == 10) {
        setV11Defaults(cfg);
        cfg.version = EEPROM_VERSION;
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

void storageCommit(const SavedConfig& cfg, CommitField field, uint8_t layer) {
    // Only the field the menu set deliberately; any other aux drift stays live.
    switch (field) {
    case CommitField::All:     copyLiveModulatedFields(committed, cfg);            break;
    case CommitField::Root:    committed.root   = cfg.root;                        break;
    case CommitField::Scale:   committed.scale  = cfg.scale;                       break;
    case CommitField::Octave:  committed.octave = cfg.octave;                      break;
    case CommitField::Voice:   committed.layer[layer].voice   = cfg.layer[layer].voice;   break;
    case CommitField::Shift:   committed.layer[layer].shift   = cfg.layer[layer].shift;   break;
    case CommitField::LowNote: committed.layer[layer].lowNote = cfg.layer[layer].lowNote; break;
    }
    storageSave(cfg);
}
