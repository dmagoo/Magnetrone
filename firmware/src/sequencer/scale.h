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
    COUNT
};

struct ScaleInfo {
    const char* name;
    uint8_t     intervals[12];  // semitone offsets from root
    uint8_t     length;
};

extern const ScaleInfo SCALES[static_cast<uint8_t>(Scale::COUNT)];

// Returns MIDI note number for a given scale degree.
// degree is 0-based; wraps across octaves automatically.
uint8_t scaleNote(RootNote root, Scale scale, uint8_t degree, uint8_t octave);
