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

static uint16_t  learnedMask = 0;
static ScaleInfo learned     = { "Learned", { 0 }, 1 };

void scaleSetLearned(uint16_t mask) {
    mask = (mask | 1) & 0x0FFF;
    learnedMask = mask;
    learned.length = 0;
    for (uint8_t i = 0; i < 12; i++) {
        if (mask & (1u << i)) learned.intervals[learned.length++] = i;
    }
}

uint16_t scaleLearnedMask() {
    return learnedMask;
}

bool scaleHasLearned() {
    return learnedMask != 0;
}

static const ScaleInfo& scaleInfo(Scale scale) {
    if (scale == Scale::Learned) return scaleHasLearned() ? learned : SCALES[0];
    uint8_t i = static_cast<uint8_t>(scale);
    return SCALES[i < SCALE_BUILTIN_COUNT ? i : 0];
}

uint8_t scaleNote(RootNote root, Scale scale, uint8_t degree, uint8_t octave) {
    const ScaleInfo& s = scaleInfo(scale);
    uint8_t octaveOffset = degree / s.length;
    uint8_t interval     = s.intervals[degree % s.length];
    // MIDI note: C4 = 60, octave 0 = C0 = 12
    return 12 + (octave + octaveOffset) * 12
              + static_cast<uint8_t>(root)
              + interval;
}
