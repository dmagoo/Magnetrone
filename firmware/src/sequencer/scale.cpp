#include "scale.h"

const ScaleInfo SCALES[SCALE_BUILTIN_COUNT] = {
    { "Major",          { 0, 2, 4, 5, 7, 9, 11 },          7 },
    { "Minor",          { 0, 2, 3, 5, 7, 8, 10 },          7 },
    { "Pent. Major",    { 0, 2, 4, 7, 9 },                  5 },
    { "Pent. Minor",    { 0, 3, 5, 7, 10 },                 5 },
    { "Blues",          { 0, 3, 5, 6, 7, 10 },              6 },
    { "Chromatic",      { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 }, 12 },
    { "Dorian",         { 0, 2, 3, 5, 7, 9, 10 },           7 },
    { "Mixolydian",     { 0, 2, 4, 5, 7, 9, 10 },           7 },
};

uint16_t scaleMaskClean(uint16_t mask) {
    return (uint16_t)((mask | 1) & 0x0FFF);
}

uint8_t scaleNote(RootNote root, Scale scale, uint16_t learned, uint8_t degree, uint8_t octave) {
    ScaleInfo l = { "Learned", { 0 }, 0 };
    const ScaleInfo* s;
    if (scale == Scale::Learned && learned) {
        uint16_t mask = scaleMaskClean(learned);
        for (uint8_t i = 0; i < 12; i++) {
            if (mask & (1u << i)) l.intervals[l.length++] = i;
        }
        s = &l;
    } else {
        uint8_t i = static_cast<uint8_t>(scale);
        s = &SCALES[i < SCALE_BUILTIN_COUNT ? i : 0];
    }
    uint8_t octaveOffset = degree / s->length;
    uint8_t interval     = s->intervals[degree % s->length];
    // MIDI note: C4 = 60, octave 0 = C0 = 12
    return 12 + (octave + octaveOffset) * 12
              + static_cast<uint8_t>(root)
              + interval;
}
