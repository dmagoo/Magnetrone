#include "scenes.h"
#include <Arduino.h>
#include <math.h>
#include "layers.h"
#include "audio/voice.h"
#include "pitch.h"
#include "motion/bar.h"
#include "motion/stepper.h"
#include "demos.h"

// Saving the new current scene waits this long after the downbeat, so an
// EEPROM write never lands on the notes it would delay.
static const uint32_t SAVE_DELAY_MS = 500;

static const uint8_t NONE = 0xFF;

static uint8_t  pending      = NONE;   // queued to load at the next bar
static uint32_t lastPhase    = 0;
static bool     changed      = false;
static bool     saveDue      = false;
static uint32_t saveAtMs     = 0;

// The demo last built, for sceneGet().
static Scene     demoScene;
static VoiceSlot demoVoices[NUM_LAYERS];
static uint8_t   demoBuilt = NONE;

bool sceneIsSlot(uint8_t id) {
    return id < NUM_SCENES;
}

bool sceneIsDemo(uint8_t id) {
    return id >= SCENE_DEMO_FIRST && id - SCENE_DEMO_FIRST < DEMO_COUNT;
}

uint8_t sceneCount() {
    return NUM_SCENES + DEMO_COUNT;
}

bool sceneUsed(const SavedConfig& cfg, uint8_t slot) {
    return (slot < NUM_SCENES && cfg.sceneUsed[slot]) || sceneIsDemo(slot);
}

static void buildDemo(uint8_t id) {
    if (demoBuilt == id) return;
    demoBuild(id - SCENE_DEMO_FIRST, demoScene, demoVoices);
    demoBuilt = id;
}

const Scene& sceneGet(const SavedConfig& cfg, uint8_t id) {
    if (sceneIsDemo(id)) { buildDemo(id); return demoScene; }
    return cfg.scenes[id < NUM_SCENES ? id : SCENE_DEFAULTS];
}

// The scene's own voice for a layer; unused if it has none. Defaults never
// has one.
static VoiceSlot sceneVoiceOf(const SavedConfig& cfg, uint8_t id, uint8_t l) {
    if (sceneIsDemo(id)) { buildDemo(id); return demoVoices[l]; }
    if (id == SCENE_DEFAULTS || id >= NUM_SCENES) return VoiceSlot{};
    return cfg.sceneVoices[id][l];
}

uint8_t sceneSelected(const SavedConfig& cfg) {
    return (pending != NONE) ? pending : cfg.currentScene;
}

// Makes the scene in `slot` the live sound, its own voices included.
static void put(SavedConfig& cfg, uint8_t slot) {
    const Scene& s = sceneGet(cfg, slot);
    for (uint8_t l = 0; l < NUM_LAYERS; l++) cfg.layer[l] = s.layer[l];
    pitchSetOffset(s.pitch);
    layerSetBalance(s.balance);
    for (uint8_t l = 0; l < NUM_LAYERS; l++) layerSetSceneVoice(l, sceneVoiceOf(cfg, slot, l));
    layersResetVoices();   // Voice Edit tweaks are not part of a scene
    layersApply(cfg);
}

void scenesInit(SavedConfig& cfg) {
    // A demo this firmware no longer has falls back to Defaults.
    if (!sceneUsed(cfg, cfg.currentScene)) cfg.currentScene = SCENE_DEFAULTS;
    put(cfg, cfg.currentScene);
}

static void apply(SavedConfig& cfg, uint8_t slot) {
    if (!sceneUsed(cfg, slot)) return;
    cfg.currentScene = slot;
    put(cfg, slot);
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
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        s.layer[l] = cfg.layer[l];
        // A layer playing the scene's own voice takes a copy of it along.
        if (cfg.layer[l].voice == VOICE_SCENE) cfg.sceneVoices[slot][l] = layerSceneVoice(l);
    }
    s.pitch   = pitchGetOffset();
    s.balance = layerBalance();
    cfg.sceneUsed[slot] = true;
    cfg.currentScene    = slot;
    pending = NONE;
    storageSave(cfg);
}

// Field by field: memcmp would also compare the padding.
static bool fxEqual(const LayerFx& a, const LayerFx& b) {
    return a.cutoff == b.cutoff && a.resonance == b.resonance &&
           a.chorusRate == b.chorusRate && a.chorusDepth == b.chorusDepth &&
           a.chorusMix == b.chorusMix && a.delayMode == b.delayMode &&
           a.delaySync == b.delaySync && a.delayMs == b.delayMs &&
           a.delayFeedback == b.delayFeedback && a.delayMix == b.delayMix &&
           a.roomSize == b.roomSize && a.damping == b.damping &&
           a.reverbMix == b.reverbMix && a.sameAsA == b.sameAsA;
}

static bool layerEqual(const LayerCfg& a, const LayerCfg& b) {
    return a.mode == b.mode && a.voice == b.voice && a.channel == b.channel &&
           a.root == b.root && a.scale == b.scale && a.octave == b.octave &&
           (a.scale != Scale::Learned || a.learned == b.learned) &&
           a.level == b.level && a.shift == b.shift && a.wrap == b.wrap &&
           a.shiftSameAsA == b.shiftSameAsA && a.lowNote == b.lowNote &&
           a.lowNoteSameAsA == b.lowNoteSameAsA &&
           fxEqual(a.fx, b.fx);
}

bool sceneModified(const SavedConfig& cfg) {
    const Scene& s = sceneGet(cfg, cfg.currentScene);
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
