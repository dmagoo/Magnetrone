#include "hall.h"
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "motion/stepper.h"

static const uint8_t HALL_PINS[NUM_HALL_SENSORS] = {
    PIN_HALL_1, PIN_HALL_2, PIN_HALL_3, PIN_HALL_4,
    PIN_HALL_5, PIN_HALL_6, PIN_HALL_7, PIN_HALL_8
};

// How many recent passes each sensor learns from (and Sensor Timing shows).
static const uint8_t TIMING_PASSES = 8;

// Once learned, the fallback waits this many times the learned angle.
static const float FALLBACK_FACTOR = 1.5f;

static uint16_t lastReading[NUM_HALL_SENSORS]  = {};
static int16_t  lastDeviation[NUM_HALL_SENSORS] = {};  // signed, from baseline
static HallPole  triggerEdge[NUM_HALL_SENSORS] = {};  // set for one cycle on trigger
static uint32_t lastTriggerMs[NUM_HALL_SENSORS] = {}; // millis() of last trigger edge

static uint16_t baseline[NUM_HALL_SENSORS] = {};   // seeded in hallInit()
static uint16_t minDrop[NUM_HALL_SENSORS]  = {};   // from each sensor's noise
static uint16_t threshold = HALL_THRESHOLD_DEFAULT;
static int8_t   polarity  = 1;   // sign of a real hit; see hallSetPolarity()

// Per-sensor angles in motor steps, worked out once from the track radii.
// fullSteps: the magnet's width as an angle on that track. seedSteps: the
// threshold-to-peak angle before anything is learned, half of that.
// PROVISIONAL seed: (diameter / radius) / 2 radians, not measured.
static uint32_t fullSteps[NUM_HALL_SENSORS] = {};
static uint32_t seedSteps[NUM_HALL_SENSORS] = {};

// One magnet pass over one sensor, from the threshold crossing until the
// reading is back near baseline.
struct Pass {
    bool     active;     // crossed the threshold, not yet re-armed
    bool     fired;      // the note has played
    bool     peakFound;  // the reading has fallen back from its highest
    bool     fellBack;   // the fallback angle passed with no peak
    bool     stopped;    // the platter stopped during the pass: no angle
    HallPole pole;
    int32_t  armPos;     // motor position at the threshold crossing
    int32_t  peakPos;    // motor position of the highest reading
    uint16_t peakMag;
    uint16_t samples;    // readings taken during the pass
};
static Pass pass[NUM_HALL_SENSORS] = {};

// The last TIMING_PASSES passes per sensor, oldest overwritten. An angle is
// kept only for passes with a peak (bit set in peakMask).
struct Timing {
    uint16_t angle[TIMING_PASSES];   // threshold to peak, motor steps
    uint8_t  peakMask;
    uint8_t  fallMask;
    uint8_t  next;
    uint8_t  count;                  // up to TIMING_PASSES
    uint32_t learned;                // average of the angles, 0 = none yet
    uint16_t lastPeak;               // the last pass's highest reading
    uint16_t lastSamples;            // and how many readings it lasted
};
static Timing timing[NUM_HALL_SENSORS] = {};

static uint32_t absSteps(int32_t a, int32_t b) {
    return (uint32_t)(a > b ? a - b : b - a);
}

void hallInit() {
    analogReadResolution(HALL_ADC_BITS);
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        baseline[i] = HALL_BASELINE_DEFAULT;
        minDrop[i]  = HALL_NOISE_DEFAULT * HALL_MIN_DROP_NOISE_X;
    }
    // TEMPORARY - pull hall pins low to reduce noise from floating inputs during
    // bench testing. Remove before connecting real sensors.
    // for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
    //    pinMode(HALL_PINS[i], INPUT_PULLDOWN);
    //}

    const float stepsPerRadian = (float)stepperStepsPerRev() / (2.0f * PI);
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        float full   = MAGNET_DIAMETER_MM / TRACK_RADIUS_MM[i] * stepsPerRadian;
        fullSteps[i] = (uint32_t)lroundf(full);
        seedSteps[i] = (uint32_t)lroundf(full / 2.0f);
    }
}

void hallSetCalibration(const uint16_t* baselines, const uint16_t* noise,
                        uint16_t newThreshold) {
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        if (baselines) baseline[i] = baselines[i];
        if (noise)     minDrop[i]  = noise[i] * HALL_MIN_DROP_NOISE_X;
    }
    threshold = newThreshold;
}

void hallSetPolarity(int8_t newPolarity) {
    polarity = (newPolarity < 0) ? -1 : 1;
}

void hallResetTiming() {
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) timing[i] = Timing{};
}

// Where to fire, as rotation past the threshold crossing: the learned
// threshold-to-peak angle, or the seed until there is one.
static uint32_t predictSteps(uint8_t i) {
    return timing[i].learned ? timing[i].learned : seedSteps[i];
}

// How far to wait for a peak before counting the pass as a fallback.
static uint32_t fallbackSteps(uint8_t i) {
    uint32_t learned = (uint32_t)(timing[i].learned * FALLBACK_FACTOR);
    return max(learned, fullSteps[i]);
}

// A pass is over: add it to the sensor's record.
static void recordPass(uint8_t i) {
    const Pass& p = pass[i];
    if (p.stopped) return;   // no meaningful angle, and no fallback either

    Timing& t = timing[i];
    t.lastPeak    = p.peakMag;
    t.lastSamples = p.samples;
    uint8_t bit = (uint8_t)(1u << t.next);
    t.peakMask &= (uint8_t)~bit;
    t.fallMask &= (uint8_t)~bit;
    if (p.peakFound) {
        t.angle[t.next] = (uint16_t)min(absSteps(p.peakPos, p.armPos), (uint32_t)0xFFFF);
        t.peakMask |= bit;
    }
    if (p.fellBack) t.fallMask |= bit;
    t.next = (uint8_t)((t.next + 1) % TIMING_PASSES);
    if (t.count < TIMING_PASSES) t.count++;

    uint32_t sum = 0;
    uint8_t  n   = 0;
    for (uint8_t k = 0; k < TIMING_PASSES; k++) {
        if (t.peakMask & (1u << k)) { sum += t.angle[k]; n++; }
    }
    t.learned = n ? (sum + n / 2) / n : 0;
}

// Notes fire at the magnet's center, not at the threshold. Crossing the
// threshold starts a pass; the note fires once the platter has turned that
// sensor's threshold-to-peak angle further (predictSteps()), which is the
// same at any speed. The real peak is still tracked: the highest reading,
// confirmed once the reading falls back by a quarter of (highest minus
// threshold), at least minDrop so noise cannot fake one. Its angle is what
// each sensor learns. A real peak before the prediction fires the note
// early; so does a weak magnet dropping well under the threshold, and the
// platter stopping. With the platter at rest the note fires at the
// threshold, as there is no rotation to wait for.
void hallUpdate() {
    const bool    moving = stepperRunning();
    const int32_t pos    = stepperPosition();

    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint16_t val  = analogRead(HALL_PINS[i]);
        lastReading[i] = val;

        int16_t dev = (int16_t)val - (int16_t)baseline[i];
        lastDeviation[i] = dev;

        // Either sign fires; the sign says which pole. The fringe lobes of a
        // pass are opposite in sign to the face but far below the threshold
        // (see hall.h), so only the face ever gets here.
        uint16_t magnitude = (uint16_t)((dev < 0) ? -dev : dev);
        bool     normal    = (dev < 0) == (polarity < 0);

        triggerEdge[i] = HallPole::None;
        Pass& p = pass[i];

        if (!p.active) {
            if (magnitude < threshold ||
                (millis() - lastTriggerMs[i]) < HALL_DEBOUNCE_MS) continue;
            p = Pass{};
            p.active  = true;
            p.pole    = normal ? HallPole::Normal : HallPole::Reversed;
            p.armPos  = pos;
            p.peakPos = pos;
            p.peakMag = magnitude;
        } else if (magnitude < HALL_REARM_LEVEL) {
            // The magnet has genuinely left only when the field is gone.
            if (!p.fired) {   // fell from above the threshold in one sample
                triggerEdge[i]   = p.pole;
                lastTriggerMs[i] = millis();
            }
            if (!moving) p.stopped = true;
            recordPass(i);
            p.active = false;
            continue;
        }

        if (!moving) p.stopped = true;
        if (p.samples < 0xFFFF) p.samples++;

        if (!p.peakFound) {
            if (magnitude > p.peakMag) {
                p.peakMag = magnitude;
                p.peakPos = pos;
            }
            uint16_t need = (uint16_t)max((uint16_t)((p.peakMag - threshold) / 4),
                                          minDrop[i]);
            // A weak magnet may never fall back by `need` before it is gone,
            // so dropping under the threshold also ends the peak, but only
            // by more than minDrop: a reading hovering at the threshold
            // dips under it on noise alone right after the crossing.
            if (magnitude + minDrop[i] < threshold || p.peakMag - magnitude >= need) {
                p.peakFound = true;
            }
        }

        uint32_t turned = absSteps(pos, p.armPos);
        if (!p.peakFound && !p.fellBack && turned >= fallbackSteps(i)) {
            p.fellBack = true;
        }

        if (!p.fired && (!moving || p.peakFound || turned >= predictSteps(i))) {
            p.fired          = true;
            triggerEdge[i]   = p.pole;
            lastTriggerMs[i] = millis();
        }
    }
}

uint16_t hallRead(uint8_t index) {
    return lastReading[index];
}

int16_t hallDeviation(uint8_t index) {
    return lastDeviation[index];
}

HallPole hallTrigger(uint8_t index) {
    return triggerEdge[index];  // set only on the firing cycle
}

HallTiming hallTiming(uint8_t index) {
    const Timing& t = timing[index];
    HallTiming r;
    r.learnedSteps = t.learned;
    r.passes       = t.count;
    r.fallbacks    = (uint8_t)__builtin_popcount(t.fallMask);
    r.lastPeak     = t.lastPeak;
    r.lastSamples  = t.lastSamples;
    return r;
}
