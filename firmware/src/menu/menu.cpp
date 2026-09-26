#include "menu.h"
#include <Arduino.h>
#include <math.h>
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
#include "midi/midi_in.h"

static LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

enum class MenuState : uint8_t {
    Status,
    MainMenu,
    RootNote,
    Scale,
    Octave,
    LayerMenu,      // one layer's submenu; which one is editLayer
    LayerMode,
    LayerVoice,
    LayerChannel,
    LayerOctave,
    LayerLevel,
    LayerShift,
    LayerWrap,
    LayerLowNote,
    LayerMidiIn,
    MidiFnSetting,
    WelcomeTune,
    LcdTimeout,
    MenuTimeout,
    BeatsPerRev,
    MagnetPole,
    FirstBootPrompt,   // shown once on a fresh EEPROM: calibrate now or skip
    AuxFnDefault,
    PitchStep,
    AuxFnSelect,    // aux knob: choose what the knob modulates
    AuxLayerSelect, // aux knob: the same, inside Layer A > or Layer B >
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
    StartCheck,         // setting: whether the boot prompt above is shown
    Info,               // read-only pages: belt, StartPos, threshold, driver
    SensorLevels,       // live: each sensor's reading against its rest level
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

static const char* SCALE_ITEMS[] = {
    "Major","Minor","PMajor","PMinor","Blues","Chromat","Dorian","Mixolyd","Back"
};
static const uint8_t SCALE_COUNT = 9;

// Every scale's name, Learned included, for the live display, scenes and the
// Aux Scale Fn. The main menu list above has no Learned: it can only be
// learned, from MIDI keys.
static const char* SCALE_NAMES[] = {
    "Major","Minor","PMajor","PMinor","Blues","Chromat","Dorian","Mixolyd","Learned"
};

static const char* OCTAVE_ITEMS[] = {
    "0","1","2","3","4","5","6","7","Back"
};
static const uint8_t OCTAVE_COUNT = 9;

// Voice names come from voice.cpp, filled in by menuInit(); this adds Back.
static const char* VOICE_ITEMS[VOICE_COUNT + 1];
static const uint8_t VOICE_ITEMS_COUNT = VOICE_COUNT + 1;

// --- Layer submenu --------------------------------------------------------
// Layer A and Layer B share one submenu; editLayer says which is open.
static uint8_t editLayer = LAYER_A;

static const char* LAYER_ITEMS[] = {
    "Mode","Voice","Channel","Octave","Level","Shift","Wrap","Low Note","MIDI In","Back"
};
static const uint8_t LAYER_ITEMS_COUNT = 10;
enum : uint8_t { LAYER_ITEM_MODE, LAYER_ITEM_VOICE, LAYER_ITEM_CHANNEL,
                 LAYER_ITEM_OCTAVE, LAYER_ITEM_LEVEL, LAYER_ITEM_SHIFT,
                 LAYER_ITEM_WRAP, LAYER_ITEM_LOW_NOTE, LAYER_ITEM_MIDI_IN,
                 LAYER_ITEM_BACK };

// A layer's MIDI in channel: Off, then channels 1-16, then Back. Index ==
// stored value. Filled in by menuInit().
static const char*   MIDI_IN_ITEMS[18];
static const uint8_t MIDI_IN_COUNT = 18;

// Order matches LayerMode. Layer A has no Same as A, so its list is shorter.
static const char* MODE_ITEMS_A[] = { "On","Off","Back" };
static const char* MODE_ITEMS_B[] = { "On","Off","Same as A","Back" };
static const uint8_t MODE_COUNT_A = 3;
static const uint8_t MODE_COUNT_B = 4;

// Auto, then channels 1-16, then Back. Index == stored value. Filled in by
// menuInit().
static char        CHANNEL_LABEL_BUF[16][6];
static const char* CHANNEL_ITEMS[18];
static const uint8_t CHANNEL_COUNT = 18;

// Octave offset -3..+3; index = offset + LAYER_OCTAVE_RANGE.
static const int8_t  LAYER_OCTAVE_RANGE = 3;
static const char*   LAYER_OCTAVE_ITEMS[] = { "-3","-2","-1","0","+1","+2","+3","Back" };
static const uint8_t LAYER_OCTAVE_COUNT = 8;

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

static const char* MAIN_ITEMS[] = {
    "Root Note","Scale","Octave","Layer A","Layer B","Welcome Tune",
    "LCD Timeout","Menu Timeout","Beats/Rev","Aux Fn","Pitch Step","MIDI Fn",
    "Tools","Exit"
};  // Exit returns to the live display; submenus keep "Back"
static const uint8_t MAIN_COUNT = 14;
enum : uint8_t { MAIN_ITEM_ROOT, MAIN_ITEM_SCALE, MAIN_ITEM_OCTAVE, MAIN_ITEM_LAYER_A,
                 MAIN_ITEM_LAYER_B, MAIN_ITEM_WELCOME, MAIN_ITEM_LCD_TIMEOUT,
                 MAIN_ITEM_MENU_TIMEOUT, MAIN_ITEM_BEATS, MAIN_ITEM_AUX_FN,
                 MAIN_ITEM_PITCH_STEP, MAIN_ITEM_MIDI_FN, MAIN_ITEM_TOOLS, MAIN_ITEM_EXIT };

// Menu timeout choices in seconds, as LCD Timeout offers its own.
static const uint8_t MENU_TIMEOUT_VALUES[] = { 5, 10, 30, 60, MENU_TIMEOUT_NEVER };
static const char*   MENU_TIMEOUT_LABELS[] = { "5 sec","10 sec","30 sec","1 min","Never","Back" };
static const uint8_t MENU_TIMEOUT_COUNT = 6;   // 5 options + Back

// What incoming MIDI keys do. Order matches MidiFn.
static const char* MIDI_FN_ITEMS[] = { "Off","Pitch","Shift","Scale Learn","Chord","Back" };
static const uint8_t MIDI_FN_COUNT = 6;

// Calibration and maintenance, kept out of the main menu. StartPos is the
// bar start: where the start mark on the platter passes the arm.
static const char* TOOLS_ITEMS[] = {
    "Go to StartPos","Full Calibrate","Reset Calib.","Calib. StartPos",
    "Magnet Pole","StartPos Check","Info","Sensor Levels","Reset Settings",
    "Factory Reset","Back"
};
static const uint8_t TOOLS_COUNT = 11;
enum : uint8_t { TOOL_GO_TO_START, TOOL_FULL_CAL, TOOL_RESET_CAL,
                 TOOL_CALIB_START, TOOL_MAGNET_POLE, TOOL_START_CHECK,
                 TOOL_INFO, TOOL_SENSOR_LEVELS, TOOL_RESET_SETTINGS,
                 TOOL_FACTORY_RESET, TOOL_BACK };

// Info: one page per value, turned through with the menu knob.
enum : uint8_t { INFO_BELT, INFO_START_POS, INFO_THRESHOLD, INFO_DRIVER,
                 INFO_PAGE_COUNT };

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
// What the aux knob can be bound to. The stored cfg.auxFn is this number, so
// new Fns go at the END; the order on screen is set by the lists below.
// Storage migration (migrateAuxFn() in storage.cpp) knows this order too.
enum class AuxFn : uint8_t { Octave, RootNote, ScaleFn, ShiftA, ShiftB,
                             LowNoteA, LowNoteB, Pitch,
                             Balance, VoiceA, VoiceB, LoadScene,
                             OctaveA, OctaveB, COUNT };
static const uint8_t AUX_FN_COUNT = (uint8_t)AuxFn::COUNT;

// The Aux list, as shown: the shared Fns, a submenu per layer, the scene Fn,
// then three actions. Layer entries open the per-layer list below.
static const char* AUX_TOP_LABELS[] = {
    "Octave","Root Note","Scale","Pitch","Layer A >","Layer B >",
    "A/B Balance","Load Scene","Save Scene","Reset All","Exit"
};
enum : uint8_t { AUXT_OCTAVE, AUXT_ROOT, AUXT_SCALE, AUXT_PITCH,
                 AUXT_LAYER_A, AUXT_LAYER_B, AUXT_BALANCE, AUXT_LOAD_SCENE,
                 AUXT_SAVE_SCENE, AUXT_RESET, AUXT_EXIT, AUXT_COUNT };
// The Fn behind each top entry; COUNT where the entry is not a Fn.
static const AuxFn AUX_TOP_FN[AUXT_COUNT] = {
    AuxFn::Octave, AuxFn::RootNote, AuxFn::ScaleFn, AuxFn::Pitch,
    AuxFn::COUNT, AuxFn::COUNT, AuxFn::Balance, AuxFn::LoadScene,
    AuxFn::COUNT, AuxFn::COUNT, AuxFn::COUNT
};

// Inside Layer A > / Layer B >, in the layer submenu's order.
static const char* AUX_LAYER_LABELS[] = { "Voice","Octave","Shift","Low Note","Back" };
static const uint8_t AUX_LAYER_COUNT = 5;   // 4 Fns + Back
static const AuxFn AUX_LAYER_FN[NUM_LAYERS][AUX_LAYER_COUNT - 1] = {
    { AuxFn::VoiceA, AuxFn::OctaveA, AuxFn::ShiftA, AuxFn::LowNoteA },
    { AuxFn::VoiceB, AuxFn::OctaveB, AuxFn::ShiftB, AuxFn::LowNoteB },
};
static uint8_t auxLayer = LAYER_A;   // which layer's list is open

// Main > Aux Fn, which sets the binding ahead of time, stays one flat list.
// Order matches AuxFn.
static const char* AUX_FN_MENU_LABELS[] = {
    "Octave","Root Note","Scale","Layer A Shift","Layer B Shift",
    "Layer A Low","Layer B Low","Pitch",
    "A/B Balance","Layer A Voice","Layer B Voice","Load Scene",
    "Layer A Octave","Layer B Octave","Back"
};
static const uint8_t AUX_FN_MENU_COUNT   = AUX_FN_COUNT + 1;  // + Back

// Scene list entries: "2: D Minor", or "3: (empty)". Rebuilt before each
// draw, since the root and scale names come from the slots. The save list
// adds Back.
static char        SCENE_LABEL_BUF[NUM_SCENES][16];
static const char* SCENE_ITEMS[NUM_SCENES + 1];

// The Load Scene list: Defaults first, then the 8 slots.
static const char*   LOAD_ITEMS[NUM_SCENES + 1];
static const uint8_t LOAD_COUNT = NUM_SCENES + 1;
static uint8_t loadPosToSlot(uint8_t p) { return p == 0 ? SCENE_DEFAULTS : (uint8_t)(p - 1); }
static uint8_t loadSlotToPos(uint8_t s) { return s == SCENE_DEFAULTS ? 0 : (uint8_t)(s + 1); }
static const uint8_t SCENE_SAVE_COUNT = NUM_SCENES + 1;

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
static void openAuxSelect(const SavedConfig& cfg) {
    for (uint8_t l = 0; l < NUM_LAYERS; l++) {
        for (uint8_t i = 0; i < AUX_LAYER_COUNT - 1; i++) {
            if ((uint8_t)AUX_LAYER_FN[l][i] == cfg.auxFn) {
                auxLayer = l;
                enterState(MenuState::AuxLayerSelect, i);
                return;
            }
        }
    }
    uint8_t top = 0;
    for (uint8_t i = 0; i < AUXT_COUNT; i++) {
        if ((uint8_t)AUX_TOP_FN[i] == cfg.auxFn) { top = i; break; }
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
static const uint8_t BEATS_VALUES[] = { 1, 2, 3, 4, 6, 8 };
static const char*   BEATS_LABELS[] = { "1","2","3","4","6","8","Back" };
static const uint8_t BEATS_COUNT = 7;   // 6 options + Back

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
    snprintf(msg, sizeof(msg), "Saved Scene %u", (unsigned)(slot + 1));
    menuMessage(msg, "");
    delay(1000);
    enterState(MenuState::Status);
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

// Turns the platter so the start mark stops at the arm, the shorter way
// round. A check: if the start is right, the mark lands under the arm.
// Blocking; the move takes a second or two.
static void goToStart() {
    if (!barKnown()) {
        menuMessage("StartPos unknown", "Calib. StartPos");
        delay(2000);
        return;
    }
    menuMessage("Moving...", "");
    stepperStop();
    waitForRest(GO_TO_STOP_MS);

    int32_t n     = (int32_t)barStepsPerRev();
    int32_t steps = (n - (int32_t)barPhase()) % n;   // forward to the mark
    if (steps > n / 2) steps -= n;                   // shorter the other way
    stepperMoveBy(steps, GO_TO_START_RPM);
    waitForRest(GO_TO_ARRIVE_MS);
}

// Find Start works on a platter at rest: stop it first if it is playing.
static void enterFindStart() {
    if (stepperRunning()) stepperStop();
    jogMoving = false;
    enterState(MenuState::FindStart);
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
    // right half names the current scene, with * once the live settings
    // differ from it; blank until a scene has been loaded or saved.
    int bpm = (int)lroundf(cfg.rpm * cfg.beatsPerRev);
    char scene[16] = "";   // "Scene 2*"; sized for any %u
    if (sceneUsed(cfg, cfg.currentScene)) {
        // Defaults is scene 0, as in the Load Scene list.
        unsigned n = (cfg.currentScene == SCENE_DEFAULTS) ? 0 : cfg.currentScene + 1;
        snprintf(scene, sizeof(scene), "Scene %u%c", n, sceneModified(cfg) ? '*' : ' ');
    }
    lcdLine(0, "BPM:%-4d%s", bpm, scene);
    char vb[7];
    buildVolBar(vb, cfg.volume, cfg.muted);
    lcdLine(1, "%-2s %-7s%s",
        ROOT_ITEMS[static_cast<uint8_t>(cfg.root)],
        SCALE_NAMES[static_cast<uint8_t>(cfg.scale)],
        vb);
}

static void buildSceneLabels(const SavedConfig& cfg) {
    for (uint8_t i = 0; i < NUM_SCENES; i++) {
        const SceneSlot& s = cfg.scenes[i];
        if (s.used) {
            snprintf(SCENE_LABEL_BUF[i], sizeof(SCENE_LABEL_BUF[i]), "%u: %s %s", i + 1,
                     ROOT_ITEMS[(uint8_t)s.root], SCALE_NAMES[(uint8_t)s.scale]);
        } else {
            snprintf(SCENE_LABEL_BUF[i], sizeof(SCENE_LABEL_BUF[i]), "%u: (empty)", i + 1);
        }
        SCENE_ITEMS[i] = SCENE_LABEL_BUF[i];
    }
    SCENE_ITEMS[NUM_SCENES] = "Back";

    LOAD_ITEMS[0] = "0: Defaults";
    for (uint8_t i = 0; i < NUM_SCENES; i++) LOAD_ITEMS[i + 1] = SCENE_LABEL_BUF[i];
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
        "Belt ratio", "StartPos", "Threshold", "Motor driver"
    };
    lcdLine(0, "%-13s%u/%u", NAMES[page], page + 1, INFO_PAGE_COUNT);
    switch (page) {
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

// Apply one aux knob step to whatever the knob is bound to.
//
// RAM ONLY. These are live performance moves, not configuration: the saved
// value is the one dialled in from the main menu, so a session always starts
// from a known place instead of wherever the knob was left.
//
// This function writing straight into cfg is safe because storage.cpp keeps a
// separate committed copy of exactly these fields and never lets the live ones
// reach EEPROM. If you add a target here, add its field to
// copyLiveModulatedFields() in storage.cpp or it will start persisting.
//
// Wrap vs clamp follows the shape of the value: wrap anything cyclic, clamp
// anything that is a magnitude. Octave is a magnitude, and wrapping 7 back to
// 0 would be a seven-octave jump mid-performance. Track Shift is either: with
// Wrap it is a rotation, 7 to 0 moving one sensor; with No Wrap it transposes
// the whole run, and 7 to 0 would drop every sensor an octave at once. Low
// Note clamps too, right = Outer as listed, so a stray click cannot flip it.
static void auxApplyDelta(SavedConfig& cfg, int8_t delta) {
    switch ((AuxFn)cfg.auxFn) {
        case AuxFn::Octave: {
            int v = (int)cfg.octave + delta;
            cfg.octave = (uint8_t)constrain(v, 0, 7);          // clamp: a range
            break;
        }
        case AuxFn::RootNote: {
            int v = ((int)cfg.root + delta) % 12;
            if (v < 0) v += 12;
            cfg.root = (RootNote)v;                            // wrap: a circle
            break;
        }
        case AuxFn::ScaleFn: {
            int n = scaleHasLearned() ? (int)Scale::COUNT : (int)SCALE_BUILTIN_COUNT;
            int v = ((int)cfg.scale + delta) % n;
            if (v < 0) v += n;
            cfg.scale = (Scale)v;                              // wrap: a list
            break;
        }
        case AuxFn::ShiftA:
        case AuxFn::ShiftB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::ShiftA) ? LAYER_A : LAYER_B;
            // B playing A's shift: turning B's would change a hidden setting.
            if (layerShiftSource(cfg, l) != l) break;
            int v = (int)cfg.layer[l].shift + delta;
            if (layerWraps(cfg, l)) {                          // kits always wrap
                v %= NUM_HALL_SENSORS;
                if (v < 0) v += NUM_HALL_SENSORS;              // wrap: a rotation
            } else {
                v = constrain(v, 0, NUM_HALL_SENSORS - 1);     // clamp: a transpose
            }
            cfg.layer[l].shift = (uint8_t)v;
            break;
        }
        case AuxFn::LowNoteA:
        case AuxFn::LowNoteB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::LowNoteA) ? LAYER_A : LAYER_B;
            if (layerLowNoteSource(cfg, l) != l) break;        // playing A's
            int v = (int)cfg.layer[l].lowNote + delta;
            cfg.layer[l].lowNote = (uint8_t)constrain(v, 0, LOW_NOTE_VALUES - 1);
            break;
        }
        case AuxFn::Pitch: {
            uint8_t div = cfg.pitchStepDiv ? cfg.pitchStepDiv : 1;
            pitchAdjust((float)delta / (float)div);
            break;
        }
        case AuxFn::Balance:
            layerSetBalance(layerBalance() + delta);           // clamp: a range
            break;
        case AuxFn::VoiceA:
        case AuxFn::VoiceB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::VoiceA) ? LAYER_A : LAYER_B;
            // B in Same as A plays A's voice, so turning B's would do nothing
            // audible; leave it alone rather than change a hidden setting.
            if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) break;
            int v = ((int)cfg.layer[l].voice + delta) % VOICE_COUNT;
            if (v < 0) v += VOICE_COUNT;
            cfg.layer[l].voice = (uint8_t)v;                   // wrap: a list
            layersApply(cfg);
            break;
        }
        case AuxFn::OctaveA:
        case AuxFn::OctaveB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::OctaveA) ? LAYER_A : LAYER_B;
            if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) break;
            int v = (int)cfg.layer[l].octaveOffset + delta;
            cfg.layer[l].octaveOffset =                        // clamp: a range
                (int8_t)constrain(v, -LAYER_OCTAVE_RANGE, LAYER_OCTAVE_RANGE);
            break;
        }
        case AuxFn::LoadScene: {
            // Steps through Defaults and the saved scenes, wrapping, and
            // queues the one landed on to load at the next bar start.
            // Defaults is always there, so the loop always finds one.
            uint8_t sel  = sceneSelected(cfg);
            int     step = (delta > 0) ? 1 : -1;
            int     pos  = (sel == SCENE_NONE) ? (step > 0 ? -1 : LOAD_COUNT)
                                               : loadSlotToPos(sel);
            for (int n = abs(delta); n > 0; n--) {
                do { pos = (pos + step + LOAD_COUNT) % LOAD_COUNT; }
                while (!sceneUsed(cfg, loadPosToSlot((uint8_t)pos)));
            }
            sceneQueue(cfg, loadPosToSlot((uint8_t)pos));
            break;
        }
        default: break;
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
static void drawAuxParam(const SavedConfig& cfg) {
    switch ((AuxFn)cfg.auxFn) {
        case AuxFn::Octave:
            // Explicit counts: these lists carry a trailing "Back" for menu use
            // that has no meaning here, where the button is already back.
            drawList(OCTAVE_ITEMS, 8, cfg.octave);
            break;
        case AuxFn::RootNote:
            drawList(ROOT_ITEMS, 12, (uint8_t)cfg.root);
            break;
        case AuxFn::ScaleFn:
            // Learned is in the list only once something has been learned.
            drawList(SCALE_NAMES, scaleHasLearned() ? (uint8_t)Scale::COUNT : SCALE_BUILTIN_COUNT,
                     (uint8_t)cfg.scale);
            break;
        case AuxFn::ShiftA:
        case AuxFn::ShiftB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::ShiftA) ? LAYER_A : LAYER_B;
            if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) {
                lcdLine(0, "Layer B is");
                lcdLine(1, "Same as A");
            } else if (l == LAYER_B && cfg.layer[LAYER_B].shiftSameAsA) {
                lcdLine(0, "B Shift is");
                lcdLine(1, "Same as A");
            } else {
                drawList(SHIFT_ITEMS_A, NUM_HALL_SENSORS,
                         (uint8_t)constrain(cfg.layer[l].shift, 0, NUM_HALL_SENSORS - 1));
            }
            break;
        }
        case AuxFn::LowNoteA:
        case AuxFn::LowNoteB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::LowNoteA) ? LAYER_A : LAYER_B;
            if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) {
                lcdLine(0, "Layer B is");
                lcdLine(1, "Same as A");
            } else if (l == LAYER_B && cfg.layer[LAYER_B].lowNoteSameAsA) {
                lcdLine(0, "B Low Note is");
                lcdLine(1, "Same as A");
            } else {
                drawList(LOW_NOTE_ITEMS_A, LOW_NOTE_VALUES,
                         (uint8_t)constrain(cfg.layer[l].lowNote, 0, LOW_NOTE_VALUES - 1));
            }
            break;
        }
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
        case AuxFn::VoiceA:
        case AuxFn::VoiceB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::VoiceA) ? LAYER_A : LAYER_B;
            if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) {
                lcdLine(0, "Layer B is");
                lcdLine(1, "Same as A");
            } else {
                drawList(VOICE_ITEMS, VOICE_COUNT, cfg.layer[l].voice);
            }
            break;
        }
        case AuxFn::OctaveA:
        case AuxFn::OctaveB: {
            uint8_t l = ((AuxFn)cfg.auxFn == AuxFn::OctaveA) ? LAYER_A : LAYER_B;
            if (l == LAYER_B && cfg.layer[LAYER_B].mode == LayerMode::SameAsA) {
                lcdLine(0, "Layer B is");
                lcdLine(1, "Same as A");
            } else {
                drawList(LAYER_OCTAVE_ITEMS, LAYER_OCTAVE_COUNT - 1,   // no Back
                         (uint8_t)(constrain(cfg.layer[l].octaveOffset, -LAYER_OCTAVE_RANGE,
                                             LAYER_OCTAVE_RANGE) + LAYER_OCTAVE_RANGE));
            }
            break;
        }
        case AuxFn::LoadScene: {
            buildSceneLabels(cfg);
            uint8_t sel = sceneSelected(cfg);
            drawList(LOAD_ITEMS, LOAD_COUNT, sel == SCENE_NONE ? 0 : loadSlotToPos(sel));
            break;
        }
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

// Plays each sensor's Layer A note, hall 1 to hall 8 and back, at current BPM.
// Blocking -- returns when the last note's release has finished, or at once if
// the menu button is pressed. A slow saved tempo makes the full tune long, and
// the press is the way to skip it. The caller carries on as if it had finished,
// so a cancel skips only the tune, never a prompt that follows it.
static void playWelcomeTune(const SavedConfig& cfg) {
    int bpm = (int)(fabsf(cfg.rpm) * cfg.beatsPerRev);
    bpm = constrain(bpm, 40, 200);
    uint32_t beatMs = 60000UL / (uint32_t)bpm;
    uint32_t noteMs  = min((uint32_t)layerVoice(cfg, LAYER_A).noteMs, beatMs);
    uint8_t  channel = layerChannel(cfg, LAYER_A);
    bool     kit     = voiceIsKit(layerVoice(cfg, LAYER_A));
    int      octave  = constrain((int)cfg.octave + layerEffective(cfg, LAYER_A).octaveOffset, 0, 9);

    lcdLine(0, "  Music  Table  ");
    lcdLine(1, "~~~~~~~~~~~~~~~~");

    // Each step is what that sensor plays on Layer A, Track Shift, Wrap, Low
    // Note and octave offset included, so the tune previews the table. A kit
    // voice plays its drums. Returns true if cancelled; the note is still
    // turned off, so a cancel never leaves one hanging.
    auto step = [&](uint8_t i) -> bool {
        uint8_t degree = layerDegree(cfg, LAYER_A, i);
        bool cancelled;
        if (kit) {
            uint8_t sounded = midiDrumOn(channel, degree, 100);
            cancelled = tuneWait(noteMs);
            midiDrumOff(channel, sounded);
        } else {
            uint8_t note = scaleNote(cfg.root, cfg.scale, degree, (uint8_t)octave);
            uint8_t sounded = midiNoteOn(LAYER_A, channel, note, 100);
            cancelled = tuneWait(noteMs);
            midiNoteOff(LAYER_A, channel, sounded);
        }
        return cancelled || tuneWait(beatMs - noteMs);
    };

    // Forward pass.
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        if (step(i)) return;
    }

    // Pause for one beat.
    if (tuneWait(beatMs)) return;

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

    for (uint8_t i = 0; i < VOICE_COUNT; i++) VOICE_ITEMS[i] = voiceGet(i).name;
    VOICE_ITEMS[VOICE_COUNT] = "Back";

    CHANNEL_ITEMS[0] = "Auto";
    for (uint8_t c = 1; c <= 16; c++) {
        snprintf(CHANNEL_LABEL_BUF[c - 1], sizeof(CHANNEL_LABEL_BUF[0]), "Ch %u", c);
        CHANNEL_ITEMS[c] = CHANNEL_LABEL_BUF[c - 1];
    }
    CHANNEL_ITEMS[17] = "Back";

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

    // live speed control - all states
    //
    // cfg.rpm is SIGNED: negative means the platter runs in reverse. Turning
    // down past the low end passes through a stop and out the other side into
    // reverse. The platter cannot usefully turn below MIN_RPM, so the band
    // between -MIN_RPM and +MIN_RPM is a dead zone that reads as zero, and
    // leaving zero jumps straight to +/-MIN_RPM rather than crawling back up
    // through a dead zone it could never escape one detent at a time.
    if (ev.speedDelta != 0) {
        float r;
        if (cfg.rpm == 0.0f) {
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
                  state == MenuState::AuxParam ||
                  state == MenuState::SceneSaveSelect ||
                  state == MenuState::SceneSaveConfirm);

    // A scene load landed on the bar: the status line (or the scene list)
    // shows it.
    if (sceneTakeChanged()) needsRedraw = true;
    // So did something from MIDI in (root, scale, octave, volume, shift).
    if (midiInTakeChanged()) needsRedraw = true;

    // Screens you read rather than drive. Timing out would cut the reading
    // short; the menu button is the way out.
    bool isView = (state == MenuState::Info ||
                   state == MenuState::SensorLevels);

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
                    uint8_t cur = cfg.currentScene;
                    enterState(MenuState::SceneSaveSelect, cur < NUM_SCENES ? cur : 0);
                } else if (cursor == AUXT_RESET) {
                    // Throw away every live modulation at once and drop back to
                    // the live display, where the restored values are visible on
                    // the status line: the saved settings, with the current
                    // scene's pitch and balance. Nothing is written.
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
                } else {
                    auxBind(cfg, AUX_LAYER_FN[auxLayer][cursor]);
                }
            }
            break;

        // Save Scene: the aux knob picks a slot, the aux button chooses it. A
        // used slot asks first. The menu button bails, as on the Aux screens.
        case MenuState::SceneSaveSelect:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + SCENE_SAVE_COUNT) % SCENE_SAVE_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor >= NUM_SCENES)            enterState(MenuState::AuxFnSelect, AUXT_SAVE_SCENE);
                else if (sceneUsed(cfg, cursor)) {   saveSlot = cursor; enterState(MenuState::SceneSaveConfirm, 1); }
                else                                 saveScene(cfg, cursor);
            }
            break;

        case MenuState::SceneSaveConfirm:
            if (ev.menuPressed) { enterState(MenuState::Status); break; }
            if (ev.auxDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.auxPressed) {
                if (cursor == 0) saveScene(cfg, saveSlot);
                else             enterState(MenuState::SceneSaveSelect, saveSlot);
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
                    case MAIN_ITEM_ROOT: enterState(MenuState::RootNote,
                                static_cast<uint8_t>(cfg.root)); break;
                    case MAIN_ITEM_SCALE: enterState(MenuState::Scale,
                                static_cast<uint8_t>(cfg.scale)); break;
                    case MAIN_ITEM_OCTAVE: enterState(MenuState::Octave, cfg.octave); break;
                    case MAIN_ITEM_LAYER_A: editLayer = LAYER_A; enterState(MenuState::LayerMenu); break;
                    case MAIN_ITEM_LAYER_B: editLayer = LAYER_B; enterState(MenuState::LayerMenu); break;
                    case MAIN_ITEM_WELCOME: enterState(MenuState::WelcomeTune,
                                cfg.playWelcomeTune ? 0 : 1); break;
                    case MAIN_ITEM_LCD_TIMEOUT: {
                        // Find current timeout value in the options list.
                        uint8_t idx = 1; // default to 5s if not found
                        for (uint8_t i = 0; i < 7; i++) {
                            if (LCD_TIMEOUT_VALUES[i] == cfg.lcdTimeout) { idx = i; break; }
                        }
                        enterState(MenuState::LcdTimeout, idx);
                        break;
                    }
                    case MAIN_ITEM_BEATS: {
                        // Land the cursor on the stored value, not the top.
                        uint8_t idx = 3;  // default to 4 beats if not found
                        for (uint8_t i = 0; i < 6; i++) {
                            if (BEATS_VALUES[i] == cfg.beatsPerRev) { idx = i; break; }
                        }
                        enterState(MenuState::BeatsPerRev, idx);
                        break;
                    }
                    case MAIN_ITEM_AUX_FN: enterState(MenuState::AuxFnDefault,
                                (uint8_t)constrain(cfg.auxFn, 0, AUX_FN_COUNT - 1)); break;
                    case MAIN_ITEM_PITCH_STEP: {
                        uint8_t idx = 0;
                        for (uint8_t i = 0; i < PITCH_STEP_COUNT - 1; i++) {
                            if (PITCH_STEP_VALUES[i] == cfg.pitchStepDiv) { idx = i; break; }
                        }
                        enterState(MenuState::PitchStep, idx);
                        break;
                    }
                    case MAIN_ITEM_MENU_TIMEOUT: {
                        uint8_t idx = 2;   // 30 sec if not found
                        for (uint8_t i = 0; i < MENU_TIMEOUT_COUNT - 1; i++) {
                            if (MENU_TIMEOUT_VALUES[i] == cfg.menuTimeout) { idx = i; break; }
                        }
                        enterState(MenuState::MenuTimeout, idx);
                        break;
                    }
                    case MAIN_ITEM_MIDI_FN:
                        enterState(MenuState::MidiFnSetting,
                                   (uint8_t)constrain(cfg.midiFn, 0, (int)MidiFn::COUNT - 1));
                        break;
                    case MAIN_ITEM_TOOLS: enterState(MenuState::Tools); break;
                    case MAIN_ITEM_EXIT:  enterState(MenuState::Status); break;
                }
            }
            break;

        case MenuState::RootNote:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + ROOT_COUNT) % ROOT_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 12) { cfg.root = static_cast<RootNote>(cursor); storageCommit(cfg, CommitField::Root); }
                enterState(MenuState::MainMenu, MAIN_ITEM_ROOT);
            }
            break;

        case MenuState::Scale:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + SCALE_COUNT) % SCALE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 8) { cfg.scale = static_cast<Scale>(cursor); storageCommit(cfg, CommitField::Scale); }
                enterState(MenuState::MainMenu, MAIN_ITEM_SCALE);
            }
            break;

        case MenuState::Octave:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + OCTAVE_COUNT) % OCTAVE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 8) { cfg.octave = cursor; storageCommit(cfg, CommitField::Octave); }
                enterState(MenuState::MainMenu, MAIN_ITEM_OCTAVE);
            }
            break;

        case MenuState::LayerMenu:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + LAYER_ITEMS_COUNT) % LAYER_ITEMS_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                const LayerCfg& lc = cfg.layer[editLayer];
                switch (cursor) {
                    case LAYER_ITEM_MODE:
                        enterState(MenuState::LayerMode, (uint8_t)lc.mode); break;
                    case LAYER_ITEM_VOICE:
                        enterState(MenuState::LayerVoice,
                                   (uint8_t)constrain(lc.voice, 0, VOICE_COUNT - 1)); break;
                    case LAYER_ITEM_CHANNEL:
                        enterState(MenuState::LayerChannel,
                                   (uint8_t)constrain(lc.channel, 0, 16)); break;
                    case LAYER_ITEM_OCTAVE:
                        enterState(MenuState::LayerOctave,
                                   (uint8_t)(constrain(lc.octaveOffset, -LAYER_OCTAVE_RANGE,
                                                       LAYER_OCTAVE_RANGE) + LAYER_OCTAVE_RANGE)); break;
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
                    case LAYER_ITEM_MIDI_IN:
                        enterState(MenuState::LayerMidiIn,
                                   (uint8_t)constrain(cfg.midiInChannel[editLayer], 0, 16));
                        break;
                    default:
                        enterState(MenuState::MainMenu, editLayer == LAYER_A ? MAIN_ITEM_LAYER_A : MAIN_ITEM_LAYER_B); break;
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
                    cfg.layer[editLayer].mode = (LayerMode)cursor;
                    storageSave(cfg);
                    layersApply(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_MODE);
            }
            break;
        }

        case MenuState::LayerVoice:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + VOICE_ITEMS_COUNT) % VOICE_ITEMS_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < VOICE_COUNT) {   // last entry is Back
                    cfg.layer[editLayer].voice = cursor;
                    storageCommit(cfg, CommitField::Voice, editLayer);
                    layersApply(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_VOICE);
            }
            break;

        case MenuState::LayerChannel:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + CHANNEL_COUNT) % CHANNEL_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < CHANNEL_COUNT - 1) {   // last entry is Back
                    cfg.layer[editLayer].channel = cursor;   // 0 = Auto
                    storageSave(cfg);
                    layersApply(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_CHANNEL);
            }
            break;

        case MenuState::LayerOctave:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + LAYER_OCTAVE_COUNT) % LAYER_OCTAVE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < LAYER_OCTAVE_COUNT - 1) {   // last entry is Back
                    cfg.layer[editLayer].octaveOffset = (int8_t)cursor - LAYER_OCTAVE_RANGE;
                    storageCommit(cfg, CommitField::LayerOctave, editLayer);   // also an aux target
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_OCTAVE);
            }
            break;

        case MenuState::LayerLevel:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + LEVEL_COUNT) % LEVEL_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < LEVEL_COUNT - 1) {   // last entry is Back
                    cfg.layer[editLayer].level = cursor * 10;
                    storageSave(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_LEVEL);
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
                    cfg.layer[editLayer].shift = cursor;
                    cfg.layer[editLayer].shiftSameAsA = false;
                    storageCommit(cfg, CommitField::Shift, editLayer);
                } else if (editLayer == LAYER_B && cursor == SHIFT_ITEM_SAME_AS_A) {
                    cfg.layer[LAYER_B].shiftSameAsA = true;
                    storageSave(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_SHIFT);
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
                    cfg.layer[editLayer].wrap = (cursor == 0);
                    storageSave(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_WRAP);
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
                    cfg.layer[editLayer].lowNote = cursor;
                    cfg.layer[editLayer].lowNoteSameAsA = false;
                    storageCommit(cfg, CommitField::LowNote, editLayer);
                } else if (editLayer == LAYER_B && cursor == LOW_NOTE_ITEM_SAME_AS_A) {
                    cfg.layer[LAYER_B].lowNoteSameAsA = true;
                    storageSave(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_LOW_NOTE);
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
                    cfg.midiInChannel[editLayer] = cursor;   // 0 = Off
                    storageSave(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_MIDI_IN);
            }
            break;

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
                enterState(MenuState::MainMenu, MAIN_ITEM_MIDI_FN);
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
                enterState(MenuState::MainMenu, MAIN_ITEM_WELCOME);
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
                enterState(MenuState::MainMenu, MAIN_ITEM_LCD_TIMEOUT);
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
                enterState(MenuState::MainMenu, MAIN_ITEM_MENU_TIMEOUT);
            }
            break;

        case MenuState::BeatsPerRev:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + BEATS_COUNT) % BEATS_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < BEATS_COUNT - 1) {   // last entry is Back
                    cfg.beatsPerRev = BEATS_VALUES[cursor];
                    storageSave(cfg);
                }
                enterState(MenuState::MainMenu, MAIN_ITEM_BEATS);
            }
            break;

        case MenuState::AuxFnDefault:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + AUX_FN_MENU_COUNT) % AUX_FN_MENU_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < AUX_FN_COUNT) {   // last entry is Back
                    cfg.auxFn = cursor;
                    storageSave(cfg);
                }
                enterState(MenuState::MainMenu, MAIN_ITEM_AUX_FN);
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
                enterState(MenuState::MainMenu, MAIN_ITEM_PITCH_STEP);
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
                    case TOOL_FULL_CAL:    enterState(MenuState::CalClearPrompt); break;
                    case TOOL_RESET_CAL:   enterState(MenuState::ResetCalPrompt); break;
                    case TOOL_CALIB_START: enterState(MenuState::StartPosMode); break;
                    case TOOL_MAGNET_POLE:
                        enterState(MenuState::MagnetPole, cfg.magnetPolarity < 0 ? 1 : 0); break;
                    case TOOL_START_CHECK:
                        enterState(MenuState::StartCheck, cfg.startCheck ? 0 : 1); break;
                    case TOOL_INFO:
                        driverVersion = stepperDriverVersion();   // UART, so once
                        enterState(MenuState::Info);
                        break;
                    case TOOL_SENSOR_LEVELS: enterState(MenuState::SensorLevels); break;
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
                enterState(MenuState::Tools, TOOL_MAGNET_POLE);
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
            enterState(MenuState::CalMagnetPrompt);
            break;

        case MenuState::CalMagnetPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) enterState(MenuState::CalDetecting);
                else             leaveTo(TOOL_FULL_CAL);
            }
            break;

        case MenuState::CalDetecting:
            // On failure the error has been shown; ask again, so the magnet
            // can be moved and retried without sampling the clear platter again.
            if (calibrationDetect(cfg) == CalibrationStatus::Success) {
                leaveTo(TOOL_FULL_CAL);
            } else {
                enterState(MenuState::CalMagnetPrompt);
            }
            break;

        case MenuState::StartCheckPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                fromBoot = true;
                if (cursor == 0) enterState(MenuState::StartPosMode);
                else             leaveTo(TOOL_CALIB_START);
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
                if (cursor == 0) enterState(MenuState::StartPosAuto);
                else             leaveTo(TOOL_CALIB_START);
            }
            break;

        case MenuState::StartPosAuto:
            // On failure the error has been shown; ask again.
            if (calibrationFindStart(cfg) == CalibrationStatus::Success) {
                leaveTo(TOOL_CALIB_START);
            } else {
                enterState(MenuState::StartPosAutoPrompt);
            }
            break;

        case MenuState::FindStart: {
            // The menu knob jogs the platter, powered, so the step count stays
            // exact. Each detent keeps it turning a moment; quick detents turn
            // it faster.
            uint32_t now = millis();
            if (ev.menuDelta) {
                float rpm = (now - lastJogMs < JOG_FAST_GAP_MS) ? JOG_FAST_RPM : JOG_FINE_RPM;
                stepperJog(ev.menuDelta > 0 ? rpm : -rpm);
                lastJogMs = now;
                jogMoving = true;
            } else if (jogMoving && now - lastJogMs > JOG_HOLD_MS) {
                stepperStop();
                jogMoving = false;
            }
            if (ev.menuPressed) {
                if (jogMoving) { stepperStop(); jogMoving = false; }
                enterState(MenuState::FindStartConfirm);
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
                enterState(MenuState::Tools, TOOL_START_CHECK);
            }
            break;

        case MenuState::ResetCalPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    // Reset calibration fields only.
                    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
                        cfg.hallBaseline[i] = HALL_BASELINE_DEFAULT;
                    }
                    cfg.hallThreshold = HALL_THRESHOLD_DEFAULT;
                    cfg.rpmCorrection = 1.0f;
                    cfg.calibrated    = false;
                    cfg.magnetPolarity = DEFAULT_MAGNET_POLARITY;
                    barForget(cfg);
                    storageSave(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
                    hallSetPolarity(cfg.magnetPolarity);
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
                        // Reset Settings keeps calibration, StartPos and the
                        // scenes; everything else is factory. The settings
                        // are now the Defaults scene.
                        d.calibrated     = cfg.calibrated;
                        d.hallThreshold  = cfg.hallThreshold;
                        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) d.hallBaseline[i] = cfg.hallBaseline[i];
                        d.rpmCorrection  = cfg.rpmCorrection;
                        d.magnetPolarity = cfg.magnetPolarity;
                        d.barPhase       = cfg.barPhase;
                        d.barPhaseValid  = cfg.barPhaseValid;
                        for (uint8_t i = 0; i < NUM_SCENES; i++) {
                            d.scenes[i]       = cfg.scenes[i];
                            d.sceneLearned[i] = cfg.sceneLearned[i];
                            for (uint8_t l = 0; l < NUM_LAYERS; l++)
                                d.sceneLayerOctave[i][l] = cfg.sceneLayerOctave[i][l];
                        }
                        d.currentScene = SCENE_DEFAULTS;
                    }
                    cfg = d;
                    storageCommit(cfg, CommitField::All);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
                    hallSetPolarity(cfg.magnetPolarity);
                    stepperSetCorrection(cfg.rpmCorrection);
                    if (factory) barInit(cfg);     // defaults: start unknown
                    audioSetVolume(cfg.volume);
                    cfg.muted ? audioMute() : audioUnmute();
                    pitchSetOffset(0.0f);
                    layerSetBalance(0);
                    midiInReset();
                    layersApply(cfg);
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
            case MenuState::RootNote:
                drawList(ROOT_ITEMS, ROOT_COUNT, cursor);
                break;
            case MenuState::Scale:
                drawList(SCALE_ITEMS, SCALE_COUNT, cursor);
                break;
            case MenuState::Octave:
                drawList(OCTAVE_ITEMS, OCTAVE_COUNT, cursor);
                break;
            case MenuState::LayerMenu:
                drawList(LAYER_ITEMS, LAYER_ITEMS_COUNT, cursor);
                break;
            case MenuState::LayerMode:
                if (editLayer == LAYER_A) drawList(MODE_ITEMS_A, MODE_COUNT_A, cursor);
                else                      drawList(MODE_ITEMS_B, MODE_COUNT_B, cursor);
                break;
            case MenuState::LayerVoice:
                drawList(VOICE_ITEMS, VOICE_ITEMS_COUNT, cursor);
                break;
            case MenuState::LayerChannel:
                drawList(CHANNEL_ITEMS, CHANNEL_COUNT, cursor);
                break;
            case MenuState::LayerOctave:
                drawList(LAYER_OCTAVE_ITEMS, LAYER_OCTAVE_COUNT, cursor);
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
            case MenuState::MidiFnSetting:
                drawList(MIDI_FN_ITEMS, MIDI_FN_COUNT, cursor);
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
            case MenuState::AuxParam:
                drawAuxParam(cfg);
                break;
            case MenuState::SceneSaveSelect:
                buildSceneLabels(cfg);
                drawList(SCENE_ITEMS, SCENE_SAVE_COUNT, cursor);
                break;
            case MenuState::SceneSaveConfirm:
                lcdLine(0, "Overwrite %u?", (unsigned)(saveSlot + 1));
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
                lcdLine(0, "Magnet on mark");
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
                lcdLine(0, "Magnet on mark");
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
            case MenuState::StartCheck:
                drawList(WELCOME_ITEMS, WELCOME_COUNT, cursor);
                break;
            case MenuState::Info:
                drawInfo(cfg, cursor);
                lastLiveDraw = millis();
                break;
            case MenuState::SensorLevels:
                drawSensorLevels();
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
