#pragma once
#include <stdint.h>

void     hallInit();
void     hallUpdate();
uint16_t hallRead(uint8_t index);           // raw ADC value, index 0-7
bool     hallTriggered(uint8_t index);      // true on rising edge only, one cycle
void     hallSetCalibration(uint16_t baseline, uint16_t threshold);
