#include "scenes.h"
#include <Arduino.h>
#include <math.h>
#include "layers.h"
#include "pitch.h"
#include "scale.h"
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

// Everything one scene holds, wherever it is stored: SceneSlot plus the two
// fields that live beside it in SavedConfig (added in later versions).
struct SceneView {
    SceneSlot s;
    int8_t    octave[NUM_LAYERS];   // per-layer octave offset
    uint16_t  learned;              // Learned scale mask, 0 if none
};

// Reads scene `slot` into v. The Defaults scene is the factory values of the
// same fields, built from storageDefaults(), and is never stored.
static bool view(const SavedConfig& cfg, uint8_t slot, SceneView& v) {
    if (slot == SCENE_DEFAULTS) {
        SavedConfig d = storageDefaults();
        v.s.used   = true;
        v.s.root   = d.root;
        v.s.scale  = d.scale;
        v.s.octave = d.octave;
        for (uint8_t l = 0; l < NUM_LAYERS; l++) {
            v.s.voice[l]   = d.layer[l].voice;
            v.s.shift[l]   = d.layer[l].shift;
            v.s.lowNote[l] = d.layer[l].lowNote;
            v.octave[l]    = d.layer[l].octaveOffset;
        }
        v.s.balance = 0;
        v.s.pitch   = 0.0f;
        v.learned   = 0;
        return true;
    }
    if (slot >= NUM_SCENES || !cfg.scenes[slot].used) return false;
    v.s = cfg.scenes[slot];
    for (uint8_t l = 0; l < NUM_LAYERS; l++) v.octave[l] = cfg.sceneLayerOctave[slot][l];
    v.learned = cfg.sceneLearned[slot];
    return true;
}

bool sceneUsed(const SavedConfig& cfg, uint8_t slot) {
    return slot == SCENE_DEFAULTS || (slot < NUM_SCENES && cfg.scenes[slot].used);
}

uint8_t sceneSelected(const SavedConfig& cfg) {
    return (pending != SCENE_NONE) ? pending : cfg.currentScene;
}

// Copies a scene's fields into cfg (the settings, not pitch or balance).
static void putFields(SavedConfig& c, const SceneView& v) {
    c.root   = v.s.root;
    c.scale  = v.s.scale;
    c.octave = v.s.octave;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        c.layer[l].voice        = v.s.voice[l];
        c.layer[l].shift        = v.s.shift[l];
        c.layer[l].lowNote      = v.s.lowNote[l];
        c.layer[l].octaveOffset = v.octave[l];
    }
}

// A learned scale lives only in scenes. If the saved scale is Learned, its
// notes come from the current scene; with none to be had, play Major.
static void restoreLearned(SavedConfig& cfg) {
    if (cfg.scale != Scale::Learned) return;
    SceneView v;
    uint16_t mask = view(cfg, cfg.currentScene, v) ? v.learned : 0;
    if (mask) scaleSetLearned(mask);
    else if (!scaleHasLearned()) cfg.scale = Scale::Major;
}

void scenesInit(SavedConfig& cfg) {
    // The rest of the scene is already in the saved settings; pitch, balance
    // and a learned scale are what it alone keeps.
    SceneView v;
    if (view(cfg, cfg.currentScene, v)) {
        pitchSetOffset(v.s.pitch);
        layerSetBalance(v.s.balance);
    }
    restoreLearned(cfg);
}

static void apply(SavedConfig& cfg, uint8_t slot) {
    SceneView v;
    if (!view(cfg, slot, v)) return;
    putFields(cfg, v);
    pitchSetOffset(v.s.pitch);
    layerSetBalance(v.s.balance);
    if (v.s.scale == Scale::Learned && v.learned) scaleSetLearned(v.learned);
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
        SceneView v;
        if (view(cfg, cfg.currentScene, v)) {
            SavedConfig c = cfg;
            putFields(c, v);
            storageCommit(c, CommitField::All);
        }
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
            apply(cfg, slot);
        }
    }
    lastPhase = phase;
}

void sceneSave(SavedConfig& cfg, uint8_t slot) {
    if (slot >= NUM_SCENES) return;   // Defaults is read-only
    SceneSlot& s = cfg.scenes[slot];
    s.used   = true;
    s.root   = cfg.root;
    s.scale  = cfg.scale;
    s.octave = cfg.octave;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        s.voice[l]   = cfg.layer[l].voice;
        s.shift[l]   = cfg.layer[l].shift;
        s.lowNote[l] = cfg.layer[l].lowNote;
        cfg.sceneLayerOctave[slot][l] = cfg.layer[l].octaveOffset;
    }
    s.pitch   = pitchGetOffset();
    s.balance = layerBalance();
    cfg.sceneLearned[slot] = (cfg.scale == Scale::Learned) ? scaleLearnedMask() : 0;
    cfg.currentScene = slot;
    pending = SCENE_NONE;
    storageCommit(cfg, CommitField::All);
}

bool sceneModified(const SavedConfig& cfg) {
    SceneView v;
    if (!view(cfg, cfg.currentScene, v)) return false;
    if (cfg.root != v.s.root || cfg.scale != v.s.scale || cfg.octave != v.s.octave) return true;
    if (cfg.scale == Scale::Learned && scaleLearnedMask() != v.learned) return true;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (cfg.layer[l].voice        != v.s.voice[l] ||
            cfg.layer[l].shift        != v.s.shift[l] ||
            cfg.layer[l].lowNote      != v.s.lowNote[l] ||
            cfg.layer[l].octaveOffset != v.octave[l]) return true;
    }
    return fabsf(pitchGetOffset() - v.s.pitch) > 0.001f || layerBalance() != v.s.balance;
}

void sceneRevertLive(SavedConfig& cfg) {
    storageRevertLive(cfg);
    SceneView v;
    bool have = view(cfg, cfg.currentScene, v);
    pitchSetOffset(have ? v.s.pitch : 0.0f);
    layerSetBalance(have ? v.s.balance : 0);
    restoreLearned(cfg);
    layersApply(cfg);
}

bool sceneTakeChanged() {
    bool c = changed;
    changed = false;
    return c;
}
