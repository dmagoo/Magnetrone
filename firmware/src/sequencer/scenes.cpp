#include "scenes.h"
#include <Arduino.h>
#include <math.h>
#include "layers.h"
#include "pitch.h"
#include "motion/bar.h"
#include "motion/stepper.h"

// Saving the new current scene waits this long after the downbeat, so an
// EEPROM write never lands on the notes it would delay.
static const uint32_t SAVE_DELAY_MS = 500;

static const uint8_t NONE = 0xFF;

static uint8_t  pending      = NONE;   // queued to load at the next bar
static uint32_t lastPhase    = 0;
static bool     changed      = false;
static bool     saveDue      = false;
static uint32_t saveAtMs     = 0;

bool sceneUsed(const SavedConfig& cfg, uint8_t slot) {
    return slot < NUM_SCENES && cfg.sceneUsed[slot];
}

uint8_t sceneSelected(const SavedConfig& cfg) {
    return (pending != NONE) ? pending : cfg.currentScene;
}

// Makes the scene in `slot` the live sound.
static void put(SavedConfig& cfg, uint8_t slot) {
    const Scene& s = cfg.scenes[slot];
    for (uint8_t l = 0; l < NUM_LAYERS; l++) cfg.layer[l] = s.layer[l];
    pitchSetOffset(s.pitch);
    layerSetBalance(s.balance);
    layersApply(cfg);
}

void scenesInit(SavedConfig& cfg) {
    put(cfg, cfg.currentScene);
}

static void apply(SavedConfig& cfg, uint8_t slot) {
    if (!sceneUsed(cfg, slot)) return;
    put(cfg, slot);
    cfg.currentScene = slot;
    changed  = true;
    saveDue  = true;
    saveAtMs = millis() + SAVE_DELAY_MS;
}

void sceneQueue(SavedConfig& cfg, uint8_t slot) {
    if (!sceneUsed(cfg, slot)) return;
    pending   = slot;
    lastPhase = barPhase();
}

void sceneLoadNow(SavedConfig& cfg, uint8_t slot) {
    pending = NONE;
    apply(cfg, slot);
}

void scenesUpdate(SavedConfig& cfg) {
    if (saveDue && (int32_t)(millis() - saveAtMs) >= 0) {
        saveDue = false;
        storageSave(cfg);   // remembers the current scene for power-up
    }

    uint32_t phase = barPhase();
    if (pending != NONE) {
        bool land;
        if (!stepperRunning() || stepperJogging() || !barKnown()) {
            land = true;   // no bar to wait for
        } else {
            // The phase wraps as the mark passes the arm, whichever way the
            // platter turns: a jump of more than half a bar is the wrap.
            int32_t half = (int32_t)barStepsPerRev() / 2;
            int32_t d    = (int32_t)phase - (int32_t)lastPhase;
            land = (d < -half || d > half);
        }
        if (land) {
            uint8_t slot = pending;
            pending = NONE;
            apply(cfg, slot);
        }
    }
    lastPhase = phase;
}

void sceneSave(SavedConfig& cfg, uint8_t slot) {
    if (slot == SCENE_DEFAULTS || slot >= NUM_SCENES) return;   // Defaults: menu only
    Scene& s = cfg.scenes[slot];
    for (uint8_t l = 0; l < NUM_LAYERS; l++) s.layer[l] = cfg.layer[l];
    s.pitch   = pitchGetOffset();
    s.balance = layerBalance();
    cfg.sceneUsed[slot] = true;
    cfg.currentScene    = slot;
    pending = NONE;
    storageSave(cfg);
}

static bool layerEqual(const LayerCfg& a, const LayerCfg& b) {
    return a.mode == b.mode && a.voice == b.voice && a.channel == b.channel &&
           a.root == b.root && a.scale == b.scale && a.octave == b.octave &&
           (a.scale != Scale::Learned || a.learned == b.learned) &&
           a.level == b.level && a.shift == b.shift && a.wrap == b.wrap &&
           a.shiftSameAsA == b.shiftSameAsA && a.lowNote == b.lowNote &&
           a.lowNoteSameAsA == b.lowNoteSameAsA;
}

bool sceneModified(const SavedConfig& cfg) {
    const Scene& s = cfg.scenes[cfg.currentScene];
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!layerEqual(cfg.layer[l], s.layer[l])) return true;
    }
    return fabsf(pitchGetOffset() - s.pitch) > 0.001f || layerBalance() != s.balance;
}

void sceneRevertLive(SavedConfig& cfg) {
    pending = NONE;
    put(cfg, cfg.currentScene);
    changed = true;
}

bool sceneTakeChanged() {
    bool c = changed;
    changed = false;
    return c;
}
