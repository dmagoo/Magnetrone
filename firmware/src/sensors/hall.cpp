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
static HallPole  triggerEdge[NUM_HALL_SENSORS] = {};  // set for one cycle on trigger
static uint32_t lastTriggerMs[NUM_HALL_SENSORS] = {}; // millis() of last trigger edge

static uint16_t baseline[NUM_HALL_SENSORS] = {};   // seeded in hallInit()
static uint16_t threshold = HALL_THRESHOLD_DEFAULT;
static int8_t   polarity  = 1;   // sign of a real hit; see hallSetPolarity()

void hallInit() {
    analogReadResolution(HALL_ADC_BITS);
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) baseline[i] = HALL_BASELINE_DEFAULT;
    // TEMPORARY - pull hall pins low to reduce noise from floating inputs during
    // bench testing. Remove before connecting real sensors.
    // for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
    //    pinMode(HALL_PINS[i], INPUT_PULLDOWN);
    //}
}

void hallSetCalibration(const uint16_t* baselines, uint16_t newThreshold) {
    if (baselines) {
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) baseline[i] = baselines[i];
    }
    threshold = newThreshold;
}

void hallSetPolarity(int8_t newPolarity) {
    polarity = (newPolarity < 0) ? -1 : 1;
}

void hallUpdate() {
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint16_t val  = analogRead(HALL_PINS[i]);
        lastReading[i] = val;

        int16_t dev = (int16_t)val - (int16_t)baseline[i];
        lastDeviation[i] = dev;

        // Either sign fires; the sign says which pole. The fringe lobes of a
        // pass are opposite in sign to the face but far below the threshold
        // (see hall.h), so only the face ever gets here.
        int16_t magnitude = (dev < 0) ? (int16_t)-dev : dev;
        bool    normal    = (dev < 0) == (polarity < 0);

        triggerEdge[i] = HallPole::None;
        if (!triggered[i] && magnitude >= (int16_t)threshold &&
            (millis() - lastTriggerMs[i]) >= HALL_DEBOUNCE_MS) {
            triggered[i]     = true;
            triggerEdge[i]   = normal ? HallPole::Normal : HallPole::Reversed;
            lastTriggerMs[i] = millis();
        } else if (triggered[i] && magnitude < (int16_t)HALL_REARM_LEVEL) {
            // The magnet has genuinely left only when the field is gone.
            triggered[i] = false;
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
    return triggerEdge[index];  // set only on the rising edge, one cycle
}
