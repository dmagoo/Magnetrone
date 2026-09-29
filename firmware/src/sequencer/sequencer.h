#pragma once
#include "config/storage.h"

void sequencerUpdate(const SavedConfig& cfg);

// Which tracks may play: bit i is sensor i (0 = innermost). All on unless
// Placement Mode has muted some. Not saved.
void sequencerSetTrackMask(uint8_t mask);
