#pragma once
#include <stdint.h>
#include "config/storage.h"

// =============================================================================
// Scenes -- the saved sounds of the table.
//
// A scene holds the whole sound: both layers (mode, voice, key, octave, level,
// shift and the rest) plus the Pitch offset and A/B Balance. The table plays
// the current scene, with any Aux tweaks on top of it.
//
//   Scene 0, Defaults   what the Sound Defaults menu edits. Always there.
//   Scenes 1-8          change only through Save Scene.
//   Demos               built into the firmware, read-only (demos.h). Their
//                       ids follow the slots: SCENE_DEMO_FIRST is Demo 1.
//
// A scene id is a slot (0-8) or a demo. Loading either works the same way,
// and a demo stays the current scene across a restart like a slot does.
//
// Aux tweaks are live only: power-up loads the current scene afresh, and Aux
// Reset All goes back to it. Save Scene keeps them, in a slot.
//
// Loading a scene from the Aux lands on the next bar start, so it can be
// picked any time and falls on the downbeat; with the platter stopped or the
// start unknown it applies at once.
// =============================================================================

constexpr uint8_t SCENE_DEMO_FIRST = NUM_SCENES;

bool    sceneIsSlot(uint8_t id);   // 0-8
bool    sceneIsDemo(uint8_t id);   // a demo this firmware has
uint8_t sceneCount();              // slots plus demos: the Load Scene list

// The scene `id` holds. A demo is built on demand, so the reference is good
// only until the next call.
const Scene& sceneGet(const SavedConfig& cfg, uint8_t id);

void    scenesInit(SavedConfig& cfg);     // at boot: loads the current scene
void    scenesUpdate(SavedConfig& cfg);   // call every loop: lands a queued load

// A used slot, or a demo.
bool    sceneUsed(const SavedConfig& cfg, uint8_t slot);

// The scene shown as selected: the one queued to load, else the current one.
uint8_t sceneSelected(const SavedConfig& cfg);

// Queues `slot` to load at the next bar start (or now, see above).
void    sceneQueue(SavedConfig& cfg, uint8_t slot);

// Loads `slot` at once, dropping any queued load.
void    sceneLoadNow(SavedConfig& cfg, uint8_t slot);

// Queues the scene a Scene Code holds (scene_code.h), the way sceneQueue()
// does a slot. It replaces the live sound only: the current scene stays,
// shown as changed, and nothing is saved.
void    sceneQueueCode(SavedConfig& cfg, const Scene& s);

// Saves the live sound to `slot` (1-8) and makes it the current scene. A
// layer playing Scene Voice takes the voice along, so a demo saves whole.
void    sceneSave(SavedConfig& cfg, uint8_t slot);

// True when the live sound differs from the current scene.
bool    sceneModified(const SavedConfig& cfg);

// Aux Reset All: back to the current scene as saved.
void    sceneRevertLive(SavedConfig& cfg);

// True once after a load has landed, so the live display can redraw.
bool    sceneTakeChanged();
