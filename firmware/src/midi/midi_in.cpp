#include "midi_in.h"
#include <Arduino.h>
#include "config.h"
#include "audio/audio.h"
#include "sequencer/scale.h"
#include "sequencer/pitch.h"
#include "sequencer/layers.h"
#include "audio/voice.h"
#include "audio/kit.h"

// Bytes handled per loop, so a burst of input cannot stall the platter or
// the sensors. The UART buffers the rest.
static const uint8_t  MAX_BYTES_PER_LOOP = 64;

// Volume from CC is saved once it has been still this long: a fader sends a
// stream of values, and each would otherwise be an EEPROM write.
static const uint32_t VOLUME_SAVE_DELAY_MS = 2000;

// Set Scale: keys pressed within this long of the first one count as one chord.
// Fingered: keys let go within this long of each other count as one release.
static const uint32_t CHORD_WINDOW_MS = 40;
static const uint8_t  CHORD_MAX_KEYS  = 4;

// Scale Learn, Fingered and One Finger write one note per track into the
// Custom scale.
static_assert(NUM_HALL_SENSORS == CUSTOM_SCALE_SLOTS, "one Custom slot per track");
static const uint8_t  TRACKS = NUM_HALL_SENSORS;

// Parser state: running status is kept, so a message may arrive without its
// status byte if the last one had the same.
static uint8_t  status   = 0;
static uint8_t  data[2]  = { 0, 0 };
static uint8_t  count    = 0;

static bool     changed      = false;
static bool     volumeDirty  = false;
static uint32_t volumeAtMs   = 0;

// Set Scale: the keys of the chord being collected, and when it closes.
static uint8_t  chordKeys[CHORD_MAX_KEYS];
static uint8_t  chordCount  = 0;
static uint32_t chordAtMs   = 0;
static uint8_t  chordLayers = 0;   // the layers its keys arrived for

// Fingered: each layer's held keys, in the order pressed, and a release
// waiting to see whether the rest of the chord follows.
static uint8_t  heldKeys[NUM_LAYERS][TRACKS];
static uint8_t  heldCount[NUM_LAYERS]   = { 0 };
static uint8_t  releaseLayers           = 0;
static uint32_t releaseAtMs[NUM_LAYERS] = { 0 };

// Play Along: the channel each layer's sounding key came in on, 0 = none. A
// Note Off goes by this, not by who listens now, so a key cannot hang when the
// channel or the layer's mode changes while it is held.
static uint8_t  keyChannel[NUM_LAYERS][128] = {};

static void keysOff() {
    audioKeysOff();
    memset(keyChannel, 0, sizeof(keyChannel));
}

void midiInReset() {
    chordCount    = 0;
    releaseLayers = 0;
    for (uint8_t l = 0; l < NUM_LAYERS; l++) heldCount[l] = 0;
    keysOff();
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

// --- The arm, track by track (Scale Learn, Fingered) -------------------------
// Track t counts from the layer's Low Note end. Notes are semitones from the
// layer's root and octave, so Root Note and Octave still transpose them.

static uint8_t trackDegree(const SavedConfig& cfg, uint8_t l, uint8_t t) {
    bool outer = cfg.layer[layerLowNoteSource(cfg, l)].lowNote == (uint8_t)LowNote::Outer;
    return layerDegree(cfg, l, outer ? (uint8_t)(TRACKS - 1 - t) : t);
}

static int layerBase(const LayerCfg& lc) {
    return 12 + 12 * constrain((int)lc.octave, 0, 9) + (int)lc.root;   // as scaleNote()
}

// Into the range a Custom slot holds, by octaves, so the note name is kept.
static int8_t foldStep(int st) {
    while (st > CUSTOM_STEP_MAX) st -= 12;
    while (st < CUSTOM_STEP_MIN) st += 12;
    return (int8_t)st;
}

// What track t plays now, whatever the scale.
static int armStep(const SavedConfig& cfg, uint8_t l, uint8_t t) {
    const LayerCfg& lc = cfg.layer[l];
    return scaleStep(lc.scale, lc.learned, lc.custom, trackDegree(cfg, l, t));
}

// Makes the layer's scale Custom with track t playing arm[t], at the current
// Shift and Wrap: each track's degree picks its slot, and a degree past the
// last slot (No Wrap) plays that slot an octave up. The tracks cover every
// slot once, so the whole map is replaced.
static void writeArm(SavedConfig& cfg, uint8_t l, const int arm[TRACKS]) {
    LayerCfg& lc = cfg.layer[l];
    for (uint8_t t = 0; t < TRACKS; t++) {
        uint8_t d = trackDegree(cfg, l, t);
        lc.custom[d % CUSTOM_SCALE_SLOTS] = foldStep(arm[t] - 12 * (d / CUSTOM_SCALE_SLOTS));
    }
    lc.scale = Scale::Custom;
}

// --- MIDI Fn: Scale Learn ----------------------------------------------------
// Live entry of a Custom scale, one key per track. A key goes in at the far
// end of the arm and everything slides one track toward the Low Note end, the
// oldest dropping off there: play eight keys and they lie low to high in the
// order played. Repeats are fine and nothing is sorted. It starts from the
// scale already playing, so the first key already changes something.
//
// The root stays on its track (slot 1's): whatever slides onto it is the new
// root. Root Note moves to it the shortest way, Octave carrying when it
// crosses C, and every slot moves the other way, so no note changes.
static void keyLearn(SavedConfig& cfg, uint8_t layers, uint8_t note) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!(layers & (1u << l))) continue;
        if (voiceIsKit(layerVoice(cfg, l))) continue;   // drums have no scale
        LayerCfg& lc = cfg.layer[l];

        int arm[TRACKS];
        for (uint8_t t = 0; t + 1 < TRACKS; t++) arm[t] = armStep(cfg, l, t + 1);
        arm[TRACKS - 1] = (int)note - layerBase(lc);
        writeArm(cfg, l, arm);

        int up = ((lc.custom[0] % 12) + 12) % 12;
        if (up > 6) up -= 12;
        int oldBase = layerBase(lc);
        int r = (int)lc.root + up;
        int o = (int)lc.octave + (r < 0 ? -1 : r > 11 ? 1 : 0);
        lc.root   = (RootNote)((r + 12) % 12);
        lc.octave = (uint8_t)constrain(o, 0, 7);
        int moved = layerBase(lc) - oldBase;
        for (uint8_t s = 0; s < CUSTOM_SCALE_SLOTS; s++) lc.custom[s] = foldStep(lc.custom[s] - moved);
        changed = true;
    }
}

// --- MIDI Fn: Fingered -------------------------------------------------------
// The keys held down are the scale, low to high from the Low Note track; the
// lowest is the root and sets the octave. Fewer than eight repeat up by
// octaves to the top of the Custom range, then start again from the bottom.
// Past eight, later keys are ignored. Letting keys go shrinks the scale, but
// lifting the whole hand keeps the last chord: releases close together are
// one, and one that leaves nothing held changes nothing.
//
// One Finger fills the arm the same way, from the chord's notes.
static void keysToArm(SavedConfig& cfg, uint8_t l, const uint8_t* keys, uint8_t n) {
    if (!n || voiceIsKit(layerVoice(cfg, l))) return;
    LayerCfg& lc = cfg.layer[l];

    uint8_t low = keys[0];
    for (uint8_t i = 1; i < n; i++) if (keys[i] < low) low = keys[i];
    lc.root   = (RootNote)(low % 12);
    lc.octave = (uint8_t)constrain((int)low / 12 - 1, 0, 7);   // C4 = 60, as Pitch
    int base = layerBase(lc);

    // Folding can reorder keys a long way apart, so sort after it.
    int st[TRACKS];
    for (uint8_t i = 0; i < n; i++) {
        int v = foldStep((int)keys[i] - base);
        uint8_t j = i;
        for (; j > 0 && st[j - 1] > v; j--) st[j] = st[j - 1];
        st[j] = v;
    }

    int arm[TRACKS];
    uint8_t t = 0;
    for (uint8_t up = 0; t < TRACKS; up++) {
        bool any = false;
        for (uint8_t i = 0; i < n && t < TRACKS; i++) {
            int v = st[i] + 12 * up;
            if (v > CUSTOM_STEP_MAX) continue;
            arm[t++] = v;
            any = true;
        }
        if (!any) up = 255;   // past the top: back to the held keys (wraps to 0)
    }
    writeArm(cfg, l, arm);
    changed = true;
}

static void fingeredApply(SavedConfig& cfg, uint8_t l) {
    keysToArm(cfg, l, heldKeys[l], heldCount[l]);
}

static void fingeredOn(SavedConfig& cfg, uint8_t layers, uint8_t note) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!(layers & (1u << l))) continue;
        releaseLayers &= (uint8_t)~(1u << l);   // a press shows at once
        bool held = false;
        for (uint8_t i = 0; i < heldCount[l]; i++) if (heldKeys[l][i] == note) held = true;
        if (!held && heldCount[l] < TRACKS) heldKeys[l][heldCount[l]++] = note;
        fingeredApply(cfg, l);
    }
}

static void fingeredOff(uint8_t layers, uint8_t note) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!(layers & (1u << l))) continue;
        for (uint8_t i = 0; i < heldCount[l]; i++) {
            if (heldKeys[l][i] != note) continue;
            for (uint8_t j = i + 1; j < heldCount[l]; j++) heldKeys[l][j - 1] = heldKeys[l][j];
            heldCount[l]--;
            releaseLayers |= (uint8_t)(1u << l);
            releaseAtMs[l] = millis() + CHORD_WINDOW_MS;
            break;
        }
    }
}

// --- MIDI Fn: Set Scale and One Finger ---------------------------------------
// Single-finger chords, as on arranger keyboards (Yamaha's Single Finger):
// the highest key is the root, and extra keys to its left pick the chord.
//                                   Set Scale     One Finger
//   root alone                      Major         major      (C E G)
//   + a black key to its left       Minor         minor      (C Eb G)
//   + a white key to its left       Mixolydian    7th        (C E G Bb)
//   + a black and a white key       Dorian        minor 7th  (C Eb G Bb)
// Set Scale sets that scale; One Finger puts the chord's notes on the arm,
// as Fingered would with them held. The root key also sets the octave, as
// the Pitch Fn does. The chord stays until the next one.
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

    if ((MidiFn)cfg.midiFn == MidiFn::OneFinger) {
        uint8_t keys[4] = { root, (uint8_t)(root + (black ? 3 : 4)), (uint8_t)(root + 7),
                            (uint8_t)(root + 10) };
        uint8_t n = white ? 4 : 3;
        for (uint8_t i = 1; i < n; i++) if (keys[i] > 127) keys[i] -= 12;
        for (uint8_t l = 0; l < NUM_LAYERS; l++) {
            if (chordLayers & (1u << l)) keysToArm(cfg, l, keys, n);
        }
        return;
    }

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

// --- MIDI Fn: Play Along -----------------------------------------------------
// Keys play the layer's voice, as played: no Root, Octave or scale, but the
// Pitch offset and the bend wheel apply, so they stay in tune with the table.
// The table keeps playing; keys share the layer's synth voices with the
// magnets. With both layers on one channel only Layer A plays. On a drum layer
// a key plays the drum with its GM note number; other keys are silent. Key
// velocity is scaled by the layer's Level and A/B Balance, as magnets are.
// Nothing goes to MIDI out.
static void playOn(const SavedConfig& cfg, uint8_t layers, uint8_t ch, uint8_t note,
                   uint8_t velocity) {
    uint8_t l = (layers & (1u << LAYER_A)) ? LAYER_A : LAYER_B;
    if (!layerActive(cfg, l)) return;
    const Voice& voice = layerVoice(cfg, l);
    if (voiceIsSilent(voice)) return;
    float gain = layerGain(cfg, l);
    if (gain <= 0.0f) return;
    uint8_t vel = (uint8_t)constrain((int)lroundf(velocity * gain), 1, 127);

    if (voiceIsKit(voice)) {
        for (uint8_t s = 0; s < NUM_HALL_SENSORS; s++) {
            if (KIT_GM_NOTE[s] == note) audioDrumHit(s, vel);   // one-shot, no off
        }
        return;
    }
    if (keyChannel[l][note]) audioKeyOff(l, note);   // a repeat without an off
    audioKeyOn(l, note, vel);
    keyChannel[l][note] = ch;
}

static void playOff(uint8_t ch, uint8_t note) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (keyChannel[l][note] != ch) continue;
        audioKeyOff(l, note);
        keyChannel[l][note] = 0;
    }
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
    // Velocity 0 is a Note Off. Play Along's keys are released whoever
    // listens now.
    bool off = (type == 0x80) || (type == 0x90 && d1 == 0);
    if (off) playOff(ch, d0);

    uint8_t layers = listeners(cfg, ch);
    if (!layers) return;

    switch (type) {
        case 0x80:
        case 0x90:
            // Of the others, only Fingered cares about releases.
            if (off) {
                if ((MidiFn)cfg.midiFn == MidiFn::Fingered) fingeredOff(layers, d0);
                break;
            }
            switch ((MidiFn)cfg.midiFn) {
                case MidiFn::Pitch:      keyPitch(cfg, layers, d0);    break;
                case MidiFn::Shift:      keyShift(cfg, layers, d0);    break;
                case MidiFn::ScaleLearn: keyLearn(cfg, layers, d0);    break;
                case MidiFn::SetScale:
                case MidiFn::OneFinger:  keyChord(layers, d0);         break;
                case MidiFn::Fingered:   fingeredOn(cfg, layers, d0);  break;
                case MidiFn::PlayAlong:  playOn(cfg, layers, ch, d0, d1); break;
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

    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        if (!(releaseLayers & (1u << l)) || (int32_t)(millis() - releaseAtMs[l]) < 0) continue;
        releaseLayers &= (uint8_t)~(1u << l);
        fingeredApply(cfg, l);   // with nothing held, keeps the last chord
    }

    if (volumeDirty && millis() - volumeAtMs >= VOLUME_SAVE_DELAY_MS) {
        volumeDirty = false;
        storageSave(cfg);
    }
}
