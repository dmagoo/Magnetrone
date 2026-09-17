#include "stepper.h"
#include "platter.h"

// Adapter layer. The app still talks to the free-function stepper API because
// menu.cpp and calibration.cpp are built around it; the implementation behind
// it is now the Platter class driving the TMC2209 over UART. Keeping the old
// surface meant those callers did not have to change.
//
// The previous A4988 implementation lived here directly: a bare IntervalTimer
// toggling PIN_STEP with no ramping. Platter adds soft start, slew-limited
// acceleration and the verified UART bring-up.

static Platter platter(Platter::defaultConfig());

// Platter::isStopped() is false before anything has been commanded, so asking
// it alone would report "running" at boot and turn the first knob press into a
// stop instead of a start. This tracks whether motion has ever been asked for.
static bool commanded = false;

void stepperInit() {
    platter.begin();
    // begin() energises the coils. Nothing has been commanded yet, so hold off
    // the holding torque (and its idle heat) until the first start.
    platter.disable();
}

void stepperUpdate() {
    platter.update();
}

void stepperStart(float rpm) {
    commanded = true;
    platter.enable();
    platter.setRPM(rpm);   // spins up from rest via pull-in + ramp
}

void stepperStop() {
    // Ramps to rest and keeps the coils energised so the platter holds
    // position rather than freewheeling. That is Platter's documented choice.
    platter.stop();
}

void stepperSetRPM(float rpm) {
    if (!commanded) return;   // matches the old API: no effect until started
    platter.setRPM(rpm);
}

bool stepperRunning() {
    return commanded && !platter.isStopped();
}

float stepperCurrentRPM() {
    return platter.currentRPM();
}
