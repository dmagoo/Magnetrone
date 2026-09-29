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

// Returns MIDI note number for a given scale degree. `learned` is the mask
// used when scale is Learned, ignored otherwise.
// degree is 0-based; wraps across octaves automatically.
uint8_t scaleNote(RootNote root, Scale scale, uint16_t learned, uint8_t degree, uint8_t octave);
