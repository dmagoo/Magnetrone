#pragma once
#include <stdint.h>
#include "sequencer/scale.h"
#include "config.h"

constexpr uint16_t EEPROM_MAGIC   = 0xBEEF;
constexpr uint8_t  EEPROM_VERSION = 19;
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

// Everything one layer plays with. Scenes hold two of these.
struct LayerCfg {
    LayerMode mode;
    uint8_t   voice;          // VoiceId
    uint8_t   channel;        // LAYER_CHANNEL_AUTO, or a MIDI channel 1-16
    RootNote  root;
    Scale     scale;
    uint16_t  learned;        // the Learned scale's mask, 0 if none (scale.h)
    uint8_t   octave;         // 0-7
    uint8_t   level;          // percent, 0-100
    // Track Shift: scale degrees added to every sensor's degree, and whether
    // the result wraps around the arm (Wrap) or carries into the next octave
    // (No Wrap). Kit voices always wrap.
    uint8_t   shift;          // 0 to NUM_HALL_SENSORS-1
    bool      wrap;
    bool      shiftSameAsA;   // Layer B only: play A's shift and Wrap
    uint8_t   lowNote;        // LowNote
    bool      lowNoteSameAsA; // Layer B only: play A's Low Note
};

// A scene: the whole sound of the table, both layers plus the Pitch offset
// and A/B Balance. Scene 0 is the Defaults scene, the one the Sound Defaults
// menu edits; 1 to 8 change only through Save Scene.
struct Scene {
    LayerCfg layer[NUM_LAYERS];
    int8_t   balance;
    float    pitch;             // semitones, as pitchGetOffset()
};

constexpr uint8_t SCENE_DEFAULTS = 0;
constexpr uint8_t NUM_SCENES     = 9;   // Defaults + 8 saved

constexpr uint8_t NUM_SLOT_HARMONICS = 16;   // = NUM_HARMONICS in voice.h
// Set in VoiceSlot::wave when the voice plays its edited harmonics rather
// than the stock wave. An early version 18 build had a "Harmonic" wave (4)
// instead; a slot holding it reads as edited Sine.
constexpr uint8_t SLOT_HARMONICS_EDITED = 0x80;

// A saved voice: what Voice Edit changes, plus the built-in voice it was made
// from, which gives the rest (Auto MIDI channel, note source).
struct VoiceSlot {
    bool     used;
    uint8_t  base;         // VoiceId
    uint8_t  wave;         // Wave, plus SLOT_HARMONICS_EDITED
    uint8_t  sustainPct;   // 0-100
    uint16_t attackMs;
    uint16_t decayMs;
    uint16_t releaseMs;
    uint16_t noteMs;
    uint8_t  harmonics[NUM_SLOT_HARMONICS];   // percent, for Wave::Harmonic (added in version 18)
};
constexpr uint8_t NUM_SAVED_VOICES = 8;   // = NUM_CUSTOM_VOICES in voice.h

struct SavedConfig {
    uint16_t magic;
    uint8_t  version;

    // Calibration and the bar start. Kept first so a later layout can hand
    // them over even when everything else is wiped.
    bool     calibrated;      // false until calibration has run
    uint16_t hallThreshold;   // ADC deviation to trigger a note
    uint16_t hallBaseline[NUM_HALL_SENSORS];  // ADC value at rest, per sensor
    float    rpmCorrection;   // actual_rpm / commanded_rpm measured during calibration
    int8_t   magnetPolarity;  // +1 or -1: which way a real hit deviates
    // Where the platter sat in the bar when it last came to rest, in motor
    // steps past the start mark. Valid only if it was saved at rest: it is
    // cleared when the platter starts, so a power cut mid-spin leaves it
    // invalid rather than wrong.
    int32_t  barPhase;
    bool     barPhaseValid;

    // The knobs.
    float    volume;
    float    rpm;
    bool     muted;

    // System.
    bool     playWelcomeTune;   // play scale preview on boot
    uint8_t  lcdTimeout;        // backlight timeout in seconds; 0=always off, 255=always on
    uint8_t  menuTimeout;       // seconds; MENU_TIMEOUT_NEVER = never
    bool     startCheck;        // at boot, offer Find Start if the start is unknown

    // Play Setup.
    uint8_t  beatsPerRev;       // beats per platter revolution; BPM = |rpm| * this
    uint8_t  auxFn;             // which parameter the aux knob is bound to
    uint8_t  pitchStepDiv;      // one aux step = 1/this of a semitone
    uint8_t  midiFn;            // what incoming keys do (MidiFn)
    uint8_t  midiInChannel[NUM_LAYERS];  // 1-16, 0 = off

    // The live sound: what the table plays right now, the current scene plus
    // any Aux tweaks. It is written along with everything else but never read
    // back: at power-up the current scene is loaded over it, so Aux tweaks
    // are gone after a restart.
    LayerCfg layer[NUM_LAYERS];

    Scene    scenes[NUM_SCENES];
    bool     sceneUsed[NUM_SCENES];   // Defaults is always used
    uint8_t  currentScene;            // last loaded or saved

    // Added in version 17. Custom 1-8, shared by every scene, and each
    // scene's own voice per layer (VOICE_SCENE). Scene 0's are never used.
    VoiceSlot customVoices[NUM_SAVED_VOICES];
    VoiceSlot sceneVoices[NUM_SCENES][NUM_LAYERS];

    // Added in version 19. Front, where the player sits: the bar phase (steps
    // past the start mark) at which the mark is in front of them. Set at the
    // end of Calib. StartPos. It is where the player sits relative to the arm,
    // so it does not depend on the start mark and Reset Cal keeps it.
    uint32_t frontPhase;
    bool     frontKnown;
};

void storageLoad(SavedConfig& cfg);

// Writes the whole config to EEPROM. Only changed bytes are written.
void storageSave(const SavedConfig& cfg);

// Factory values, with only the Defaults scene in use.
SavedConfig storageDefaults();

// The factory sound: what the Defaults scene holds until the menu changes it.
Scene storageFactoryScene();
