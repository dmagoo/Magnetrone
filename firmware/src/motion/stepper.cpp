#include "stepper.h"
#include <Arduino.h>
#include <IntervalTimer.h>
#include "pins.h"
#include "config.h"

static IntervalTimer stepTimer;
static volatile bool running    = false;
static volatile bool stepState  = false;
static float         currentRPM = 0.0f;

static void stepISR() {
    stepState = !stepState;
    digitalWrite(PIN_STEP, stepState);
}

void stepperInit() {
    pinMode(PIN_STEP,   OUTPUT);
    pinMode(PIN_DIR,    OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    pinMode(PIN_SLEEP,  OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);  // disabled on boot
    digitalWrite(PIN_SLEEP,  LOW);   // sleeping on boot
}

void stepperStart(float rpm) {
    rpm = constrain(rpm, MIN_RPM, MAX_RPM);
    currentRPM = rpm;
    digitalWrite(PIN_SLEEP,  HIGH);  // wake before enabling
    digitalWrite(PIN_ENABLE, LOW);
    // period is half the step period because ISR toggles (two toggles = one step)
    stepTimer.begin(stepISR, rpmToStepPeriodUs(rpm) / 2);
    running = true;
}

void stepperStop() {
    stepTimer.end();
    digitalWrite(PIN_ENABLE, HIGH);
    digitalWrite(PIN_SLEEP,  LOW);   // sleep after stopping
    running = false;
}

void stepperSetRPM(float rpm) {
    if (!running) return;
    rpm = constrain(rpm, MIN_RPM, MAX_RPM);
    currentRPM = rpm;
    stepTimer.update(rpmToStepPeriodUs(rpm) / 2);
}

bool stepperRunning() {
    return running;
}

float stepperCurrentRPM() {
    return currentRPM;
}
