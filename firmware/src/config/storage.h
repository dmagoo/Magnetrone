#pragma once
#include <stdint.h>
#include "sequencer/scale.h"
#include "config.h"

constexpr uint16_t EEPROM_MAGIC   = 0xBEEF;
constexpr uint8_t  EEPROM_VERSION = 14;
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

// Which end of the sensor arm plays the low end of the run. Inner: hall 1, the
// inner track, is lowest. Outer: the run is reversed, hall 8 is lowest.
enum class LowNote : uint8_t {
    Inner,
    Outer,
};

struct LayerCfg {
    LayerMode mode;
    uint8_t   voice;          // VoiceId
    uint8_t   channel;        // LAYER_CHANNEL_AUTO, or a MIDI channel 1-16
    int8_t    octaveOffset;   // added to the master octave
    uint8_t   level;          // percent, 0-100
    // Added in version 10. Track Shift: scale degrees added to every sensor's
    // degree, and whether the result wraps around the arm (Wrap) or carries
    // into the next octave (No Wrap). Kit voices always wrap.
    uint8_t   shift;          // 0 to NUM_HALL_SENSORS-1
    bool      wrap;
    bool      shiftSameAsA;   // Layer B only: play A's shift and Wrap
    uint8_t   lowNote;        // LowNote
    bool      lowNoteSameAsA; // Layer B only: play A's Low Note
};

// The version 9 layout of LayerCfg, for migrating it: layer[] is the last
// field, so growing LayerCfg moves layer[LAYER_B] and it has to be read back
// with the old stride.
struct LayerCfgV9 {
    LayerMode mode;
    uint8_t   voice;
    uint8_t   channel;
    int8_t    octaveOffset;
    uint8_t   level;
};

// A scene: everything the Aux knob can change, saved together so a setup can
// be recalled live. Pitch and A/B Balance are otherwise never saved.
constexpr uint8_t NUM_SCENES  = 8;
constexpr uint8_t SCENE_NONE     = 0xFF;   // no scene loaded yet
constexpr uint8_t SCENE_DEFAULTS = 0xFE;   // the read-only factory scene, "0: Defaults"

struct SceneSlot {
    bool     used;
    RootNote root;
    Scale    scale;
    uint8_t  octave;
    uint8_t  voice[NUM_LAYERS];
    uint8_t  shift[NUM_LAYERS];
    uint8_t  lowNote[NUM_LAYERS];
    int8_t   balance;
    float    pitch;             // semitones, as pitchGetOffset()
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
    int8_t   sensorShiftV9;   // UNUSED since version 10 (moved into each
                              // layer as LayerCfg::shift); kept for the layout
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
    // Added in version 11. Where the platter sat in the bar when it last came
    // to rest, in motor steps past the start mark. Valid only if it was saved
    // at rest: it is cleared when the platter starts, so a power cut mid-spin
    // leaves it invalid rather than wrong.
    int32_t  barPhase;
    bool     barPhaseValid;
    bool     startCheck;       // at boot, offer Find Start if the start is unknown
    // Added in version 12.
    SceneSlot scenes[NUM_SCENES];
    uint8_t   currentScene;    // last loaded or saved, or SCENE_NONE
    // Added in version 13. MIDI in. Kept out of SceneSlot and LayerCfg so
    // neither changes size.
    uint8_t   midiInChannel[NUM_LAYERS];  // 1-16, 0 = off
    uint8_t   midiFn;                     // what incoming keys do (MidiFn)
    uint16_t  sceneLearned[NUM_SCENES];   // each scene's Learned scale mask
    // Added in version 14: each scene's per-layer octave offset, now an Aux Fn.
    int8_t    sceneLayerOctave[NUM_SCENES][NUM_LAYERS];
};

void storageLoad(SavedConfig& cfg);

// Writes everything except the fields the aux knob modulates live, which keep
// the values the menu last committed. Use this for ordinary saves (speed,
// volume, calibration results) -- it is safe to call at any time without
// worrying that in-flight performance modulation will be persisted.
void storageSave(const SavedConfig& cfg);

// Which aux-modulated field storageCommit() adopts. All is for Reset Settings,
// Factory Reset and scene loads and saves, where every value was set deliberately.
enum class CommitField : uint8_t { All, Root, Scale, Octave, Voice, Shift, LowNote,
                                   LayerOctave };

// The menu's save: adopts the live value of ONE aux-modulated field (for the
// per-layer fields, on the given layer) as the new committed one, then writes.
// Other aux drift in play stays out of EEPROM. Use this ONLY where the user
// deliberately set that value from a menu, not from the aux knob.
void storageCommit(const SavedConfig& cfg, CommitField field, uint8_t layer = 0);

// Discards live aux modulation: copies the committed values of the
// aux-modulated fields back over the live ones. Writes nothing to EEPROM --
// there is nothing to write, since that drift was never saved in the first
// place. Does not touch the pitch offset, which lives in pitch.cpp.
void storageRevertLive(SavedConfig& cfg);

SavedConfig storageDefaults();
