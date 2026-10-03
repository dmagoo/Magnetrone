#include "midi_in.h"
#include <Arduino.h>
#include "config.h"
#include "audio/audio.h"
#include "sequencer/scale.h"
#include "sequencer/pitch.h"
#include "sequencer/layers.h"
#include "audio/voice.h"

// Bytes handled per loop, so a burst of input cannot stall the platter or
// the sensors. The UART buffers the rest.
static const uint8_t  MAX_BYTES_PER_LOOP = 64;

// Volume from CC is saved once it has been still this long: a fader sends a
// stream of values, and each would otherwise be an EEPROM write.
static const uint32_t VOLUME_SAVE_DELAY_MS = 2000;

static const uint8_t  LEARN_NOTES = 7;

// Chord: keys pressed within this long of the first one count as one chord.
static const uint32_t CHORD_WINDOW_MS = 40;
static const uint8_t  CHORD_MAX_KEYS  = 4;

// Parser state: running status is kept, so a message may arrive without its
// status byte if the last one had the same.
static uint8_t  status   = 0;
static uint8_t  data[2]  = { 0, 0 };
static uint8_t  count    = 0;

static bool     changed      = false;
static bool     volumeDirty  = false;
static uint32_t volumeAtMs   = 0;

// Scale Learn: the last seven different pitch classes, oldest first, each
// with the note it was first played as (only used to find the lowest).
static uint8_t  learnPc[LEARN_NOTES];
static uint8_t  learnNote[LEARN_NOTES];
static uint8_t  learnCount = 0;

// Chord: the keys of the chord being collected, and when it closes.
static uint8_t  chordKeys[CHORD_MAX_KEYS];
static uint8_t  chordCount  = 0;
static uint32_t chordAtMs   = 0;
static uint8_t  chordLayers = 0;   // the layers its keys arrived for

void midiInReset() {
    learnCount = 0;
    chordCount = 0;
}

bool midiInTakeChanged() {
    bool c = changed;
    changed = false;
    return c;
}

// Which layers listen on `ch`: bit 0 = A, bit 1 = B. Layer B in Same as A
// plays A's settings, so a message for B alone would change hidden ones; it
// is left out, as the Aux Fns leave it alone.
static uint8_t listeners(const SavedConfig& cfg, uint8_t ch) {
    uint8_t m = 0;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) continue;
        if (cfg.midiInChannel[l] && cfg.midiInChannel[l] == ch) m |= (1u << l);
    }
    return m;
}

// --- MIDI Fn: Pitch ----------------------------------------------------------
// The key becomes the layer's root and octave: play a G3 and its root is G in
// octave 3. Whole semitones of the Pitch offset are dropped so the root plays
// the key itself; a microtonal remainder is kept.
static void keyPitch(SavedConfig& cfg, uint8_t layers, uint8_t note) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!(layers & (1u << l))) continue;
        cfg.layer[l].root   = (RootNote)(note % 12);
        cfg.layer[l].octave = (uint8_t)constrain((int)note / 12 - 1, 0, 7);   // C4 = 60
    }
    float off = pitchGetOffset();
    pitchSetOffset(off - roundf(off));
    changed = true;
}

// --- MIDI Fn: Shift ----------------------------------------------------------
// The key becomes the note of the layer's low track: the shift is set so the
// low end of the run plays that key's pitch class. A key outside the scale
// snaps to the nearest scale note (the lower one on a tie).
static void keyShift(SavedConfig& cfg, uint8_t layers, uint8_t note) {
    uint8_t want = note % 12;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!(layers & (1u << l))) continue;
        // Same rules as the Aux Shift Fns: B playing A's shift leaves a hidden
        // setting alone. Drums have no pitch to match.
        if (layerShiftSource(cfg, l) != l) continue;
        if (voiceIsKit(layerVoice(cfg, l))) continue;

        const LayerCfg& lc = cfg.layer[l];
        uint8_t best = 0, bestDist = 12;
        for (uint8_t s = 0; s < NUM_HALL_SENSORS; s++) {
            // Degree s is what the low track plays at shift s.
            uint8_t pc   = scaleNote(lc.root, lc.scale, lc.learned, lc.custom, s, 0) % 12;
            uint8_t d    = (uint8_t)((pc + 12 - want) % 12);
            uint8_t dist = min(d, (uint8_t)(12 - d));
            if (dist < bestDist) { bestDist = dist; best = s; }
        }
        cfg.layer[l].shift = best;
        changed = true;
    }
}

// --- MIDI Fn: Scale Learn ----------------------------------------------------
// Seven different keys make a 7-note scale, compared by pitch class (C4 and
// C5 are the same key; a repeat does not count). It is a rolling set: past
// seven, each new key replaces the oldest, so any seven keys in a row define
// the scale. The root is the lowest key actually played among the seven.
// Until seven have been collected, the current scale keeps playing.
static void keyLearn(SavedConfig& cfg, uint8_t layers, uint8_t note) {
    uint8_t pc = note % 12;
    for (uint8_t i = 0; i < learnCount; i++) if (learnPc[i] == pc) return;

    if (learnCount == LEARN_NOTES) {
        for (uint8_t i = 1; i < LEARN_NOTES; i++) {
            learnPc[i - 1]   = learnPc[i];
            learnNote[i - 1] = learnNote[i];
        }
        learnCount--;
    }
    learnPc[learnCount]   = pc;
    learnNote[learnCount] = note;
    learnCount++;
    if (learnCount < LEARN_NOTES) return;

    uint8_t low = 0;
    for (uint8_t i = 1; i < LEARN_NOTES; i++) if (learnNote[i] < learnNote[low]) low = i;
    uint8_t root = learnPc[low];

    uint16_t mask = 0;
    for (uint8_t i = 0; i < LEARN_NOTES; i++) mask |= (uint16_t)(1u << ((learnPc[i] + 12 - root) % 12));
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!(layers & (1u << l))) continue;
        cfg.layer[l].learned = scaleMaskClean(mask);
        cfg.layer[l].root    = (RootNote)root;
        cfg.layer[l].scale   = Scale::Learned;
    }
    changed = true;
}

// --- MIDI Fn: Chord ----------------------------------------------------------
// Single-finger chords, as on arranger keyboards (Yamaha's Single Finger):
// the highest key is the root, and extra keys to its left pick the scale.
//   root alone                      Major
//   + a black key to its left       Minor
//   + a white key to its left       Mixolydian (a 7th chord)
//   + a black and a white key       Dorian (a minor 7th)
// The root key also sets the octave, as the Pitch Fn does.
static void keyChord(uint8_t layers, uint8_t note) {
    if (chordCount == 0) { chordAtMs = millis() + CHORD_WINDOW_MS; chordLayers = 0; }
    chordLayers |= layers;
    if (chordCount < CHORD_MAX_KEYS) chordKeys[chordCount++] = note;
}

static bool isBlack(uint8_t note) {
    switch (note % 12) { case 1: case 3: case 6: case 8: case 10: return true; }
    return false;
}

static void chordClose(SavedConfig& cfg) {
    uint8_t root = chordKeys[0];
    for (uint8_t i = 1; i < chordCount; i++) if (chordKeys[i] > root) root = chordKeys[i];
    bool black = false, white = false;
    for (uint8_t i = 0; i < chordCount; i++) {
        if (chordKeys[i] == root) continue;
        if (isBlack(chordKeys[i])) black = true;
        else                       white = true;
    }
    chordCount = 0;

    Scale scale = (black && white) ? Scale::Dorian
                : black            ? Scale::Minor
                : white            ? Scale::Mixolydian
                :                    Scale::Major;
    keyPitch(cfg, chordLayers, root);
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (chordLayers & (1u << l)) cfg.layer[l].scale = scale;
    }
    changed = true;
}

// An effects CC, on each listening layer. Layer B with that effect set to
// Same as A plays A's, so its own is left alone rather than changed unheard.
static bool fxCc(SavedConfig& cfg, uint8_t layers, uint8_t cc, uint8_t value) {
    FxId fx;
    uint8_t LayerFx::* field;
    uint8_t top = 100;   // what CC 127 sets, so the whole knob does something
    switch (cc) {
        case MIDI_CC_CUTOFF:    fx = FxId::Tone;   field = &LayerFx::cutoff;    break;
        case MIDI_CC_RESONANCE: fx = FxId::Tone;   field = &LayerFx::resonance; break;
        case MIDI_CC_CHORUS:    fx = FxId::Chorus; field = &LayerFx::chorusMix; break;
        case MIDI_CC_DELAY:     fx = FxId::Delay;  field = &LayerFx::delayMix;  break;
        case MIDI_CC_DELAY_FEEDBACK:
            fx = FxId::Delay; field = &LayerFx::delayFeedback; top = FX_FEEDBACK_MAX; break;
        case MIDI_CC_REVERB:    fx = FxId::Reverb; field = &LayerFx::reverbMix; break;
        default: return false;
    }
    uint8_t pct = (uint8_t)(((uint16_t)value * top + 63) / 127);
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if ((layers & (1u << l)) && layerFxSource(cfg, l, fx) == l) cfg.layer[l].fx.*field = pct;
    }
    layersApplyEffects(cfg);
    return true;
}

// -----------------------------------------------------------------------------

static void handle(SavedConfig& cfg, uint8_t type, uint8_t ch, uint8_t d0, uint8_t d1) {
    uint8_t layers = listeners(cfg, ch);
    if (!layers) return;

    switch (type) {
        case 0x90:
            if (d1 == 0) break;   // velocity 0 is a Note Off; keys act on press only
            switch ((MidiFn)cfg.midiFn) {
                case MidiFn::Pitch:      keyPitch(cfg, layers, d0);  break;
                case MidiFn::Shift:      keyShift(cfg, layers, d0);  break;
                case MidiFn::ScaleLearn: keyLearn(cfg, layers, d0);  break;
                case MidiFn::Chord:      keyChord(layers, d0);       break;
                default: break;
            }
            break;

        case 0xB0:
            if (d0 == MIDI_CC_VOLUME) {
                cfg.volume = (float)d1 / 127.0f;
                audioSetVolume(cfg.volume);
                volumeDirty = true;
                volumeAtMs  = millis();
                changed = true;
            } else if (d0 == MIDI_CC_OCTAVE) {
                for (uint8_t l = 0; l < NUM_LAYERS; l++) {
                    if (layers & (1u << l))
                        cfg.layer[l].octave = (uint8_t)((uint16_t)d1 * 8 / 128);   // 0-127 onto 0-7
                }
                changed = true;
            } else if (cfg.midiCc && fxCc(cfg, layers, d0, d1)) {
                changed = true;
            }
            break;

        case 0xE0: {
            // 14-bit, centre 8192: a temporary offset that springs back.
            int bend = (int)(((uint16_t)d1 << 7) | d0) - 8192;
            pitchSetWheel((float)bend / 8192.0f * MIDI_BEND_IN_RANGE);
            break;
        }

        default:
            break;
    }
}

void midiInUpdate(SavedConfig& cfg) {
    for (uint8_t n = 0; n < MAX_BYTES_PER_LOOP && Serial1.available(); n++) {
        uint8_t b = (uint8_t)Serial1.read();
        if (b >= 0xF8) continue;               // real-time: can arrive mid-message
        if (b & 0x80) {
            // A channel message starts; system messages (SysEx and the like)
            // cancel running status and their data is skipped.
            status = (b < 0xF0) ? b : 0;
            count  = 0;
            continue;
        }
        if (!status) continue;

        data[count++] = b;
        uint8_t type = status & 0xF0;
        uint8_t need = (type == 0xC0 || type == 0xD0) ? 1 : 2;
        if (count < need) continue;
        count = 0;
        handle(cfg, type, (uint8_t)((status & 0x0F) + 1), data[0], data[1]);
    }

    if (chordCount && (int32_t)(millis() - chordAtMs) >= 0) chordClose(cfg);

    if (volumeDirty && millis() - volumeAtMs >= VOLUME_SAVE_DELAY_MS) {
        volumeDirty = false;
        storageSave(cfg);
    }
}
