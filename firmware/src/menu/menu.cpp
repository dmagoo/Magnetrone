#include "menu.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <LiquidCrystal_I2C.h>
#include "ui/encoder.h"
#include "motion/stepper.h"
#include "audio/audio.h"
#include "midi/midi.h"
#include "config/storage.h"
#include "sequencer/scale.h"
#include "config.h"
#include "lcd_chars.h"
#include "calibration/calibration.h"
#include "sensors/hall.h"
#include "sequencer/pitch.h"
#include "sequencer/layers.h"
#include "motion/bar.h"
#include "sequencer/scenes.h"
#include "sequencer/demos.h"
#include "midi/midi_in.h"
#include "sequencer/sequencer.h"

static LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

enum class MenuState : uint8_t {
    Status,
    MainMenu,
    SoundDefaults,    // Layer A / Layer B: edits the Defaults scene
    SoundGatePrompt,  // another scene is playing: load Defaults first?
    PlaySetup,
    System,
    LayerMenu,      // one layer's submenu; which one is editLayer
    LayerMode,
    LayerTurns,     // Layer B only
    LayerVoice,
    LayerChannel,
    LayerRoot,
    LayerScale,
    ScaleEditList,  // Edit Scale: the layer's eight notes (Sound Defaults or Aux)
    ScaleEditSlot,  //   one note
    ScaleSaveSelect,  // Edit Scale > Save As: pick Custom 1-8
    ScaleSaveConfirm, //   overwrite a used one?
    LayerOctave,
    LayerLevel,
    LayerShift,
    LayerWrap,
    LayerLowNote,
    LayerMidiIn,
    FxEffectList,   // Sound Defaults > Layer > Effects: which effect
    FxParamList,    //   that effect's settings
    FxParamEdit,    //   one setting's value
    MidiFnSetting,
    MidiCcSetting,
    SceneLoadSetting,
    WelcomeTune,
    LcdTimeout,
    MenuTimeout,
    BeatsPerRev,
    MagnetPole,
    FirstBootPrompt,   // shown once on a fresh EEPROM: calibrate now or skip
    AuxFnDefault,
    AuxFnDefaultFx, // Play Setup > Aux Fn > Effects
    PitchStep,
    AuxFnSelect,    // aux knob: choose what the knob modulates
    AuxLayerSelect, // aux knob: the same, inside Layer A or Layer B
    AuxFxSelect,    // aux knob: Layer A/B > Effects, the effect Fns
    VoiceEditList,  // aux knob: the layer's voice, which setting to tweak
    VoiceEditParam, // aux knob: tweak it, live
    HarmonicList,   // aux knob: Voice Edit > Harmonics, which harmonic
    HarmonicParam,  // aux knob: its level, live
    FilterList,     // aux knob: Voice Edit > Filter, which setting
    FilterParam,    // aux knob: its value, live
    VoiceSaveSelect,  // aux knob: Save As, pick the slot
    VoiceSaveConfirm, // aux knob: overwrite a used custom slot?
    AuxParam,       // aux knob: modulate the chosen parameter, live
    SceneSaveSelect,  // aux knob: pick the scene slot to save to
    SceneSaveConfirm, // aux knob: overwrite a used slot?
    CalClearPrompt,     // calibration step 1: clear the platter
    CalSampling,        //   blocking: baselines
    CalMagnetPrompt,    // calibration step 2: one magnet on the start mark
    CalDetecting,       //   blocking: threshold, pole, belt, bar start
    Tools,              // calibration and maintenance submenu
    StartCheckPrompt,   // at boot: the bar start was lost, find it or skip
    StartPosMode,       // Calib. StartPos: Auto or Manual
    StartPosAutoPrompt, // Auto: one magnet on the mark, outer track otherwise clear
    StartPosAuto,       //   blocking: find the start from that magnet
    FindStart,          // Manual: jog the platter until the start mark is at the arm
    FindStartConfirm,
    FindFront,          // Manual, first: jog the mark to the player (Front)
    FindFrontConfirm,
    Placement,          // Placement Mode: the knobs position the platter
    StartCheck,         // setting: whether the boot prompt above is shown
    Info,               // read-only pages: belt, StartPos, threshold, driver
    SensorLevels,       // live: each sensor's reading against its rest level
    SensorTiming,       // live: each sensor's learned trigger timing
    TrackNotes,         // live: what each track plays now, per layer
    ResetCalPrompt,
    ResetSettingsPrompt,
    FactoryResetPrompt
};

static MenuState state        = MenuState::Status;
static uint8_t   cursor       = 0;
static uint32_t  lastActivity = 0;
static bool      needsRedraw  = true;

// Backlight state -- tracks whether the LCD backlight is currently on.
static bool      backlightOn     = true;
static uint32_t  lastInteraction = 0;  // millis() of last any-encoder event

// -------------------------------------------------------------------------
// Display labels
// -------------------------------------------------------------------------

static const char* ROOT_ITEMS[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B","Back"
};
static const uint8_t ROOT_COUNT = 13;

// Every scale's name, Learned and Custom included, for the scale lists, the
// live display and scenes.
static const char* SCALE_NAMES[] = {
    "Major","Minor","PMajor","PMinor","Blues","Chromat","Dorian","Mixolyd","Learned",
    "Custom"
};
static_assert(sizeof(SCALE_NAMES) / sizeof(SCALE_NAMES[0]) == (uint8_t)Scale::COUNT,
              "a scale has no name");

static const char* OCTAVE_ITEMS[] = {
    "0","1","2","3","4","5","6","7","Back"
};
static const uint8_t OCTAVE_COUNT = 9;

// --- Layer submenu --------------------------------------------------------
// Layer A and Layer B share one submenu; editLayer says which is open.
static uint8_t editLayer = LAYER_A;

// Layer Turns is Layer B's only: Layer A's list skips it (layerMenuItem()).
static const char* LAYER_ITEMS[] = {
    "Mode","Layer Turns","Voice","Channel","Root Note","Scale","Edit Scale","Octave",
    "Level","Shift","Wrap","Low Note","Effects","MIDI In","Back"
};
static const uint8_t LAYER_ITEMS_COUNT = 15;
enum : uint8_t { LAYER_ITEM_MODE, LAYER_ITEM_TURNS, LAYER_ITEM_VOICE, LAYER_ITEM_CHANNEL,
                 LAYER_ITEM_ROOT, LAYER_ITEM_SCALE, LAYER_ITEM_EDIT_SCALE, LAYER_ITEM_OCTAVE,
                 LAYER_ITEM_LEVEL, LAYER_ITEM_SHIFT, LAYER_ITEM_WRAP,
                 LAYER_ITEM_LOW_NOTE, LAYER_ITEM_EFFECTS, LAYER_ITEM_MIDI_IN,
                 LAYER_ITEM_BACK };

// The open layer's list: how many entries, which item sits at a position,
// and where an item sits. Layer A's list has no Layer Turns.
static uint8_t layerMenuCount() {
    return (editLayer == LAYER_B) ? LAYER_ITEMS_COUNT : LAYER_ITEMS_COUNT - 1;
}
static uint8_t layerMenuItem(uint8_t pos) {
    return (editLayer == LAYER_A && pos >= LAYER_ITEM_TURNS) ? pos + 1 : pos;
}
static uint8_t layerMenuPos(uint8_t item) {
    return (editLayer == LAYER_A && item > LAYER_ITEM_TURNS) ? item - 1 : item;
}

// The layer submenu as drawn: each entry with the layer's cue, if any (see
// layerCue()). Rebuilt before each draw.
static char        LAYER_LABEL_BUF[LAYER_ITEMS_COUNT][17];
static const char* LAYER_LABELS[LAYER_ITEMS_COUNT];

// A layer's MIDI in channel: Off, then channels 1-16, then Back. Index ==
// stored value. Filled in by menuInit().
static const char*   MIDI_IN_ITEMS[18];
static const uint8_t MIDI_IN_COUNT = 18;

// Order matches LayerMode. Layer A has no Same as A, so its list is shorter.
static const char* MODE_ITEMS_A[] = { "On","Off","Back" };
static const char* MODE_ITEMS_B[] = { "On","Off","Same as A","Stack","Back" };
static const uint8_t MODE_COUNT_A = 3;
static const uint8_t MODE_COUNT_B = 5;

// Order matches LayerTurns.
static const char* TURNS_ITEMS[] = { "Together","Alternate","Back" };
static const uint8_t TURNS_COUNT = 3;

// Auto, then channels 1-16, then Back. Index == stored value. Filled in by
// menuInit().
static char        CHANNEL_LABEL_BUF[16][6];
static const char* CHANNEL_ITEMS[18];
static const uint8_t CHANNEL_COUNT = 18;

// Level in 10% steps; index = level / 10.
static const char*   LEVEL_ITEMS[] = {
    "0%","10%","20%","30%","40%","50%","60%","70%","80%","90%","100%","Back"
};
static const uint8_t LEVEL_COUNT = 12;

// Track Shift, 0-7; index == shift. Layer B's list adds Same as A, which binds
// its shift (and Wrap) to Layer A's, separately from the layer mode.
static const char*   SHIFT_ITEMS_A[] = { "0","1","2","3","4","5","6","7","Back" };
static const char*   SHIFT_ITEMS_B[] = { "0","1","2","3","4","5","6","7","Same as A","Back" };
static const uint8_t SHIFT_COUNT_A = 9;
static const uint8_t SHIFT_COUNT_B = 10;
static const uint8_t SHIFT_ITEM_SAME_AS_A = NUM_HALL_SENSORS;

// Index 0 = Wrap.
static const char*   WRAP_ITEMS[] = { "Wrap","No Wrap","Back" };
static const uint8_t WRAP_COUNT = 3;

// Order matches LowNote. Layer B's list adds Same as A, its own binding,
// separate from both the layer mode and the shift's.
static const char*   LOW_NOTE_ITEMS_A[] = { "Inner","Outer","Back" };
static const char*   LOW_NOTE_ITEMS_B[] = { "Inner","Outer","Same as A","Back" };
static const uint8_t LOW_NOTE_COUNT_A = 3;
static const uint8_t LOW_NOTE_COUNT_B = 4;
static const uint8_t LOW_NOTE_VALUES  = 2;
static const uint8_t LOW_NOTE_ITEM_SAME_AS_A = LOW_NOTE_VALUES;

// --- Effects ----------------------------------------------------------------
// Every effect setting, grouped by effect in chain order. Most are a percent
// in 10% steps, stored in one LayerFx field, up to `maxPct`; `offAt` is the
// value that reads "Off" (the effect is out of the sound there), or
// FX_NO_OFF. The Delay's Mode and Time are lists instead.
static const uint8_t FX_NO_OFF = 0xFF;
enum FxParam : uint8_t { FXP_CUTOFF, FXP_RESONANCE,
                         FXP_CHORUS_RATE, FXP_CHORUS_DEPTH, FXP_CHORUS_MIX,
                         FXP_DELAY_MODE, FXP_DELAY_TIME, FXP_DELAY_FEEDBACK, FXP_DELAY_MIX,
                         FXP_ROOM_SIZE, FXP_DAMPING, FXP_REVERB_MIX, FXP_COUNT };
enum class FxKind : uint8_t { Pct, Mode, Time };
struct FxParamInfo {
    const char* name; FxId fx; FxKind kind; uint8_t LayerFx::* field; uint8_t offAt; uint8_t maxPct;
};
static const FxParamInfo FX_PARAMS[FXP_COUNT] = {
    { "Cutoff",    FxId::Tone,   FxKind::Pct,  &LayerFx::cutoff,        FX_CUTOFF_OFF, 100 },
    { "Resonance", FxId::Tone,   FxKind::Pct,  &LayerFx::resonance,     FX_NO_OFF,     100 },
    { "Rate",      FxId::Chorus, FxKind::Pct,  &LayerFx::chorusRate,    FX_NO_OFF,     100 },
    { "Depth",     FxId::Chorus, FxKind::Pct,  &LayerFx::chorusDepth,   FX_NO_OFF,     100 },   // 0% is not Off
    { "Mix",       FxId::Chorus, FxKind::Pct,  &LayerFx::chorusMix,     0,             100 },
    { "Mode",      FxId::Delay,  FxKind::Mode, &LayerFx::delayMode,     FX_NO_OFF,     0 },
    { "Time",      FxId::Delay,  FxKind::Time, nullptr,                 FX_NO_OFF,     0 },
    { "Feedback",  FxId::Delay,  FxKind::Pct,  &LayerFx::delayFeedback, FX_NO_OFF,     FX_FEEDBACK_MAX },
    { "Mix",       FxId::Delay,  FxKind::Pct,  &LayerFx::delayMix,      0,             100 },
    { "Room Size", FxId::Reverb, FxKind::Pct,  &LayerFx::roomSize,      FX_NO_OFF,     100 },
    { "Damping",   FxId::Reverb, FxKind::Pct,  &LayerFx::damping,       FX_NO_OFF,     100 },
    { "Mix",       FxId::Reverb, FxKind::Pct,  &LayerFx::reverbMix,     0,             100 },
};

static const char* FX_NAMES[FX_COUNT] = { "Tone", "Chorus", "Delay", "Reverb" };
static const char* DELAY_MODE_ITEMS[] = { "Sync", "Free" };

// The effects in the Effects menu, in chain order.
static const FxId  FX_MENU[] = { FxId::Tone, FxId::Chorus, FxId::Delay, FxId::Reverb };
static const uint8_t FX_MENU_COUNT = sizeof(FX_MENU) / sizeof(FX_MENU[0]);

// The Free times as shown, "10 ms" to "2400 ms". Filled in by menuInit().
static char FREE_TIME_BUF[DELAY_FREE_COUNT][12];

// The value list for one setting, and where the setting sits in it. The
// percentages run "0%" to their top, the Off end reading "Off"; the Delay's
// Time lists Sync fractions or Free times, whichever its Mode is. With
// `back`, a Back entry follows. Returns the count, Back included.
static const uint8_t FX_VALUE_MAX = DELAY_FREE_COUNT;
static const char* fxValueLabels[FX_VALUE_MAX + 1];
static uint8_t buildFxValueLabels(uint8_t p, const LayerFx& fx, bool back) {
    const FxParamInfo& fp = FX_PARAMS[p];
    uint8_t n = 0;
    switch (fp.kind) {
        case FxKind::Pct:
            for (uint8_t v = 0; v <= fp.maxPct; v += 10, n++) {
                fxValueLabels[n] = (v == fp.offAt) ? "Off" : LEVEL_ITEMS[v / 10];
            }
            break;
        case FxKind::Mode:
            for (; n < (uint8_t)DelayMode::COUNT; n++) fxValueLabels[n] = DELAY_MODE_ITEMS[n];
            break;
        case FxKind::Time:
            if (fx.delayMode == (uint8_t)DelayMode::Free) {
                for (; n < DELAY_FREE_COUNT; n++) fxValueLabels[n] = FREE_TIME_BUF[n];
            } else {
                for (; n < DELAY_SYNC_COUNT; n++) fxValueLabels[n] = delaySyncName(n);
            }
            break;
    }
    fxValueLabels[n] = "Back";
    return back ? n + 1 : n;
}

static uint8_t fxValueIndex(uint8_t p, const LayerFx& fx) {
    const FxParamInfo& fp = FX_PARAMS[p];
    switch (fp.kind) {
        case FxKind::Mode: return fx.delayMode < (uint8_t)DelayMode::COUNT ? fx.delayMode : 0;
        case FxKind::Time:
            if (fx.delayMode == (uint8_t)DelayMode::Free) return delayFreeIndex(fx.delayMs);
            return fx.delaySync < DELAY_SYNC_COUNT ? fx.delaySync : 0;
        default:           return (uint8_t)(min(fx.*fp.field, fp.maxPct) / 10);
    }
}

static void fxSetIndex(uint8_t p, LayerFx& fx, uint8_t i) {
    const FxParamInfo& fp = FX_PARAMS[p];
    switch (fp.kind) {
        case FxKind::Mode: fx.delayMode = i; break;
        case FxKind::Time:
            if (fx.delayMode == (uint8_t)DelayMode::Free) fx.delayMs = delayFreeMs(i);
            else                                          fx.delaySync = i;
            break;
        default:           fx.*fp.field = (uint8_t)min(i * 10, (int)fp.maxPct); break;
    }
}

// The Effects menu is for editLayer; these say which effect and setting are open.
static FxId    fxMenuEffect = FxId::Tone;
static uint8_t fxMenuParam  = FXP_CUTOFF;

// One effect's settings, as listed: its FxParams in order.
static uint8_t fxParamsOf(FxId fx, uint8_t out[FXP_COUNT]) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < FXP_COUNT; i++) if (FX_PARAMS[i].fx == fx) out[n++] = i;
    return n;
}

static const char* MAIN_ITEMS[] = {
    "Sound Defaults","Play Setup","Tools","System","Exit"
};  // Exit returns to the live display; submenus keep "Back"
static const uint8_t MAIN_COUNT = 5;
enum : uint8_t { MAIN_ITEM_SOUND, MAIN_ITEM_PLAY, MAIN_ITEM_TOOLS, MAIN_ITEM_SYSTEM,
                 MAIN_ITEM_EXIT };

// Sound Defaults: the two layers of the Defaults scene. Drawn with each
// layer's cue, so built before each draw.
static char        SOUND_LABEL_BUF[NUM_LAYERS][17];
static const char* SOUND_ITEMS[NUM_LAYERS + 1];
static const uint8_t SOUND_COUNT = NUM_LAYERS + 1;   // + Back

// Play Setup: how the controls and MIDI in behave while playing.
static const char* PLAY_ITEMS[] = {
    "Beats/Rev","Pitch Step","Aux Fn","Scene Load","MIDI Fn","MIDI CC","Back"
};
static const uint8_t PLAY_COUNT = 7;
enum : uint8_t { PLAY_ITEM_BEATS, PLAY_ITEM_PITCH_STEP, PLAY_ITEM_AUX_FN,
                 PLAY_ITEM_SCENE_LOAD, PLAY_ITEM_MIDI_FN, PLAY_ITEM_MIDI_CC, PLAY_ITEM_BACK };

// Play Setup > Scene Load. Index 0 = Next Bar.
static const char*   SCENE_LOAD_ITEMS[] = { "Next Bar","Now","Back" };
static const uint8_t SCENE_LOAD_COUNT = 3;

// System: set once and forgotten, nothing to do with the performance.
static const char* SYSTEM_ITEMS[] = {
    "LCD Timeout","Menu Timeout","Welcome Tune","StartPos Check","Magnet Pole","Back"
};
static const uint8_t SYSTEM_COUNT = 6;
enum : uint8_t { SYS_ITEM_LCD_TIMEOUT, SYS_ITEM_MENU_TIMEOUT, SYS_ITEM_WELCOME,
                 SYS_ITEM_START_CHECK, SYS_ITEM_MAGNET_POLE, SYS_ITEM_BACK };

// Menu timeout choices in seconds, as LCD Timeout offers its own.
static const uint8_t MENU_TIMEOUT_VALUES[] = { 5, 10, 30, 60, MENU_TIMEOUT_NEVER };
static const char*   MENU_TIMEOUT_LABELS[] = { "5 sec","10 sec","30 sec","1 min","Never","Back" };
static const uint8_t MENU_TIMEOUT_COUNT = 6;   // 5 options + Back

// What incoming MIDI keys do. Order matches MidiFn.
static const char* MIDI_FN_ITEMS[] = { "Off","Pitch","Shift","Scale Learn","Chord","Back" };
static const uint8_t MIDI_FN_COUNT = 6;

// Calibration and maintenance, kept out of the main menu. StartPos is the
// bar start: where the start mark on the platter passes the arm. Front is
// where the player sits.
static const char* TOOLS_ITEMS[] = {
    "Go to StartPos","Go to Front","Placement Mode","Full Calibrate",
    "Reset Calib.","Calib. StartPos","Machine Info","Sensor Levels","Sensor Timing",
    "Track Notes","Reset Settings","Factory Reset","Back"
};
static const uint8_t TOOLS_COUNT = 13;
enum : uint8_t { TOOL_GO_TO_START, TOOL_GO_TO_FRONT, TOOL_PLACEMENT,
                 TOOL_FULL_CAL, TOOL_RESET_CAL,
                 TOOL_CALIB_START, TOOL_INFO, TOOL_SENSOR_LEVELS,
                 TOOL_SENSOR_TIMING, TOOL_TRACK_NOTES, TOOL_RESET_SETTINGS, TOOL_FACTORY_RESET, TOOL_BACK };

// Info: one page per value, turned through with the menu knob.
enum : uint8_t { INFO_RPM, INFO_BELT, INFO_START_POS, INFO_THRESHOLD,
                 INFO_DRIVER, INFO_PAGE_COUNT };

// Live screens (the StartPos page, Sensor Levels) redraw at this rate:
// enough to watch a magnet go by, cheap on the I2C bus.
static const uint32_t LIVE_REDRAW_MS = 200;
static uint32_t lastLiveDraw = 0;
static uint8_t  driverVersion = 0;   // read once on entering Info

// Calib. StartPos: Auto finds it from a magnet on the mark, Manual jogs.
static const char* START_MODE_ITEMS[] = { "Auto","Manual","Back" };
static const uint8_t START_MODE_COUNT = 3;

// Go to StartPos speed, and backstops for its waits.
static const float    GO_TO_START_RPM  = 12.0f;
static const uint32_t GO_TO_STOP_MS    = 6000;
static const uint32_t GO_TO_ARRIVE_MS  = 10000;

// Which way a passing magnet pushes the sensor output. Calibration measures
// this; the override exists so a wrong guess does not leave the table silent.
static const int8_t POLE_VALUES[] = { 1, -1 };
static const char*  POLE_LABELS[] = { "Normal","Flipped","Back" };
static const uint8_t POLE_COUNT = 3;   // 2 options + Back

// -------------------------------------------------------------------------
// Aux function knob
// -------------------------------------------------------------------------
// What the aux knob can be bound to. The stored cfg.auxFn is this number;
// the order on screen is set by the lists below. The per-layer Fns come in
// A/B pairs, in AuxKind order, from VoiceA on.
enum class AuxFn : uint8_t { Pitch, Balance, LoadScene,
                             Turns,   // version 22: saved bindings from VoiceA on moved up one
                             VoiceA, VoiceB, RootA, RootB, ScaleA, ScaleB,
                             OctaveA, OctaveB, ShiftA, ShiftB, LowNoteA, LowNoteB,
                             WrapA, WrapB,   // appended, so saved bindings keep their numbers
                             ModeA, ModeB,
                             CutoffA, CutoffB, ReverbMixA, ReverbMixB,
                             DelayMixA, DelayMixB, DelayFeedbackA, DelayFeedbackB,
                             COUNT };
static const uint8_t AUX_FN_COUNT = (uint8_t)AuxFn::COUNT;
static_assert(DEFAULT_AUX_FN == (uint8_t)AuxFn::VoiceA, "DEFAULT_AUX_FN is meant to be Layer A Voice");

// What a per-layer Fn changes. The layer comes from the A/B pair.
// The effect kinds (Cutoff on) live in the layer's Effects list, not its
// main one.
enum class AuxKind : uint8_t { Voice, Root, Scale, Octave, Shift, LowNote, Wrap, Mode,
                               Cutoff, ReverbMix, DelayMix, DelayFeedback, COUNT };
static const uint8_t AUX_LAYER_KIND_COUNT = (uint8_t)AuxKind::Cutoff;
static bool auxKindIsFx(AuxKind k) { return k >= AuxKind::Cutoff && k < AuxKind::COUNT; }

static bool    auxIsLayerFn(AuxFn fn) { return fn >= AuxFn::VoiceA && fn < AuxFn::COUNT; }
static AuxKind auxKindOf(AuxFn fn)   { return (AuxKind)(((uint8_t)fn - (uint8_t)AuxFn::VoiceA) / NUM_LAYERS); }
static uint8_t auxLayerOf(AuxFn fn)  { return ((uint8_t)fn - (uint8_t)AuxFn::VoiceA) % NUM_LAYERS; }
static AuxFn   auxLayerFn(AuxKind k, uint8_t layer) {
    return (AuxFn)((uint8_t)AuxFn::VoiceA + (uint8_t)k * NUM_LAYERS + layer);
}

// The Aux list, as shown: Pitch, a submenu per layer, Balance and Layer
// Turns (both about the two layers together, so not inside either), the
// scene Fn, then three actions. Layer entries open the per-layer list below.
static const char* AUX_TOP_LABELS[] = {
    "Pitch","Layer A","Layer B","A/B Balance","Layer Turns","Load Scene",
    "Save Scene","Reset All","Exit"
};
enum : uint8_t { AUXT_PITCH, AUXT_LAYER_A, AUXT_LAYER_B, AUXT_BALANCE, AUXT_TURNS,
                 AUXT_LOAD_SCENE, AUXT_SAVE_SCENE, AUXT_RESET, AUXT_EXIT, AUXT_COUNT };
// The Fn behind each top entry; COUNT where the entry is not a Fn.
static const AuxFn AUX_TOP_FN[AUXT_COUNT] = {
    AuxFn::Pitch, AuxFn::COUNT, AuxFn::COUNT, AuxFn::Balance, AuxFn::Turns,
    AuxFn::LoadScene, AuxFn::COUNT, AuxFn::COUNT, AuxFn::COUNT
};

// Inside Layer A / Layer B: the per-layer Fns in AuxKind order, with
// Voice Edit after Voice and Edit Scale after Scale, then Effects and Back.
static const char* AUX_LAYER_LABELS[] = {
    "Voice","Voice Edit","Root Note","Scale","Edit Scale","Octave","Shift","Low Note",
    "Wrap","Mode","Effects","Back"
};
static const uint8_t AUX_LAYER_COUNT      = AUX_LAYER_KIND_COUNT + 4;   // + Voice Edit, Edit Scale, Effects, Back
static const uint8_t AUX_LAYER_VOICE_EDIT = 1;
static const uint8_t AUX_LAYER_EDIT_SCALE = 4;
static const uint8_t AUX_LAYER_EFFECTS    = AUX_LAYER_KIND_COUNT + 2;

// Inside Layer A/B > Effects: the effect Fns, in chain order, then Back.
// Each is one effect setting, from FX_PARAMS.
static const AuxKind AUX_FX_KINDS[]  = { AuxKind::Cutoff, AuxKind::DelayMix,
                                         AuxKind::DelayFeedback, AuxKind::ReverbMix };
static const char*   AUX_FX_LABELS[] = { "Tone Cutoff", "Delay Mix", "Delay Feedback",
                                         "Reverb Mix", "Back" };
static const uint8_t AUX_FX_COUNT    = sizeof(AUX_FX_KINDS) / sizeof(AUX_FX_KINDS[0]) + 1;   // + Back

static uint8_t auxFxParam(AuxKind k) {
    switch (k) {
        case AuxKind::Cutoff:        return FXP_CUTOFF;
        case AuxKind::ReverbMix:     return FXP_REVERB_MIX;
        case AuxKind::DelayMix:      return FXP_DELAY_MIX;
        case AuxKind::DelayFeedback: return FXP_DELAY_FEEDBACK;
        default:                     return FXP_CUTOFF;
    }
}
static uint8_t auxFxPos(AuxKind k) {
    for (uint8_t i = 0; i < AUX_FX_COUNT - 1; i++) if (AUX_FX_KINDS[i] == k) return i;
    return 0;
}
static uint8_t auxLayer = LAYER_A;   // which layer's list is open

// Where each AuxKind sits in that list, and back. The effect kinds sit
// under Effects.
static uint8_t auxKindPos(AuxKind k) {
    if (auxKindIsFx(k)) return AUX_LAYER_EFFECTS;
    if (k == AuxKind::Voice) return 0;
    return (k <= AuxKind::Scale) ? (uint8_t)k + 1 : (uint8_t)k + 2;
}
static AuxKind auxPosKind(uint8_t pos) {
    if (pos == 0) return AuxKind::Voice;
    return (pos < AUX_LAYER_EDIT_SCALE) ? (AuxKind)(pos - 1) : (AuxKind)(pos - 2);
}

// --- Voice Edit -------------------------------------------------------------
// Live tweaks to the voice a layer plays (layers.h). Save As keeps them, in a
// custom slot or as the current scene's own voice for the layer.
static const char* VOICE_EDIT_LABELS[] = {
    "Wave","Harmonics","Attack","Decay","Sustain","Release","Length","Filter",
    "Save As...","Back"
};
enum : uint8_t { VE_WAVE, VE_HARMONICS, VE_ATTACK, VE_DECAY, VE_SUSTAIN, VE_RELEASE,
                 VE_LENGTH, VE_FILTER, VE_SAVE, VE_BACK, VE_COUNT };

// Voice Edit > Filter: the voice's own filter and its envelope. Cutoff 100%
// reads Off (the filter is left out); Amount 0% leaves it fixed, not off.
static const char* FILTER_LABELS[] = {
    "Cutoff","Resonance","Amount","Attack","Decay","Sustain","Release","Back"
};
enum : uint8_t { VF_CUTOFF, VF_RESONANCE, VF_AMOUNT, VF_ATTACK, VF_DECAY, VF_SUSTAIN,
                 VF_RELEASE, VF_BACK, VF_COUNT };
static uint8_t filterParam = VF_CUTOFF;   // what FilterParam changes

// The Voice Edit list as shown: "Harmonics*" once they are edited, the same
// mark a tweaked voice gets in the Voice list.
static const char* veLabels[VE_COUNT];
static void buildVoiceEditLabels(const SavedConfig& cfg) {
    for (uint8_t i = 0; i < VE_COUNT; i++) veLabels[i] = VOICE_EDIT_LABELS[i];
    if (layerVoice(cfg, auxLayer).harmonicsEdited) veLabels[VE_HARMONICS] = "Harmonics*";
}

// The voices a layer can pick, as shown: the built-ins, the used custom
// slots, then (for the Aux, outside Defaults) the scene's own voice if it
// has one for this layer. Rebuilt before use, since slots come and go.
static const uint8_t VOICE_CHOICE_MAX = VOICE_COUNT + NUM_CUSTOM_VOICES + 1;
static uint8_t     voiceChoiceIds[VOICE_CHOICE_MAX];
static const char* voiceChoiceLabels[VOICE_CHOICE_MAX + 1];   // + Back
static char        voiceChoiceMarked[17];

static uint8_t buildVoiceChoices(const SavedConfig& cfg, uint8_t layer, bool withScene) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < VOICE_COUNT; i++) voiceChoiceIds[n++] = i;
    for (uint8_t i = 0; i < NUM_CUSTOM_VOICES; i++) {
        if (cfg.customVoices[i].used) voiceChoiceIds[n++] = VOICE_CUSTOM_FIRST + i;
    }
    if (withScene && layerSceneVoice(layer).used) voiceChoiceIds[n++] = VOICE_SCENE;
    for (uint8_t i = 0; i < n; i++) voiceChoiceLabels[i] = voiceIdName(voiceChoiceIds[i]);
    voiceChoiceLabels[n] = "Back";
    return n;
}

// Where voice `id` sits in the list just built; 0 if it is not there.
static uint8_t voiceChoicePos(uint8_t id, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) if (voiceChoiceIds[i] == id) return i;
    return 0;
}

// Save As: this layer's slot in the current scene (only for scenes 1-8),
// first so it is reached without scrolling past the customs, then Custom
// 1-8, then Back. Labels rebuilt before each draw.
static char        VOICE_SAVE_BUF[NUM_CUSTOM_VOICES + 1][17];
static const char* VOICE_SAVE_ITEMS[NUM_CUSTOM_VOICES + 2];
static uint8_t     voiceSaveTarget = 0;   // the custom slot being confirmed

// 1 when the list starts with the scene's slot, which moves the customs down.
static uint8_t voiceSaveCustomPos(const SavedConfig& cfg) {
    return sceneIsSlot(cfg.currentScene) && cfg.currentScene != SCENE_DEFAULTS ? 1 : 0;
}

static uint8_t buildVoiceSaveItems(const SavedConfig& cfg, uint8_t layer) {
    uint8_t n = 0;
    if (voiceSaveCustomPos(cfg)) {
        snprintf(VOICE_SAVE_BUF[n], sizeof(VOICE_SAVE_BUF[n]), "Scene %c Voice %c",
                 '0' + cfg.currentScene, 'A' + layer);   // scenes are 1-8 here
        VOICE_SAVE_ITEMS[n] = VOICE_SAVE_BUF[n];
        n++;
    }
    for (uint8_t i = 0; i < NUM_CUSTOM_VOICES; i++, n++) {
        snprintf(VOICE_SAVE_BUF[n], sizeof(VOICE_SAVE_BUF[n]), "Custom %u%s",
                 (unsigned)(i + 1), cfg.customVoices[i].used ? "" : " empty");
        VOICE_SAVE_ITEMS[n] = VOICE_SAVE_BUF[n];
    }
    VOICE_SAVE_ITEMS[n] = "Back";
    return n + 1;
}

// After a save: say where it went, then back to the Voice Edit list.
static void enterState(MenuState s, uint8_t initialCursor);
static void voiceSaved(const char* where) {
    char msg[17];
    snprintf(msg, sizeof(msg), "Saved %s", where);
    menuMessage(msg, "");
    delay(1000);
    enterState(MenuState::VoiceEditList, VE_SAVE);
}
static uint8_t voiceEditParam = VE_WAVE;   // what VoiceEditParam changes

// Envelope and note times, in ms. We hear these roughly in proportion to
// their length, so the steps widen as they grow: each click is a change you
// can hear, and a few turns cover the range. Every preset's value is a step.
static const uint16_t TIME_STEPS[] = {
    0, 5, 10, 20, 50, 100, 150, 200, 250, 300, 400, 500, 600, 800,
    1000, 1500, 2000, 2500, 3000
};
static const uint8_t TIME_STEP_COUNT = sizeof(TIME_STEPS) / sizeof(TIME_STEPS[0]);

// Moves a time `delta` steps from its nearest step, within [lo, hi] ms.
static uint16_t timeStep(uint16_t ms, int8_t delta, uint16_t lo, uint16_t hi) {
    uint8_t near = 0;
    for (uint8_t i = 1; i < TIME_STEP_COUNT; i++) {
        if (abs((int)TIME_STEPS[i] - (int)ms) < abs((int)TIME_STEPS[near] - (int)ms)) near = i;
    }
    int i = constrain((int)near + delta, 0, TIME_STEP_COUNT - 1);
    return (uint16_t)constrain((int)TIME_STEPS[i], (int)lo, (int)hi);
}

// Harmonic levels, in percent. As with the times, we hear them in ratios,
// so each step is about the same change (roughly 3 dB). Saved voices keep
// the percentage, so this list can change without breaking them.
static const uint8_t HARM_STEPS[] = { 0, 1, 2, 3, 5, 7, 10, 15, 20, 30, 40, 50, 70, 100 };
static const uint8_t HARM_STEP_COUNT = sizeof(HARM_STEPS) / sizeof(HARM_STEPS[0]);

// Moves a level `delta` steps from its nearest step.
static uint8_t harmStep(uint8_t pct, int8_t delta) {
    uint8_t near = 0;
    for (uint8_t i = 1; i < HARM_STEP_COUNT; i++) {
        if (abs((int)HARM_STEPS[i] - (int)pct) < abs((int)HARM_STEPS[near] - (int)pct)) near = i;
    }
    return HARM_STEPS[constrain((int)near + delta, 0, HARM_STEP_COUNT - 1)];
}

static uint8_t harmonicSel = 0;   // which harmonic HarmonicParam changes

// A voice's harmonic levels as heard: its edited ones, or until then its
// stock wave's.
static void harmonicLevels(const Voice& v, uint8_t out[NUM_HARMONICS]) {
    if (v.harmonicsEdited) memcpy(out, v.harmonics, NUM_HARMONICS);
    else                   voiceHarmonicsFrom(voiceWave(v.waveform), out);
}

// The Harmonics list: "H1 100%" to "H16 0%", Reset, then Back. Rebuilt
// before use. Reset drops the edits, back to the stock wave.
static const uint8_t HARM_RESET = NUM_HARMONICS;
static const uint8_t HARM_COUNT = NUM_HARMONICS + 2;
static char        HARM_BUF[NUM_HARMONICS][10];
static const char* HARM_ITEMS[HARM_COUNT];
static void buildHarmonicItems(const SavedConfig& cfg) {
    uint8_t levels[NUM_HARMONICS];
    harmonicLevels(layerVoice(cfg, auxLayer), levels);
    for (uint8_t i = 0; i < NUM_HARMONICS; i++) {
        snprintf(HARM_BUF[i], sizeof(HARM_BUF[i]), "H%u %u%%", (unsigned)(i + 1), (unsigned)levels[i]);
        HARM_ITEMS[i] = HARM_BUF[i];
    }
    HARM_ITEMS[HARM_RESET]     = "Reset";
    HARM_ITEMS[HARM_COUNT - 1] = "Back";
}

// Layer B in Same as A plays A's voice, and Drums and None have nothing to
// tweak.
static bool voiceEditable(const SavedConfig& cfg, uint8_t l) {
    if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) return false;
    const Voice& v = layerVoice(cfg, l);
    return !voiceIsKit(v) && !voiceIsSilent(v);
}

// Play Setup > Aux Fn, which sets the binding ahead of time: one list, with
// the effect Fns in an Effects group so it stays manageable. Order matches
// AuxFn up to the effects.
static const char* AUX_FN_MENU_LABELS[] = {
    "Pitch","A/B Balance","Load Scene","Layer Turns",
    "Layer A Voice","Layer B Voice","Layer A Root","Layer B Root",
    "Layer A Scale","Layer B Scale","Layer A Octave","Layer B Octave",
    "Layer A Shift","Layer B Shift","Layer A Low","Layer B Low",
    "Layer A Wrap","Layer B Wrap","Layer A Mode","Layer B Mode","Effects","Back"
};
static const uint8_t AUX_FN_MENU_PLAIN   = (uint8_t)AuxFn::CutoffA;   // Fns listed directly
static const uint8_t AUX_FN_MENU_EFFECTS = AUX_FN_MENU_PLAIN;
static const uint8_t AUX_FN_MENU_COUNT   = AUX_FN_MENU_PLAIN + 2;     // + Effects, Back

// Its Effects group: the effect Fns, A then B for each, in chain order.
static const AuxFn   AUX_FN_FX[] = {
    AuxFn::CutoffA, AuxFn::CutoffB, AuxFn::DelayMixA, AuxFn::DelayMixB,
    AuxFn::DelayFeedbackA, AuxFn::DelayFeedbackB, AuxFn::ReverbMixA, AuxFn::ReverbMixB
};
static const char*   AUX_FN_FX_LABELS[] = {
    "Layer A Cutoff","Layer B Cutoff","Layer A Dly Mix","Layer B Dly Mix",
    "Layer A Dly Fb","Layer B Dly Fb","Layer A Rev Mix","Layer B Rev Mix","Back"
};
static const uint8_t AUX_FN_FX_COUNT = sizeof(AUX_FN_FX) / sizeof(AUX_FN_FX[0]) + 1;   // + Back
static uint8_t auxFnFxPos(AuxFn fn) {
    for (uint8_t i = 0; i < AUX_FN_FX_COUNT - 1; i++) if (AUX_FN_FX[i] == fn) return i;
    return 0;
}

// Scene list entries: "0: Defaults", "2: D Minor" (Layer A's key),
// "3: (empty)", then the demos, "Demo1: Shimmer" (the name cut to fit).
// Rebuilt before each draw, since the names come from the slots. Load lists
// every scene, index == scene id. Save lists 1-8, then Back: Defaults
// changes only from the Sound Defaults menu, and demos are read-only.
static const uint8_t LOAD_COUNT = NUM_SCENES + DEMO_COUNT;
static char        SCENE_LABEL_BUF[LOAD_COUNT][16];
static const char* LOAD_ITEMS[LOAD_COUNT];
static const char* SAVE_ITEMS[NUM_SCENES];
static const uint8_t SAVE_COUNT = NUM_SCENES;   // 8 slots + Back

// How far one aux step moves Pitch, as a divisor of a semitone.
static const uint8_t PITCH_STEP_VALUES[] = { 1, 2, 3, 4, 8 };
static const char*   PITCH_STEP_LABELS[] = {
    "1 semitone","1/2 semitone","1/3 semitone","1/4 semitone","1/8 semitone","Back"
};
static const uint8_t PITCH_STEP_COUNT = 6;   // 5 options + Back

// Remembers how the parameter screen was reached, because the aux button is
// always "back" and back means different things on the two paths: straight to
// the live display if the knob was simply turned there, or up to the Fn list if
// that is where the parameter screen was entered from.
static bool auxEnteredFromLive = false;

// Opens the Aux list on the current binding: inside its layer's list for a
// per-layer Fn, else the top list.
static void enterState(MenuState s, uint8_t initialCursor = 0);
static void lcdLine(uint8_t row, const char* fmt, ...);
static void openAuxSelect(const SavedConfig& cfg) {
    AuxFn fn = (AuxFn)cfg.auxFn;
    if (auxIsLayerFn(fn)) {
        auxLayer = auxLayerOf(fn);
        if (auxKindIsFx(auxKindOf(fn))) enterState(MenuState::AuxFxSelect, auxFxPos(auxKindOf(fn)));
        else                            enterState(MenuState::AuxLayerSelect, auxKindPos(auxKindOf(fn)));
        return;
    }
    uint8_t top = 0;
    for (uint8_t i = 0; i < AUXT_COUNT; i++) {
        if (AUX_TOP_FN[i] == fn) { top = i; break; }
    }
    enterState(MenuState::AuxFnSelect, top);
}

// Binds the knob to `fn` and opens its parameter screen. Rebinding IS saved;
// it changes rarely, unlike the values the knob modulates.
static void auxBind(SavedConfig& cfg, AuxFn fn) {
    cfg.auxFn = (uint8_t)fn;
    storageSave(cfg);
    auxEnteredFromLive = false;
    enterState(MenuState::AuxParam, 0);
}

// Beats per platter revolution. 4 = one revolution is one 4/4 bar.
// Every count from 1 to BEATS_MAX, then Back; index + 1 == the value. Filled
// in by menuInit().
static const uint8_t BEATS_MAX   = 32;
static char          BEATS_LABEL_BUF[BEATS_MAX][3];
static const char*   BEATS_LABELS[BEATS_MAX + 1];
static const uint8_t BEATS_COUNT = BEATS_MAX + 1;   // + Back

static const char* WELCOME_ITEMS[] = { "On", "Off", "Back" };
static const uint8_t WELCOME_COUNT = 3;

// LCD timeout option values in seconds (0=always off, 255=always on).
static const uint8_t LCD_TIMEOUT_VALUES[] = { 0, 1, 5, 10, 30, 60, 255 };
static const char*   LCD_TIMEOUT_LABELS[] = {
    "Always Off","1 sec","5 sec","10 sec","30 sec","1 min","Always On","Back"
};
static const uint8_t LCD_TIMEOUT_COUNT = 8;  // 7 options + Back

static const uint8_t CONFIRM_COUNT = 2;

// Calibration and Calib. StartPos can be reached from a boot prompt or the
// Tools menu, and finish back where they came from.
static bool fromBoot = false;

// Find Start jog: slow for placing the mark exactly, faster when the knob is
// spun, stopping shortly after the knob does.
static const float    JOG_FINE_RPM    = 1.5f;
static const float    JOG_FAST_RPM    = 6.0f;
static const uint32_t JOG_FAST_GAP_MS = 80;    // detents closer than this: fast
static const uint32_t JOG_HOLD_MS     = 150;   // keep turning this long per detent
static uint32_t lastJogMs = 0;
static bool     jogMoving = false;

// -------------------------------------------------------------------------

static void enterState(MenuState s, uint8_t initialCursor) {
    state        = s;
    cursor       = initialCursor;
    needsRedraw  = true;
    lastActivity = millis();
}

// Save Scene: the slot being confirmed, and the save itself, which lands back
// on the live display where the scene now shows.
static uint8_t saveSlot = 0;
static void saveScene(SavedConfig& cfg, uint8_t slot) {
    sceneSave(cfg, slot);
    char msg[17];
    snprintf(msg, sizeof(msg), "Saved Scene %u", (unsigned)slot);
    menuMessage(msg, "");
    delay(1000);
    enterState(MenuState::Status);
}

// A Sound Defaults edit: `set` changes one field of the open layer, in the
// Defaults scene and in the live sound alike, so it is heard at once. Only
// that field: other Aux tweaks in play stay live, unsaved. The menu is only
// open with Defaults playing (see MAIN_ITEM_SOUND).
template <typename F>
static void editDefaults(SavedConfig& cfg, F set) {
    set(cfg.scenes[SCENE_DEFAULTS].layer[editLayer]);
    set(cfg.layer[editLayer]);
    storageSave(cfg);
    layersApply(cfg);
}

// --- Scale lists and Edit Scale ---------------------------------------------
// The scale lists: the built-ins, Learned (Aux only, once learned), Custom
// (the layer's own), then each saved Custom 1-8. A list entry is a Scale, or
// SCALE_CHOICE_SLOT + n for Custom n+1. Picking Custom n+1 copies it into the
// layer, so the layer plays Custom from then on.
static const uint8_t SCALE_CHOICE_SLOT = (uint8_t)Scale::COUNT;
static const uint8_t SCALE_CHOICE_MAX  = (uint8_t)Scale::COUNT + NUM_CUSTOM_SCALES + 1;   // + Back
static uint8_t     scaleChoiceIds[SCALE_CHOICE_MAX];
static const char* scaleChoiceLabels[SCALE_CHOICE_MAX];
static const char* CUSTOM_SCALE_NAMES[NUM_CUSTOM_SCALES] = {
    "Custom 1","Custom 2","Custom 3","Custom 4","Custom 5","Custom 6","Custom 7","Custom 8"
};

// Which Custom n each layer last picked, so the list can sit on it while
// the layer still plays it unchanged. RAM only.
static const uint8_t NO_CUSTOM_SCALE = 0xFF;
static uint8_t customScaleFrom[NUM_LAYERS] = { NO_CUSTOM_SCALE, NO_CUSTOM_SCALE };

// Fills the list for a layer; returns the count, Back not included (the
// labels end with it).
static uint8_t buildScaleChoices(const SavedConfig& cfg, const LayerCfg& lc, bool aux) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < SCALE_BUILTIN_COUNT; i++) scaleChoiceIds[n++] = i;
    if (aux && lc.learned) scaleChoiceIds[n++] = (uint8_t)Scale::Learned;
    scaleChoiceIds[n++] = (uint8_t)Scale::Custom;
    for (uint8_t i = 0; i < NUM_CUSTOM_SCALES; i++) {
        if (cfg.customScales[i].used) scaleChoiceIds[n++] = SCALE_CHOICE_SLOT + i;
    }
    for (uint8_t i = 0; i < n; i++) {
        uint8_t id = scaleChoiceIds[i];
        scaleChoiceLabels[i] = (id >= SCALE_CHOICE_SLOT) ? CUSTOM_SCALE_NAMES[id - SCALE_CHOICE_SLOT]
                                                         : SCALE_NAMES[id];
    }
    scaleChoiceLabels[n] = "Back";
    return n;
}

// Where the layer's scale sits in the list just built; `missing` if not in it
// (a Learned scale in the menu's list).
static uint8_t scaleChoicePos(const SavedConfig& cfg, const LayerCfg& lc, uint8_t l,
                              uint8_t n, uint8_t missing) {
    uint8_t id = (uint8_t)lc.scale;
    uint8_t from = customScaleFrom[l];
    if (lc.scale == Scale::Custom && from < NUM_CUSTOM_SCALES && cfg.customScales[from].used &&
        memcmp(lc.custom, cfg.customScales[from].steps, CUSTOM_SCALE_SLOTS) == 0) {
        id = SCALE_CHOICE_SLOT + from;
    }
    for (uint8_t i = 0; i < n; i++) if (scaleChoiceIds[i] == id) return i;
    return missing;
}

// Sets a layer to a list entry. Custom with none set yet starts from the
// layer's current scale, so nothing changes until it is edited.
static void scaleChoose(const SavedConfig& cfg, LayerCfg& c, uint8_t id) {
    if (id >= SCALE_CHOICE_SLOT) {
        memcpy(c.custom, cfg.customScales[id - SCALE_CHOICE_SLOT].steps, CUSTOM_SCALE_SLOTS);
        c.scale = Scale::Custom;
    } else if (id == (uint8_t)Scale::Custom) {
        if (!scaleCustomSet(c.custom)) scaleToCustom(c.scale, c.learned, c.custom, c.custom);
        c.scale = Scale::Custom;
    } else {
        c.scale = (Scale)id;
    }
}

// Edit Scale runs from Sound Defaults (menu knob, editing the Defaults scene)
// or from the Aux (aux knob, editing the live sound); this says which.
static bool    scaleEditAux    = false;
static uint8_t scaleEditSlot   = 0;   // which note ScaleEditSlot changes
static uint8_t scaleSaveTarget = 0;   // Custom n for ScaleSaveConfirm

static uint8_t scaleEditLayer() {
    return scaleEditAux ? auxLayer : editLayer;
}
static const LayerCfg& scaleEditCfg(const SavedConfig& cfg) {
    return scaleEditAux ? cfg.layer[auxLayer] : cfg.scenes[SCENE_DEFAULTS].layer[editLayer];
}
// On the Aux, Layer B in Same as A plays A's scale: leave B's alone.
static bool scaleEditBlocked(const SavedConfig& cfg) {
    return scaleEditAux && auxLayer == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA;
}

// The notes Edit Scale shows: the layer's Custom scale, or until it plays
// one, its current scale's first eight notes.
static void scaleEditSteps(const LayerCfg& lc, int8_t out[CUSTOM_SCALE_SLOTS]) {
    scaleToCustom(lc.scale, lc.learned, lc.custom, out);
}

// A step as a note in the layer's key, with its octave from the layer's
// Octave: "E+0", "C+1", "B-1".
static void scaleStepName(const LayerCfg& lc, int8_t step, char* out, size_t len) {
    int n   = (int)lc.root + step;
    int oct = (n >= 0) ? n / 12 : -((11 - n) / 12);
    snprintf(out, len, "%s%+d", ROOT_ITEMS[n - oct * 12], oct);
}

// The Edit Scale list: "1: C+0" to "8: C+1", Reset, Save As, Back.
static const uint8_t SE_RESET = CUSTOM_SCALE_SLOTS;
static const uint8_t SE_SAVE  = CUSTOM_SCALE_SLOTS + 1;
static const uint8_t SE_BACK  = CUSTOM_SCALE_SLOTS + 2;
static const uint8_t SE_COUNT = CUSTOM_SCALE_SLOTS + 3;
static char        SE_BUF[CUSTOM_SCALE_SLOTS][12];
static const char* SE_ITEMS[SE_COUNT];
static void buildScaleEditItems(const SavedConfig& cfg) {
    const LayerCfg& lc = scaleEditCfg(cfg);
    int8_t steps[CUSTOM_SCALE_SLOTS];
    scaleEditSteps(lc, steps);
    for (uint8_t i = 0; i < CUSTOM_SCALE_SLOTS; i++) {
        char name[8];
        scaleStepName(lc, steps[i], name, sizeof(name));
        snprintf(SE_BUF[i], sizeof(SE_BUF[i]), "%u: %s", (unsigned)(i + 1), name);
        SE_ITEMS[i] = SE_BUF[i];
    }
    SE_ITEMS[SE_RESET] = "Reset";
    SE_ITEMS[SE_SAVE] = "Save As";
    SE_ITEMS[SE_BACK] = "Back";
}

// Changes the layer's scale: the live sound on the Aux, the Defaults scene
// (and the live sound) from Sound Defaults. From Sound Defaults it is saved
// with `save`, so turning a note does not write the EEPROM on every click.
template <typename F>
static void scaleEditSet(SavedConfig& cfg, F set, bool save) {
    if (!scaleEditAux) set(cfg.scenes[SCENE_DEFAULTS].layer[editLayer]);
    set(cfg.layer[scaleEditLayer()]);
    if (!scaleEditAux && save) storageSave(cfg);
}

// One click on a note. The first edit turns the layer's scale into a Custom
// one with the same notes.
static void scaleEditStep(SavedConfig& cfg, int8_t delta) {
    uint8_t s = scaleEditSlot;
    scaleEditSet(cfg, [s, delta](LayerCfg& c) {
        scaleToCustom(c.scale, c.learned, c.custom, c.custom);
        c.scale = Scale::Custom;
        c.custom[s] = (int8_t)constrain((int)c.custom[s] + delta, CUSTOM_STEP_MIN, CUSTOM_STEP_MAX);
    }, false);
    customScaleFrom[scaleEditLayer()] = NO_CUSTOM_SCALE;
}

// The Save As list: "Custom 1" or "Custom 1 empty" to Custom 8, then Back.
static const uint8_t SCALE_SAVE_COUNT = NUM_CUSTOM_SCALES + 1;
static char        SCALE_SAVE_BUF[NUM_CUSTOM_SCALES][16];
static const char* SCALE_SAVE_ITEMS[SCALE_SAVE_COUNT];
static void buildScaleSaveItems(const SavedConfig& cfg) {
    for (uint8_t i = 0; i < NUM_CUSTOM_SCALES; i++) {
        snprintf(SCALE_SAVE_BUF[i], sizeof(SCALE_SAVE_BUF[i]), "%s%s", CUSTOM_SCALE_NAMES[i],
                 cfg.customScales[i].used ? "" : " empty");
        SCALE_SAVE_ITEMS[i] = SCALE_SAVE_BUF[i];
    }
    SCALE_SAVE_ITEMS[NUM_CUSTOM_SCALES] = "Back";
}

// Save As: the notes shown into Custom n, and the layer set to play them as
// Custom (it sounds the same). Then says so and goes back to the list.
static void scaleSaveCustom(SavedConfig& cfg, uint8_t n) {
    int8_t steps[CUSTOM_SCALE_SLOTS];
    scaleEditSteps(scaleEditCfg(cfg), steps);
    cfg.customScales[n].used = true;
    memcpy(cfg.customScales[n].steps, steps, CUSTOM_SCALE_SLOTS);
    scaleEditSet(cfg, [&steps](LayerCfg& c) {
        memcpy(c.custom, steps, CUSTOM_SCALE_SLOTS);
        c.scale = Scale::Custom;
    }, false);
    customScaleFrom[scaleEditLayer()] = n;
    storageSave(cfg);
    char msg[17];
    snprintf(msg, sizeof(msg), "Saved %s", CUSTOM_SCALE_NAMES[n]);
    menuMessage(msg, "");
    delay(1000);
    enterState(MenuState::ScaleEditList, SE_SAVE);
}

// Reset: the layer's scale as it was before the fiddling. On the Aux that is
// the current scene's, as saved; from Sound Defaults, which changes the
// Defaults scene as it goes, it is the scale when Edit Scale was opened.
static LayerCfg scaleEditSnap{};
static void scaleEditReset(SavedConfig& cfg) {
    LayerCfg src = scaleEditAux ? sceneGet(cfg, cfg.currentScene).layer[auxLayer] : scaleEditSnap;
    scaleEditSet(cfg, [&src](LayerCfg& c) {
        c.scale   = src.scale;
        c.learned = src.learned;
        memcpy(c.custom, src.custom, CUSTOM_SCALE_SLOTS);
    }, true);
    customScaleFrom[scaleEditLayer()] = NO_CUSTOM_SCALE;
}

// Where Edit Scale's Back goes.
static void scaleEditLeave() {
    if (scaleEditAux) enterState(MenuState::AuxLayerSelect, AUX_LAYER_EDIT_SCALE);
    else              enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_EDIT_SCALE));
}

// Ends a calibration or StartPos flow: back to the live display if it began
// at a boot prompt, else to the Tools entry it was opened from.
static void leaveTo(uint8_t toolItem) {
    if (fromBoot) enterState(MenuState::Status);
    else          enterState(MenuState::Tools, toolItem);
    fromBoot = false;
}

// Waits, servicing the ramp, until the platter is at rest or `ms` runs out.
static void waitForRest(uint32_t ms) {
    uint32_t deadline = millis() + ms;
    while (stepperRunning() && millis() < deadline) {
        stepperUpdate();
        delay(2);
    }
}

// Turns the platter the shorter way round until it is `phase` steps past the
// start, which puts the mark there. Blocking; the move takes a second or two.
static void goToPhase(uint32_t phase) {
    menuMessage("Moving...", "");
    stepperStop();
    waitForRest(GO_TO_STOP_MS);

    int32_t n     = (int32_t)barStepsPerRev();
    int32_t steps = ((int32_t)phase - (int32_t)barPhase()) % n;
    if (steps < 0)     steps += n;                   // forward to the target
    if (steps > n / 2) steps -= n;                   // shorter the other way
    stepperMoveBy(steps, GO_TO_START_RPM);
    waitForRest(GO_TO_ARRIVE_MS);
}

// The mark to the arm. A check: if the start is right, the mark lands under
// the arm.
static void goToStart() {
    if (!barKnown()) {
        menuMessage("StartPos unknown", "Calib. StartPos");
        delay(2000);
        return;
    }
    goToPhase(0);
}

// The mark to the player, where magnets are easiest to place or take off.
static void goToFront(const SavedConfig& cfg) {
    if (!barKnown() || !cfg.frontKnown) {
        menuMessage(barKnown() ? "Front unknown" : "StartPos unknown", "Calib. StartPos");
        delay(2000);
        return;
    }
    goToPhase(cfg.frontPhase);
}

// Front, where the player sits. Auto and Full Calibrate start with the magnet
// turned by hand to the player, motor off, and count the spin from there to
// the arm. Manual jogs the mark to the player first, then to the arm.
static int32_t frontPos     = 0;       // motor position with the mark at Front
static bool    frontPending = false;   // Manual: Front chosen, StartPos not yet

// Find Start works on a platter at rest: stop it first if it is playing.
static void enterFindStart() {
    if (stepperRunning()) stepperStop();
    jogMoving    = false;
    frontPending = false;
    enterState(MenuState::FindFront);   // Front first, then the arm
}

// A knob jogs the platter, powered, so the step count stays exact. Each
// detent keeps it turning a moment; quick detents turn it faster. Call every
// pass with that knob's delta, 0 included, so the jog stops on time.
static void jogUpdate(int8_t delta) {
    uint32_t now = millis();
    if (delta) {
        float rpm = (now - lastJogMs < JOG_FAST_GAP_MS) ? JOG_FAST_RPM : JOG_FINE_RPM;
        stepperJog(delta > 0 ? rpm : -rpm);
        lastJogMs = now;
        jogMoving = true;
    } else if (jogMoving && now - lastJogMs > JOG_HOLD_MS) {
        stepperStop();
        jogMoving = false;
    }
}

// ---------------------------------------------------------------------------
// Placement Mode: the platter as a workbench for placing magnets at Front.
// Speed jogs (press: Go to Front), Aux snaps to the next step (press: the next
// beat), Volume picks and mutes tracks, Menu exits. Snapping and the readout
// need StartPos and Front.
// ---------------------------------------------------------------------------
static const uint8_t PLACE_STEPS = 16;   // the platter's markings
static uint8_t placeTracks = 0xFF;       // bit i = sensor i plays
static uint8_t placeCursor = 0;          // selected track, 0 = innermost
static bool    placeMoving = false;      // a snap move is under way
static int8_t  placeDir    = 0;          // its direction
static int32_t placeTarget = 0;          // its end, as a motor position
static bool    placeWasRunning = false;  // for one last redraw at rest

static bool placeGridKnown(const SavedConfig& cfg) {
    return barKnown() && cfg.frontKnown;
}

// Where the platter is at Front, in steps of bar time: the point on the
// platter that sits at Front when the platter is at motor position `pos`.
// 0 is the start mark.
static int32_t placeAtFront(const SavedConfig& cfg, int32_t pos) {
    int32_t n = (int32_t)barStepsPerRev();
    int32_t u = ((int32_t)barPhaseAt(pos) - (int32_t)cfg.frontPhase) % n;
    return u < 0 ? u + n : u;
}

// Line k of a grid of `lines` per revolution, in the same units. k may be -1
// or `lines`, a line just past either end.
static int32_t placeLine(int32_t k, uint8_t lines) {
    return (int32_t)((int64_t)k * (int64_t)barStepsPerRev() / lines);
}

// Steps to the next grid line from `u`, forward (dir > 0) or back. Never 0.
static int32_t placeToLine(int32_t u, uint8_t lines, int8_t dir) {
    int32_t n = (int32_t)barStepsPerRev();
    int32_t k = (int32_t)((int64_t)u * lines / n);
    if (dir > 0) {
        while (placeLine(k, lines) <= u) k++;
        return placeLine(k, lines) - u;
    }
    while (placeLine(k, lines) >= u) k--;
    return placeLine(k, lines) - u;
}

// Snap `count` grid lines forward or back. A snap under way in the same
// direction is extended from where it is heading; one the other way, or a
// jog, is left to finish first.
static void placeSnap(const SavedConfig& cfg, uint8_t lines, int8_t dir, uint8_t count) {
    if (!placeGridKnown(cfg) || jogMoving) return;
    if (placeMoving && dir != placeDir) return;
    int32_t from = placeMoving ? placeTarget : stepperPosition();
    int32_t to   = from;
    for (uint8_t i = 0; i < count; i++) {
        to += placeToLine(placeAtFront(cfg, to), lines, dir);
    }
    placeTarget = to;
    placeDir    = dir;
    placeMoving = true;
    stepperMoveBy(to - stepperPosition(), GO_TO_START_RPM);
}

static void enterPlacement() {
    if (stepperRunning()) stepperStop();
    jogMoving   = false;
    placeMoving = false;
    placeTracks = 0xFF;   // all on each time; the mutes are not kept
    placeCursor = 0;
    sequencerSetTrackMask(placeTracks);
    enterState(MenuState::Placement);
}

static void leavePlacement() {
    stepperStop();
    jogMoving   = false;
    placeMoving = false;
    placeTracks = 0xFF;
    sequencerSetTrackMask(placeTracks);
    enterState(MenuState::Tools, TOOL_PLACEMENT);
}

static void drawPlacement(const SavedConfig& cfg) {
    if (placeGridKnown(cfg)) {
        // Tenths of a step, 1-based like the markings: the mark is 1.0.
        int32_t n      = (int32_t)barStepsPerRev();
        int32_t tenths = (int32_t)(((int64_t)placeAtFront(cfg, stepperPosition()) *
                                    PLACE_STEPS * 10 + n / 2) / n) % (PLACE_STEPS * 10);
        lcdLine(0, "Step %d.%d/%u", (int)(tenths / 10 + 1), (int)(tenths % 10),
                (unsigned)PLACE_STEPS);
    } else {
        lcdLine(0, barKnown() ? "Front unknown" : "StartPos unknown");
    }
    // Two columns per track: the cursor, then the number or - if muted.
    char row[17];
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        row[i * 2]     = (i == placeCursor) ? LCD_ARROW_RIGHT : ' ';
        row[i * 2 + 1] = (placeTracks & (1u << i)) ? (char)('1' + i) : '-';
    }
    row[16] = '\0';
    lcdLine(1, "%s", row);
}

// Motor off at rest, for the hand-turn prompts. Turning the platter by hand
// moves the mark unseen, so the old start is forgotten rather than left wrong.
static void releaseForHandTurn(SavedConfig& cfg) {
    stepperStop();
    waitForRest(GO_TO_STOP_MS);
    stepperRelease();
    if (barKnown()) {
        barForget(cfg);
        storageSave(cfg);
    }
}

// The spin from Front found the start: Front is where it began.
static void setFrontFrom(SavedConfig& cfg, int32_t pos) {
    cfg.frontPhase = barPhaseAt(pos);
    cfg.frontKnown = true;
    storageSave(cfg);
}

// Wake the backlight and reset the inactivity timer.
static void backlightActivity(uint8_t lcdTimeout) {
    lastInteraction = millis();
    if (!backlightOn && lcdTimeout != LCD_TIMEOUT_ALWAYS_OFF) {
        lcd.backlight();
        backlightOn = true;
    }
}

static void lcdLine(uint8_t row, const char* fmt, ...) {
    char buf[17];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // pad to 16
    int len = strlen(buf);
    while (len < 16) buf[len++] = ' ';
    buf[16] = '\0';
    lcd.setCursor(0, row);
    lcd.print(buf);
}

static void buildVolBar(char* out, float vol, bool muted) {
    if (muted) { strcpy(out, "[MUTE]"); return; }
    int n = constrain((int)(vol * 4.0f + 0.5f), 0, 4);
    out[0] = '[';
    for (int i = 0; i < 4; i++) out[1 + i] = i < n ? LCD_BLOCK : ' ';
    out[5] = ']';
    out[6] = '\0';
}

static void drawStatus(const SavedConfig& cfg) {
    // BPM carries the platter's direction in its sign, as cfg.rpm does. The
    // right half names the current scene, with * once the live sound differs
    // from it. The key shown is Layer A's.
    int bpm = (int)lroundf(cfg.rpm * cfg.beatsPerRev);
    bool demo = sceneIsDemo(cfg.currentScene);
    lcdLine(0, "BPM:%-4d%s %u%c", bpm, demo ? "Demo" : "Scene",
            (unsigned)(demo ? cfg.currentScene - SCENE_DEMO_FIRST + 1 : cfg.currentScene),
            sceneModified(cfg) ? '*' : ' ');
    const LayerCfg& a = layerEffective(cfg, LAYER_A);
    char vb[7];
    buildVolBar(vb, cfg.volume, cfg.muted);
    lcdLine(1, "%-2s %-7s%s",
        ROOT_ITEMS[static_cast<uint8_t>(a.root) % 12],
        SCALE_NAMES[static_cast<uint8_t>(a.scale) % (uint8_t)Scale::COUNT],
        vb);
}

static void buildSceneLabels(const SavedConfig& cfg) {
    for (uint8_t i = 0; i < NUM_SCENES; i++) {
        const LayerCfg& a = cfg.scenes[i].layer[LAYER_A];
        if (i == SCENE_DEFAULTS) {
            snprintf(SCENE_LABEL_BUF[i], sizeof(SCENE_LABEL_BUF[i]), "0: Defaults");
        } else if (cfg.sceneUsed[i]) {
            snprintf(SCENE_LABEL_BUF[i], sizeof(SCENE_LABEL_BUF[i]), "%u: %s %s", i,
                     ROOT_ITEMS[(uint8_t)a.root % 12],
                     SCALE_NAMES[(uint8_t)a.scale % (uint8_t)Scale::COUNT]);
        } else {
            snprintf(SCENE_LABEL_BUF[i], sizeof(SCENE_LABEL_BUF[i]), "%u: (empty)", i);
        }
        LOAD_ITEMS[i] = SCENE_LABEL_BUF[i];
    }
    for (uint8_t d = 0; d < DEMO_COUNT; d++) {
        uint8_t i = SCENE_DEMO_FIRST + d;
        snprintf(SCENE_LABEL_BUF[i], sizeof(SCENE_LABEL_BUF[i]), "Demo%u: %s",
                 (unsigned)(d + 1), demoName(d));
        LOAD_ITEMS[i] = SCENE_LABEL_BUF[i];
    }
    for (uint8_t i = 1; i < NUM_SCENES; i++) SAVE_ITEMS[i - 1] = SCENE_LABEL_BUF[i];
    SAVE_ITEMS[SAVE_COUNT - 1] = "Back";
}

// Layer mode cues: a layer whose settings have no effect says so, in the
// Sound Defaults list and on its submenu entries. "(=A)": Layer B in Same as
// A plays A's settings. "(off)": the layer is Off. Read from the Defaults
// scene, which is what these menus edit.
static const char* layerCue(const SavedConfig& cfg, uint8_t layer) {
    LayerMode m = cfg.scenes[SCENE_DEFAULTS].layer[layer].mode;
    if (layer == LAYER_B && m == LayerMode::SameAsA) return " (=A)";
    if (m == LayerMode::Off) return " (off)";
    return "";
}

static void buildSoundLabels(const SavedConfig& cfg) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        snprintf(SOUND_LABEL_BUF[l], sizeof(SOUND_LABEL_BUF[l]), "Layer %c%s",
                 'A' + l, layerCue(cfg, l));
        SOUND_ITEMS[l] = SOUND_LABEL_BUF[l];
    }
    SOUND_ITEMS[NUM_LAYERS] = "Back";
}

// Mode is what the cue is about, Layer Turns counts whatever the mode, and
// Back is not a setting: none of them is tagged.
static void buildLayerLabels(const SavedConfig& cfg) {
    const char* cue = layerCue(cfg, editLayer);
    for (uint8_t i = 0; i < layerMenuCount(); i++) {
        uint8_t item = layerMenuItem(i);
        bool tag = (item != LAYER_ITEM_MODE && item != LAYER_ITEM_TURNS &&
                    item != LAYER_ITEM_BACK);
        snprintf(LAYER_LABEL_BUF[i], sizeof(LAYER_LABEL_BUF[i]), "%s%s",
                 LAYER_ITEMS[item], tag ? cue : "");
        LAYER_LABELS[i] = LAYER_LABEL_BUF[i];
    }
}

// The Effects menu as drawn: each effect, and on Layer B each setting,
// tagged "(=A)" while it plays A's (Layer B in Same as A, or that effect set
// to Same as A). Layer B's settings list ends with the Same as A switch.
static const uint8_t FX_LIST_MAX = FXP_COUNT + 2;   // + Same as A, Back
static char        FX_LABEL_BUF[FX_LIST_MAX][17];
static const char* FX_LABELS[FX_LIST_MAX];

static const char* fxCue(const SavedConfig& cfg, FxId fx) {
    const char* cue = layerCue(cfg, editLayer);
    if (*cue) return cue;
    const LayerCfg& lc = cfg.scenes[SCENE_DEFAULTS].layer[editLayer];
    if (editLayer == LAYER_B && (lc.fx.sameAsA & (1u << (uint8_t)fx))) return " (=A)";
    return "";
}

static void buildFxEffectLabels(const SavedConfig& cfg) {
    for (uint8_t i = 0; i < FX_MENU_COUNT; i++) {
        snprintf(FX_LABEL_BUF[i], sizeof(FX_LABEL_BUF[i]), "%s%s",
                 FX_NAMES[(uint8_t)FX_MENU[i]], fxCue(cfg, FX_MENU[i]));
        FX_LABELS[i] = FX_LABEL_BUF[i];
    }
    FX_LABELS[FX_MENU_COUNT] = "Back";
}

// Returns the count, Same as A and Back included; `params` gets the FxParams.
static uint8_t buildFxParamLabels(const SavedConfig& cfg, uint8_t params[FXP_COUNT]) {
    uint8_t n = fxParamsOf(fxMenuEffect, params);
    const char* cue = fxCue(cfg, fxMenuEffect);
    for (uint8_t i = 0; i < n; i++) {
        snprintf(FX_LABEL_BUF[i], sizeof(FX_LABEL_BUF[i]), "%s%s", FX_PARAMS[params[i]].name, cue);
        FX_LABELS[i] = FX_LABEL_BUF[i];
    }
    if (editLayer == LAYER_B) {
        bool bound = cfg.scenes[SCENE_DEFAULTS].layer[LAYER_B].fx.sameAsA & (1u << (uint8_t)fxMenuEffect);
        snprintf(FX_LABEL_BUF[n], sizeof(FX_LABEL_BUF[n]), "Same as A: %s", bound ? "Yes" : "No");
        FX_LABELS[n] = FX_LABEL_BUF[n];
        n++;
    }
    FX_LABELS[n++] = "Back";
    return n;
}

static void drawList(const char** items, uint8_t count, uint8_t cur) {
    if (cur == 0) {
        lcdLine(0, "%c%-15s", LCD_ARROW_RIGHT, items[0]);
        lcdLine(1, count > 1 ? " %-15s" : "", count > 1 ? items[1] : "");
    } else {
        lcdLine(0, " %-15s", items[cur - 1]);
        lcdLine(1, "%c%-15s", LCD_ARROW_RIGHT, items[cur]);
    }
}

// One Info page: the name and page number on top, the value below.
static void drawInfo(const SavedConfig& cfg, uint8_t page) {
    static const char* NAMES[INFO_PAGE_COUNT] = {
        "RPM", "Belt ratio", "StartPos", "Threshold", "Motor driver"
    };
    lcdLine(0, "%-13s%u/%u", NAMES[page], page + 1, INFO_PAGE_COUNT);
    switch (page) {
        case INFO_RPM:
            // The set speed, signed like the BPM (negative is reverse).
            if (stepperRunning()) lcdLine(1, "%d", (int)lroundf(cfg.rpm));
            else                  lcdLine(1, "%d (stopped)", (int)lroundf(cfg.rpm));
            break;
        case INFO_BELT: {
            if (!cfg.calibrated) { lcdLine(1, "Not calibrated"); break; }
            // As shown after calibration: GEAR_RATIO / correction, in tenths.
            int tenths = constrain((int)lroundf((float)GEAR_RATIO * 10.0f /
                                                cfg.rpmCorrection), 0, 999);
            lcdLine(1, "%d.%d:1", tenths / 10, tenths % 10);
            break;
        }
        case INFO_START_POS: {
            if (!barKnown()) { lcdLine(1, "Unknown"); break; }
            // Where the platter is in the bar now, 0 at the mark.
            uint32_t deg = barPhase() * 360UL / barStepsPerRev();
            lcdLine(1, "Known, at %lu%c", (unsigned long)deg, LCD_DEGREE);
            break;
        }
        case INFO_THRESHOLD:
            lcdLine(1, "%u", cfg.hallThreshold);
            break;
        case INFO_DRIVER:
            if (driverVersion == STEPPER_DRIVER_VERSION) lcdLine(1, "OK (v0x%02X)", driverVersion);
            else                                         lcdLine(1, "No reply (0x%02X)", driverVersion);
            break;
    }
}

// Each sensor's live deviation from its resting level, sensors 1-4 on top
// and 5-8 below: the sign (which pole) then the size, capped at 999.
static void drawSensorLevels() {
    char row[2][17];
    for (uint8_t r = 0; r < 2; r++) {
        char* p = row[r];
        for (uint8_t k = 0; k < 4; k++) {
            int16_t d = hallDeviation(r * 4 + k);
            int mag = min(abs((int)d), 999);
            p += snprintf(p, 5, "%c%3d", d < 0 ? '-' : '+', mag);
        }
        lcdLine(r, "%s", row[r]);
    }
}

// One sensor's learned timing. Top: the sensor, passes recorded of the last
// 8, how many of those had no clear peak in time (F: a saturated or weak
// magnet, or its height), and the learned angle from the threshold crossing
// to the peak. Bottom: the last pass's highest deviation (Pk), the noise
// calibration recorded (N), and how many readings the last pass lasted (S).
//   S3 8/8 F2 12.4�
//   Pk1234 N12 S123
static void drawSensorTiming(const SavedConfig& cfg, uint8_t sensor) {
    HallTiming t = hallTiming(sensor);
    char angle[8];
    if (t.learnedSteps == 0) {
        snprintf(angle, sizeof(angle), "  --");
    } else {
        // Tenths of a degree by integer maths.
        uint32_t tenths = (t.learnedSteps * 3600UL + stepperStepsPerRev() / 2) / stepperStepsPerRev();
        snprintf(angle, sizeof(angle), "%2lu.%lu", (unsigned long)min(tenths / 10, 99UL),
                 (unsigned long)(tenths % 10));
    }
    lcdLine(0, "S%u %u/8 F%u %s%c", sensor + 1, t.passes, t.fallbacks, angle, LCD_DEGREE);
    lcdLine(1, "Pk%u N%u S%u", t.lastPeak, cfg.hallNoise[sensor], min(t.lastSamples, (uint16_t)999));
}

// Track Notes: what each track plays right now, after everything that moves
// it (root, scale, octave, Pitch, Shift, Wrap, Low Note), Layer A on top,
// Layer B below. A 2-character cell per track, hall 1 (inner) on the left:
//   C D E F G A B C
//   X t T C O S K H
// The menu knob flips to the keys: "A: G Blues" / "B: Drums".
static const char* NOTE_SHARP[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
static const char* NOTE_FLAT[12]  = { "C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B" };

// One letter per drum slot (kit.h): crash, low and high tom, clap, open hat,
// snare, kick, closed hat.
static const char* DRUM_LETTERS[NUM_HALL_SENSORS] = { "X","t","T","C","O","S","K","H" };

// Flats when the scale's parent major key is a flat key (F, Bb, Eb, Ab, Db),
// as a key signature would: D Minor gets Bb, E Minor F#. Chromatic and
// Learned have no key and use sharps.
static bool spellFlats(uint8_t root, Scale scale) {
    uint8_t toMajor;
    switch (scale) {
        case Scale::Major: case Scale::PentatonicMajor:                   toMajor = 0;  break;
        case Scale::Minor: case Scale::PentatonicMinor: case Scale::Blues: toMajor = 3;  break;
        case Scale::Dorian:                                               toMajor = 10; break;
        case Scale::Mixolydian:                                           toMajor = 5;  break;
        default: return false;
    }
    uint8_t key = (uint8_t)((root + toMajor) % 12);
    return key == 5 || key == 10 || key == 3 || key == 8 || key == 1;
}

// The layer's root with the Pitch offset's whole semitones, as it sounds.
static uint8_t soundingRoot(const LayerCfg& lc) {
    return (uint8_t)((((int)lc.root + pitchNoteShift()) % 12 + 12) % 12);
}

static void trackNotesRow(const SavedConfig& cfg, uint8_t l, char out[17]) {
    const Voice& v = layerVoice(cfg, l);
    if (!layerActive(cfg, l) || voiceIsSilent(v)) {
        snprintf(out, 17, "%c: off", 'A' + l);
        return;
    }
    const LayerCfg& lc = layerEffective(cfg, l);
    bool kit = voiceIsKit(v);
    const char* const* names = spellFlats(soundingRoot(lc), lc.scale) ? NOTE_FLAT : NOTE_SHARP;
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint8_t d = layerDegree(cfg, l, i);
        const char* name;
        if (kit) {
            name = DRUM_LETTERS[d % NUM_HALL_SENSORS];
        } else {
            int note = scaleNote(lc.root, lc.scale, lc.learned, lc.custom, d, (uint8_t)constrain(lc.octave, 0, 9))
                     + pitchNoteShift();
            name = names[(note % 12 + 12) % 12];
        }
        snprintf(out + 2 * i, 3, "%-2s", name);
    }
}

static void trackKeyRow(const SavedConfig& cfg, uint8_t l, char out[17]) {
    const Voice& v = layerVoice(cfg, l);
    char c = 'A' + l;
    if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) {
        snprintf(out, 17, "%c: Same as A", c);
    } else if (!layerActive(cfg, l)) {
        snprintf(out, 17, "%c: Off", c);
    } else if (voiceIsKit(v) || voiceIsSilent(v)) {
        snprintf(out, 17, "%c: %s", c, v.name);
    } else {
        const LayerCfg& lc = layerEffective(cfg, l);
        uint8_t root = soundingRoot(lc);
        snprintf(out, 17, "%c: %s %s", c,
                 (spellFlats(root, lc.scale) ? NOTE_FLAT : NOTE_SHARP)[root],
                 SCALE_NAMES[(uint8_t)lc.scale % (uint8_t)Scale::COUNT]);
    }
}

static void drawTrackNotes(const SavedConfig& cfg, uint8_t view) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        char row[17];
        if (view == 0) trackNotesRow(cfg, l, row);
        else           trackKeyRow(cfg, l, row);
        lcdLine(l, "%s", row);
    }
}

// Apply one aux knob step to whatever the knob is bound to.
//
// LIVE ONLY. These are performance moves on top of the current scene, not
// configuration: power-up and Aux Reset All go back to the scene as saved,
// and only Save Scene keeps them. Nothing here writes EEPROM.
//
// Wrap vs clamp follows the shape of the value: wrap anything cyclic, clamp
// anything that is a magnitude. Octave is a magnitude, and wrapping 7 back to
// 0 would be a seven-octave jump mid-performance. Track Shift is either: with
// Wrap it is a rotation, 7 to 0 moving one sensor; with No Wrap it transposes
// the whole run, and 7 to 0 would drop every sensor an octave at once. Low
// Note clamps too, right = Outer as listed, so a stray click cannot flip it.
static void auxApplyDelta(SavedConfig& cfg, int8_t delta) {
    AuxFn fn = (AuxFn)cfg.auxFn;
    switch (fn) {
        case AuxFn::Pitch: {
            // One offset for the whole table, so both layers move together
            // and the interval between them holds.
            uint8_t div = cfg.pitchStepDiv ? cfg.pitchStepDiv : 1;
            pitchAdjust((float)delta / (float)div);
            return;
        }
        case AuxFn::Balance:
            layerSetBalance(layerBalance() + delta);           // clamp: a range
            return;
        case AuxFn::Turns: {
            // Kept in Layer B, but about both layers, so it turns whatever
            // B's mode.
            int v = (int)cfg.layer[LAYER_B].turns + delta;
            cfg.layer[LAYER_B].turns = (LayerTurns)constrain(v, 0, TURNS_COUNT - 2);   // clamp: a list
            return;
        }
        case AuxFn::LoadScene: {
            // Steps through the used scenes, wrapping, and queues the one
            // landed on to load at the next bar start. Defaults is always
            // there, so the loop always finds one.
            int step = (delta > 0) ? 1 : -1;
            int pos  = sceneSelected(cfg);
            for (int n = abs(delta); n > 0; n--) {
                do { pos = (pos + step + LOAD_COUNT) % LOAD_COUNT; }
                while (!sceneUsed(cfg, (uint8_t)pos));
            }
            sceneQueue(cfg, (uint8_t)pos);
            return;
        }
        default:
            break;
    }
    if (!auxIsLayerFn(fn)) return;

    uint8_t l = auxLayerOf(fn);
    // B in Same as A plays A's settings, so turning B's would do nothing
    // audible; leave it alone rather than change a hidden setting. Its Mode
    // is the way out of Same as A, so that one still turns.
    if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA &&
        auxKindOf(fn) != AuxKind::Mode) return;
    LayerCfg& lc = cfg.layer[l];

    switch (auxKindOf(fn)) {
        case AuxKind::Voice: {
            uint8_t n = buildVoiceChoices(cfg, l, true);
            int v = ((int)voiceChoicePos(lc.voice, n) + delta) % n;
            if (v < 0) v += n;
            lc.voice = voiceChoiceIds[v];                      // wrap: a list
            layersApply(cfg);
            break;
        }
        case AuxKind::Root: {
            int v = ((int)lc.root + delta) % 12;
            if (v < 0) v += 12;
            lc.root = (RootNote)v;                             // wrap: a circle
            break;
        }
        case AuxKind::Scale: {
            // Learned is in the list only once this layer has learned one;
            // Custom and the saved Custom 1-8 follow.
            int n = buildScaleChoices(cfg, lc, true);
            int v = ((int)scaleChoicePos(cfg, lc, l, n, 0) + delta) % n;
            if (v < 0) v += n;
            uint8_t id = scaleChoiceIds[v];                    // wrap: a list
            scaleChoose(cfg, lc, id);
            customScaleFrom[l] = (id >= SCALE_CHOICE_SLOT) ? id - SCALE_CHOICE_SLOT : NO_CUSTOM_SCALE;
            break;
        }
        case AuxKind::Octave: {
            int v = (int)lc.octave + delta;
            lc.octave = (uint8_t)constrain(v, 0, 7);           // clamp: a range
            break;
        }
        case AuxKind::Shift: {
            // B playing A's shift: turning B's would change a hidden setting.
            if (layerShiftSource(cfg, l) != l) break;
            int v = (int)lc.shift + delta;
            if (layerWraps(cfg, l)) {                          // kits always wrap
                v %= NUM_HALL_SENSORS;
                if (v < 0) v += NUM_HALL_SENSORS;              // wrap: a rotation
            } else {
                v = constrain(v, 0, NUM_HALL_SENSORS - 1);     // clamp: a transpose
            }
            lc.shift = (uint8_t)v;
            break;
        }
        case AuxKind::LowNote: {
            if (layerLowNoteSource(cfg, l) != l) break;        // playing A's
            int v = (int)lc.lowNote + delta;
            lc.lowNote = (uint8_t)constrain(v, 0, LOW_NOTE_VALUES - 1);
            break;
        }
        case AuxKind::Wrap: {
            // Wrap goes with the shift: B playing A's shift plays A's Wrap.
            if (layerShiftSource(cfg, l) != l) break;
            int v = (lc.wrap ? 0 : 1) + delta;                 // index 0 = Wrap
            lc.wrap = constrain(v, 0, 1) == 0;                 // clamp: a list
            break;
        }
        case AuxKind::Mode: {
            // Layer A has On and Off; B also Same as A and Stack.
            int n = (l == LAYER_A) ? MODE_COUNT_A - 1 : MODE_COUNT_B - 1;   // - Back
            int v = (int)lc.mode + delta;
            lc.mode = (LayerMode)constrain(v, 0, n - 1);       // clamp: a list
            layersApply(cfg);   // Same as A changes which voice B plays
            break;
        }
        case AuxKind::Cutoff:
        case AuxKind::ReverbMix:
        case AuxKind::DelayMix:
        case AuxKind::DelayFeedback: {
            // B playing A's effect: turning B's would change a hidden setting.
            uint8_t p = auxFxParam(auxKindOf(fn));
            if (layerFxSource(cfg, l, FX_PARAMS[p].fx) != l) break;
            int n = buildFxValueLabels(p, lc.fx, false);
            int v = (int)fxValueIndex(p, lc.fx) + delta;
            fxSetIndex(p, lc.fx, (uint8_t)constrain(v, 0, n - 1));   // clamp: a magnitude
            layersApplyEffects(cfg);
            break;
        }
        default:
            break;
    }
}

// A per-layer Fn's parameter screen. Layer B in Same as A says so instead,
// as do B's Shift and Low Note while bound to A's.
static void drawAuxLayerParam(const SavedConfig& cfg, AuxKind kind, uint8_t l) {
    if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA &&
        kind != AuxKind::Mode) {
        lcdLine(0, "Layer B is");
        lcdLine(1, "Same as A");
        return;
    }
    const LayerCfg& lc = cfg.layer[l];
    // Explicit counts: these lists carry a trailing "Back" for menu use that
    // has no meaning here, where the button is already back.
    switch (kind) {
        case AuxKind::Voice: {
            // A tweaked voice is marked, "Piano*", so you know it is not stock.
            uint8_t n   = buildVoiceChoices(cfg, l, true);
            uint8_t cur = voiceChoicePos(lc.voice, n);
            if (layerVoiceIsTweaked(cfg, l)) {
                snprintf(voiceChoiceMarked, sizeof(voiceChoiceMarked), "%s*", voiceChoiceLabels[cur]);
                voiceChoiceLabels[cur] = voiceChoiceMarked;
            }
            drawList(voiceChoiceLabels, n, cur);
            break;
        }
        case AuxKind::Root:
            drawList(ROOT_ITEMS, 12, (uint8_t)lc.root % 12);
            break;
        case AuxKind::Scale: {
            uint8_t n = buildScaleChoices(cfg, lc, true);
            drawList(scaleChoiceLabels, n, scaleChoicePos(cfg, lc, l, n, 0));
            break;
        }
        case AuxKind::Octave:
            drawList(OCTAVE_ITEMS, 8, (uint8_t)constrain(lc.octave, 0, 7));
            break;
        case AuxKind::Shift:
            if (l == LAYER_B && lc.shiftSameAsA) {
                lcdLine(0, "B Shift is");
                lcdLine(1, "Same as A");
            } else {
                drawList(SHIFT_ITEMS_A, NUM_HALL_SENSORS,
                         (uint8_t)constrain(lc.shift, 0, NUM_HALL_SENSORS - 1));
            }
            break;
        case AuxKind::LowNote:
            if (l == LAYER_B && lc.lowNoteSameAsA) {
                lcdLine(0, "B Low Note is");
                lcdLine(1, "Same as A");
            } else {
                drawList(LOW_NOTE_ITEMS_A, LOW_NOTE_VALUES,
                         (uint8_t)constrain(lc.lowNote, 0, LOW_NOTE_VALUES - 1));
            }
            break;
        case AuxKind::Wrap:
            if (l == LAYER_B && lc.shiftSameAsA) {
                lcdLine(0, "B Shift is");
                lcdLine(1, "Same as A");
            } else if (voiceIsKit(layerVoice(cfg, l))) {
                lcdLine(0, "Drums always");
                lcdLine(1, "wrap");
            } else {
                drawList(WRAP_ITEMS, 2, lc.wrap ? 0 : 1);
            }
            break;
        case AuxKind::Mode:
            if (l == LAYER_A) drawList(MODE_ITEMS_A, MODE_COUNT_A - 1, (uint8_t)lc.mode);
            else              drawList(MODE_ITEMS_B, MODE_COUNT_B - 1, (uint8_t)lc.mode);
            break;
        case AuxKind::Cutoff:
        case AuxKind::ReverbMix:
        case AuxKind::DelayMix:
        case AuxKind::DelayFeedback: {
            uint8_t p = auxFxParam(kind);
            FxId    fx = FX_PARAMS[p].fx;
            if (layerFxSource(cfg, l, fx) != l) {
                lcdLine(0, "B %s is", FX_NAMES[(uint8_t)fx]);
                lcdLine(1, "Same as A");
            } else {
                drawList(fxValueLabels, buildFxValueLabels(p, lc.fx, false), fxValueIndex(p, lc.fx));
            }
            break;
        }
        default:
            break;
    }
}

// Both lines go to the parameter itself. The Fn name is deliberately NOT shown:
// you already know what you just bound the knob to, and spending half a 16x2
// display repeating it costs the line that could show where the value sits
// relative to its neighbours.
//
// List-shaped parameters therefore get the normal two-line list with the cursor
// on the current value, so the next value is visible before you turn into it.
// Pitch is continuous rather than a list, so it shows its offset and the step
// size currently in force.
// Voice Edit's list, or why this layer's voice cannot be edited.
static void drawVoiceEditList(const SavedConfig& cfg) {
    if (voiceEditable(cfg, auxLayer)) {
        buildVoiceEditLabels(cfg);
        drawList(veLabels, VE_COUNT, cursor);
        return;
    }
    if (auxLayer == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) {
        lcdLine(0, "Layer B is");
        lcdLine(1, "Same as A");
    } else {
        lcdLine(0, "%s can't be", layerVoice(cfg, auxLayer).name);
        lcdLine(1, "edited");
    }
}

// One voice setting: its name on top, the value below.
static void drawVoiceEditParam(const SavedConfig& cfg) {
    const Voice& v = layerVoice(cfg, auxLayer);
    lcdLine(0, "%s", VOICE_EDIT_LABELS[voiceEditParam]);
    switch (voiceEditParam) {
        case VE_WAVE:    lcdLine(1, "%s%s", voiceWaveName(voiceWave(v.waveform)),
                                 v.harmonicsEdited ? "*" : "");                     break;
        case VE_ATTACK:  lcdLine(1, "%u ms", (unsigned)v.attackMs);                 break;
        case VE_DECAY:   lcdLine(1, "%u ms", (unsigned)v.decayMs);                  break;
        case VE_SUSTAIN: lcdLine(1, "%d%%", (int)lroundf(v.sustain * 100.0f));      break;
        case VE_RELEASE: lcdLine(1, "%u ms", (unsigned)v.releaseMs);                break;
        case VE_LENGTH:  lcdLine(1, "%u ms", (unsigned)v.noteMs);                   break;
        default:         lcdLine(1, "");                                            break;
    }
}

// One harmonic's level: which one on top, the level below.
static void drawHarmonicParam(const SavedConfig& cfg) {
    uint8_t levels[NUM_HARMONICS];
    harmonicLevels(layerVoice(cfg, auxLayer), levels);
    lcdLine(0, "H%u", (unsigned)(harmonicSel + 1));
    lcdLine(1, "%u%%", (unsigned)levels[harmonicSel]);
}

// The first change starts the levels from the stock wave, so it sounds the
// same as before apart from that one harmonic. A click that changes nothing
// (already at 0 or 100%) does not count as an edit.
static void harmonicEditApply(SavedConfig& cfg, int8_t delta) {
    Voice& v = layerVoiceEdit(cfg, auxLayer);
    uint8_t levels[NUM_HARMONICS];
    harmonicLevels(v, levels);
    uint8_t next = harmStep(levels[harmonicSel], delta);
    if (next == levels[harmonicSel]) return;
    levels[harmonicSel] = next;
    memcpy(v.harmonics, levels, NUM_HARMONICS);
    v.harmonicsEdited = true;
    layerVoiceTweaked(cfg, auxLayer);
}

// One Voice Edit step, heard from the next note. Wave wraps (a list); the
// rest clamp (magnitudes). A new wave drops any harmonic edits.
static void voiceEditApply(SavedConfig& cfg, int8_t delta) {
    Voice& v = layerVoiceEdit(cfg, auxLayer);
    switch (voiceEditParam) {
        case VE_WAVE: {
            int w = ((int)voiceWave(v.waveform) + delta) % WAVE_COUNT;
            if (w < 0) w += WAVE_COUNT;
            v.waveform        = voiceWaveform((Wave)w);
            v.harmonicsEdited = false;
            break;
        }
        case VE_ATTACK:  v.attackMs  = timeStep(v.attackMs,  delta, 0,  2000); break;
        case VE_DECAY:   v.decayMs   = timeStep(v.decayMs,   delta, 0,  2000); break;
        case VE_RELEASE: v.releaseMs = timeStep(v.releaseMs, delta, 0,  3000); break;
        case VE_LENGTH:  v.noteMs    = timeStep(v.noteMs,    delta, 10, 2000); break;
        case VE_SUSTAIN: {
            int pct = (int)lroundf(v.sustain * 20.0f) * 5 + delta * 5;   // 5% steps
            v.sustain = (float)constrain(pct, 0, 100) / 100.0f;
            break;
        }
        default: return;
    }
    layerVoiceTweaked(cfg, auxLayer);
}

// One filter setting: its name on top, the value below.
static void drawFilterParam(const SavedConfig& cfg) {
    const VoiceFilter& f = layerVoice(cfg, auxLayer).filter;
    lcdLine(0, "Filter %s", FILTER_LABELS[filterParam]);
    switch (filterParam) {
        case VF_CUTOFF:
            if (f.cutoff >= VOICE_FILTER_OFF) lcdLine(1, "Off");
            else                              lcdLine(1, "%u%%", (unsigned)f.cutoff);
            break;
        case VF_RESONANCE: lcdLine(1, "%u%%", (unsigned)f.resonance);   break;
        case VF_AMOUNT:    lcdLine(1, "%u%%", (unsigned)f.amount);      break;
        case VF_ATTACK:    lcdLine(1, "%u ms", (unsigned)f.attackMs);   break;
        case VF_DECAY:     lcdLine(1, "%u ms", (unsigned)f.decayMs);    break;
        case VF_SUSTAIN:   lcdLine(1, "%u%%", (unsigned)f.sustainPct);  break;
        case VF_RELEASE:   lcdLine(1, "%u ms", (unsigned)f.releaseMs);  break;
        default:           lcdLine(1, "");                              break;
    }
}

// One filter step, heard from the next note. All clamp (magnitudes). The
// percentages move in 10% steps, as the layer's Tone; Sustain in 5%, as the
// voice's own; the times as the envelope's.
static void filterEditApply(SavedConfig& cfg, int8_t delta) {
    VoiceFilter& f = layerVoiceEdit(cfg, auxLayer).filter;
    auto pct10 = [delta](uint8_t v) { return (uint8_t)constrain(((int)v + 5) / 10 * 10 + delta * 10, 0, 100); };
    switch (filterParam) {
        case VF_CUTOFF:    f.cutoff    = pct10(f.cutoff);    break;
        case VF_RESONANCE: f.resonance = pct10(f.resonance); break;
        case VF_AMOUNT:    f.amount    = pct10(f.amount);    break;
        case VF_ATTACK:    f.attackMs  = timeStep(f.attackMs,  delta, 0, 2000); break;
        case VF_DECAY:     f.decayMs   = timeStep(f.decayMs,   delta, 0, 2000); break;
        case VF_RELEASE:   f.releaseMs = timeStep(f.releaseMs, delta, 0, 3000); break;
        case VF_SUSTAIN:
            f.sustainPct = (uint8_t)constrain(((int)f.sustainPct + 2) / 5 * 5 + delta * 5, 0, 100);
            break;
        default: return;
    }
    layerVoiceTweaked(cfg, auxLayer);
}

static void drawAuxParam(const SavedConfig& cfg) {
    AuxFn fn = (AuxFn)cfg.auxFn;
    if (auxIsLayerFn(fn)) {
        drawAuxLayerParam(cfg, auxKindOf(fn), auxLayerOf(fn));
        return;
    }
    switch (fn) {
        case AuxFn::Pitch: {
            // Cents rather than semitones so fractional steps read sensibly,
            // and so nothing here depends on %f in snprintf.
            int cents = (int)lroundf(pitchGetOffset() * 100.0f);
            lcdLine(0, "%+d cents", cents);

            uint8_t idx = 0;
            for (uint8_t i = 0; i < PITCH_STEP_COUNT - 1; i++) {
                if (PITCH_STEP_VALUES[i] == cfg.pitchStepDiv) { idx = i; break; }
            }
            lcdLine(1, "step %s", PITCH_STEP_LABELS[idx]);
            break;
        }
        case AuxFn::Balance: {
            // Each side's share, then a slider: A on the left, B on the right.
            int b = layerBalance();
            int pctA = (b <= 0) ? 100 : (BALANCE_STEPS - b) * 100 / BALANCE_STEPS;
            int pctB = (b >= 0) ? 100 : (BALANCE_STEPS + b) * 100 / BALANCE_STEPS;
            lcdLine(0, "A %3d%%    B %3d%%", pctA, pctB);

            char bar[17];
            const int track = 14;   // cells between the A and B end markers
            int pos = (b + BALANCE_STEPS) * (track - 1) / (2 * BALANCE_STEPS);
            bar[0] = 'A';
            for (int i = 0; i < track; i++) bar[1 + i] = (i == pos) ? LCD_BLOCK : '-';
            bar[15] = 'B';
            bar[16] = 0;
            lcdLine(1, "%s", bar);
            break;
        }
        case AuxFn::Turns:
            drawList(TURNS_ITEMS, TURNS_COUNT - 1, (uint8_t)cfg.layer[LAYER_B].turns);   // - Back
            break;
        case AuxFn::LoadScene:
            buildSceneLabels(cfg);
            drawList(LOAD_ITEMS, LOAD_COUNT, sceneSelected(cfg));
            break;
        default:
            lcdLine(0, "");
            lcdLine(1, "");
            break;
    }
}


// -------------------------------------------------------------------------

// Waits out part of the welcome tune, returning true early if the menu button
// is pressed. The tune blocks loop(), so this keeps the encoders read and the
// platter's ramp serviced while it waits. Only the menu press is taken; any
// other input stays pending for menuUpdate().
static bool tuneWait(uint32_t ms) {
    uint32_t start = millis();
    do {
        stepperUpdate();
        encoderUpdate();
        if (encoderTakeMenuPress()) return true;
        delay(1);
    } while (millis() - start < ms);
    return false;
}

// Plays each sensor's Layer A note, hall 1 to hall 8 and back, in sixteenth
// notes at the current BPM.
// Blocking -- returns when the last note's release has finished, or at once if
// the menu button is pressed. A slow saved tempo makes the full tune long, and
// the press is the way to skip it. The caller carries on as if it had finished,
// so a cancel skips only the tune, never a prompt that follows it.
static void playWelcomeTune(const SavedConfig& cfg) {
    int bpm = (int)(fabsf(cfg.rpm) * cfg.beatsPerRev);
    bpm = constrain(bpm, 40, 200);
    uint32_t stepMs = 60000UL / (uint32_t)bpm / 4;   // a sixteenth
    uint32_t noteMs  = min((uint32_t)layerVoice(cfg, LAYER_A).noteMs, stepMs);
    uint8_t  channel = layerChannel(cfg, LAYER_A);
    bool     kit     = voiceIsKit(layerVoice(cfg, LAYER_A));
    bool     silent  = voiceIsSilent(layerVoice(cfg, LAYER_A));
    const LayerCfg& a = layerEffective(cfg, LAYER_A);
    uint8_t  octave  = (uint8_t)constrain(a.octave, 0, 9);

    lcdLine(0, "  Music  Table  ");
    lcdLine(1, "~~~~~~~~~~~~~~~~");

    // Each step is what that sensor plays on Layer A, key, octave, Track
    // Shift, Wrap and Low Note included, so the tune previews the table. A
    // kit voice plays its drums; voice None keeps time in silence. Returns true if cancelled; the note is still
    // turned off, so a cancel never leaves one hanging.
    auto step = [&](uint8_t i) -> bool {
        uint8_t degree = layerDegree(cfg, LAYER_A, i);
        bool cancelled;
        if (silent) {
            cancelled = tuneWait(noteMs);
        } else if (kit) {
            uint8_t sounded = midiDrumOn(channel, degree, 100);
            cancelled = tuneWait(noteMs);
            midiDrumOff(channel, sounded);
        } else {
            uint8_t note = scaleNote(a.root, a.scale, a.learned, a.custom, degree, octave);
            uint8_t sounded = midiNoteOn(LAYER_A, channel, note, 100);
            cancelled = tuneWait(noteMs);
            midiNoteOff(LAYER_A, channel, sounded);
        }
        return cancelled || tuneWait(stepMs - noteMs);
    };

    // Forward pass.
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        if (step(i)) return;
    }

    // Pause for one sixteenth.
    if (tuneWait(stepMs)) return;

    // Reverse pass.
    for (int8_t i = NUM_HALL_SENSORS - 1; i >= 0; i--) {
        if (step((uint8_t)i)) return;
    }

    // Let the last note's release tail finish.
    tuneWait(400);
}

// -------------------------------------------------------------------------

void menuInit(const SavedConfig& cfg) {
    lcd.init();


    CHANNEL_ITEMS[0] = "Auto";
    for (uint8_t c = 1; c <= 16; c++) {
        snprintf(CHANNEL_LABEL_BUF[c - 1], sizeof(CHANNEL_LABEL_BUF[0]), "Ch %u", c);
        CHANNEL_ITEMS[c] = CHANNEL_LABEL_BUF[c - 1];
    }
    CHANNEL_ITEMS[17] = "Back";

    for (uint8_t b = 1; b <= BEATS_MAX; b++) {
        snprintf(BEATS_LABEL_BUF[b - 1], sizeof(BEATS_LABEL_BUF[0]), "%u", b);
        BEATS_LABELS[b - 1] = BEATS_LABEL_BUF[b - 1];
    }
    BEATS_LABELS[BEATS_MAX] = "Back";

    for (uint8_t i = 0; i < DELAY_FREE_COUNT; i++) {
        snprintf(FREE_TIME_BUF[i], sizeof(FREE_TIME_BUF[i]), "%u ms", (unsigned)delayFreeMs(i));
    }

    MIDI_IN_ITEMS[0] = "Off";
    for (uint8_t c = 1; c <= 16; c++) MIDI_IN_ITEMS[c] = CHANNEL_ITEMS[c];
    MIDI_IN_ITEMS[17] = "Back";

    // Apply initial backlight state based on saved timeout setting.
    if (cfg.lcdTimeout == LCD_TIMEOUT_ALWAYS_OFF) {
        lcd.noBacklight();
        backlightOn = false;
    } else {
        lcd.backlight();
        backlightOn = true;
    }

    lastInteraction = millis();

    if (cfg.playWelcomeTune) playWelcomeTune(cfg);

    // A fresh EEPROM has never measured the sensors, and the defaults cannot
    // fit all eight: their rest levels differ by more than the re-arm window,
    // so some sensors would fire once and then go quiet. Offer to fix that
    // before the table is played rather than letting it look like a fault.
    if (!cfg.calibrated) enterState(MenuState::FirstBootPrompt);
    // The bar start was lost (power cut while spinning, or never found):
    // offer to find it, unless that check has been turned off.
    else if (!barKnown() && cfg.startCheck) enterState(MenuState::StartCheckPrompt);
    else                 enterState(MenuState::Status);
}

void menuMessage(const char* line1, const char* line2) {
    lcdLine(0, "%-16s", line1);
    lcdLine(1, "%-16s", line2);
}

void menuUpdate(SavedConfig& cfg) {
    EncoderEvent ev = encoderEvents();

    // Any encoder activity resets the backlight timer.
    bool anyActivity = ev.speedDelta != 0 || ev.speedPressed  ||
                       ev.volumeDelta != 0 || ev.volumePressed ||
                       ev.menuDelta != 0  || ev.menuPressed    ||
                       ev.auxDelta != 0   || ev.auxPressed;
    if (anyActivity) backlightActivity(cfg.lcdTimeout);

    // Backlight timeout check.
    if (backlightOn &&
        cfg.lcdTimeout != LCD_TIMEOUT_ALWAYS_ON &&
        cfg.lcdTimeout != LCD_TIMEOUT_ALWAYS_OFF) {
        if (millis() - lastInteraction > (uint32_t)cfg.lcdTimeout * 1000UL) {
            lcd.noBacklight();
            backlightOn = false;
        }
    }

    // Placement Mode takes over the speed, volume and aux knobs. Speed and
    // volume are kept from the live controls below; aux stays in ev.
    // At the "Magnet at front" prompts the speed knob jogs instead, so the
    // mark can be brought to the player without touching the playing speed.
    EncoderEvent place{};
    if (state == MenuState::Placement) {
        place = ev;
        ev.speedDelta  = 0;  ev.speedPressed  = false;
        ev.volumeDelta = 0;  ev.volumePressed = false;
    } else if (state == MenuState::CalMagnetPrompt ||
               state == MenuState::StartPosAutoPrompt) {
        jogUpdate(ev.speedDelta);   // position counted, so Front stays exact
        ev.speedDelta = 0;  ev.speedPressed = false;
    }

    // live speed control - all states except Placement Mode and the
    // "Magnet at front" prompts
    //
    // cfg.rpm is SIGNED: negative means the platter runs in reverse. Turning
    // down past the low end passes through a stop and out the other side into
    // reverse. The platter cannot usefully turn below MIN_RPM, so the band
    // between -MIN_RPM and +MIN_RPM is a dead zone that reads as zero, and
    // leaving zero jumps straight to +/-MIN_RPM rather than crawling back up
    // through a dead zone it could never escape one detent at a time.
    // Stopped with a press, the platter is at zero too: turning starts from
    // +/-MIN_RPM the way the knob turns, not from the old speed. The press
    // is the way to resume it.
    if (ev.speedDelta != 0) {
        float r;
        if (cfg.rpm == 0.0f || !stepperRunning()) {
            r = (ev.speedDelta > 0) ? MIN_RPM : -MIN_RPM;
        } else {
            r = cfg.rpm + ev.speedDelta;
            if (fabsf(r) < MIN_RPM) r = 0.0f;
        }
        cfg.rpm = constrain(r, -MAX_RPM, MAX_RPM);

        if (cfg.rpm == 0.0f) stepperStop();
        else                 stepperStart(cfg.rpm);

        storageSave(cfg);
        needsRedraw = true;
    }
    if (ev.speedPressed) {
        if (stepperRunning()) stepperStop();
        else if (cfg.rpm != 0.0f) stepperStart(cfg.rpm);
        needsRedraw = true;
    }

    // live volume control - all states
    if (ev.volumeDelta != 0) {
        cfg.volume = constrain(cfg.volume + ev.volumeDelta * 0.05f, 0.0f, 1.0f);
        audioSetVolume(cfg.volume);
        storageSave(cfg);
        needsRedraw = true;
    }
    if (ev.volumePressed) {
        cfg.muted = !cfg.muted;
        cfg.muted ? audioMute() : audioUnmute();
        storageSave(cfg);
        needsRedraw = true;
    }

    // Prompts are questions waiting on an answer. Neither the menu timeout nor
    // a brushed speed or volume knob may dismiss one: they ask you to go and do
    // something physical (place a magnet, decide about wiping settings), which
    // reliably takes longer than the menu timeout, and having the question
    // vanish mid-task was a real bug. They stay put until answered.
    bool isPrompt = (state == MenuState::FirstBootPrompt ||
                     state == MenuState::CalClearPrompt ||
                     state == MenuState::CalSampling ||
                     state == MenuState::CalMagnetPrompt ||
                     state == MenuState::CalDetecting ||
                     state == MenuState::StartCheckPrompt ||
                     state == MenuState::StartPosMode ||
                     state == MenuState::StartPosAutoPrompt ||
                     state == MenuState::StartPosAuto ||
                     state == MenuState::FindStart ||
                     state == MenuState::FindStartConfirm ||
                     state == MenuState::FindFront ||
                     state == MenuState::FindFrontConfirm ||
                     state == MenuState::Placement ||   // a mode, not a prompt, but
                                                        // it stays until Menu exits it
                     state == MenuState::ResetCalPrompt ||
                     state == MenuState::ResetSettingsPrompt ||
                     state == MenuState::FactoryResetPrompt);

    // Speed and volume act from any screen (above), but their feedback lives
    // on the status line, so touching either one -- turn or press -- brings
    // the live display back from the menus and the Aux screens. Any menu or
    // aux input in the same pass is dropped, since it was aimed at the screen
    // that just closed.
    bool liveKnob = ev.speedDelta != 0 || ev.speedPressed ||
                    ev.volumeDelta != 0 || ev.volumePressed;
    if (liveKnob && state != MenuState::Status && !isPrompt) {
        enterState(MenuState::Status);
        ev.menuDelta = 0;  ev.menuPressed = false;
        ev.auxDelta  = 0;  ev.auxPressed  = false;
    }

    // menu activity tracking + timeout

    // The aux screens are driven by the aux knob, which this timer does not
    // watch, and they are used mid-performance. Timing out would yank the
    // player back to the live display between moves. The aux button is already
    // the way out.
    bool isAux = (state == MenuState::AuxFnSelect ||
                  state == MenuState::AuxLayerSelect ||
                  state == MenuState::AuxFxSelect ||
                  state == MenuState::VoiceEditList ||
                  state == MenuState::VoiceEditParam ||
                  state == MenuState::HarmonicList ||
                  state == MenuState::HarmonicParam ||
                  state == MenuState::FilterList ||
                  state == MenuState::FilterParam ||
                  state == MenuState::VoiceSaveSelect ||
                  state == MenuState::VoiceSaveConfirm ||
                  state == MenuState::AuxParam ||
                  state == MenuState::SceneSaveSelect ||
                  state == MenuState::SceneSaveConfirm ||
                  (scaleEditAux && (state == MenuState::ScaleEditList ||
                                    state == MenuState::ScaleEditSlot ||
                                    state == MenuState::ScaleSaveSelect ||
                                    state == MenuState::ScaleSaveConfirm)));

    // A scene load landed on the bar: the status line (or the scene list)
    // shows it.
    if (sceneTakeChanged()) needsRedraw = true;
    // So did something from MIDI in (root, scale, octave, volume, shift).
    if (midiInTakeChanged()) needsRedraw = true;

    // Screens you read rather than drive. Timing out would cut the reading
    // short; the menu button is the way out.
    bool isView = (state == MenuState::Info ||
                   state == MenuState::SensorLevels ||
                   state == MenuState::SensorTiming ||
                   state == MenuState::TrackNotes);

    // A hidden way out: the aux button in the menus goes straight to the live
    // display. Not in the prompts (calibration, resets), where a stray press
    // could cut one short.
    if (ev.auxPressed && state != MenuState::Status && !isPrompt && !isAux) {
        enterState(MenuState::Status);
        ev.menuDelta = 0;  ev.menuPressed = false;
        ev.auxDelta  = 0;  ev.auxPressed  = false;
    }

    if (ev.menuDelta != 0 || ev.menuPressed) lastActivity = millis();
    if (state != MenuState::Status && !isPrompt && !isAux && !isView &&
        cfg.menuTimeout != MENU_TIMEOUT_NEVER &&
        millis() - lastActivity > (uint32_t)cfg.menuTimeout * 1000UL) {
        enterState(MenuState::Status);
    }

    // state machine
    switch (state) {

        case MenuState::Status:
            if (ev.menuPressed || ev.menuDelta != 0) {
                // Turning the menu knob opens the menu the same as pressing it:
                // cursor at the top, the detent discarded, like the aux knob's
                // first step.
                enterState(MenuState::MainMenu);
            } else if (ev.auxPressed) {
                // Straight to the top of the Fn list. Not to the current
                // binding: turning the knob here already gets to that.
                auxEnteredFromLive = false;
                enterState(MenuState::AuxFnSelect, 0);
            } else if (ev.auxDelta != 0) {
                // The first step only opens the parameter screen; it shows the
                // current value unchanged and modulating starts from the next
                // step. The delta is deliberately discarded here.
                auxEnteredFromLive = true;
                enterState(MenuState::AuxParam);
            }
            break;

        // On both Aux screens the menu button bails straight to the live
        // display, whichever way the screen was reached. The menu knob stays
        // ignored here; the button is the way out.
        case MenuState::AuxFnSelect:
            if (ev.menuPressed) {
                enterState(MenuState::Status);
                break;
            }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + AUXT_COUNT) % AUXT_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor == AUXT_EXIT) {
                    enterState(MenuState::Status);
                } else if (cursor == AUXT_LAYER_A || cursor == AUXT_LAYER_B) {
                    auxLayer = (cursor == AUXT_LAYER_A) ? LAYER_A : LAYER_B;
                    enterState(MenuState::AuxLayerSelect, 0);
                } else if (cursor == AUXT_SAVE_SCENE) {
                    // The list is slots 1-8: cursor on the current one.
                    uint8_t cur = cfg.currentScene;
                    bool    slot = sceneIsSlot(cur) && cur != SCENE_DEFAULTS;
                    enterState(MenuState::SceneSaveSelect, slot ? cur - 1 : 0);
                } else if (cursor == AUXT_RESET) {
                    // Throw away every live tweak at once and drop back to the
                    // live display: the current scene, as saved. Nothing is
                    // written.
                    sceneRevertLive(cfg);
                    enterState(MenuState::Status);
                } else {
                    auxBind(cfg, AUX_TOP_FN[cursor]);
                }
            }
            break;

        case MenuState::AuxLayerSelect:
            if (ev.menuPressed) {
                enterState(MenuState::Status);
                break;
            }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + AUX_LAYER_COUNT) % AUX_LAYER_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor == AUX_LAYER_COUNT - 1) {   // Back, to the top list
                    enterState(MenuState::AuxFnSelect,
                               auxLayer == LAYER_A ? AUXT_LAYER_A : AUXT_LAYER_B);
                } else if (cursor == AUX_LAYER_VOICE_EDIT) {
                    enterState(MenuState::VoiceEditList, 0);
                } else if (cursor == AUX_LAYER_EDIT_SCALE) {
                    scaleEditAux = true;
                    enterState(MenuState::ScaleEditList, 0);
                } else if (cursor == AUX_LAYER_EFFECTS) {
                    enterState(MenuState::AuxFxSelect, 0);
                } else {
                    auxBind(cfg, auxLayerFn(auxPosKind(cursor), auxLayer));
                }
            }
            break;

        case MenuState::AuxFxSelect:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + AUX_FX_COUNT) % AUX_FX_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor == AUX_FX_COUNT - 1) enterState(MenuState::AuxLayerSelect, AUX_LAYER_EFFECTS);
                else                            auxBind(cfg, auxLayerFn(AUX_FX_KINDS[cursor], auxLayer));
            }
            break;

        // Voice Edit: like the rest of the Aux, the aux knob picks and the aux
        // button chooses or goes back; the menu button goes home. A voice
        // that cannot be edited shows why, and the aux button goes back.
        case MenuState::VoiceEditList:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (!voiceEditable(cfg, auxLayer)) {
                if (ev.auxPressed) enterState(MenuState::AuxLayerSelect, AUX_LAYER_VOICE_EDIT);
                break;
            }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + VE_COUNT) % VE_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor == VE_BACK) {
                    enterState(MenuState::AuxLayerSelect, AUX_LAYER_VOICE_EDIT);
                } else if (cursor == VE_SAVE) {
                    enterState(MenuState::VoiceSaveSelect, 0);
                } else if (cursor == VE_HARMONICS) {
                    enterState(MenuState::HarmonicList, 0);
                } else if (cursor == VE_FILTER) {
                    enterState(MenuState::FilterList, 0);
                } else {
                    voiceEditParam = cursor;
                    enterState(MenuState::VoiceEditParam);
                }
            }
            break;

        // Harmonics: the aux knob picks H1-H16, the aux button opens one.
        // If the voice stops being editable underneath (a MIDI Fn or scene
        // load), these screens go back to Voice Edit.
        case MenuState::HarmonicList:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (!voiceEditable(cfg, auxLayer)) { enterState(MenuState::VoiceEditList, VE_HARMONICS); break; }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + HARM_COUNT) % HARM_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor == HARM_COUNT - 1) {
                    enterState(MenuState::VoiceEditList, VE_HARMONICS);
                } else if (cursor == HARM_RESET) {
                    // The stock wave again, band-limited Saw and Square
                    // included. The list stays open, showing its levels.
                    Voice& v = layerVoiceEdit(cfg, auxLayer);
                    if (v.harmonicsEdited) {
                        v.harmonicsEdited = false;
                        layerVoiceTweaked(cfg, auxLayer);
                    }
                    needsRedraw = true;
                } else {
                    harmonicSel = cursor;
                    enterState(MenuState::HarmonicParam);
                }
            }
            break;

        // Filter: as Harmonics, the aux knob picks a setting and the aux
        // button opens it.
        case MenuState::FilterList:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (!voiceEditable(cfg, auxLayer)) { enterState(MenuState::VoiceEditList, VE_FILTER); break; }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + VF_COUNT) % VF_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor == VF_BACK) enterState(MenuState::VoiceEditList, VE_FILTER);
                else { filterParam = cursor; enterState(MenuState::FilterParam); }
            }
            break;

        case MenuState::FilterParam:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (!voiceEditable(cfg, auxLayer)) { enterState(MenuState::VoiceEditList, VE_FILTER); break; }
            if (ev.auxDelta) { filterEditApply(cfg, ev.auxDelta); needsRedraw = true; }
            if (ev.auxPressed) enterState(MenuState::FilterList, filterParam);
            break;

        case MenuState::HarmonicParam:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (!voiceEditable(cfg, auxLayer)) { enterState(MenuState::VoiceEditList, VE_HARMONICS); break; }
            if (ev.auxDelta) { harmonicEditApply(cfg, ev.auxDelta); needsRedraw = true; }
            if (ev.auxPressed) enterState(MenuState::HarmonicList, harmonicSel);
            break;

        // Save As: the aux knob picks a slot, the aux button saves. A used
        // custom slot asks first; the scene's own slot does not, since
        // saving there is how a tweak is kept with the scene.
        case MenuState::VoiceSaveSelect: {
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            uint8_t count = buildVoiceSaveItems(cfg, auxLayer);
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + count) % count);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                uint8_t first = voiceSaveCustomPos(cfg);
                if (cursor == count - 1) {
                    enterState(MenuState::VoiceEditList, VE_SAVE);
                } else if (cursor < first) {
                    layerVoiceSaveScene(cfg, auxLayer);
                    voiceSaved("Scene Voice");
                } else {
                    voiceSaveTarget = cursor - first;
                    if (cfg.customVoices[voiceSaveTarget].used) {
                        enterState(MenuState::VoiceSaveConfirm, 1);
                    } else {
                        layerVoiceSaveCustom(cfg, auxLayer, voiceSaveTarget);
                        voiceSaved(voiceIdName(VOICE_CUSTOM_FIRST + voiceSaveTarget));
                    }
                }
            }
            break;
        }

        case MenuState::VoiceSaveConfirm:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (ev.auxDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.auxPressed) {
                if (cursor == 0) {
                    layerVoiceSaveCustom(cfg, auxLayer, voiceSaveTarget);
                    voiceSaved(voiceIdName(VOICE_CUSTOM_FIRST + voiceSaveTarget));
                } else {
                    enterState(MenuState::VoiceSaveSelect, voiceSaveTarget + voiceSaveCustomPos(cfg));
                }
            }
            break;

        case MenuState::VoiceEditParam:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (ev.auxDelta) {
                // The voice can stop being editable underneath (a MIDI Fn or
                // scene load changing it); then this screen just goes back.
                if (!voiceEditable(cfg, auxLayer)) enterState(MenuState::VoiceEditList, voiceEditParam);
                else { voiceEditApply(cfg, ev.auxDelta); needsRedraw = true; }
            }
            if (ev.auxPressed) enterState(MenuState::VoiceEditList, voiceEditParam);
            break;

        // Save Scene: the aux knob picks a slot, the aux button chooses it. A
        // used slot asks first. The menu button bails, as on the Aux screens.
        case MenuState::SceneSaveSelect:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + SAVE_COUNT) % SAVE_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                uint8_t slot = cursor + 1;           // the list starts at scene 1
                if (cursor >= SAVE_COUNT - 1)      enterState(MenuState::AuxFnSelect, AUXT_SAVE_SCENE);
                else if (sceneUsed(cfg, slot)) {   saveSlot = slot; enterState(MenuState::SceneSaveConfirm, 1); }
                else                               saveScene(cfg, slot);
            }
            break;

        case MenuState::SceneSaveConfirm:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (ev.auxDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.auxPressed) {
                if (cursor == 0) saveScene(cfg, saveSlot);
                else             enterState(MenuState::SceneSaveSelect, saveSlot - 1);
            }
            break;

        case MenuState::AuxParam:
            if (ev.menuPressed) {
                enterState(MenuState::Status);
                break;
            }
            if (ev.auxDelta) {
                auxApplyDelta(cfg, ev.auxDelta);   // applied instantly, RAM only
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                // The button is always "back", and back depends on how we got here.
                if (auxEnteredFromLive) enterState(MenuState::Status);
                else                    openAuxSelect(cfg);
            }
            break;

        case MenuState::MainMenu:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + MAIN_COUNT) % MAIN_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                switch (cursor) {
                    case MAIN_ITEM_SOUND:
                        // Sound Defaults edits the Defaults scene, and you hear
                        // each change as you make it, so Defaults has to be the
                        // scene playing (and not about to be replaced).
                        if (sceneSelected(cfg) != SCENE_DEFAULTS) enterState(MenuState::SoundGatePrompt);
                        else                                      enterState(MenuState::SoundDefaults);
                        break;
                    case MAIN_ITEM_PLAY:   enterState(MenuState::PlaySetup); break;
                    case MAIN_ITEM_SYSTEM: enterState(MenuState::System);    break;
                    case MAIN_ITEM_TOOLS:  enterState(MenuState::Tools);     break;
                    default:               enterState(MenuState::Status);    break;
                }
            }
            break;

        case MenuState::SoundGatePrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    sceneLoadNow(cfg, SCENE_DEFAULTS);
                    enterState(MenuState::SoundDefaults);
                } else {
                    enterState(MenuState::MainMenu, MAIN_ITEM_SOUND);
                }
            }
            break;

        case MenuState::SoundDefaults:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + SOUND_COUNT) % SOUND_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < NUM_LAYERS) { editLayer = cursor; enterState(MenuState::LayerMenu); }
                else                     enterState(MenuState::MainMenu, MAIN_ITEM_SOUND);
            }
            break;

        case MenuState::PlaySetup:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + PLAY_COUNT) % PLAY_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                switch (cursor) {
                    case PLAY_ITEM_BEATS: {
                        // Land the cursor on the stored value, not the top.
                        enterState(MenuState::BeatsPerRev,
                                   (uint8_t)constrain(cfg.beatsPerRev, 1, BEATS_MAX) - 1);
                        break;
                    }
                    case PLAY_ITEM_PITCH_STEP: {
                        uint8_t idx = 0;
                        for (uint8_t i = 0; i < PITCH_STEP_COUNT - 1; i++) {
                            if (PITCH_STEP_VALUES[i] == cfg.pitchStepDiv) { idx = i; break; }
                        }
                        enterState(MenuState::PitchStep, idx);
                        break;
                    }
                    case PLAY_ITEM_AUX_FN:
                        enterState(MenuState::AuxFnDefault,
                                   cfg.auxFn < AUX_FN_MENU_PLAIN ? cfg.auxFn : AUX_FN_MENU_EFFECTS);
                        break;
                    case PLAY_ITEM_MIDI_FN:
                        enterState(MenuState::MidiFnSetting,
                                   (uint8_t)constrain(cfg.midiFn, 0, (int)MidiFn::COUNT - 1));
                        break;
                    case PLAY_ITEM_MIDI_CC:
                        enterState(MenuState::MidiCcSetting, cfg.midiCc ? 0 : 1);
                        break;
                    case PLAY_ITEM_SCENE_LOAD:
                        enterState(MenuState::SceneLoadSetting, cfg.sceneLoadNow ? 1 : 0);
                        break;
                    default: enterState(MenuState::MainMenu, MAIN_ITEM_PLAY); break;
                }
            }
            break;

        case MenuState::System:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + SYSTEM_COUNT) % SYSTEM_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                switch (cursor) {
                    case SYS_ITEM_LCD_TIMEOUT: {
                        // Find current timeout value in the options list.
                        uint8_t idx = 1; // default to 5s if not found
                        for (uint8_t i = 0; i < 7; i++) {
                            if (LCD_TIMEOUT_VALUES[i] == cfg.lcdTimeout) { idx = i; break; }
                        }
                        enterState(MenuState::LcdTimeout, idx);
                        break;
                    }
                    case SYS_ITEM_MENU_TIMEOUT: {
                        uint8_t idx = 2;   // 30 sec if not found
                        for (uint8_t i = 0; i < MENU_TIMEOUT_COUNT - 1; i++) {
                            if (MENU_TIMEOUT_VALUES[i] == cfg.menuTimeout) { idx = i; break; }
                        }
                        enterState(MenuState::MenuTimeout, idx);
                        break;
                    }
                    case SYS_ITEM_WELCOME:
                        enterState(MenuState::WelcomeTune, cfg.playWelcomeTune ? 0 : 1); break;
                    case SYS_ITEM_START_CHECK:
                        enterState(MenuState::StartCheck, cfg.startCheck ? 0 : 1); break;
                    case SYS_ITEM_MAGNET_POLE:
                        enterState(MenuState::MagnetPole, cfg.magnetPolarity < 0 ? 1 : 0); break;
                    default: enterState(MenuState::MainMenu, MAIN_ITEM_SYSTEM); break;
                }
            }
            break;

        case MenuState::LayerMenu:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + layerMenuCount()) % layerMenuCount();
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                // The cursor lands on the Defaults scene's value: that is what
                // this menu edits.
                const LayerCfg& lc = cfg.scenes[SCENE_DEFAULTS].layer[editLayer];
                switch (layerMenuItem(cursor)) {
                    case LAYER_ITEM_MODE:
                        enterState(MenuState::LayerMode, (uint8_t)lc.mode); break;
                    case LAYER_ITEM_TURNS:
                        enterState(MenuState::LayerTurns, (uint8_t)lc.turns); break;
                    case LAYER_ITEM_VOICE:
                        enterState(MenuState::LayerVoice,
                                   voiceChoicePos(lc.voice, buildVoiceChoices(cfg, editLayer, false)));
                        break;
                    case LAYER_ITEM_CHANNEL:
                        enterState(MenuState::LayerChannel,
                                   (uint8_t)constrain(lc.channel, 0, 16)); break;
                    case LAYER_ITEM_ROOT:
                        enterState(MenuState::LayerRoot, (uint8_t)lc.root % 12); break;
                    case LAYER_ITEM_SCALE: {
                        // A learned scale is not in the menu list: land on Back.
                        uint8_t n = buildScaleChoices(cfg, lc, false);
                        enterState(MenuState::LayerScale, scaleChoicePos(cfg, lc, editLayer, n, n));
                        break;
                    }
                    case LAYER_ITEM_EDIT_SCALE:
                        scaleEditAux  = false;
                        scaleEditSnap = lc;   // what Reset goes back to
                        enterState(MenuState::ScaleEditList, 0);
                        break;
                    case LAYER_ITEM_OCTAVE:
                        enterState(MenuState::LayerOctave, (uint8_t)constrain(lc.octave, 0, 7)); break;
                    case LAYER_ITEM_LEVEL:
                        enterState(MenuState::LayerLevel,
                                   (uint8_t)(constrain(lc.level, 0, 100) / 10)); break;
                    case LAYER_ITEM_SHIFT:
                        enterState(MenuState::LayerShift,
                                   (editLayer == LAYER_B && lc.shiftSameAsA)
                                       ? SHIFT_ITEM_SAME_AS_A
                                       : (uint8_t)constrain(lc.shift, 0, NUM_HALL_SENSORS - 1));
                        break;
                    case LAYER_ITEM_WRAP:
                        enterState(MenuState::LayerWrap, lc.wrap ? 0 : 1); break;
                    case LAYER_ITEM_LOW_NOTE:
                        enterState(MenuState::LayerLowNote,
                                   (editLayer == LAYER_B && lc.lowNoteSameAsA)
                                       ? LOW_NOTE_ITEM_SAME_AS_A
                                       : (uint8_t)constrain(lc.lowNote, 0, LOW_NOTE_VALUES - 1));
                        break;
                    case LAYER_ITEM_EFFECTS:
                        enterState(MenuState::FxEffectList, 0);
                        break;
                    case LAYER_ITEM_MIDI_IN:
                        enterState(MenuState::LayerMidiIn,
                                   (uint8_t)constrain(cfg.midiInChannel[editLayer], 0, 16));
                        break;
                    default:
                        enterState(MenuState::SoundDefaults, editLayer); break;
                }
            }
            break;

        case MenuState::LayerMode: {
            uint8_t count = (editLayer == LAYER_A) ? MODE_COUNT_A : MODE_COUNT_B;
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + count) % count;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < count - 1) {   // last entry is Back
                    editDefaults(cfg, [](LayerCfg& c) { c.mode = (LayerMode)cursor; });
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_MODE));
            }
            break;
        }

        case MenuState::LayerTurns:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + TURNS_COUNT) % TURNS_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < TURNS_COUNT - 1) {   // last entry is Back
                    editDefaults(cfg, [](LayerCfg& c) { c.turns = (LayerTurns)cursor; });
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_TURNS));
            }
            break;

        case MenuState::LayerVoice: {
            uint8_t n = buildVoiceChoices(cfg, editLayer, false);
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + n + 1) % (n + 1);
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < n) {   // last entry is Back
                    uint8_t id = voiceChoiceIds[cursor];
                    editDefaults(cfg, [id](LayerCfg& c) { c.voice = id; });
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_VOICE));
            }
            break;
        }

        case MenuState::LayerChannel:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + CHANNEL_COUNT) % CHANNEL_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < CHANNEL_COUNT - 1) {   // last entry is Back
                    editDefaults(cfg, [](LayerCfg& c) { c.channel = cursor; });   // 0 = Auto
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_CHANNEL));
            }
            break;

        case MenuState::LayerRoot:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + ROOT_COUNT) % ROOT_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 12) editDefaults(cfg, [](LayerCfg& c) { c.root = (RootNote)cursor; });
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_ROOT));
            }
            break;

        case MenuState::LayerScale: {
            uint8_t n = buildScaleChoices(cfg, cfg.scenes[SCENE_DEFAULTS].layer[editLayer], false);
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + n + 1) % (n + 1);
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < n) {   // last entry is Back
                    uint8_t id = scaleChoiceIds[cursor];
                    editDefaults(cfg, [&cfg, id](LayerCfg& c) { scaleChoose(cfg, c, id); });
                    customScaleFrom[editLayer] =
                        (id >= SCALE_CHOICE_SLOT) ? id - SCALE_CHOICE_SLOT : NO_CUSTOM_SCALE;
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_SCALE));
            }
            break;
        }

        // Edit Scale: the knob of the menu it was opened from picks and
        // changes, its button chooses or goes back. From the Aux the menu
        // button goes home; from Sound Defaults the aux button does (above).
        case MenuState::ScaleEditList: {
            int8_t delta = scaleEditAux ? ev.auxDelta : ev.menuDelta;
            bool   press = scaleEditAux ? ev.auxPressed : ev.menuPressed;
            if (scaleEditAux && ev.menuPressed) { enterState(MenuState::Status); break; }
            if (scaleEditBlocked(cfg)) {
                if (press) scaleEditLeave();
                break;
            }
            if (delta) {
                cursor = (uint8_t)((cursor + delta + SE_COUNT) % SE_COUNT);
                needsRedraw = true;
            }
            if (press) {
                if (cursor == SE_BACK)       scaleEditLeave();
                else if (cursor == SE_SAVE)  enterState(MenuState::ScaleSaveSelect, 0);
                else if (cursor == SE_RESET) { scaleEditReset(cfg); needsRedraw = true; }
                else { scaleEditSlot = cursor; enterState(MenuState::ScaleEditSlot); }
            }
            break;
        }

        case MenuState::ScaleEditSlot: {
            int8_t delta = scaleEditAux ? ev.auxDelta : ev.menuDelta;
            bool   press = scaleEditAux ? ev.auxPressed : ev.menuPressed;
            if (scaleEditAux && ev.menuPressed) { enterState(MenuState::Status); break; }
            if (scaleEditBlocked(cfg)) { enterState(MenuState::ScaleEditList, scaleEditSlot); break; }
            if (delta) { scaleEditStep(cfg, delta); needsRedraw = true; }
            if (press) {
                if (!scaleEditAux) storageSave(cfg);
                enterState(MenuState::ScaleEditList, scaleEditSlot);
            }
            break;
        }

        case MenuState::ScaleSaveSelect: {
            int8_t delta = scaleEditAux ? ev.auxDelta : ev.menuDelta;
            bool   press = scaleEditAux ? ev.auxPressed : ev.menuPressed;
            if (scaleEditAux && ev.menuPressed) { enterState(MenuState::Status); break; }
            if (delta) {
                cursor = (uint8_t)((cursor + delta + SCALE_SAVE_COUNT) % SCALE_SAVE_COUNT);
                needsRedraw = true;
            }
            if (press) {
                if (cursor == SCALE_SAVE_COUNT - 1) {
                    enterState(MenuState::ScaleEditList, SE_SAVE);
                } else {
                    scaleSaveTarget = cursor;
                    if (cfg.customScales[cursor].used) enterState(MenuState::ScaleSaveConfirm, 1);
                    else                               scaleSaveCustom(cfg, cursor);
                }
            }
            break;
        }

        case MenuState::ScaleSaveConfirm: {
            int8_t delta = scaleEditAux ? ev.auxDelta : ev.menuDelta;
            bool   press = scaleEditAux ? ev.auxPressed : ev.menuPressed;
            if (scaleEditAux && ev.menuPressed) { enterState(MenuState::Status); break; }
            if (delta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (press) {
                if (cursor == 0) scaleSaveCustom(cfg, scaleSaveTarget);
                else             enterState(MenuState::ScaleSaveSelect, scaleSaveTarget);
            }
            break;
        }

        case MenuState::LayerOctave:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + OCTAVE_COUNT) % OCTAVE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < OCTAVE_COUNT - 1) editDefaults(cfg, [](LayerCfg& c) { c.octave = cursor; });
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_OCTAVE));
            }
            break;

        case MenuState::LayerLevel:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + LEVEL_COUNT) % LEVEL_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < LEVEL_COUNT - 1) {   // last entry is Back
                    editDefaults(cfg, [](LayerCfg& c) { c.level = cursor * 10; });
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_LEVEL));
            }
            break;

        case MenuState::LayerShift: {
            uint8_t count = (editLayer == LAYER_A) ? SHIFT_COUNT_A : SHIFT_COUNT_B;
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + count) % count;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < NUM_HALL_SENSORS) {
                    // Picking a value also unbinds B's shift from A's.
                    editDefaults(cfg, [](LayerCfg& c) { c.shift = cursor; c.shiftSameAsA = false; });
                } else if (editLayer == LAYER_B && cursor == SHIFT_ITEM_SAME_AS_A) {
                    editDefaults(cfg, [](LayerCfg& c) { c.shiftSameAsA = true; });
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_SHIFT));
            }
            break;
        }

        case MenuState::LayerWrap:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + WRAP_COUNT) % WRAP_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < WRAP_COUNT - 1) {   // last entry is Back
                    // With B's shift bound this is B's own value, ignored
                    // until unbound, as B's other settings are under Same as A.
                    editDefaults(cfg, [](LayerCfg& c) { c.wrap = (cursor == 0); });
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_WRAP));
            }
            break;

        case MenuState::LayerLowNote: {
            uint8_t count = (editLayer == LAYER_A) ? LOW_NOTE_COUNT_A : LOW_NOTE_COUNT_B;
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + count) % count;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < LOW_NOTE_VALUES) {
                    // Picking a value also unbinds B's Low Note from A's.
                    editDefaults(cfg, [](LayerCfg& c) { c.lowNote = cursor; c.lowNoteSameAsA = false; });
                } else if (editLayer == LAYER_B && cursor == LOW_NOTE_ITEM_SAME_AS_A) {
                    editDefaults(cfg, [](LayerCfg& c) { c.lowNoteSameAsA = true; });
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_LOW_NOTE));
            }
            break;
        }

        case MenuState::LayerMidiIn:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + MIDI_IN_COUNT) % MIDI_IN_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < MIDI_IN_COUNT - 1) {   // last entry is Back
                    // Not part of the sound, so not in the scene: where the
                    // keyboard is plugged in does not change with the scene.
                    cfg.midiInChannel[editLayer] = cursor;   // 0 = Off
                    storageSave(cfg);
                }
                enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_MIDI_IN));
            }
            break;

        case MenuState::FxEffectList: {
            uint8_t count = FX_MENU_COUNT + 1;   // + Back
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + count) % count;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < FX_MENU_COUNT) {
                    fxMenuEffect = FX_MENU[cursor];
                    enterState(MenuState::FxParamList, 0);
                } else {
                    enterState(MenuState::LayerMenu, layerMenuPos(LAYER_ITEM_EFFECTS));
                }
            }
            break;
        }

        case MenuState::FxParamList: {
            uint8_t params[FXP_COUNT];
            uint8_t count = buildFxParamLabels(cfg, params);
            uint8_t n     = fxParamsOf(fxMenuEffect, params);
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + count) % count;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                uint8_t fxPos = 0;
                for (uint8_t i = 0; i < FX_MENU_COUNT; i++) if (FX_MENU[i] == fxMenuEffect) fxPos = i;
                if (cursor < n) {
                    // Under Same as A this edits B's own value, heard once
                    // B's effect is unbound, as B's Wrap is under a bound shift.
                    fxMenuParam = params[cursor];
                    const LayerFx& fx = cfg.scenes[SCENE_DEFAULTS].layer[editLayer].fx;
                    enterState(MenuState::FxParamEdit, fxValueIndex(fxMenuParam, fx));
                } else if (editLayer == LAYER_B && cursor == n) {
                    uint8_t bit   = 1u << (uint8_t)fxMenuEffect;
                    bool    bound = cfg.scenes[SCENE_DEFAULTS].layer[LAYER_B].fx.sameAsA & bit;
                    editDefaults(cfg, [bit, bound](LayerCfg& c) {
                        if (bound) c.fx.sameAsA &= ~bit;
                        else       c.fx.sameAsA |= bit;
                    });
                    needsRedraw = true;
                } else {
                    enterState(MenuState::FxEffectList, fxPos);
                }
            }
            break;
        }

        case MenuState::FxParamEdit: {
            uint8_t count = buildFxValueLabels(fxMenuParam, cfg.scenes[SCENE_DEFAULTS].layer[editLayer].fx, true);
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + count) % count;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < count - 1) {   // last entry is Back
                    uint8_t p = fxMenuParam, i = cursor;
                    editDefaults(cfg, [p, i](LayerCfg& c) { fxSetIndex(p, c.fx, i); });
                }
                uint8_t params[FXP_COUNT];
                uint8_t n = fxParamsOf(fxMenuEffect, params), pos = 0;
                for (uint8_t i = 0; i < n; i++) if (params[i] == fxMenuParam) pos = i;
                enterState(MenuState::FxParamList, pos);
            }
            break;
        }

        case MenuState::MidiFnSetting:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + MIDI_FN_COUNT) % MIDI_FN_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < MIDI_FN_COUNT - 1) {   // last entry is Back
                    cfg.midiFn = cursor;
                    midiInReset();   // a half-learned scale belongs to the old Fn
                    storageSave(cfg);
                }
                enterState(MenuState::PlaySetup, PLAY_ITEM_MIDI_FN);
            }
            break;

        // Whether incoming effects CCs change the sound. Off by default, so
        // settings dialed in are not changed unexpectedly.
        case MenuState::MidiCcSetting:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + WELCOME_COUNT) % WELCOME_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < WELCOME_COUNT - 1) {   // last entry is Back
                    cfg.midiCc = (cursor == 0);
                    storageSave(cfg);
                }
                enterState(MenuState::PlaySetup, PLAY_ITEM_MIDI_CC);
            }
            break;

        // When a picked scene loads: at the next bar start (on the downbeat)
        // or straight away.
        case MenuState::SceneLoadSetting:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + SCENE_LOAD_COUNT) % SCENE_LOAD_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < SCENE_LOAD_COUNT - 1) {   // last entry is Back
                    cfg.sceneLoadNow = (cursor == 1);
                    storageSave(cfg);
                }
                enterState(MenuState::PlaySetup, PLAY_ITEM_SCENE_LOAD);
            }
            break;

        case MenuState::WelcomeTune:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + WELCOME_COUNT) % WELCOME_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    cfg.playWelcomeTune = true;
                    storageSave(cfg);
                    // Play immediately as a preview.
                    playWelcomeTune(cfg);
                }
                if (cursor == 1) { cfg.playWelcomeTune = false; storageSave(cfg); }
                enterState(MenuState::System, SYS_ITEM_WELCOME);
            }
            break;

        case MenuState::LcdTimeout:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + LCD_TIMEOUT_COUNT) % LCD_TIMEOUT_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 7) {
                    cfg.lcdTimeout = LCD_TIMEOUT_VALUES[cursor];
                    storageSave(cfg);
                    // Apply immediately.
                    if (cfg.lcdTimeout == LCD_TIMEOUT_ALWAYS_OFF) {
                        lcd.noBacklight();
                        backlightOn = false;
                    } else {
                        lcd.backlight();
                        backlightOn = true;
                        lastInteraction = millis();
                    }
                }
                enterState(MenuState::System, SYS_ITEM_LCD_TIMEOUT);
            }
            break;

        case MenuState::MenuTimeout:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + MENU_TIMEOUT_COUNT) % MENU_TIMEOUT_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < MENU_TIMEOUT_COUNT - 1) {   // last entry is Back
                    cfg.menuTimeout = MENU_TIMEOUT_VALUES[cursor];
                    storageSave(cfg);
                }
                enterState(MenuState::System, SYS_ITEM_MENU_TIMEOUT);
            }
            break;

        case MenuState::BeatsPerRev:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + BEATS_COUNT) % BEATS_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < BEATS_COUNT - 1) {   // last entry is Back
                    cfg.beatsPerRev = cursor + 1;
                    storageSave(cfg);
                }
                enterState(MenuState::PlaySetup, PLAY_ITEM_BEATS);
            }
            break;

        case MenuState::AuxFnDefault:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + AUX_FN_MENU_COUNT) % AUX_FN_MENU_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor == AUX_FN_MENU_EFFECTS) {
                    enterState(MenuState::AuxFnDefaultFx, auxFnFxPos((AuxFn)cfg.auxFn));
                    break;
                }
                if (cursor < AUX_FN_MENU_PLAIN) {   // last entry is Back
                    cfg.auxFn = cursor;
                    storageSave(cfg);
                }
                enterState(MenuState::PlaySetup, PLAY_ITEM_AUX_FN);
            }
            break;

        case MenuState::AuxFnDefaultFx:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + AUX_FN_FX_COUNT) % AUX_FN_FX_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < AUX_FN_FX_COUNT - 1) {   // last entry is Back
                    cfg.auxFn = (uint8_t)AUX_FN_FX[cursor];
                    storageSave(cfg);
                    enterState(MenuState::PlaySetup, PLAY_ITEM_AUX_FN);
                } else {
                    enterState(MenuState::AuxFnDefault, AUX_FN_MENU_EFFECTS);
                }
            }
            break;

        case MenuState::PitchStep:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + PITCH_STEP_COUNT) % PITCH_STEP_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < PITCH_STEP_COUNT - 1) {   // last entry is Back
                    cfg.pitchStepDiv = PITCH_STEP_VALUES[cursor];
                    storageSave(cfg);
                }
                enterState(MenuState::PlaySetup, PLAY_ITEM_PITCH_STEP);
            }
            break;

        case MenuState::Tools:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + TOOLS_COUNT) % TOOLS_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                fromBoot = false;
                switch (cursor) {
                    case TOOL_GO_TO_START:
                        goToStart();
                        enterState(MenuState::Tools, TOOL_GO_TO_START);
                        break;
                    case TOOL_GO_TO_FRONT:
                        goToFront(cfg);
                        enterState(MenuState::Tools, TOOL_GO_TO_FRONT);
                        break;
                    case TOOL_PLACEMENT:   enterPlacement(); break;
                    case TOOL_FULL_CAL:    enterState(MenuState::CalClearPrompt); break;
                    case TOOL_RESET_CAL:   enterState(MenuState::ResetCalPrompt); break;
                    case TOOL_CALIB_START: enterState(MenuState::StartPosMode); break;
                    case TOOL_INFO:
                        driverVersion = stepperDriverVersion();   // UART, so once
                        enterState(MenuState::Info);
                        break;
                    case TOOL_SENSOR_LEVELS: enterState(MenuState::SensorLevels); break;
                    case TOOL_SENSOR_TIMING: enterState(MenuState::SensorTiming); break;
                    case TOOL_TRACK_NOTES:   enterState(MenuState::TrackNotes);   break;
                    case TOOL_RESET_SETTINGS: enterState(MenuState::ResetSettingsPrompt); break;
                    case TOOL_FACTORY_RESET:  enterState(MenuState::FactoryResetPrompt); break;
                    default: enterState(MenuState::MainMenu, MAIN_ITEM_TOOLS); break;
                }
            }
            break;

        case MenuState::MagnetPole:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + POLE_COUNT) % POLE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < POLE_COUNT - 1) {   // last entry is Back
                    cfg.magnetPolarity = POLE_VALUES[cursor];
                    storageSave(cfg);
                    hallSetPolarity(cfg.magnetPolarity);
                }
                enterState(MenuState::System, SYS_ITEM_MAGNET_POLE);
            }
            break;

        case MenuState::FirstBootPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                fromBoot = true;
                if (cursor == 0) enterState(MenuState::CalClearPrompt);
                else             leaveTo(TOOL_FULL_CAL);
            }
            break;

        case MenuState::CalClearPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) enterState(MenuState::CalSampling);
                else             leaveTo(TOOL_FULL_CAL);
            }
            break;

        case MenuState::CalSampling:
            calibrationSampleBaselines();
            releaseForHandTurn(cfg);
            enterState(MenuState::CalMagnetPrompt);
            break;

        case MenuState::CalMagnetPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (jogMoving) { stepperStop(); waitForRest(GO_TO_STOP_MS); jogMoving = false; }
                if (cursor == 0) {
                    frontPos = stepperPosition();   // hand turns are not counted
                    enterState(MenuState::CalDetecting);
                } else {
                    leaveTo(TOOL_FULL_CAL);
                }
            }
            break;

        case MenuState::CalDetecting:
            // On failure the error has been shown; ask again, so the magnet
            // can be moved and retried without sampling the clear platter again.
            if (calibrationDetect(cfg) == CalibrationStatus::Success) {
                setFrontFrom(cfg, frontPos);
                goToFront(cfg);   // back to the player, to take the magnet off
                leaveTo(TOOL_FULL_CAL);
            } else {
                releaseForHandTurn(cfg);
                enterState(MenuState::CalMagnetPrompt);
            }
            break;

        case MenuState::StartCheckPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                fromBoot = true;
                if (cursor == 0) {
                    enterState(MenuState::StartPosMode);
                } else {
                    // Skip: the platter's position now becomes StartPos, so
                    // bars, scene loads and Layer Turns have a start to count
                    // from. It is saved at rest like any StartPos.
                    barSetStart(stepperPosition());
                    leaveTo(TOOL_CALIB_START);
                }
            }
            break;

        case MenuState::StartPosMode:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + START_MODE_COUNT) % START_MODE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    // Auto reads the magnet with the saved calibration.
                    if (cfg.calibrated) {
                        releaseForHandTurn(cfg);
                        enterState(MenuState::StartPosAutoPrompt);
                    } else {
                        menuMessage("Not calibrated", "Full Calibrate");
                        delay(2000);
                        needsRedraw = true;
                    }
                } else if (cursor == 1) {
                    enterFindStart();
                } else {
                    leaveTo(TOOL_CALIB_START);
                }
            }
            break;

        case MenuState::StartPosAutoPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (jogMoving) { stepperStop(); waitForRest(GO_TO_STOP_MS); jogMoving = false; }
                if (cursor == 0) {
                    frontPos = stepperPosition();   // hand turns are not counted
                    enterState(MenuState::StartPosAuto);
                } else {
                    leaveTo(TOOL_CALIB_START);
                }
            }
            break;

        case MenuState::StartPosAuto:
            // On failure the error has been shown; ask again.
            if (calibrationFindStart(cfg) == CalibrationStatus::Success) {
                setFrontFrom(cfg, frontPos);
                goToFront(cfg);   // back to the player, to take the magnet off
                leaveTo(TOOL_CALIB_START);
            } else {
                releaseForHandTurn(cfg);
                enterState(MenuState::StartPosAutoPrompt);
            }
            break;

        case MenuState::FindStart:
        case MenuState::FindFront: {
            jogUpdate(ev.menuDelta);   // the menu knob jogs the platter
            if (ev.menuPressed) {
                if (jogMoving) { stepperStop(); jogMoving = false; }
                enterState(state == MenuState::FindStart ? MenuState::FindStartConfirm
                                                         : MenuState::FindFrontConfirm);
            }
            break;
        }

        case MenuState::FindStartConfirm:
            if (ev.menuDelta) {
                cursor = (uint8_t)((cursor + ev.menuDelta + 3) % 3);
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    // The mark is at the arm: this position is the bar start.
                    // barUpdate() saves it once the platter is at rest.
                    barSetStart(stepperPosition());
                    if (frontPending) setFrontFrom(cfg, frontPos);
                    frontPending = false;
                    menuMessage("StartPos set", "");
                    delay(1500);
                    leaveTo(TOOL_CALIB_START);
                } else if (cursor == 1) {
                    enterState(MenuState::FindStart);   // keep jogging
                } else {
                    leaveTo(TOOL_CALIB_START);
                }
            }
            break;

        case MenuState::FindFrontConfirm:
            if (ev.menuDelta) {
                cursor = (uint8_t)((cursor + ev.menuDelta + 3) % 3);
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                // Yes: the mark is at the player. Saved once StartPos is set,
                // since Front is counted from it. Skip keeps the saved Front.
                if (cursor == 1) {
                    enterState(MenuState::FindFront);   // keep jogging
                } else {
                    frontPos     = stepperPosition();
                    frontPending = (cursor == 0);
                    enterState(MenuState::FindStart);
                }
            }
            break;

        case MenuState::Placement:
            if (ev.menuPressed) { leavePlacement(); break; }
            if (!stepperRunning()) placeMoving = false;

            // Speed: jog, or press for Front.
            if (!placeMoving) jogUpdate(place.speedDelta);
            if (place.speedPressed) {
                jogMoving   = false;
                placeMoving = false;
                goToFront(cfg);
                needsRedraw = true;
            }

            // Aux: the next step either way, or press for the next beat.
            if (ev.auxDelta) {
                placeSnap(cfg, PLACE_STEPS, ev.auxDelta > 0 ? 1 : -1,
                          (uint8_t)abs(ev.auxDelta));
            }
            if (ev.auxPressed) placeSnap(cfg, cfg.beatsPerRev, 1, 1);

            // Volume: pick a track, press to mute or unmute it.
            if (place.volumeDelta) {
                placeCursor = (uint8_t)((placeCursor + place.volumeDelta + NUM_HALL_SENSORS * 8)
                                        % NUM_HALL_SENSORS);
                needsRedraw = true;
            }
            if (place.volumePressed) {
                placeTracks ^= (uint8_t)(1u << placeCursor);
                sequencerSetTrackMask(placeTracks);
                needsRedraw = true;
            }

            // The readout follows the platter while it moves, and shows where
            // it stopped.
            {
                bool running = stepperRunning();
                if (running ? millis() - lastLiveDraw >= LIVE_REDRAW_MS : placeWasRunning) {
                    needsRedraw = true;
                }
                placeWasRunning = running;
            }
            break;

        case MenuState::Info:
            if (ev.menuDelta) {
                cursor = (uint8_t)((cursor + ev.menuDelta + INFO_PAGE_COUNT) % INFO_PAGE_COUNT);
                needsRedraw = true;
            }
            if (ev.menuPressed) enterState(MenuState::Tools, TOOL_INFO);
            // The StartPos page follows the platter.
            if (cursor == INFO_START_POS && millis() - lastLiveDraw >= LIVE_REDRAW_MS) {
                needsRedraw = true;
            }
            break;

        case MenuState::SensorLevels:
            if (ev.menuPressed) enterState(MenuState::Tools, TOOL_SENSOR_LEVELS);
            if (millis() - lastLiveDraw >= LIVE_REDRAW_MS) needsRedraw = true;
            break;

        case MenuState::SensorTiming:
            if (ev.menuDelta) {
                cursor = (uint8_t)((cursor + ev.menuDelta + NUM_HALL_SENSORS) % NUM_HALL_SENSORS);
                needsRedraw = true;
            }
            if (ev.menuPressed) enterState(MenuState::Tools, TOOL_SENSOR_TIMING);
            if (millis() - lastLiveDraw >= LIVE_REDRAW_MS) needsRedraw = true;
            break;

        // The menu knob flips between the notes and the keys. Redrawn live,
        // so Aux and MIDI In changes show at once.
        case MenuState::TrackNotes:
            if (ev.menuDelta) { cursor ^= 1; needsRedraw = true; }
            if (ev.menuPressed) enterState(MenuState::Tools, TOOL_TRACK_NOTES);
            if (millis() - lastLiveDraw >= LIVE_REDRAW_MS) needsRedraw = true;
            break;

        case MenuState::StartCheck:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + WELCOME_COUNT) % WELCOME_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < WELCOME_COUNT - 1) {   // last entry is Back
                    cfg.startCheck = (cursor == 0);
                    storageSave(cfg);
                }
                enterState(MenuState::System, SYS_ITEM_START_CHECK);
            }
            break;

        case MenuState::ResetCalPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    // Reset calibration fields only.
                    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
                        cfg.hallBaseline[i] = HALL_BASELINE_DEFAULT;
                        cfg.hallNoise[i]    = HALL_NOISE_DEFAULT;
                    }
                    cfg.hallThreshold = HALL_THRESHOLD_DEFAULT;
                    cfg.rpmCorrection = 1.0f;
                    cfg.calibrated    = false;
                    cfg.magnetPolarity = DEFAULT_MAGNET_POLARITY;
                    barForget(cfg);
                    storageSave(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallNoise, cfg.hallThreshold);
                    hallSetPolarity(cfg.magnetPolarity);
                    hallResetTiming();
                    stepperSetCorrection(cfg.rpmCorrection);
                    menuMessage("Cal reset", "");
                    delay(1500);
                }
                enterState(MenuState::Tools, TOOL_RESET_CAL);
            }
            break;

        case MenuState::ResetSettingsPrompt:
        case MenuState::FactoryResetPrompt: {
            bool factory = (state == MenuState::FactoryResetPrompt);
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    SavedConfig d = storageDefaults();
                    if (!factory) {
                        // Reset Settings keeps calibration, StartPos, Front,
                        // the saved scenes 1-8 and the saved voices;
                        // everything else is factory, the Defaults scene
                        // included, and Defaults now plays.
                        d.calibrated     = cfg.calibrated;
                        d.hallThreshold  = cfg.hallThreshold;
                        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
                            d.hallBaseline[i] = cfg.hallBaseline[i];
                            d.hallNoise[i]    = cfg.hallNoise[i];
                        }
                        d.rpmCorrection  = cfg.rpmCorrection;
                        d.magnetPolarity = cfg.magnetPolarity;
                        d.barPhase       = cfg.barPhase;
                        d.barPhaseValid  = cfg.barPhaseValid;
                        d.frontPhase     = cfg.frontPhase;
                        d.frontKnown     = cfg.frontKnown;
                        for (uint8_t i = 1; i < NUM_SCENES; i++) {
                            d.scenes[i]    = cfg.scenes[i];
                            d.sceneUsed[i] = cfg.sceneUsed[i];
                            for (uint8_t l = 0; l < NUM_LAYERS; l++)
                                d.sceneVoices[i][l] = cfg.sceneVoices[i][l];
                        }
                        // Scenes 1-8 can use the custom voices: keep them.
                        for (uint8_t i = 0; i < NUM_SAVED_VOICES; i++)
                            d.customVoices[i] = cfg.customVoices[i];
                        // And the custom scales, which are yours as the voices are.
                        for (uint8_t i = 0; i < NUM_CUSTOM_SCALES; i++)
                            d.customScales[i] = cfg.customScales[i];
                    }
                    cfg = d;
                    storageSave(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallNoise, cfg.hallThreshold);
                    hallSetPolarity(cfg.magnetPolarity);
                    stepperSetCorrection(cfg.rpmCorrection);
                    if (factory) {
                        barInit(cfg);              // defaults: start unknown
                        hallResetTiming();         // learned against the old threshold
                    }
                    audioSetVolume(cfg.volume);
                    cfg.muted ? audioMute() : audioUnmute();
                    midiInReset();
                    sceneLoadNow(cfg, SCENE_DEFAULTS);
                    menuMessage(factory ? "Factory reset" : "Settings reset", "");
                    delay(1500);
                }
                enterState(MenuState::Tools, factory ? TOOL_FACTORY_RESET : TOOL_RESET_SETTINGS);
            }
            break;
        }
    }

    // redraw
    if (needsRedraw) {
        needsRedraw = false;
        switch (state) {
            case MenuState::Status:
                drawStatus(cfg);
                break;
            case MenuState::MainMenu:
                drawList(MAIN_ITEMS, MAIN_COUNT, cursor);
                break;
            case MenuState::SoundDefaults:
                buildSoundLabels(cfg);
                drawList(SOUND_ITEMS, SOUND_COUNT, cursor);
                break;
            case MenuState::SoundGatePrompt:
                lcdLine(0, "Load Defaults?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::PlaySetup:
                drawList(PLAY_ITEMS, PLAY_COUNT, cursor);
                break;
            case MenuState::System:
                drawList(SYSTEM_ITEMS, SYSTEM_COUNT, cursor);
                break;
            case MenuState::LayerMenu:
                buildLayerLabels(cfg);
                drawList(LAYER_LABELS, layerMenuCount(), cursor);
                break;
            case MenuState::LayerTurns:
                drawList(TURNS_ITEMS, TURNS_COUNT, cursor);
                break;
            case MenuState::LayerMode:
                if (editLayer == LAYER_A) drawList(MODE_ITEMS_A, MODE_COUNT_A, cursor);
                else                      drawList(MODE_ITEMS_B, MODE_COUNT_B, cursor);
                break;
            case MenuState::LayerVoice: {
                uint8_t n = buildVoiceChoices(cfg, editLayer, false);
                drawList(voiceChoiceLabels, n + 1, cursor);
                break;
            }
            case MenuState::VoiceSaveSelect: {
                uint8_t count = buildVoiceSaveItems(cfg, auxLayer);
                drawList(VOICE_SAVE_ITEMS, count, cursor);
                break;
            }
            case MenuState::VoiceSaveConfirm:
                lcdLine(0, "Overwrite?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::LayerChannel:
                drawList(CHANNEL_ITEMS, CHANNEL_COUNT, cursor);
                break;
            case MenuState::LayerRoot:
                drawList(ROOT_ITEMS, ROOT_COUNT, cursor);
                break;
            case MenuState::LayerScale: {
                uint8_t n = buildScaleChoices(cfg, cfg.scenes[SCENE_DEFAULTS].layer[editLayer], false);
                drawList(scaleChoiceLabels, n + 1, cursor);
                break;
            }
            case MenuState::ScaleEditList:
                if (scaleEditBlocked(cfg)) {
                    lcdLine(0, "Layer B is");
                    lcdLine(1, "Same as A");
                } else {
                    buildScaleEditItems(cfg);
                    drawList(SE_ITEMS, SE_COUNT, cursor);
                }
                break;
            case MenuState::ScaleEditSlot: {
                const LayerCfg& lc = scaleEditCfg(cfg);
                int8_t steps[CUSTOM_SCALE_SLOTS];
                scaleEditSteps(lc, steps);
                char name[8];
                scaleStepName(lc, steps[scaleEditSlot], name, sizeof(name));
                lcdLine(0, "Slot %u", (unsigned)(scaleEditSlot + 1));
                lcdLine(1, "%s", name);
                break;
            }
            case MenuState::ScaleSaveSelect:
                buildScaleSaveItems(cfg);
                drawList(SCALE_SAVE_ITEMS, SCALE_SAVE_COUNT, cursor);
                break;
            case MenuState::ScaleSaveConfirm:
                lcdLine(0, "Overwrite?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::LayerOctave:
                drawList(OCTAVE_ITEMS, OCTAVE_COUNT, cursor);
                break;
            case MenuState::LayerLevel:
                drawList(LEVEL_ITEMS, LEVEL_COUNT, cursor);
                break;
            case MenuState::LayerShift:
                if (editLayer == LAYER_A) drawList(SHIFT_ITEMS_A, SHIFT_COUNT_A, cursor);
                else                      drawList(SHIFT_ITEMS_B, SHIFT_COUNT_B, cursor);
                break;
            case MenuState::LayerWrap:
                drawList(WRAP_ITEMS, WRAP_COUNT, cursor);
                break;
            case MenuState::LayerLowNote:
                if (editLayer == LAYER_A) drawList(LOW_NOTE_ITEMS_A, LOW_NOTE_COUNT_A, cursor);
                else                      drawList(LOW_NOTE_ITEMS_B, LOW_NOTE_COUNT_B, cursor);
                break;
            case MenuState::LayerMidiIn:
                drawList(MIDI_IN_ITEMS, MIDI_IN_COUNT, cursor);
                break;
            case MenuState::FxEffectList:
                buildFxEffectLabels(cfg);
                drawList(FX_LABELS, FX_MENU_COUNT + 1, cursor);
                break;
            case MenuState::FxParamList: {
                uint8_t params[FXP_COUNT];
                drawList(FX_LABELS, buildFxParamLabels(cfg, params), cursor);
                break;
            }
            case MenuState::FxParamEdit:
                drawList(fxValueLabels,
                         buildFxValueLabels(fxMenuParam, cfg.scenes[SCENE_DEFAULTS].layer[editLayer].fx, true),
                         cursor);
                break;
            case MenuState::MidiFnSetting:
                drawList(MIDI_FN_ITEMS, MIDI_FN_COUNT, cursor);
                break;
            case MenuState::SceneLoadSetting:
                drawList(SCENE_LOAD_ITEMS, SCENE_LOAD_COUNT, cursor);
                break;
            case MenuState::MidiCcSetting:
                drawList(WELCOME_ITEMS, WELCOME_COUNT, cursor);
                break;
            case MenuState::WelcomeTune:
                drawList(WELCOME_ITEMS, WELCOME_COUNT, cursor);
                break;
            case MenuState::LcdTimeout:
                drawList(LCD_TIMEOUT_LABELS, LCD_TIMEOUT_COUNT, cursor);
                break;
            case MenuState::MenuTimeout:
                drawList(MENU_TIMEOUT_LABELS, MENU_TIMEOUT_COUNT, cursor);
                break;
            case MenuState::BeatsPerRev:
                drawList(BEATS_LABELS, BEATS_COUNT, cursor);
                break;
            case MenuState::AuxFnDefault:
                drawList(AUX_FN_MENU_LABELS, AUX_FN_MENU_COUNT, cursor);
                break;
            case MenuState::AuxFnDefaultFx:
                drawList(AUX_FN_FX_LABELS, AUX_FN_FX_COUNT, cursor);
                break;
            case MenuState::PitchStep:
                drawList(PITCH_STEP_LABELS, PITCH_STEP_COUNT, cursor);
                break;
            case MenuState::MagnetPole:
                drawList(POLE_LABELS, POLE_COUNT, cursor);
                break;
            case MenuState::AuxFnSelect:
                drawList(AUX_TOP_LABELS, AUXT_COUNT, cursor);
                break;
            case MenuState::AuxLayerSelect:
                drawList(AUX_LAYER_LABELS, AUX_LAYER_COUNT, cursor);
                break;
            case MenuState::AuxFxSelect:
                drawList(AUX_FX_LABELS, AUX_FX_COUNT, cursor);
                break;
            case MenuState::VoiceEditList:
                drawVoiceEditList(cfg);
                break;
            case MenuState::VoiceEditParam:
                drawVoiceEditParam(cfg);
                break;
            case MenuState::HarmonicList:
                buildHarmonicItems(cfg);
                drawList(HARM_ITEMS, HARM_COUNT, cursor);
                break;
            case MenuState::HarmonicParam:
                drawHarmonicParam(cfg);
                break;
            case MenuState::FilterList:
                drawList(FILTER_LABELS, VF_COUNT, cursor);
                break;
            case MenuState::FilterParam:
                drawFilterParam(cfg);
                break;
            case MenuState::AuxParam:
                drawAuxParam(cfg);
                break;
            case MenuState::SceneSaveSelect:
                buildSceneLabels(cfg);
                drawList(SAVE_ITEMS, SAVE_COUNT, cursor);
                break;
            case MenuState::SceneSaveConfirm:
                lcdLine(0, "Overwrite %u?", (unsigned)saveSlot);
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::FirstBootPrompt:
                lcdLine(0, "Not calibrated");
                lcdLine(1, "%c Setup %c Skip",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::CalClearPrompt:
                lcdLine(0, "Clear platter");
                lcdLine(1, "%c OK  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::CalMagnetPrompt:
                lcdLine(0, "Magnet at front");
                lcdLine(1, "%c OK  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::CalSampling:
            case MenuState::CalDetecting:
                // Calibration blocks and drives the LCD directly via menuMessage().
                break;
            case MenuState::Tools:
                drawList(TOOLS_ITEMS, TOOLS_COUNT, cursor);
                break;
            case MenuState::StartPosMode:
                drawList(START_MODE_ITEMS, START_MODE_COUNT, cursor);
                break;
            case MenuState::StartPosAutoPrompt:
                lcdLine(0, "Magnet at front");
                lcdLine(1, "%c OK  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::StartPosAuto:
                // Blocks and drives the LCD directly via menuMessage().
                break;
            case MenuState::StartCheckPrompt:
                lcdLine(0, "StartPos unknown");
                lcdLine(1, "%c Find  %c Skip",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::FindStart:
                lcdLine(0, "Mark to the arm");
                lcdLine(1, "Turn, then press");
                break;
            case MenuState::FindStartConfirm:
                lcdLine(0, "StartPos here?");
                lcdLine(1, "%cYes %cMore %cBack",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 2 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::FindFront:
                lcdLine(0, "Mark to front");
                lcdLine(1, "Turn, then press");
                break;
            case MenuState::FindFrontConfirm:
                lcdLine(0, "Front here?");
                lcdLine(1, "%cYes %cMore %cSkip",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 2 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::StartCheck:
                drawList(WELCOME_ITEMS, WELCOME_COUNT, cursor);
                break;
            case MenuState::Placement:
                drawPlacement(cfg);
                lastLiveDraw = millis();
                break;
            case MenuState::Info:
                drawInfo(cfg, cursor);
                lastLiveDraw = millis();
                break;
            case MenuState::SensorLevels:
                drawSensorLevels();
                lastLiveDraw = millis();
                break;
            case MenuState::SensorTiming:
                drawSensorTiming(cfg, cursor);
                lastLiveDraw = millis();
                break;
            case MenuState::TrackNotes:
                drawTrackNotes(cfg, cursor);
                lastLiveDraw = millis();
                break;
            case MenuState::ResetCalPrompt:
                lcdLine(0, "Reset cal data?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::ResetSettingsPrompt:
                lcdLine(0, "Reset settings?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::FactoryResetPrompt:
                lcdLine(0, "Erase all data?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
        }
    }
}
