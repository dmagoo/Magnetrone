#include "scene_code.h"
#include <stddef.h>
#include <string.h>
#include "layers.h"
#include "audio/voice.h"

const char SCENE_CODE_ALPHABET[33] = "AHJLMTWXY347CEFKNPR96DQGUVB8S5Z2";

static const uint8_t CODE_VERSION = 1;    // what sceneCodeEncode() writes
static const uint8_t VERSION_BITS = 4;
static const uint8_t CHAR_BITS    = 5;
static const uint8_t CODE_BITS    = SCENE_CODE_MAX * CHAR_BITS;

// The values a code holds, one byte each, so a field is an offset.
struct CodeFields {
    uint8_t root[NUM_LAYERS];
    uint8_t octave[NUM_LAYERS];
    uint8_t scale[NUM_LAYERS];
    uint8_t voice[NUM_LAYERS];
    uint8_t shift[NUM_LAYERS];
    uint8_t lowNote[NUM_LAYERS];
    uint8_t wrap[NUM_LAYERS];
    uint8_t shiftSameAsA;     // Layer B
    uint8_t lowNoteSameAsA;   // Layer B
    uint8_t mode[NUM_LAYERS];
    uint8_t turns;            // Layer B
};

// One field of a version's layout: where it sits in CodeFields, its width,
// how many values it has, and its default, which is stored as 0.
struct FieldSpec {
    uint8_t offset;
    uint8_t bits;
    uint8_t count;
    uint8_t def;
};

#define AT(f, l) (uint8_t)(offsetof(CodeFields, f) + (l))

// --- Version 1 ----------------------------------------------------------------
// Pinned: never change these. A change to a field or a default is a new
// version, with its own table, and this one stays to read old codes.
static const FieldSpec V1_FIELDS[] = {
    { AT(root, 0),           4, 12, 0 },   // C
    { AT(root, 1),           4, 12, 0 },   // C
    { AT(octave, 0),         3,  8, 4 },
    { AT(octave, 1),         3,  8, 3 },
    { AT(scale, 0),          3,  8, 0 },   // Major
    { AT(scale, 1),          3,  8, 0 },   // Major
    { AT(voice, 0),          3,  6, 0 },   // Piano
    { AT(voice, 1),          3,  6, 4 },   // Drums
    { AT(shift, 0),          3,  8, 0 },
    { AT(shift, 1),          3,  8, 0 },
    { AT(lowNote, 0),        1,  2, 0 },   // Inner
    { AT(lowNote, 1),        1,  2, 0 },   // Inner
    { AT(wrap, 0),           1,  2, 0 },   // No Wrap
    { AT(wrap, 1),           1,  2, 0 },   // No Wrap
    { AT(shiftSameAsA, 0),   1,  2, 0 },
    { AT(lowNoteSameAsA, 0), 1,  2, 0 },
    { AT(mode, 0),           1,  2, 0 },   // On, Off
    { AT(mode, 1),           2,  4, 0 },   // On, Off, Same as A, Stack
    { AT(turns, 0),          1,  2, 0 },   // Together
};
static const uint8_t V1_COUNT = sizeof(V1_FIELDS) / sizeof(V1_FIELDS[0]);

// Version 1's ranges are today's. If one of these fails, the code needs a
// new version before the change.
static_assert((uint8_t)Scale::Learned == 8, "built-in scales changed: new code version");
static_assert(VOICE_COUNT == 6, "built-in voices changed: new code version");
static_assert(NUM_HALL_SENSORS == 8, "shift range changed: new code version");
static_assert((uint8_t)LayerMode::Stack == 3, "layer modes changed: new code version");

// Everything outside a version 1 code, as it was when version 1 was made.
static void v1Pinned(Scene& s) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        LayerCfg& c = s.layer[l];
        c.channel   = LAYER_CHANNEL_AUTO;
        c.learned   = 0;
        c.level     = 100;
        c.custom[0] = CUSTOM_UNSET;
        for (uint8_t i = 1; i < CUSTOM_SCALE_SLOTS; i++) c.custom[i] = 0;
        LayerFx& f = c.fx;
        f.cutoff        = 100;   // Off
        f.resonance     = 0;
        f.chorusRate    = 30;
        f.chorusDepth   = 50;
        f.chorusMix     = 0;
        f.delayMode     = 0;     // Sync
        f.delaySync     = 1;     // 1/2 beat
        f.delayMs       = 300;
        f.delayFeedback = 30;
        f.delayMix      = 0;
        f.roomSize      = 50;
        f.damping       = 50;
        f.reverbMix     = 0;
        f.sameAsA       = 0;
    }
    s.balance = 0;
    s.pitch   = 0.0f;
}

// --- Encoding ---------------------------------------------------------------

// The built-in a layer's voice is, or was made from.
static uint8_t builtinVoice(const SavedConfig& cfg, uint8_t l) {
    uint8_t id = cfg.layer[l].voice;
    if (voiceIsCustomId(id))  id = cfg.customVoices[id - VOICE_CUSTOM_FIRST].base;
    else if (id == VOICE_SCENE) id = layerSceneVoice(l).base;
    return id < VOICE_COUNT ? id : (uint8_t)VoiceId::Piano;
}

static uint8_t atMost(uint8_t v, uint8_t max) { return v > max ? max : v; }

void sceneCodeEncode(const SavedConfig& cfg, char* out) {
    CodeFields f{};
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        const LayerCfg& c = cfg.layer[l];
        f.root[l]    = (uint8_t)c.root % 12;
        f.octave[l]  = atMost(c.octave, 7);
        f.scale[l]   = (uint8_t)c.scale < (uint8_t)Scale::Learned ? (uint8_t)c.scale
                                                                  : (uint8_t)Scale::Major;
        f.voice[l]   = builtinVoice(cfg, l);
        f.shift[l]   = atMost(c.shift, NUM_HALL_SENSORS - 1);
        f.lowNote[l] = c.lowNote ? 1 : 0;
        f.wrap[l]    = c.wrap ? 1 : 0;
    }
    const LayerCfg& b = cfg.layer[LAYER_B];
    f.shiftSameAsA   = b.shiftSameAsA ? 1 : 0;
    f.lowNoteSameAsA = b.lowNoteSameAsA ? 1 : 0;
    f.mode[LAYER_A]  = cfg.layer[LAYER_A].mode == LayerMode::Off ? 1 : 0;   // A is On or Off
    f.mode[LAYER_B]  = atMost((uint8_t)b.mode, 3);
    f.turns          = b.turns == LayerTurns::Alternate ? 1 : 0;

    // MSB first: the version, then each field, its default stored as 0.
    const uint8_t* v = (const uint8_t*)&f;
    uint64_t acc = 0;
    uint8_t  pos = 0;
    auto put = [&](uint8_t value, uint8_t bits) {
        acc |= (uint64_t)value << (CODE_BITS - pos - bits);
        pos += bits;
    };
    put(CODE_VERSION, VERSION_BITS);
    for (uint8_t i = 0; i < V1_COUNT; i++) {
        const FieldSpec& s = V1_FIELDS[i];
        put((uint8_t)((v[s.offset] + s.count - s.def) % s.count), s.bits);
    }

    uint8_t len = 0;
    for (uint8_t i = 0; i < SCENE_CODE_MAX; i++) {
        uint8_t c = (acc >> (CODE_BITS - CHAR_BITS * (i + 1))) & 31;
        out[i] = SCENE_CODE_ALPHABET[c];
        if (c) len = i + 1;   // trailing zero characters are dropped
    }
    out[len] = '\0';
}

// --- Decoding ---------------------------------------------------------------

bool sceneCodeDecode(const char* code, Scene& out) {
    size_t n = strlen(code);
    if (n == 0 || n > SCENE_CODE_MAX) return false;

    // Missing characters are zeros: defaults.
    uint64_t acc = 0;
    for (size_t i = 0; i < n; i++) {
        const char* p = strchr(SCENE_CODE_ALPHABET, code[i]);
        if (!code[i] || !p) return false;
        acc |= (uint64_t)(p - SCENE_CODE_ALPHABET) << (CODE_BITS - CHAR_BITS * (i + 1));
    }
    uint8_t pos = 0;
    auto get = [&](uint8_t bits) {
        uint8_t v = (uint8_t)((acc >> (CODE_BITS - pos - bits)) & ((1u << bits) - 1));
        pos += bits;
        return v;
    };

    uint8_t version = get(VERSION_BITS);
    if (version != 1) return false;

    CodeFields f{};
    uint8_t* v = (uint8_t*)&f;
    for (uint8_t i = 0; i < V1_COUNT; i++) {
        const FieldSpec& s = V1_FIELDS[i];
        uint8_t e = get(s.bits);
        if (e >= s.count) return false;
        v[s.offset] = (uint8_t)((e + s.def) % s.count);
    }
    if (pos < CODE_BITS && (acc & ((1ull << (CODE_BITS - pos)) - 1))) return false;   // padding

    // Newer fields first, as the factory has them; then what version 1 pins.
    out = storageFactoryScene();
    v1Pinned(out);
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        LayerCfg& c = out.layer[l];
        c.root    = (RootNote)f.root[l];
        c.octave  = f.octave[l];
        c.scale   = (Scale)f.scale[l];
        c.voice   = f.voice[l];
        c.shift   = f.shift[l];
        c.lowNote = f.lowNote[l];
        c.wrap    = f.wrap[l] != 0;
        c.shiftSameAsA   = false;
        c.lowNoteSameAsA = false;
        c.turns   = LayerTurns::Together;
    }
    LayerCfg& b = out.layer[LAYER_B];
    b.shiftSameAsA   = f.shiftSameAsA != 0;
    b.lowNoteSameAsA = f.lowNoteSameAsA != 0;
    b.turns          = f.turns ? LayerTurns::Alternate : LayerTurns::Together;
    out.layer[LAYER_A].mode = f.mode[LAYER_A] ? LayerMode::Off : LayerMode::On;
    b.mode                  = (LayerMode)f.mode[LAYER_B];
    return true;
}
