#include "sequencer.h"
#include <Arduino.h>
#include "scale.h"
#include "layers.h"
#include "sensors/hall.h"
#include "midi/midi.h"
#include "motion/bar.h"
#include "motion/stepper.h"
#include "config.h"

struct NoteState {
    bool     active;
    // Everything the Note Off must match, captured at Note On. The note number
    // is the one actually EMITTED, after the global pitch offset; the offset,
    // the layer's channel and even its voice can all change mid-note, and the
    // Note Off has to go where the Note On went or the note hangs.
    uint8_t  note;
    uint8_t  channel;
    bool     kit;       // a drum hit: its off goes to midiDrumOff()
    uint32_t offTime;   // millis() when Note Off should fire
};

// One slot per sensor per layer: the same sensor can have a Layer A note still
// sounding when a reversed magnet fires its Layer B note.
static NoteState notes[NUM_LAYERS][NUM_HALL_SENSORS] = {};

static uint8_t trackMask = 0xFF;

void sequencerSetTrackMask(uint8_t mask) {
    trackMask = mask;
}

static void noteOff(uint8_t layer, uint8_t i) {
    NoteState& n = notes[layer][i];
    if (n.kit) midiDrumOff(n.channel, n.note);
    else       midiNoteOff(layer, n.channel, n.note);
    n.active = false;
}

static void noteOn(const SavedConfig& cfg, uint8_t l, uint8_t i, uint32_t now);

void sequencerUpdate(const SavedConfig& cfg) {
    uint32_t now = millis();

    // fire pending Note Offs
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
            if (notes[l][i].active && now >= notes[l][i].offTime) noteOff(l, i);
        }
    }

    // check for new triggers. A magnet plays the layer of its pole, or both
    // layers when Layer B is on Stack. With Layer Turns on Alternate, only
    // the magnets of the layer whose revolution it is are heard: A on even
    // revolutions, B on odd.
    bool    stack     = (cfg.layer[LAYER_B].mode == LayerMode::Stack);
    bool    alternate = (cfg.layer[LAYER_B].turns == LayerTurns::Alternate);
    uint8_t turn      = barOddRevAt(stepperPosition()) ? LAYER_B : LAYER_A;
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        HallPole pole = hallTrigger(i);
        if (pole == HallPole::None) continue;
        if (!(trackMask & (1u << i))) continue;   // muted in Placement Mode

        uint8_t poleLayer = (pole == HallPole::Normal) ? LAYER_A : LAYER_B;
        if (alternate && poleLayer != turn) continue;
        for (uint8_t l = 0; l < NUM_LAYERS; l++) {
            if (stack || l == poleLayer) noteOn(cfg, l, i, now);
        }
    }
}

// One layer's note for sensor i, if that layer is playing.
static void noteOn(const SavedConfig& cfg, uint8_t l, uint8_t i, uint32_t now) {
    if (!layerActive(cfg, l)) return;

    const LayerCfg& layer = layerEffective(cfg, l);
    if (voiceIsSilent(layerVoice(cfg, l))) return;   // voice None
    float gain = layerGain(cfg, l);
    if (gain <= 0.0f) return;

    // The layer's Track Shift (and, without Wrap, octave carry) is
    // resolved in layers.cpp.
    uint8_t degree = layerDegree(cfg, l, i);

    // retrigger: cancel existing note if active
    if (notes[l][i].active) noteOff(l, i);

    // Level rides on velocity, so the internal synth and external synths
    // both follow it. 1 is the floor because velocity 0 means Note Off.
    uint8_t velocity = (uint8_t)constrain((int)lroundf(127.0f * gain), 1, 127);
    uint8_t channel  = layerChannel(cfg, l);
    bool    kit      = voiceIsKit(layerVoice(cfg, l));

    if (kit) {
        // Track Shift rotates the kit around the sensors, so a drum can be
        // moved to a busier track. layerDegree() always wraps for a kit,
        // whatever the layer's Wrap setting: one drum per sensor.
        notes[l][i].note = midiDrumOn(channel, degree, velocity);
    } else {
        uint8_t note = scaleNote(layer.root, layer.scale, layer.learned, degree,
                                 (uint8_t)constrain(layer.octave, 0, 9));
        notes[l][i].note = midiNoteOn(l, channel, note, velocity);
    }
    notes[l][i].kit     = kit;
    notes[l][i].channel = channel;
    notes[l][i].active  = true;
    notes[l][i].offTime = now + layerVoice(cfg, l).noteMs;
}
