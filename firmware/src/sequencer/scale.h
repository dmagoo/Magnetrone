#pragma once
#include <stdint.h>

enum class RootNote : uint8_t {
    C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B
};

enum class Scale : uint8_t {
    Major = 0,
    Minor,
    PentatonicMajor,
    PentatonicMinor,
    Blues,
    Chromatic,
    Dorian,
    Mixolydian,
    Learned,       // set by Scale Learn from MIDI keys; each layer keeps its own
    Custom,        // one note per track, set by Edit Scale; each layer keeps its own
    COUNT
};

constexpr uint8_t SCALE_BUILTIN_COUNT = (uint8_t)Scale::Learned;

struct ScaleInfo {
    const char* name;
    uint8_t     intervals[12];  // semitone offsets from root
    uint8_t     length;
};

extern const ScaleInfo SCALES[SCALE_BUILTIN_COUNT];

// A Learned scale is a 12-bit mask: bit i set = i semitones above the root is
// in the scale. Bit 0 (the root) is always set. 0 = none learned yet, in which
// case Learned plays as Major. Each layer holds its own (LayerCfg::learned).
uint16_t scaleMaskClean(uint16_t mask);

// A Custom scale is not a set of notes the tracks climb through but one note
// per track: each slot holds semitones from the root, -1 to +2 octaves.
// Degrees past the last slot carry into the next octave, as scales do.
// CUSTOM_UNSET in slot 0 means none set yet, in which case Custom plays as
// Major. Each layer holds its own (LayerCfg::custom).
constexpr uint8_t CUSTOM_SCALE_SLOTS = 8;
constexpr int8_t  CUSTOM_STEP_MIN    = -12;
constexpr int8_t  CUSTOM_STEP_MAX    = 35;
constexpr int8_t  CUSTOM_UNSET       = -128;

inline bool scaleCustomSet(const int8_t* custom) {
    return custom && custom[0] != CUSTOM_UNSET;
}

// Semitones from the root of a scale degree, octaves included. `learned` is
// used when scale is Learned, `custom` when it is Custom; each is ignored
// otherwise.
int scaleStep(Scale scale, uint16_t learned, const int8_t* custom, uint8_t degree);

// The scale's first CUSTOM_SCALE_SLOTS degrees as a Custom scale, so a layer
// switched to Custom plays the same until it is edited. `out` may be `custom`.
void scaleToCustom(Scale scale, uint16_t learned, const int8_t* custom,
                   int8_t out[CUSTOM_SCALE_SLOTS]);

// Returns MIDI note number for a given scale degree, kept within 0-127.
// degree is 0-based; wraps across octaves automatically.
uint8_t scaleNote(RootNote root, Scale scale, uint16_t learned, const int8_t* custom,
                  uint8_t degree, uint8_t octave);
