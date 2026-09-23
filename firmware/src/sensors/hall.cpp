#include "hall.h"
#include <Arduino.h>
#include "pins.h"
#include "config.h"

static const uint8_t HALL_PINS[NUM_HALL_SENSORS] = {
    PIN_HALL_1, PIN_HALL_2, PIN_HALL_3, PIN_HALL_4,
    PIN_HALL_5, PIN_HALL_6, PIN_HALL_7, PIN_HALL_8
};

static uint16_t lastReading[NUM_HALL_SENSORS]  = {};
static int16_t  lastDeviation[NUM_HALL_SENSORS] = {};  // signed, from baseline
static bool     triggered[NUM_HALL_SENSORS]    = {};
static bool     triggerEdge[NUM_HALL_SENSORS]  = {};  // true for one cycle on trigger
static uint32_t lastTriggerMs[NUM_HALL_SENSORS] = {}; // millis() of last trigger edge

static uint16_t baseline  = HALL_BASELINE_DEFAULT;
static uint16_t threshold = HALL_THRESHOLD_DEFAULT;
static int8_t   polarity  = 1;   // sign of a real hit; see hallSetPolarity()

void hallInit() {
    analogReadResolution(HALL_ADC_BITS);
    // TEMPORARY - pull hall pins low to reduce noise from floating inputs during
    // bench testing. Remove before connecting real sensors.
    // for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
    //    pinMode(HALL_PINS[i], INPUT_PULLDOWN);
    //}
}

void hallSetCalibration(uint16_t newBaseline, uint16_t newThreshold) {
    baseline  = newBaseline;
    threshold = newThreshold;
}

void hallSetPolarity(int8_t newPolarity) {
    polarity = (newPolarity < 0) ? -1 : 1;
}

void hallUpdate() {
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint16_t val  = analogRead(HALL_PINS[i]);
        lastReading[i] = val;

        int16_t dev = (int16_t)val - (int16_t)baseline;
        lastDeviation[i] = dev;

        // Signed, deliberately. A magnet pass reads as fringe / face / fringe,
        // and the fringe lobes are opposite in sign to the face field but big
        // enough to clear the threshold by themselves. Comparing absolute
        // deviation made them look like real hits, which is what fired two or
        // three notes per pass. Projecting onto the expected polarity leaves
        // the fringes negative, so they never reach the threshold at all.
        int16_t signedDev = (polarity < 0) ? (int16_t)-dev : dev;

        // Re-arm on magnitude rather than the signed value: the magnet has
        // genuinely left only when the field is gone, whichever way it pointed.
        int16_t magnitude = (dev < 0) ? (int16_t)-dev : dev;

        if (!triggered[i] && signedDev >= (int16_t)threshold &&
            (millis() - lastTriggerMs[i]) >= HALL_DEBOUNCE_MS) {
            triggered[i]    = true;
            triggerEdge[i]  = true;
            lastTriggerMs[i] = millis();
        } else if (triggered[i] && magnitude < (int16_t)HALL_REARM_LEVEL) {
            triggered[i]   = false;
            triggerEdge[i] = false;
        } else {
            triggerEdge[i] = false;
        }
    }
}

uint16_t hallRead(uint8_t index) {
    return lastReading[index];
}

int16_t hallDeviation(uint8_t index) {
    return lastDeviation[index];
}

bool hallTriggered(uint8_t index) {
    return triggerEdge[index];  // true only on the rising edge, one cycle
}
