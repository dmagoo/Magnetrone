#pragma once
#include <stdint.h>
#include "config/storage.h"
#include "demos_data.h"   // generated before each build: DEMO_COUNT

// =============================================================================
// Demo scenes, built into the firmware and read-only.
//
// Each demo is a JSON file in firmware/demos/ (format: docs/scene-format.md).
// tools/build_demos.py checks them before every build and writes
// demos_data.h, which holds only the fields each demo sets. A demo is built
// by starting from storageFactoryScene() and applying those, so a change to
// the factory values reaches every demo that leaves the field out.
// =============================================================================

const char* demoName(uint8_t i);   // 0 to DEMO_COUNT-1

// Demo `i` as a scene, and each layer's scene voice (unused if it has none).
void demoBuild(uint8_t i, Scene& s, VoiceSlot voices[NUM_LAYERS]);
