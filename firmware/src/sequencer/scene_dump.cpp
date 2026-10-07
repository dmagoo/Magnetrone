#include "scene_dump.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "layers.h"
#include "audio/voice.h"

// The JSON names, in enum order. docs/scene-format.md and
// tools/build_demos.py use the same ones.
static const char* VOICE_KEYS[VOICE_COUNT] = { "piano", "strings", "synth", "bass", "drums", "none",
                                               "ePiano", "organ", "brass", "mallets", "reed", "guitar" };
static const char* ROOT_KEYS[12] = { "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b" };
static const char* SCALE_KEYS[(uint8_t)Scale::COUNT] = {
    "major", "minor", "pentatonicMajor", "pentatonicMinor", "blues",
    "chromatic", "dorian", "mixolydian", "learned", "custom"
};
static const char* MODE_KEYS[] = { "on", "off", "sameAsA", "stack" };
static const char* WAVE_KEYS[WAVE_COUNT] = { "sine", "triangle", "saw", "square" };

static const char* pick(const char* const* keys, uint8_t n, uint8_t i) {
    return keys[i < n ? i : 0];
}

// A Custom scale's steps, "[0, 4, 7, 12, ...]".
static String stepsList(const int8_t* steps) {
    String t = "[";
    for (uint8_t i = 0; i < CUSTOM_SCALE_SLOTS; i++) {
        if (i) t += ", ";
        t += (int)steps[i];
    }
    return t + "]";
}

// One JSON object, built as text so an object with nothing in it can be
// left out of its parent.
class Obj {
public:
    explicit Obj(uint8_t depth) : depth_(depth) {}
    bool empty() const { return n_ == 0; }

    void raw(const char* key, const String& value) {
        body_ += n_++ ? ",\n" : "\n";
        for (uint8_t i = 0; i <= depth_; i++) body_ += "  ";
        body_ += '"';
        body_ += key;
        body_ += "\": ";
        body_ += value;
    }
    void num(const char* key, long v)          { raw(key, String(v)); }
    void str(const char* key, const char* v)   { raw(key, String('"') + v + '"'); }
    void flag(const char* key, bool v)         { raw(key, v ? "true" : "false"); }
    void obj(const char* key, const Obj& o)    { if (!o.empty()) raw(key, o.text()); }

    String text() const {
        if (n_ == 0) return "{}";
        String s = "{" + body_ + "\n";
        for (uint8_t i = 0; i < depth_; i++) s += "  ";
        return s + "}";
    }

private:
    uint8_t depth_;
    uint8_t n_ = 0;
    String  body_;
};

// Percentages with an Off end print "off" there, as the menus show them.
static String pctOr(uint8_t v, uint8_t offAt) {
    return v == offAt ? String("\"off\"") : String(v);
}

static String number(float v) {
    String s(v, 3);
    while (s.endsWith("0")) s.remove(s.length() - 1);
    if (s.endsWith(".")) s += "0";
    return s;
}

// A voice, leaving out what matches its base built-in. The base is always
// written: the rest is read against it.
static Obj voiceObj(const VoiceSlot& v, uint8_t depth) {
    VoiceSlot st = storageStockVoice(v.base);
    Obj o(depth);
    o.str("base", pick(VOICE_KEYS, VOICE_COUNT, v.base));
    uint8_t wave = v.wave & ~SLOT_HARMONICS_EDITED;
    if (wave != st.wave) o.str("wave", pick(WAVE_KEYS, WAVE_COUNT, wave));
    if (v.wave & SLOT_HARMONICS_EDITED) {
        String h = "[";
        for (uint8_t i = 0; i < NUM_SLOT_HARMONICS; i++) {
            if (i) h += ", ";
            h += v.harmonics[i];
        }
        o.raw("harmonics", h + "]");
    }
    if (v.attackMs   != st.attackMs)   o.num("attack",  v.attackMs);
    if (v.decayMs    != st.decayMs)    o.num("decay",   v.decayMs);
    if (v.sustainPct != st.sustainPct) o.num("sustain", v.sustainPct);
    if (v.releaseMs  != st.releaseMs)  o.num("release", v.releaseMs);
    if (v.noteMs     != st.noteMs)     o.num("length",  v.noteMs);

    const VoiceFilter& f = v.filter;
    const VoiceFilter& g = st.filter;
    Obj fo(depth + 1);
    if (f.cutoff     != g.cutoff)     fo.raw("cutoff", pctOr(f.cutoff, VOICE_FILTER_OFF));
    if (f.resonance  != g.resonance)  fo.num("resonance", f.resonance);
    if (f.amount     != g.amount)     fo.num("amount",    f.amount);
    if (f.attackMs   != g.attackMs)   fo.num("attack",    f.attackMs);
    if (f.decayMs    != g.decayMs)    fo.num("decay",     f.decayMs);
    if (f.sustainPct != g.sustainPct) fo.num("sustain",   f.sustainPct);
    if (f.releaseMs  != g.releaseMs)  fo.num("release",   f.releaseMs);
    o.obj("filter", fo);
    return o;
}

// Layer B's per-effect Same as A, written inside each effect.
static void sameAsA(Obj& o, const LayerFx& a, const LayerFx& f, FxId fx) {
    uint8_t bit = 1u << (uint8_t)fx;
    if ((a.sameAsA & bit) != (f.sameAsA & bit)) o.flag("sameAsA", a.sameAsA & bit);
}

static Obj layerObj(const LayerCfg& a, const LayerCfg& f, const VoiceSlot& sv,
                    bool isB, uint8_t depth) {
    Obj o(depth);
    if (a.mode != f.mode) o.str("mode", pick(MODE_KEYS, 4, (uint8_t)a.mode));
    if (a.voice != f.voice) {
        char custom[10];
        if (voiceIsCustomId(a.voice)) {
            snprintf(custom, sizeof(custom), "custom%u", (unsigned)(a.voice - VOICE_CUSTOM_FIRST + 1));
            o.str("voice", custom);
        } else if (a.voice == VOICE_SCENE) {
            o.str("voice", "sceneVoice");
        } else {
            o.str("voice", pick(VOICE_KEYS, VOICE_COUNT, a.voice));
        }
    }
    if (a.voice == VOICE_SCENE && sv.used) o.obj("sceneVoice", voiceObj(sv, depth + 1));
    if (a.channel != f.channel) {
        if (a.channel == LAYER_CHANNEL_AUTO) o.str("channel", "auto");
        else                                 o.num("channel", a.channel);
    }
    if (a.root  != f.root)  o.str("root",  pick(ROOT_KEYS, 12, (uint8_t)a.root));
    if (a.scale != f.scale) o.str("scale", pick(SCALE_KEYS, (uint8_t)Scale::COUNT, (uint8_t)a.scale));
    if (a.learned != f.learned) {
        String l = "[";
        bool first = true;
        for (uint8_t i = 0; i < 12; i++) {
            if (!(a.learned & (1u << i))) continue;
            if (!first) l += ", ";
            l += i;
            first = false;
        }
        o.raw("learned", l + "]");
    }
    if (scaleCustomSet(a.custom)) o.raw("custom", stepsList(a.custom));
    if (a.octave != f.octave) o.num("octave", a.octave);
    if (a.level  != f.level)  o.num("level",  a.level);
    if (a.shift  != f.shift)  o.num("shift",  a.shift);
    if (a.wrap   != f.wrap)   o.flag("wrap",  a.wrap);
    if (isB && a.shiftSameAsA != f.shiftSameAsA) o.flag("shiftSameAsA", a.shiftSameAsA);
    if (a.lowNote != f.lowNote) o.str("lowNote", a.lowNote == (uint8_t)LowNote::Outer ? "outer" : "inner");
    if (isB && a.lowNoteSameAsA != f.lowNoteSameAsA) o.flag("lowNoteSameAsA", a.lowNoteSameAsA);
    if (isB && a.rootSameAsA != f.rootSameAsA)       o.flag("rootSameAsA", a.rootSameAsA);
    if (isB && a.scaleSameAsA != f.scaleSameAsA)     o.flag("scaleSameAsA", a.scaleSameAsA);
    if (isB && a.voiceSameAsA != f.voiceSameAsA)     o.flag("voiceSameAsA", a.voiceSameAsA);
    if (isB && a.octaveSameAsA != f.octaveSameAsA)   o.flag("octaveSameAsA", a.octaveSameAsA);
    if (isB && a.turns != f.turns) {
        static const char* const TURNS_KEYS[] = { "together", "alternate", "custom" };
        o.str("turns", pick(TURNS_KEYS, 3, (uint8_t)a.turns));
    }
    if (a.turnLen != f.turnLen || a.turnMask != f.turnMask) {
        // As the Edit Turns screen shows it: the turn's number or - if
        // silent, then | if the cycle ends before turn 8.
        char p[TURN_MAX + 2];
        uint8_t n = (a.turnLen >= 1 && a.turnLen <= TURN_MAX) ? a.turnLen : TURN_MAX;
        uint8_t i = 0;
        for (; i < n; i++) p[i] = (a.turnMask & (1u << i)) ? (char)('1' + i) : '-';
        if (n < TURN_MAX) p[i++] = '|';
        p[i] = '\0';
        o.str("turnPattern", p);
    }

    const LayerFx& x = a.fx;
    const LayerFx& y = f.fx;
    Obj tone(depth + 1), chorus(depth + 1), delay(depth + 1), reverb(depth + 1);
    if (isB) {
        sameAsA(tone, x, y, FxId::Tone);
        sameAsA(chorus, x, y, FxId::Chorus);
        sameAsA(delay, x, y, FxId::Delay);
        sameAsA(reverb, x, y, FxId::Reverb);
    }
    if (x.cutoff    != y.cutoff)    tone.raw("cutoff", pctOr(x.cutoff, FX_CUTOFF_OFF));
    if (x.resonance != y.resonance) tone.num("resonance", x.resonance);
    if (x.chorusRate  != y.chorusRate)  chorus.num("rate",  x.chorusRate);
    if (x.chorusDepth != y.chorusDepth) chorus.num("depth", x.chorusDepth);
    if (x.chorusMix   != y.chorusMix)   chorus.raw("mix",   pctOr(x.chorusMix, 0));
    if (x.delayMode != y.delayMode)
        delay.str("mode", x.delayMode == (uint8_t)DelayMode::Free ? "free" : "sync");
    if (x.delaySync     != y.delaySync)     delay.str("sync",     delaySyncName(x.delaySync));
    if (x.delayMs       != y.delayMs)       delay.num("time",     x.delayMs);
    if (x.delayFeedback != y.delayFeedback) delay.num("feedback", x.delayFeedback);
    if (x.delayMix      != y.delayMix)      delay.raw("mix",      pctOr(x.delayMix, 0));
    if (x.roomSize  != y.roomSize)  reverb.num("roomSize", x.roomSize);
    if (x.damping   != y.damping)   reverb.num("damping",  x.damping);
    if (x.reverbMix != y.reverbMix) reverb.raw("mix",      pctOr(x.reverbMix, 0));
    o.obj("tone", tone);
    o.obj("chorus", chorus);
    o.obj("delay", delay);
    o.obj("reverb", reverb);
    return o;
}

static void dumpScene(const SavedConfig& cfg, uint8_t slot) {
    const Scene& s = cfg.scenes[slot];
    Scene f = storageFactoryScene();
    char name[16];
    if (slot == SCENE_DEFAULTS) snprintf(name, sizeof(name), "Defaults");
    else                        snprintf(name, sizeof(name), "Scene %u", (unsigned)slot);

    Obj o(0);
    o.str("name", name);
    if (fabsf(s.pitch - f.pitch) > 0.001f) o.raw("pitch", number(s.pitch));
    if (s.balance != f.balance) o.num("balance", s.balance);
    static const VoiceSlot NONE{};
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        const VoiceSlot& sv = (slot == SCENE_DEFAULTS) ? NONE : cfg.sceneVoices[slot][l];
        o.obj(l == LAYER_A ? "layerA" : "layerB",
              layerObj(s.layer[l], f.layer[l], sv, l == LAYER_B, 1));
    }
    Serial.printf("--- Scene %u%s ---\n", (unsigned)slot, slot == SCENE_DEFAULTS ? " (Defaults)" : "");
    Serial.println(o.text());
}

void sceneDump(const SavedConfig& cfg) {
    Serial.println("Saved scenes as JSON, factory values left out (docs/scene-format.md).");
    for (uint8_t i = 0; i < NUM_SCENES; i++) {
        if (cfg.sceneUsed[i]) dumpScene(cfg, i);
    }

    Obj customs(1);
    for (uint8_t i = 0; i < NUM_SAVED_VOICES; i++) {
        if (!cfg.customVoices[i].used) continue;
        char key[10];
        snprintf(key, sizeof(key), "custom%u", (unsigned)(i + 1));
        customs.raw(key, voiceObj(cfg.customVoices[i], 2).text());
    }
    if (!customs.empty()) {
        Obj o(0);
        o.obj("customVoices", customs);
        Serial.println("--- Custom voices ---");
        Serial.println(o.text());
    }

    Obj scales(1);
    for (uint8_t i = 0; i < NUM_CUSTOM_SCALES; i++) {
        if (!cfg.customScales[i].used) continue;
        char key[10];
        snprintf(key, sizeof(key), "custom%u", (unsigned)(i + 1));
        scales.raw(key, stepsList(cfg.customScales[i].steps));
    }
    if (!scales.empty()) {
        Obj o(0);
        o.obj("customScales", scales);
        Serial.println("--- Custom scales ---");
        Serial.println(o.text());
    }
    Serial.println("--- End ---");
}
