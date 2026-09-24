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
    WelcomeTune,
    LcdTimeout,
    BeatsPerRev,
    MagnetPole,
    FirstBootPrompt,   // shown once on a fresh EEPROM: calibrate now or skip
    AuxFnDefault,
    PitchStep,
    AuxFnSelect,    // aux knob: choose what the knob modulates
    AuxParam,       // aux knob: modulate the chosen parameter, live
    CalibrationPrompt,
    CalibrationRunning,
    ResetCalPrompt,
    ResetAllPrompt
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
    "Mode","Voice","Channel","Octave","Level","Shift","Wrap","Low Note","Back"
};
static const uint8_t LAYER_ITEMS_COUNT = 9;
enum : uint8_t { LAYER_ITEM_MODE, LAYER_ITEM_VOICE, LAYER_ITEM_CHANNEL,
                 LAYER_ITEM_OCTAVE, LAYER_ITEM_LEVEL, LAYER_ITEM_SHIFT,
                 LAYER_ITEM_WRAP, LAYER_ITEM_LOW_NOTE, LAYER_ITEM_BACK };

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
    "LCD Timeout","Beats/Rev","Aux Fn","Pitch Step",
    "Magnet Pole","Calibration","Reset Cal","Reset All","Exit"
};  // Exit returns to the live display; submenus keep "Back"
static const uint8_t MAIN_COUNT = 15;

// Which way a passing magnet pushes the sensor output. Calibration measures
// this; the override exists so a wrong guess does not leave the table silent.
static const int8_t POLE_VALUES[] = { 1, -1 };
static const char*  POLE_LABELS[] = { "Normal","Flipped","Back" };
static const uint8_t POLE_COUNT = 3;   // 2 options + Back

// -------------------------------------------------------------------------
// Aux function knob
// -------------------------------------------------------------------------
// What the aux knob can be bound to. Order must match AUX_FN_LABELS, and the
// stored cfg.auxFn is an index into it.
// Storage migration (migrateAuxFn() in storage.cpp) knows this order too.
enum class AuxFn : uint8_t { Octave, RootNote, ScaleFn, ShiftA, ShiftB,
                             LowNoteA, LowNoteB, Pitch,
                             Balance, VoiceA, VoiceB, COUNT };
static const uint8_t AUX_FN_COUNT = (uint8_t)AuxFn::COUNT;

// The select list carries two trailing actions, Reset and Exit. The "set the
// default binding" menu carries only a trailing Back -- reset has no meaning
// there, since that menu sets committed values rather than modulating live ones.
static const char* AUX_FN_LABELS[] = {
    "Octave","Root Note","Scale","Layer A Shift","Layer B Shift",
    "Layer A Low","Layer B Low","Pitch",
    "A/B Balance","Layer A Voice","Layer B Voice","Reset All","Exit"
};
static const char* AUX_FN_MENU_LABELS[] = {
    "Octave","Root Note","Scale","Layer A Shift","Layer B Shift",
    "Layer A Low","Layer B Low","Pitch",
    "A/B Balance","Layer A Voice","Layer B Voice","Back"
};
static const uint8_t AUX_FN_SELECT_COUNT = AUX_FN_COUNT + 2;  // + Reset, Exit
static const uint8_t AUX_FN_MENU_COUNT   = AUX_FN_COUNT + 1;  // + Back

// Positions of the two actions at the end of the select list.
static const uint8_t AUX_ACTION_RESET = AUX_FN_COUNT;
static const uint8_t AUX_ACTION_EXIT  = AUX_FN_COUNT + 1;

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

// -------------------------------------------------------------------------

static void enterState(MenuState s, uint8_t initialCursor = 0) {
    state        = s;
    cursor       = initialCursor;
    needsRedraw  = true;
    lastActivity = millis();
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
    // Tempo is a magnitude; cfg.rpm carries direction in its sign.
    int bpm = (int)(fabsf(cfg.rpm) * cfg.beatsPerRev);
    lcdLine(0, "BPM:%-3d RPM:%-4d", bpm, (int)cfg.rpm);
    char vb[7];
    buildVolBar(vb, cfg.volume, cfg.muted);
    lcdLine(1, "%-2s %-7s%s",
        ROOT_ITEMS[static_cast<uint8_t>(cfg.root)],
        SCALE_ITEMS[static_cast<uint8_t>(cfg.scale)],
        vb);
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
            int n = (int)Scale::COUNT;
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
            drawList(SCALE_ITEMS, (uint8_t)Scale::COUNT, (uint8_t)cfg.scale);
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
    // reliably takes longer than MENU_TIMEOUT_MS, and having the question
    // vanish mid-task was a real bug. They stay put until answered.
    bool isPrompt = (state == MenuState::FirstBootPrompt ||
                     state == MenuState::CalibrationPrompt ||
                     state == MenuState::CalibrationRunning ||
                     state == MenuState::ResetCalPrompt ||
                     state == MenuState::ResetAllPrompt);

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
                  state == MenuState::AuxParam);

    if (ev.menuDelta != 0 || ev.menuPressed) lastActivity = millis();
    if (state != MenuState::Status && !isPrompt && !isAux &&
        millis() - lastActivity > MENU_TIMEOUT_MS) {
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
                // Straight to the Fn list, with the current binding selected --
                // this is also how you check what the knob is bound to, since
                // the live display has no room to show it.
                auxEnteredFromLive = false;
                enterState(MenuState::AuxFnSelect, cfg.auxFn);
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
                cursor = (uint8_t)((cursor + ev.auxDelta + AUX_FN_SELECT_COUNT) % AUX_FN_SELECT_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor == AUX_ACTION_EXIT) {
                    enterState(MenuState::Status);
                } else if (cursor == AUX_ACTION_RESET) {
                    // Throw away every live modulation at once and drop back to
                    // the live display, where the restored values are visible on
                    // the status line. Nothing is written: this drift never
                    // reached EEPROM, so reverting is purely a RAM operation.
                    storageRevertLive(cfg);
                    pitchSetOffset(0.0f);
                    layerSetBalance(0);
                    layersApply(cfg);
                    enterState(MenuState::Status);
                } else {
                    // Rebinding IS saved -- it changes rarely, unlike the values
                    // the knob modulates.
                    cfg.auxFn = cursor;
                    storageSave(cfg);
                    auxEnteredFromLive = false;
                    enterState(MenuState::AuxParam);
                }
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
                else                    enterState(MenuState::AuxFnSelect, cfg.auxFn);
            }
            break;

        case MenuState::MainMenu:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + MAIN_COUNT) % MAIN_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                switch (cursor) {
                    case 0: enterState(MenuState::RootNote,
                                static_cast<uint8_t>(cfg.root)); break;
                    case 1: enterState(MenuState::Scale,
                                static_cast<uint8_t>(cfg.scale)); break;
                    case 2: enterState(MenuState::Octave, cfg.octave); break;
                    case 3: editLayer = LAYER_A; enterState(MenuState::LayerMenu); break;
                    case 4: editLayer = LAYER_B; enterState(MenuState::LayerMenu); break;
                    case 5: enterState(MenuState::WelcomeTune,
                                cfg.playWelcomeTune ? 0 : 1); break;
                    case 6: {
                        // Find current timeout value in the options list.
                        uint8_t idx = 1; // default to 5s if not found
                        for (uint8_t i = 0; i < 7; i++) {
                            if (LCD_TIMEOUT_VALUES[i] == cfg.lcdTimeout) { idx = i; break; }
                        }
                        enterState(MenuState::LcdTimeout, idx);
                        break;
                    }
                    case 7: {
                        // Land the cursor on the stored value, not the top.
                        uint8_t idx = 3;  // default to 4 beats if not found
                        for (uint8_t i = 0; i < 6; i++) {
                            if (BEATS_VALUES[i] == cfg.beatsPerRev) { idx = i; break; }
                        }
                        enterState(MenuState::BeatsPerRev, idx);
                        break;
                    }
                    case 8: enterState(MenuState::AuxFnDefault,
                                (uint8_t)constrain(cfg.auxFn, 0, AUX_FN_COUNT - 1)); break;
                    case 9: {
                        uint8_t idx = 0;
                        for (uint8_t i = 0; i < PITCH_STEP_COUNT - 1; i++) {
                            if (PITCH_STEP_VALUES[i] == cfg.pitchStepDiv) { idx = i; break; }
                        }
                        enterState(MenuState::PitchStep, idx);
                        break;
                    }
                    case 10: enterState(MenuState::MagnetPole,
                                cfg.magnetPolarity < 0 ? 1 : 0); break;
                    case 11: enterState(MenuState::CalibrationPrompt); break;
                    case 12: enterState(MenuState::ResetCalPrompt); break;
                    case 13: enterState(MenuState::ResetAllPrompt); break;
                    case 14: enterState(MenuState::Status); break;
                }
            }
            break;

        case MenuState::RootNote:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + ROOT_COUNT) % ROOT_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 12) { cfg.root = static_cast<RootNote>(cursor); storageCommit(cfg); }
                enterState(MenuState::MainMenu, 0);
            }
            break;

        case MenuState::Scale:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + SCALE_COUNT) % SCALE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 8) { cfg.scale = static_cast<Scale>(cursor); storageCommit(cfg); }
                enterState(MenuState::MainMenu, 1);
            }
            break;

        case MenuState::Octave:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + OCTAVE_COUNT) % OCTAVE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 8) { cfg.octave = cursor; storageCommit(cfg); }
                enterState(MenuState::MainMenu, 2);
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
                    default:
                        enterState(MenuState::MainMenu, editLayer == LAYER_A ? 3 : 4); break;
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
                    storageCommit(cfg);       // voice is also an aux target
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
                    storageSave(cfg);
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
                    storageCommit(cfg);       // shift is also an aux target
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
                    storageCommit(cfg);       // Low Note is also an aux target
                } else if (editLayer == LAYER_B && cursor == LOW_NOTE_ITEM_SAME_AS_A) {
                    cfg.layer[LAYER_B].lowNoteSameAsA = true;
                    storageSave(cfg);
                }
                enterState(MenuState::LayerMenu, LAYER_ITEM_LOW_NOTE);
            }
            break;
        }

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
                enterState(MenuState::MainMenu, 5);
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
                enterState(MenuState::MainMenu, 6);
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
                enterState(MenuState::MainMenu, 7);
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
                enterState(MenuState::MainMenu, 8);
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
                enterState(MenuState::MainMenu, 9);
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
                enterState(MenuState::MainMenu, 10);
            }
            break;

        case MenuState::FirstBootPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) enterState(MenuState::CalibrationRunning);
                else             enterState(MenuState::Status);
            }
            break;

        case MenuState::CalibrationPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) enterState(MenuState::CalibrationRunning);
                else enterState(MenuState::MainMenu, 11);
            }
            break;

        case MenuState::CalibrationRunning: {
            calibrationRun(cfg);
            enterState(MenuState::MainMenu, 11);
            break;
        }

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
                    storageSave(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
                    hallSetPolarity(cfg.magnetPolarity);
                    stepperSetCorrection(cfg.rpmCorrection);
                    menuMessage("Cal reset", "");
                    delay(1500);
                }
                enterState(MenuState::MainMenu, 12);
            }
            break;

        case MenuState::ResetAllPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    cfg = storageDefaults();
                    storageCommit(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
                    hallSetPolarity(cfg.magnetPolarity);
                    stepperSetCorrection(cfg.rpmCorrection);
                    audioSetVolume(cfg.volume);
                    layerSetBalance(0);
                    layersApply(cfg);
                    menuMessage("Reset to defaults", "");
                    delay(1500);
                }
                enterState(MenuState::MainMenu, 13);
            }
            break;
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
            case MenuState::WelcomeTune:
                drawList(WELCOME_ITEMS, WELCOME_COUNT, cursor);
                break;
            case MenuState::LcdTimeout:
                drawList(LCD_TIMEOUT_LABELS, LCD_TIMEOUT_COUNT, cursor);
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
                drawList(AUX_FN_LABELS, AUX_FN_SELECT_COUNT, cursor);
                break;
            case MenuState::AuxParam:
                drawAuxParam(cfg);
                break;
            case MenuState::FirstBootPrompt:
                lcdLine(0, "Not calibrated");
                lcdLine(1, "%c Setup %c Skip",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::CalibrationPrompt:
                lcdLine(0, "Place a magnet");
                lcdLine(1, "%c OK  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::CalibrationRunning:
                // calibrationRun() blocks and drives the LCD directly via menuMessage().
                break;
            case MenuState::ResetCalPrompt:
                lcdLine(0, "Reset cal data?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
            case MenuState::ResetAllPrompt:
                lcdLine(0, "Reset ALL data?");
                lcdLine(1, "%c Yes  %c Back",
                    cursor == 0 ? LCD_ARROW_RIGHT : ' ',
                    cursor == 1 ? LCD_ARROW_RIGHT : ' ');
                break;
        }
    }
}
