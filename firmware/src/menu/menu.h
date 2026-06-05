#pragma once
#include "config/storage.h"

void menuInit(const SavedConfig& cfg);
void menuUpdate(SavedConfig& cfg);
void menuMessage(const char* line1, const char* line2);  // write directly to LCD
