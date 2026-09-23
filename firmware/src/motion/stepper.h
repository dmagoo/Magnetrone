#pragma once

void  stepperInit();

// Calibration measures actual_rpm / commanded_rpm. Feeding it back here makes
// every later setRPM land on the real speed instead of carrying a correction
// factor that nothing applies. Call at boot with the stored value, and again
// whenever calibration produces a new one.
void  stepperSetCorrection(float actualOverCommanded);
void  stepperUpdate();   // MUST be called every loop: services the accel ramp
void  stepperStart(float rpm);
void  stepperStop();
void  stepperSetRPM(float rpm);
bool  stepperRunning();
float stepperCurrentRPM();
