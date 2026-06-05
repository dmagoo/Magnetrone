#pragma once

void  stepperInit();
void  stepperStart(float rpm);
void  stepperStop();
void  stepperSetRPM(float rpm);
bool  stepperRunning();
float stepperCurrentRPM();
