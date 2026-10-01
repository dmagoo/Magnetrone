#include "storage.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <stddef.h>
#include <string.h>
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
static_assert(NUM_SLOT_HARMONICS == NUM_HARMONICS, "harmonic counts differ");

LayerFx storageFactoryFx() {
    LayerFx f{};
    f.cutoff        = FX_CUTOFF_OFF;
    f.resonance     = 0;
    f.chorusRate    = 30;
    f.chorusDepth   = 50;
    f.chorusMix     = 0;
    f.delayMode     = (uint8_t)DelayMode::Sync;
    f.delaySync     = 1;     // 1/2 beat
    f.delayMs       = 300;
    f.delayFeedback = 30;
    f.delayMix      = 0;
    f.roomSize      = 50;
    f.damping       = 50;
    f.reverbMix     = 0;
    f.sameAsA       = 0;
    return f;
}

// The version 16 to 19 layout: no effects in the layers, no filter in the
// saved voices, no MIDI CC. Kept whole, since the effects moved everything
// after the live layers.
struct LayerCfgV19 {
    LayerMode mode;
    uint8_t   voice, channel;
    RootNote  root;
    Scale     scale;
    uint16_t  learned;
    uint8_t   octave, level, shift;
    bool      wrap, shiftSameAsA;
    uint8_t   lowNote;
    bool      lowNoteSameAsA;
};
struct SceneV19 {
    LayerCfgV19 layer[NUM_LAYERS];
    int8_t      balance;
    float       pitch;
};
struct VoiceSlotV19 {
    bool     used;
    uint8_t  base, wave, sustainPct;
    uint16_t attackMs, decayMs, releaseMs, noteMs;
    uint8_t  harmonics[NUM_SLOT_HARMONICS];   // version 18 on
};
struct SavedConfigV19 {
    uint16_t magic;
    uint8_t  version;
    bool     calibrated;
    uint16_t hallThreshold;
    uint16_t hallBaseline[NUM_HALL_SENSORS];
    float    rpmCorrection;
    int8_t   magnetPolarity;
    int32_t  barPhase;
    bool     barPhaseValid;
    float    volume, rpm;
    bool     muted;
    bool     playWelcomeTune;
    uint8_t  lcdTimeout, menuTimeout;
    bool     startCheck;
    uint8_t  beatsPerRev, auxFn, pitchStepDiv, midiFn;
    uint8_t  midiInChannel[NUM_LAYERS];
    LayerCfgV19  layer[NUM_LAYERS];
    SceneV19     scenes[NUM_SCENES];
    bool         sceneUsed[NUM_SCENES];
    uint8_t      currentScene;
    VoiceSlotV19 customVoices[NUM_SAVED_VOICES];          // version 17 on
    VoiceSlotV19 sceneVoices[NUM_SCENES][NUM_LAYERS];
    uint32_t     frontPhase;                              // version 19
    bool         frontKnown;
};

static_assert(offsetof(SavedConfigV19, layer) == offsetof(SavedConfig, layer),
              "the version 20 header no longer matches version 19");

// The version 17 saved voice, before the harmonics. The saved voices came
// last in versions 17 and 18, so version 17's sit where version 18's do.
struct VoiceSlotV17 {
    bool     used;
    uint8_t  base, wave, sustainPct;
    uint16_t attackMs, decayMs, releaseMs, noteMs;
};
struct SavedVoicesV17 {
    VoiceSlotV17 custom[NUM_SAVED_VOICES];
    VoiceSlotV17 scene[NUM_SCENES][NUM_LAYERS];
};

static VoiceSlotV19 fromV17(const VoiceSlotV17& o) {
    VoiceSlotV19 s{};
    s.used       = o.used;
    s.base       = o.base;
    s.wave       = o.wave;
    s.sustainPct = o.sustainPct;
    s.attackMs   = o.attackMs;
    s.decayMs    = o.decayMs;
    s.releaseMs  = o.releaseMs;
    s.noteMs     = o.noteMs;
    return s;
}

// An old saved voice gets the filter its built-in has: Off.
static VoiceSlot fromV19(const VoiceSlotV19& o) {
    VoiceSlot s{};
    s.used       = o.used;
    s.base       = o.base;
    s.wave       = o.wave;
    s.sustainPct = o.sustainPct;
    s.attackMs   = o.attackMs;
    s.decayMs    = o.decayMs;
    s.releaseMs  = o.releaseMs;
    s.noteMs     = o.noteMs;
    memcpy(s.harmonics, o.harmonics, NUM_SLOT_HARMONICS);
    s.filter     = voiceGet(o.base).filter;
    return s;
}

// An old layer gets the factory effects: all Off.
static LayerCfg fromV19(const LayerCfgV19& o) {
    LayerCfg l{};
    l.mode           = o.mode;
    l.voice          = o.voice;
    l.channel        = o.channel;
    l.root           = o.root;
    l.scale          = o.scale;
    l.learned        = o.learned;
    l.octave         = o.octave;
    l.level          = o.level;
    l.shift          = o.shift;
    l.wrap           = o.wrap;
    l.shiftSameAsA   = o.shiftSameAsA;
    l.lowNote        = o.lowNote;
    l.lowNoteSameAsA = o.lowNoteSameAsA;
    l.fx             = storageFactoryFx();
    return l;
}

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
    a.fx             = storageFactoryFx();

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
        c.hallNoise[i]    = HALL_NOISE_DEFAULT;
    }
    c.rpmCorrection   = 1.0f;
    c.magnetPolarity  = DEFAULT_MAGNET_POLARITY;
    c.barPhase        = 0;
    c.barPhaseValid   = false;
    c.frontPhase      = 0;
    c.frontKnown      = false;
    c.volume         = DEFAULT_VOLUME;
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
    c.midiCc          = DEFAULT_MIDI_CC;

    Scene f = storageFactoryScene();
    for (uint8_t i = 0; i < NUM_SCENES; i++) {
        c.scenes[i]    = f;
        c.sceneUsed[i] = (i == SCENE_DEFAULTS);
    }
    c.currentScene = SCENE_DEFAULTS;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) c.layer[l] = f.layer[l];
    return c;
}

// Versions 16 to 19 keep everything. Version 16 has no saved voices,
// version 17's lack the harmonics, 18 has no Front, and none of them has the
// effects, the voice filter or MIDI CC.
static void fromV16to19(SavedConfig& cfg) {
    SavedConfigV19 old;
    EEPROM.get(EEPROM_ADDRESS, old);
    if (old.version <= 17) {
        SavedVoicesV17 v{};
        if (old.version == 17) EEPROM.get(EEPROM_ADDRESS + offsetof(SavedConfigV19, customVoices), v);
        for (uint8_t i = 0; i < NUM_SAVED_VOICES; i++) old.customVoices[i] = fromV17(v.custom[i]);
        for (uint8_t i = 0; i < NUM_SCENES; i++)
            for (uint8_t l = 0; l < NUM_LAYERS; l++) old.sceneVoices[i][l] = fromV17(v.scene[i][l]);
    }
    if (old.version <= 18) {
        old.frontPhase = 0;
        old.frontKnown = false;
    }

    SavedConfig c = storageDefaults();
    c.calibrated      = old.calibrated;
    c.hallThreshold   = old.hallThreshold;
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) c.hallBaseline[i] = old.hallBaseline[i];
    c.rpmCorrection   = old.rpmCorrection;
    c.magnetPolarity  = old.magnetPolarity;
    c.barPhase        = old.barPhase;
    c.barPhaseValid   = old.barPhaseValid;
    c.volume          = old.volume;
    c.rpm             = old.rpm;
    c.muted           = old.muted;
    c.playWelcomeTune = old.playWelcomeTune;
    c.lcdTimeout      = old.lcdTimeout;
    c.menuTimeout     = old.menuTimeout;
    c.startCheck      = old.startCheck;
    c.beatsPerRev     = old.beatsPerRev;
    c.auxFn           = old.auxFn;
    c.pitchStepDiv    = old.pitchStepDiv;
    c.midiFn          = old.midiFn;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        c.midiInChannel[l] = old.midiInChannel[l];
        c.layer[l]         = fromV19(old.layer[l]);
    }
    for (uint8_t i = 0; i < NUM_SCENES; i++) {
        for (uint8_t l = 0; l < NUM_LAYERS; l++) {
            c.scenes[i].layer[l] = fromV19(old.scenes[i].layer[l]);
            c.sceneVoices[i][l]  = fromV19(old.sceneVoices[i][l]);
        }
        c.scenes[i].balance = old.scenes[i].balance;
        c.scenes[i].pitch   = old.scenes[i].pitch;
        c.sceneUsed[i]      = old.sceneUsed[i];
    }
    c.currentScene = old.currentScene;
    for (uint8_t i = 0; i < NUM_SAVED_VOICES; i++) c.customVoices[i] = fromV19(old.customVoices[i]);
    c.frontPhase = old.frontPhase;
    c.frontKnown = old.frontKnown;
    cfg = c;
    storageSave(cfg);
}

void storageLoad(SavedConfig& cfg) {
    EEPROM.get(EEPROM_ADDRESS, cfg);
    if (cfg.magic == EEPROM_MAGIC && cfg.version >= 16 && cfg.version <= 19) {
        fromV16to19(cfg);
    }
    // Version 20 is this layout without the noise at the end.
    if (cfg.magic == EEPROM_MAGIC && cfg.version == 20) {
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) cfg.hallNoise[i] = HALL_NOISE_DEFAULT;
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
