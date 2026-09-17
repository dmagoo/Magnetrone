#pragma once

void  stepperInit();
void  stepperUpdate();   // MUST be called every loop: services the accel ramp
void  stepperStart(float rpm);
void  stepperStop();
void  stepperSetRPM(float rpm);
bool  stepperRunning();
float stepperCurrentRPM();
