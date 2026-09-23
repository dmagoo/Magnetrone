#include "sequencer.h"
#include <Arduino.h>
#include "scale.h"
#include "layers.h"
#include "sensors/hall.h"
#include "midi/midi.h"
#include "config.h"

struct NoteState {
    bool     active;
    // Everything the Note Off must match, captured at Note On. The note number
    // is the one actually EMITTED, after the global pitch offset; the offset,
    // the layer's channel and even its voice can all change mid-note, and the
    // Note Off has to go where the Note On went or the note hangs.
    uint8_t  note;
    uint8_t  channel;
    uint32_t offTime;   // millis() when Note Off should fire
};

// One slot per sensor per layer: the same sensor can have a Layer A note still
// sounding when a reversed magnet fires its Layer B note.
static NoteState notes[NUM_LAYERS][NUM_HALL_SENSORS] = {};

static void noteOff(uint8_t layer, uint8_t i) {
    midiNoteOff(layer, notes[layer][i].channel, notes[layer][i].note);
    notes[layer][i].active = false;
}

void sequencerUpdate(const SavedConfig& cfg) {
    uint32_t now = millis();

    // fire pending Note Offs
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
            if (notes[l][i].active && now >= notes[l][i].offTime) noteOff(l, i);
        }
    }

    // check for new triggers
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        HallPole pole = hallTrigger(i);
        if (pole == HallPole::None) continue;

        uint8_t l = (pole == HallPole::Normal) ? LAYER_A : LAYER_B;
        if (!layerActive(cfg, l)) continue;

        const LayerCfg& layer = layerEffective(cfg, l);
        float gain = layerGain(cfg, l);
        if (gain <= 0.0f) continue;

        // apply sensor phase shift
        uint8_t degree = ((int8_t)i - cfg.sensorShift + NUM_HALL_SENSORS) % NUM_HALL_SENSORS;
        int octave = constrain((int)cfg.octave + layer.octaveOffset, 0, 9);
        uint8_t note = scaleNote(cfg.root, cfg.scale, degree, (uint8_t)octave);

        // retrigger: cancel existing note if active
        if (notes[l][i].active) noteOff(l, i);

        // Level rides on velocity, so the internal synth and external synths
        // both follow it. 1 is the floor because velocity 0 means Note Off.
        uint8_t velocity = (uint8_t)constrain((int)lroundf(127.0f * gain), 1, 127);
        uint8_t channel  = layerChannel(cfg, l);

        notes[l][i].note    = midiNoteOn(l, channel, note, velocity);
        notes[l][i].channel = channel;
        notes[l][i].active  = true;
        notes[l][i].offTime = now + layerVoice(cfg, l).noteMs;
    }
}
