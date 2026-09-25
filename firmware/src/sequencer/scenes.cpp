#include "scenes.h"
#include <Arduino.h>
#include <math.h>
#include "layers.h"
#include "pitch.h"
#include "motion/bar.h"
#include "motion/stepper.h"

// The saving part of a load waits this long after the downbeat, so an EEPROM
// write never lands on the notes it would delay.
static const uint32_t COMMIT_DELAY_MS = 500;

static uint8_t  pending      = SCENE_NONE;   // queued to load at the next bar
static uint32_t lastPhase    = 0;
static bool     changed      = false;
static bool     commitDue    = false;
static uint32_t commitAtMs   = 0;

bool sceneUsed(const SavedConfig& cfg, uint8_t slot) {
    return slot < NUM_SCENES && cfg.scenes[slot].used;
}

bool sceneAnyUsed(const SavedConfig& cfg) {
    for (uint8_t i = 0; i < NUM_SCENES; i++) if (cfg.scenes[i].used) return true;
    return false;
}

uint8_t sceneSelected(const SavedConfig& cfg) {
    return (pending != SCENE_NONE) ? pending : cfg.currentScene;
}

void scenesInit(const SavedConfig& cfg) {
    // The rest of the scene is already in the saved settings; pitch and
    // balance are the two it alone keeps.
    if (sceneUsed(cfg, cfg.currentScene)) {
        const SceneSlot& s = cfg.scenes[cfg.currentScene];
        pitchSetOffset(s.pitch);
        layerSetBalance(s.balance);
    }
}

static void apply(SavedConfig& cfg, uint8_t slot) {
    const SceneSlot& s = cfg.scenes[slot];
    cfg.root   = s.root;
    cfg.scale  = s.scale;
    cfg.octave = s.octave;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        cfg.layer[l].voice   = s.voice[l];
        cfg.layer[l].shift   = s.shift[l];
        cfg.layer[l].lowNote = s.lowNote[l];
    }
    pitchSetOffset(s.pitch);
    layerSetBalance(s.balance);
    layersApply(cfg);
    cfg.currentScene = slot;
    changed    = true;
    commitDue  = true;
    commitAtMs = millis() + COMMIT_DELAY_MS;
}

void sceneQueue(SavedConfig& cfg, uint8_t slot) {
    if (!sceneUsed(cfg, slot)) return;
    pending   = slot;
    lastPhase = barPhase();
}

void scenesUpdate(SavedConfig& cfg) {
    if (commitDue && (int32_t)(millis() - commitAtMs) >= 0) {
        // The scene's values become the saved setup: commit them, not
        // whatever the knob has done since the load.
        commitDue = false;
        SavedConfig c = cfg;
        const SceneSlot& sc = cfg.scenes[cfg.currentScene];
        c.root = sc.root; c.scale = sc.scale; c.octave = sc.octave;
        for (uint8_t l = 0; l < NUM_LAYERS; l++) {
            c.layer[l].voice   = sc.voice[l];
            c.layer[l].shift   = sc.shift[l];
            c.layer[l].lowNote = sc.lowNote[l];
        }
        storageCommit(c, CommitField::All);
    }

    uint32_t phase = barPhase();
    if (pending != SCENE_NONE) {
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
            pending = SCENE_NONE;
            if (sceneUsed(cfg, slot)) apply(cfg, slot);
        }
    }
    lastPhase = phase;
}

void sceneSave(SavedConfig& cfg, uint8_t slot) {
    if (slot >= NUM_SCENES) return;
    SceneSlot& s = cfg.scenes[slot];
    s.used   = true;
    s.root   = cfg.root;
    s.scale  = cfg.scale;
    s.octave = cfg.octave;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        s.voice[l]   = cfg.layer[l].voice;
        s.shift[l]   = cfg.layer[l].shift;
        s.lowNote[l] = cfg.layer[l].lowNote;
    }
    s.pitch   = pitchGetOffset();
    s.balance = layerBalance();
    cfg.currentScene = slot;
    pending = SCENE_NONE;
    storageCommit(cfg, CommitField::All);
}

bool sceneModified(const SavedConfig& cfg) {
    if (!sceneUsed(cfg, cfg.currentScene)) return false;
    const SceneSlot& s = cfg.scenes[cfg.currentScene];
    if (cfg.root != s.root || cfg.scale != s.scale || cfg.octave != s.octave) return true;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (cfg.layer[l].voice   != s.voice[l] ||
            cfg.layer[l].shift   != s.shift[l] ||
            cfg.layer[l].lowNote != s.lowNote[l]) return true;
    }
    return fabsf(pitchGetOffset() - s.pitch) > 0.001f || layerBalance() != s.balance;
}

void sceneRevertLive(SavedConfig& cfg) {
    storageRevertLive(cfg);
    bool have = sceneUsed(cfg, cfg.currentScene);
    pitchSetOffset(have ? cfg.scenes[cfg.currentScene].pitch : 0.0f);
    layerSetBalance(have ? cfg.scenes[cfg.currentScene].balance : 0);
    layersApply(cfg);
}

bool sceneTakeChanged() {
    bool c = changed;
    changed = false;
    return c;
}
