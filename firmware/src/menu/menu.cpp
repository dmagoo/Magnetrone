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

static LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

enum class MenuState : uint8_t {
    Status,
    MainMenu,
    RootNote,
    Scale,
    Octave,
    WelcomeTune,
    LcdTimeout,
    BeatsPerRev,
    SensorShift,
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

static const char* MAIN_ITEMS[] = {
    "Root Note","Scale","Octave","Welcome Tune",
    "LCD Timeout","Beats/Rev","Track Shift","Aux Fn","Pitch Step",
    "Calibration","Reset Cal","Reset All","Back"
};
static const uint8_t MAIN_COUNT = 13;

// -------------------------------------------------------------------------
// Aux function knob
// -------------------------------------------------------------------------
// What the aux knob can be bound to. Order must match AUX_FN_LABELS, and the
// stored cfg.auxFn is an index into it.
enum class AuxFn : uint8_t { Octave, RootNote, ScaleFn, TrackShift, Pitch, COUNT };
static const uint8_t AUX_FN_COUNT = (uint8_t)AuxFn::COUNT;

// The select list carries a trailing Exit; the "set the default" menu carries a
// trailing Back. Same five names either way.
static const char* AUX_FN_LABELS[]      = { "Octave","Root Note","Scale","Track Shift","Pitch","Exit" };
static const char* AUX_FN_MENU_LABELS[] = { "Octave","Root Note","Scale","Track Shift","Pitch","Back" };
static const uint8_t AUX_FN_LIST_COUNT = AUX_FN_COUNT + 1;

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

// Which sensor index plays the root note.
static const char*   SHIFT_LABELS[] = { "0","1","2","3","4","5","6","7","Back" };
static const uint8_t SHIFT_COUNT = 9;   // 8 options + Back

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
// RAM ONLY -- deliberately never calls storageSave(). These are live
// performance moves, not configuration: the saved value is the one you dialled
// in from the main menu, so a session always starts from a known place instead
// of wherever the knob happened to be left.
//
// Wrap vs clamp follows the shape of the value: wrap anything cyclic, clamp
// anything that is a magnitude. Octave is the only magnitude here, and wrapping
// 7 back to 0 would be a seven-octave jump mid-performance.
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
        case AuxFn::TrackShift: {
            int v = ((int)cfg.sensorShift + delta) % NUM_HALL_SENSORS;
            if (v < 0) v += NUM_HALL_SENSORS;
            cfg.sensorShift = (int8_t)v;                       // wrap: a rotation
            break;
        }
        case AuxFn::Pitch: {
            uint8_t div = cfg.pitchStepDiv ? cfg.pitchStepDiv : 1;
            pitchAdjust((float)delta / (float)div);
            break;
        }
        default: break;
    }
}

// Two lines: what the knob is bound to, and where that parameter sits now.
// A list would imply a cursor you have to commit, and nothing here is committed
// -- every step has already been applied by the time it is drawn.
static void drawAuxParam(const SavedConfig& cfg) {
    lcdLine(0, "%-16s", AUX_FN_LABELS[cfg.auxFn]);

    switch ((AuxFn)cfg.auxFn) {
        case AuxFn::Octave:
            lcdLine(1, "Octave %d", cfg.octave);
            break;
        case AuxFn::RootNote:
            lcdLine(1, "%s", ROOT_ITEMS[(uint8_t)cfg.root]);
            break;
        case AuxFn::ScaleFn:
            lcdLine(1, "%s", SCALE_ITEMS[(uint8_t)cfg.scale]);
            break;
        case AuxFn::TrackShift:
            lcdLine(1, "Shift %d", cfg.sensorShift);
            break;
        case AuxFn::Pitch: {
            // Shown in cents rather than semitones so fractional steps read
            // sensibly, and so nothing here depends on %f in snprintf.
            int cents = (int)lroundf(pitchGetOffset() * 100.0f);
            lcdLine(1, "%+d cents", cents);
            break;
        }
        default:
            lcdLine(1, "");
            break;
    }
}


// -------------------------------------------------------------------------

// Plays each scale degree in order then in reverse at current BPM.
// Blocking -- returns when the last note's release has finished.
static void playWelcomeTune(const SavedConfig& cfg) {
    int bpm = (int)(fabsf(cfg.rpm) * cfg.beatsPerRev);
    bpm = constrain(bpm, 40, 200);
    uint32_t beatMs = 60000UL / (uint32_t)bpm;

    lcdLine(0, "  Music  Table  ");
    lcdLine(1, "~~~~~~~~~~~~~~~~");

    // Forward pass.
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint8_t note = scaleNote(cfg.root, cfg.scale, i, cfg.octave);
        uint8_t sounded = midiNoteOn(note, 100);
        delay(NOTE_DURATION_MS);
        midiNoteOff(sounded);
        if (beatMs > NOTE_DURATION_MS) delay(beatMs - NOTE_DURATION_MS);
    }

    // Pause for one beat.
    delay(beatMs);

    // Reverse pass.
    for (int8_t i = NUM_HALL_SENSORS - 1; i >= 0; i--) {
        uint8_t note = scaleNote(cfg.root, cfg.scale, (uint8_t)i, cfg.octave);
        uint8_t sounded = midiNoteOn(note, 100);
        delay(NOTE_DURATION_MS);
        midiNoteOff(sounded);
        if (beatMs > NOTE_DURATION_MS) delay(beatMs - NOTE_DURATION_MS);
    }

    // Let the last note's release tail finish.
    delay(400);
}

// -------------------------------------------------------------------------

void menuInit(const SavedConfig& cfg) {
    lcd.init();

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
    enterState(MenuState::Status);
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
                       ev.menuDelta != 0  || ev.menuPressed;
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

    // menu activity tracking + timeout
    //
    // Prompts are exempt. They ask you to go and do something physical (place a
    // magnet, decide about wiping settings), which reliably takes longer than
    // MENU_TIMEOUT_MS, and having the question vanish mid-task is the bug in
    // todo.md. They stay put until answered.
    bool isPrompt = (state == MenuState::CalibrationPrompt ||
                     state == MenuState::CalibrationRunning ||
                     state == MenuState::ResetCalPrompt ||
                     state == MenuState::ResetAllPrompt);

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
            if (ev.menuPressed) {
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

        case MenuState::AuxFnSelect:
            if (ev.auxDelta) {
                cursor = (uint8_t)((cursor + ev.auxDelta + AUX_FN_LIST_COUNT) % AUX_FN_LIST_COUNT);
                needsRedraw = true;
            }
            if (ev.auxPressed) {
                if (cursor >= AUX_FN_COUNT) {          // "Exit"
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
                    case 3: enterState(MenuState::WelcomeTune,
                                cfg.playWelcomeTune ? 0 : 1); break;
                    case 4: {
                        // Find current timeout value in the options list.
                        uint8_t idx = 1; // default to 5s if not found
                        for (uint8_t i = 0; i < 7; i++) {
                            if (LCD_TIMEOUT_VALUES[i] == cfg.lcdTimeout) { idx = i; break; }
                        }
                        enterState(MenuState::LcdTimeout, idx);
                        break;
                    }
                    case 5: {
                        // Land the cursor on the stored value, not the top.
                        uint8_t idx = 3;  // default to 4 beats if not found
                        for (uint8_t i = 0; i < 6; i++) {
                            if (BEATS_VALUES[i] == cfg.beatsPerRev) { idx = i; break; }
                        }
                        enterState(MenuState::BeatsPerRev, idx);
                        break;
                    }
                    case 6: enterState(MenuState::SensorShift,
                                (uint8_t)constrain(cfg.sensorShift, 0, 7)); break;
                    case 7: enterState(MenuState::AuxFnDefault,
                                (uint8_t)constrain(cfg.auxFn, 0, AUX_FN_COUNT - 1)); break;
                    case 8: {
                        uint8_t idx = 0;
                        for (uint8_t i = 0; i < PITCH_STEP_COUNT - 1; i++) {
                            if (PITCH_STEP_VALUES[i] == cfg.pitchStepDiv) { idx = i; break; }
                        }
                        enterState(MenuState::PitchStep, idx);
                        break;
                    }
                    case 9:  enterState(MenuState::CalibrationPrompt); break;
                    case 10: enterState(MenuState::ResetCalPrompt); break;
                    case 11: enterState(MenuState::ResetAllPrompt); break;
                    case 12: enterState(MenuState::Status); break;
                }
            }
            break;

        case MenuState::RootNote:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + ROOT_COUNT) % ROOT_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 12) { cfg.root = static_cast<RootNote>(cursor); storageSave(cfg); }
                enterState(MenuState::MainMenu, 0);
            }
            break;

        case MenuState::Scale:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + SCALE_COUNT) % SCALE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 8) { cfg.scale = static_cast<Scale>(cursor); storageSave(cfg); }
                enterState(MenuState::MainMenu, 1);
            }
            break;

        case MenuState::Octave:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + OCTAVE_COUNT) % OCTAVE_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < 8) { cfg.octave = cursor; storageSave(cfg); }
                enterState(MenuState::MainMenu, 2);
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
                enterState(MenuState::MainMenu, 3);
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
                enterState(MenuState::MainMenu, 4);
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
                enterState(MenuState::MainMenu, 5);
            }
            break;

        case MenuState::SensorShift:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + SHIFT_COUNT) % SHIFT_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < SHIFT_COUNT - 1) {   // last entry is Back
                    cfg.sensorShift = (int8_t)cursor;
                    storageSave(cfg);
                }
                enterState(MenuState::MainMenu, 6);
            }
            break;

        case MenuState::AuxFnDefault:
            if (ev.menuDelta) {
                cursor = (cursor + ev.menuDelta + AUX_FN_LIST_COUNT) % AUX_FN_LIST_COUNT;
                needsRedraw = true;
            }
            if (ev.menuPressed) {
                if (cursor < AUX_FN_COUNT) {   // last entry is Back
                    cfg.auxFn = cursor;
                    storageSave(cfg);
                }
                enterState(MenuState::MainMenu, 7);
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
                enterState(MenuState::MainMenu, 8);
            }
            break;

        case MenuState::CalibrationPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) enterState(MenuState::CalibrationRunning);
                else enterState(MenuState::MainMenu, 9);
            }
            break;

        case MenuState::CalibrationRunning: {
            calibrationRun(cfg);
            enterState(MenuState::MainMenu, 9);
            break;
        }

        case MenuState::ResetCalPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    // Reset calibration fields only.
                    cfg.hallBaseline  = HALL_BASELINE_DEFAULT;
                    cfg.hallThreshold = HALL_THRESHOLD_DEFAULT;
                    cfg.rpmCorrection = 1.0f;
                    cfg.calibrated    = false;
                    storageSave(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
                    stepperSetCorrection(cfg.rpmCorrection);
                    menuMessage("Cal reset", "");
                    delay(1500);
                }
                enterState(MenuState::MainMenu, 10);
            }
            break;

        case MenuState::ResetAllPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    cfg = storageDefaults();
                    storageSave(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
                    stepperSetCorrection(cfg.rpmCorrection);
                    audioSetVolume(cfg.volume);
                    menuMessage("Reset to defaults", "");
                    delay(1500);
                }
                enterState(MenuState::MainMenu, 11);
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
            case MenuState::WelcomeTune:
                drawList(WELCOME_ITEMS, WELCOME_COUNT, cursor);
                break;
            case MenuState::LcdTimeout:
                drawList(LCD_TIMEOUT_LABELS, LCD_TIMEOUT_COUNT, cursor);
                break;
            case MenuState::BeatsPerRev:
                drawList(BEATS_LABELS, BEATS_COUNT, cursor);
                break;
            case MenuState::SensorShift:
                drawList(SHIFT_LABELS, SHIFT_COUNT, cursor);
                break;
            case MenuState::AuxFnDefault:
                drawList(AUX_FN_MENU_LABELS, AUX_FN_LIST_COUNT, cursor);
                break;
            case MenuState::PitchStep:
                drawList(PITCH_STEP_LABELS, PITCH_STEP_COUNT, cursor);
                break;
            case MenuState::AuxFnSelect:
                drawList(AUX_FN_LABELS, AUX_FN_LIST_COUNT, cursor);
                break;
            case MenuState::AuxParam:
                drawAuxParam(cfg);
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
