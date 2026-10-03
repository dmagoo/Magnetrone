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

int scaleStep(Scale scale, uint16_t learned, const int8_t* custom, uint8_t degree) {
    if (scale == Scale::Custom && scaleCustomSet(custom)) {
        return custom[degree % CUSTOM_SCALE_SLOTS] + 12 * (degree / CUSTOM_SCALE_SLOTS);
    }
    ScaleInfo l = { "Learned", { 0 }, 0 };
    const ScaleInfo* s;
    if (scale == Scale::Learned && learned) {
        uint16_t mask = scaleMaskClean(learned);
        for (uint8_t i = 0; i < 12; i++) {
            if (mask & (1u << i)) l.intervals[l.length++] = i;
        }
        s = &l;
    } else {
        // Learned with none learned, or Custom with none set, plays as Major.
        uint8_t i = static_cast<uint8_t>(scale);
        s = &SCALES[i < SCALE_BUILTIN_COUNT ? i : 0];
    }
    return s->intervals[degree % s->length] + 12 * (degree / s->length);
}

void scaleToCustom(Scale scale, uint16_t learned, const int8_t* custom,
                   int8_t out[CUSTOM_SCALE_SLOTS]) {
    int8_t steps[CUSTOM_SCALE_SLOTS];
    for (uint8_t d = 0; d < CUSTOM_SCALE_SLOTS; d++) {
        int st = scaleStep(scale, learned, custom, d);
        steps[d] = (int8_t)(st < CUSTOM_STEP_MIN ? CUSTOM_STEP_MIN
                          : st > CUSTOM_STEP_MAX ? CUSTOM_STEP_MAX : st);
    }
    for (uint8_t d = 0; d < CUSTOM_SCALE_SLOTS; d++) out[d] = steps[d];
}

uint8_t scaleNote(RootNote root, Scale scale, uint16_t learned, const int8_t* custom,
                  uint8_t degree, uint8_t octave) {
    // MIDI note: C4 = 60, octave 0 = C0 = 12
    int note = 12 + octave * 12 + static_cast<uint8_t>(root)
             + scaleStep(scale, learned, custom, degree);
    return (uint8_t)(note < 0 ? 0 : note > 127 ? 127 : note);
}
