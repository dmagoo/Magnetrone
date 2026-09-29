#pragma once
#include <stdint.h>

void  stepperInit();

// Calibration measures actual_rpm / commanded_rpm. Feeding it back here makes
// every later setRPM land on the real speed instead of carrying a correction
// factor that nothing applies. Call at boot with the stored value, and again
// whenever calibration produces a new one.
void  stepperSetCorrection(float actualOverCommanded);
void  stepperUpdate();   // MUST be called every loop: services the accel ramp
void  stepperStart(float rpm);
void  stepperStop();
// Coils off, so the platter turns freely by hand. Call only at rest. Hand
// turns are not counted; the next start or jog powers it again.
void  stepperRelease();
void  stepperSetRPM(float rpm);
bool  stepperRunning();
float stepperCurrentRPM();

// Signed microstep position (+ forward, - reversed) and how many make one
// platter revolution. The count is exact: a GT2 belt cannot slip, so steps per
// revolution is fixed by the tooth ratio.
int32_t  stepperPosition();
uint32_t stepperStepsPerRev();

// Hand positioning from the menu: turns at `rpm` (signed, may be below
// MIN_RPM) until stepperStop(). The next stepperStart() ends jog mode.
void stepperJog(float rpm);
bool stepperJogging();

// Turns exactly `steps` (signed) and stops there, at up to `rpm`. Counts as
// jogging. stepperRunning() goes false on arrival.
void stepperMoveBy(int32_t steps, float rpm);

// Driver version register over UART (0x21 = TMC2209 answering). For Info.
uint8_t stepperDriverVersion();
constexpr uint8_t STEPPER_DRIVER_VERSION = 0x21;
