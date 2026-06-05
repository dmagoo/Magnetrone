#include "scale.h"

const ScaleInfo SCALES[static_cast<uint8_t>(Scale::COUNT)] = {
    { "Major",          { 0, 2, 4, 5, 7, 9, 11 },          7 },
    { "Minor",          { 0, 2, 3, 5, 7, 8, 10 },          7 },
    { "Pent. Major",    { 0, 2, 4, 7, 9 },                  5 },
    { "Pent. Minor",    { 0, 3, 5, 7, 10 },                 5 },
    { "Blues",          { 0, 3, 5, 6, 7, 10 },              6 },
    { "Chromatic",      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
    { "Dorian",         { 0, 2, 3, 5, 7, 9, 10 },           7 },
    { "Mixolydian",     { 0, 2, 4, 5, 7, 9, 10 },           7 },
};

uint8_t scaleNote(RootNote root, Scale scale, uint8_t degree, uint8_t octave) {
    const ScaleInfo& s = SCALES[static_cast<uint8_t>(scale)];
    uint8_t octaveOffset = degree / s.length;
    uint8_t interval     = s.intervals[degree % s.length];
    // MIDI note: C4 = 60, octave 0 = C0 = 12
    return 12 + (octave + octaveOffset) * 12
              + static_cast<uint8_t>(root)
              + interval;
}
