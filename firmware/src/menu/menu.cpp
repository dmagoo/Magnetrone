#include "menu.h"
#include <Arduino.h>
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

static LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

enum class MenuState : uint8_t {
    Status,
    MainMenu,
    RootNote,
    Scale,
    Octave,
    WelcomeTune,
    LcdTimeout,
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
    "LCD Timeout","Calibration","Reset Cal","Reset All","Back"
};
static const uint8_t MAIN_COUNT = 9;

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
    int bpm = (int)(cfg.rpm * BEATS_PER_REV);
    lcdLine(0, "BPM:%-3d RPM:%-3d", bpm, (int)cfg.rpm);
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

// -------------------------------------------------------------------------

// Plays each scale degree in order then in reverse at current BPM.
// Blocking -- returns when the last note's release has finished.
static void playWelcomeTune(const SavedConfig& cfg) {
    int bpm = (int)(cfg.rpm * BEATS_PER_REV);
    bpm = constrain(bpm, 40, 200);
    uint32_t beatMs = 60000UL / (uint32_t)bpm;

    lcdLine(0, "  Music  Table  ");
    lcdLine(1, "~~~~~~~~~~~~~~~~");

    // Forward pass.
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint8_t note = scaleNote(cfg.root, cfg.scale, i, cfg.octave);
        midiNoteOn(note, 100);
        delay(NOTE_DURATION_MS);
        midiNoteOff(note);
        if (beatMs > NOTE_DURATION_MS) delay(beatMs - NOTE_DURATION_MS);
    }

    // Pause for one beat.
    delay(beatMs);

    // Reverse pass.
    for (int8_t i = NUM_HALL_SENSORS - 1; i >= 0; i--) {
        uint8_t note = scaleNote(cfg.root, cfg.scale, (uint8_t)i, cfg.octave);
        midiNoteOn(note, 100);
        delay(NOTE_DURATION_MS);
        midiNoteOff(note);
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
    if (ev.speedDelta != 0) {
        cfg.rpm = constrain(cfg.rpm + ev.speedDelta, MIN_RPM, MAX_RPM);
        if (stepperRunning()) stepperSetRPM(cfg.rpm);
        else stepperStart(cfg.rpm);
        storageSave(cfg);
        needsRedraw = true;
    }
    if (ev.speedPressed) {
        if (stepperRunning()) stepperStop();
        else stepperStart(cfg.rpm);
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
    if (ev.menuDelta != 0 || ev.menuPressed) lastActivity = millis();
    if (state != MenuState::Status &&
        millis() - lastActivity > MENU_TIMEOUT_MS) {
        enterState(MenuState::Status);
    }

    // state machine
    switch (state) {

        case MenuState::Status:
            if (ev.menuPressed) enterState(MenuState::MainMenu);
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
                    case 5: enterState(MenuState::CalibrationPrompt); break;
                    case 6: enterState(MenuState::ResetCalPrompt); break;
                    case 7: enterState(MenuState::ResetAllPrompt); break;
                    case 8: enterState(MenuState::Status); break;
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

        case MenuState::CalibrationPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % 2; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) enterState(MenuState::CalibrationRunning);
                else enterState(MenuState::MainMenu, 5);
            }
            break;

        case MenuState::CalibrationRunning: {
            calibrationRun(cfg);
            enterState(MenuState::MainMenu, 5);
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
                    menuMessage("Cal reset", "");
                    delay(1500);
                }
                enterState(MenuState::MainMenu, 6);
            }
            break;

        case MenuState::ResetAllPrompt:
            if (ev.menuDelta) { cursor = (cursor + 1) % CONFIRM_COUNT; needsRedraw = true; }
            if (ev.menuPressed) {
                if (cursor == 0) {
                    cfg = storageDefaults();
                    storageSave(cfg);
                    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
                    audioSetVolume(cfg.volume);
                    menuMessage("Reset to defaults", "");
                    delay(1500);
                }
                enterState(MenuState::MainMenu, 7);
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
