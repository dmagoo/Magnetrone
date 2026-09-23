#include "sequencer.h"
#include <Arduino.h>
#include "scale.h"
#include "sensors/hall.h"
#include "midi/midi.h"
#include "config.h"

struct NoteState {
    bool     active;
    // The note number that was actually EMITTED, after the global pitch offset
    // was applied. Stored rather than recomputed, because the offset can move
    // between Note On and Note Off and the two must match or the note hangs.
    uint8_t  note;
    uint32_t offTime;   // millis() when Note Off should fire
};

static NoteState notes[NUM_HALL_SENSORS] = {};

void sequencerUpdate(const SavedConfig& cfg) {
    uint32_t now = millis();

    // fire pending Note Offs
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        if (notes[i].active && now >= notes[i].offTime) {
            midiNoteOff(notes[i].note);
            notes[i].active = false;
        }
    }

    // check for new triggers
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        if (!hallTriggered(i)) continue;

        // apply sensor phase shift
        uint8_t degree = ((int8_t)i - cfg.sensorShift + NUM_HALL_SENSORS) % NUM_HALL_SENSORS;
        uint8_t note   = scaleNote(cfg.root, cfg.scale, degree, cfg.octave);

        // retrigger: cancel existing note if active
        if (notes[i].active) {
            midiNoteOff(notes[i].note);
        }

        notes[i].note    = midiNoteOn(note, 127);
        notes[i].active  = true;
        notes[i].offTime = now + NOTE_DURATION_MS;
    }
}
