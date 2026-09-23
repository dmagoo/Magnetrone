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

// actual_rpm / commanded_rpm, as measured by calibration. Commanding
// rpm / correction makes the platter actually turn at rpm. 1.0 until
// calibration has run.
static float correction = 1.0f;

void stepperSetCorrection(float actualOverCommanded) {
    // Ignore absurd or uninitialised values rather than letting a bad EEPROM
    // read divide the commanded speed into nonsense.
    if (actualOverCommanded > 0.5f && actualOverCommanded < 2.0f) {
        correction = actualOverCommanded;
    }
}

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
    platter.setRPM(rpm / correction);   // spins up from rest via pull-in + ramp
}

void stepperStop() {
    // Ramps to rest and keeps the coils energised so the platter holds
    // position rather than freewheeling. That is Platter's documented choice.
    platter.stop();
}

void stepperSetRPM(float rpm) {
    if (!commanded) return;   // matches the old API: no effect until started
    platter.setRPM(rpm / correction);
}

bool stepperRunning() {
    return commanded && !platter.isStopped();
}

float stepperCurrentRPM() {
    // Report the real platter speed, undoing the correction applied on the way in.
    return platter.currentRPM() * correction;
}
