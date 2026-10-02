#pragma once
#include "config/storage.h"

// The "scenes" serial command: prints Defaults and each used scene 1-8 as
// JSON in the demo format (docs/scene-format.md), leaving out every field
// that matches the factory scene (and, in a voice, its base built-in), then
// the used Custom voices. Each scene is ready to copy into firmware/demos/.
void sceneDump(const SavedConfig& cfg);
