#include "storage.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <stddef.h>
#include "audio/voice.h"

// ---------------------------------------------------------------------------
// Saved vs live sound
//
// The table always plays cfg.layer[], the live sound. The Aux knob and MIDI in
// write straight into it. What survives a restart is the scenes: at power-up
// the current scene is loaded over the live sound, so Aux tweaks are gone and
// the sound is exactly what the scene holds.
//
// So there is nothing to keep out of EEPROM: storageSave() writes everything,
// the live sound included, and storageLoad() simply never trusts it.
// ---------------------------------------------------------------------------

// The version 7 to 15 layout, up to the bar start, for handing calibration
// and the bar start over to this one. Version 16 moved everything else, so
// the rest of an old config is not kept.
struct LayerCfgV15 {
    uint8_t mode, voice, channel;
    int8_t  octaveOffset;
    uint8_t level, shift;
    bool    wrap, shiftSameAsA;
    uint8_t lowNote;
    bool    lowNoteSameAsA;
};

struct SavedConfigV15 {
    uint16_t    magic;
    uint8_t     version;
    uint8_t     root, scale, octave;
    float       volume, rpm;
    bool        muted;
    int8_t      sensorShiftV9;
    bool        calibrated;
    uint16_t    hallThreshold;
    uint16_t    hallBaseline[NUM_HALL_SENSORS];
    float       rpmCorrection;
    bool        playWelcomeTune;
    uint8_t     lcdTimeout, beatsPerRev, auxFn, pitchStepDiv;
    int8_t      magnetPolarity;
    uint8_t     voiceV8;
    LayerCfgV15 layer[NUM_LAYERS];   // version 11 onwards: the bar start follows
    int32_t     barPhase;
    bool        barPhaseValid;
};

static_assert(sizeof(SavedConfig) <= E2END + 1, "SavedConfig does not fit the EEPROM");
static_assert(NUM_SAVED_VOICES == NUM_CUSTOM_VOICES, "custom voice counts differ");

Scene storageFactoryScene() {
    Scene s{};
    LayerCfg& a = s.layer[LAYER_A];
    a.mode           = LayerMode::On;
    a.voice          = (uint8_t)VoiceId::Piano;
    a.channel        = LAYER_CHANNEL_AUTO;
    a.root           = DEFAULT_ROOT;
    a.scale          = DEFAULT_SCALE;
    a.learned        = 0;
    a.octave         = DEFAULT_OCTAVE;
    a.level          = 100;
    a.shift          = DEFAULT_TRACK_SHIFT;
    a.wrap           = DEFAULT_TRACK_WRAP;
    a.shiftSameAsA   = false;
    a.lowNote        = (uint8_t)(DEFAULT_LOW_NOTE_OUTER ? LowNote::Outer : LowNote::Inner);
    a.lowNoteSameAsA = false;

    // Layer B plays its own voice out of the box, drums, with nothing bound
    // to A: undoing Same as A by hand everywhere was clunky.
    LayerCfg& b = s.layer[LAYER_B];
    b        = a;
    b.voice  = (uint8_t)VoiceId::Drums;
    b.octave = DEFAULT_OCTAVE_B;

    s.balance = 0;
    s.pitch   = 0.0f;
    return s;
}

SavedConfig storageDefaults() {
    // Written field by field rather than as an aggregate initialiser: positional
    // init of a struct this long is a silent-breakage risk every time a field
    // is added.
    SavedConfig c{};
    c.magic           = EEPROM_MAGIC;
    c.version         = EEPROM_VERSION;
    c.calibrated      = false;
    c.hallThreshold   = HALL_THRESHOLD_DEFAULT;
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        c.hallBaseline[i] = HALL_BASELINE_DEFAULT;
    }
    c.rpmCorrection   = 1.0f;
    c.magnetPolarity  = DEFAULT_MAGNET_POLARITY;
    c.barPhase        = 0;
    c.barPhaseValid   = false;
    c.volume          = DEFAULT_VOLUME;
    c.rpm             = DEFAULT_RPM;
    c.muted           = false;
    c.playWelcomeTune = DEFAULT_PLAY_WELCOME_TUNE;
    c.lcdTimeout      = DEFAULT_LCD_TIMEOUT;
    c.menuTimeout     = DEFAULT_MENU_TIMEOUT;
    c.startCheck      = DEFAULT_START_CHECK;
    c.beatsPerRev     = DEFAULT_BEATS_PER_REV;
    c.auxFn           = DEFAULT_AUX_FN;
    c.pitchStepDiv    = DEFAULT_PITCH_STEP_DIV;
    c.midiFn          = DEFAULT_MIDI_FN;
    c.midiInChannel[LAYER_A] = DEFAULT_MIDI_IN_CHANNEL_A;
    c.midiInChannel[LAYER_B] = DEFAULT_MIDI_IN_CHANNEL_B;

    Scene f = storageFactoryScene();
    for (uint8_t i = 0; i < NUM_SCENES; i++) {
        c.scenes[i]    = f;
        c.sceneUsed[i] = (i == SCENE_DEFAULTS);
    }
    c.currentScene = SCENE_DEFAULTS;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) c.layer[l] = f.layer[l];
    return c;
}

void storageLoad(SavedConfig& cfg) {
    EEPROM.get(EEPROM_ADDRESS, cfg);
    // Version 16 is a prefix of this one: only the saved voices are new.
    if (cfg.magic == EEPROM_MAGIC && cfg.version == 16) {
        for (uint8_t i = 0; i < NUM_SAVED_VOICES; i++) cfg.customVoices[i] = VoiceSlot{};
        for (uint8_t i = 0; i < NUM_SCENES; i++)
            for (uint8_t l = 0; l < NUM_LAYERS; l++) cfg.sceneVoices[i][l] = VoiceSlot{};
        cfg.version = EEPROM_VERSION;
        storageSave(cfg);
    }
    if (cfg.magic == EEPROM_MAGIC && cfg.version == EEPROM_VERSION) {
        if (cfg.currentScene >= NUM_SCENES || !cfg.sceneUsed[cfg.currentScene]) {
            cfg.currentScene = SCENE_DEFAULTS;
        }
        cfg.sceneUsed[SCENE_DEFAULTS] = true;
        return;
    }

    // Anything else starts from factory. An older layout keeps its
    // calibration (from version 7) and bar start (from version 11), so the
    // table does not have to be calibrated again.
    SavedConfigV15 old;
    EEPROM.get(EEPROM_ADDRESS, old);
    SavedConfig d = storageDefaults();
    if (old.magic == EEPROM_MAGIC && old.version >= 7 && old.version <= 15) {
        d.calibrated     = old.calibrated;
        d.hallThreshold  = old.hallThreshold;
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) d.hallBaseline[i] = old.hallBaseline[i];
        d.rpmCorrection  = old.rpmCorrection;
        d.magnetPolarity = old.magnetPolarity;
        if (old.version >= 11) {
            d.barPhase      = old.barPhase;
            d.barPhaseValid = old.barPhaseValid;
        }
    }
    cfg = d;
    storageSave(cfg);
}

void storageSave(const SavedConfig& cfg) {
    EEPROM.put(EEPROM_ADDRESS, cfg);
}
