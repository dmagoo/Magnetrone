#include "diag.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "pins.h"
#include "config.h"
#include "config/storage.h"
#include "motion/stepper.h"
#include "sensors/hall.h"
#include "audio/audio.h"
#include "audio/voice.h"

// -----------------------------------------------------------------------------
// Entry flag. "diag" writes it and reboots; the next boot reads and clears it.
// It lives in the last EEPROM byte, past the end of SavedConfig, so settings
// are never touched. Erased EEPROM reads 0xFF, which is not the flag.
// -----------------------------------------------------------------------------
constexpr int     DIAG_FLAG_ADDR = E2END;
constexpr uint8_t DIAG_FLAG      = 0xD1;
static_assert(EEPROM_ADDRESS + sizeof(SavedConfig) <= DIAG_FLAG_ADDR,
              "SavedConfig reaches the diagnostics flag byte");

constexpr uint8_t  AUDIO_I2C_ADDRESS = 0x0A;   // SGTL5000

constexpr uint32_t CHECK_MS        = 1000;     // device checks and the LCD uptime
constexpr uint32_t REPORT_MS       = 5000;     // full report + beep
constexpr uint32_t STREAM_PERIOD_US = 5000;    // "hall <n>": 200 readings/s

// The beep matches test_i2c_audio: 440 Hz sine at about half level, 300 ms.
constexpr float    BEEP_HZ   = 440.0f;
constexpr uint8_t  BEEP_VEL  = 64;
constexpr uint32_t BEEP_MS   = 300;
constexpr uint8_t  BEEP_NOTE = 69;             // id only, for the note off
constexpr float    DIAG_VOLUME = 0.5f;

// The motor's rated current per phase (17HS4401S). "current" will not go past it.
constexpr uint16_t MAX_CURRENT_MA = 1700;

// DRV_STATUS / GSTAT bits (TMC2209 datasheet).
constexpr uint32_t DRV_OTPW  = 1u << 0;
constexpr uint32_t DRV_OT    = 1u << 1;
constexpr uint32_t DRV_S2GA  = 1u << 2;
constexpr uint32_t DRV_S2GB  = 1u << 3;
constexpr uint32_t DRV_S2VSA = 1u << 4;
constexpr uint32_t DRV_S2VSB = 1u << 5;
constexpr uint32_t DRV_OLA   = 1u << 6;
constexpr uint32_t DRV_OLB   = 1u << 7;
constexpr uint32_t DRV_STST  = 1u << 31;       // standstill
constexpr uint8_t  GSTAT_RESET   = 1u << 0;
constexpr uint8_t  GSTAT_DRV_ERR = 1u << 1;
constexpr uint8_t  GSTAT_UV_CP   = 1u << 2;

static SavedConfig        cfg;
static LiquidCrystal_I2C  lcd(LCD_I2C_ADDRESS, 16, 2);

// Line levels read before Wire takes the pins.
static bool sdaHigh = false;
static bool sclHigh = false;

// Device state as of the last check, to print changes the moment they happen.
// Seeded as healthy, so the first check at boot prints only what is wrong.
static bool     lcdOk        = true;
static bool     audioOk      = true;
static uint32_t i2cErrors    = 0;      // failed pings since boot
static bool     drvAnswering = true;
static bool     drvGconfOk   = true;
static uint32_t drvFaults    = 0;      // fault bits of DRV_STATUS last seen
static StepperDriverStatus drv{};

// Chopper mode as last set, for the report.
static bool  chopSpread = false;
static float chopHybridRpm = 0.0f;

// Hall stats over the current report window.
static uint16_t hallMin[NUM_HALL_SENSORS];
static uint16_t hallMax[NUM_HALL_SENSORS];
static uint16_t hallTrigs[NUM_HALL_SENSORS];

static uint32_t lastCheckMs  = 0;
static uint32_t lastReportMs = 0;
static uint32_t beepOffMs    = 0;
static bool     beeping      = false;
static uint32_t reportNo     = 0;

static int8_t   streamSensor = -1;     // 0-7 while "hall <n>" runs
static uint32_t lastStreamUs = 0;

// -----------------------------------------------------------------------------
// Serial line input, shared with normal mode. Echoes what is typed, since the
// monitor does not.
// -----------------------------------------------------------------------------
static char    line[48];
static uint8_t lineLen = 0;

static bool readLine() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (lineLen == 0) continue;
            line[lineLen] = '\0';
            lineLen = 0;
            Serial.println();
            return true;
        }
        if (c == '\b' || c == 127) {
            if (lineLen > 0) { lineLen--; Serial.print("\b \b"); }
            continue;
        }
        if (lineLen < sizeof(line) - 1) {
            line[lineLen++] = c;
            Serial.write(c);
        }
    }
    return false;
}

static void reboot() {
    Serial.flush();
    delay(50);
    SCB_AIRCR = 0x05FA0004;   // system reset request
    while (true) {}
}

bool diagWanted() {
    bool flagged = EEPROM.read(DIAG_FLAG_ADDR) == DIAG_FLAG;
    if (flagged) EEPROM.write(DIAG_FLAG_ADDR, 0xFF);
    pinMode(PIN_MENU_BTN, INPUT_PULLUP);
    delay(5);
    bool held = digitalRead(PIN_MENU_BTN) == LOW;   // active low
    return flagged || held;
}

void diagPollSerial() {
    if (!readLine()) return;
    if (strcmp(line, "diag") == 0) {
        Serial.println("Rebooting into diagnostics...");
        EEPROM.write(DIAG_FLAG_ADDR, DIAG_FLAG);
        reboot();
    }
    Serial.println("Type diag to reboot into diagnostics.");
}

// -----------------------------------------------------------------------------
// Checks
// -----------------------------------------------------------------------------
static bool ping(uint8_t addr) {
    Wire.beginTransmission(addr);
    bool ok = Wire.endTransmission() == 0;
    if (!ok) i2cErrors++;
    return ok;
}

static uint32_t faultBits(uint32_t status) {
    uint32_t f = status & (DRV_OTPW | DRV_OT | DRV_S2GA | DRV_S2GB | DRV_S2VSA | DRV_S2VSB);
    // Open-load reads are meaningless at standstill.
    if (!(status & DRV_STST)) f |= status & (DRV_OLA | DRV_OLB);
    return f;
}

static void printFaults(uint32_t f) {
    if (!f)              { Serial.print("none"); return; }
    if (f & DRV_OT)      Serial.print("OVER-TEMP ");
    if (f & DRV_OTPW)    Serial.print("temp-warning ");
    if (f & DRV_S2GA)    Serial.print("short-to-GND-A ");
    if (f & DRV_S2GB)    Serial.print("short-to-GND-B ");
    if (f & DRV_S2VSA)   Serial.print("short-to-supply-A ");
    if (f & DRV_S2VSB)   Serial.print("short-to-supply-B ");
    if (f & DRV_OLA)     Serial.print("open-load-A ");
    if (f & DRV_OLB)     Serial.print("open-load-B ");
}

static void stamp() {
    Serial.print("[");
    Serial.print(millis() / 1000.0f, 1);
    Serial.print(" s] ");
}

static void checkI2c() {
    bool l = ping(LCD_I2C_ADDRESS);
    bool a = ping(AUDIO_I2C_ADDRESS);
    if (l != lcdOk)   { stamp(); Serial.println(l ? "LCD 0x27 back" : "LCD 0x27 LOST"); }
    if (a != audioOk) { stamp(); Serial.println(a ? "Audio 0x0A back" : "Audio 0x0A LOST"); }
    lcdOk = l;
    audioOk = a;
}

static void checkDriver() {
    drv = stepperDriverStatus();
    bool answering = drv.version == STEPPER_DRIVER_VERSION;
    if (answering != drvAnswering) {
        stamp();
        Serial.println(answering ? "Motor driver answering"
                                 : "Motor driver NOT answering (no 24 V, or UART)");
    }
    drvAnswering = answering;
    if (!answering) return;   // the other registers are garbage

    bool gconfOk = drv.gconf == drv.gconfWanted;
    if (gconfOk != drvGconfOk) {
        stamp();
        if (gconfOk) Serial.println("Driver config OK");
        else Serial.printf("Driver config LOST: GCONF 0x%03lX, wanted 0x%03lX\n",
                           (unsigned long)drv.gconf, (unsigned long)drv.gconfWanted);
    }
    drvGconfOk = gconfOk;

    if (drv.gstat & (GSTAT_RESET | GSTAT_DRV_ERR | GSTAT_UV_CP)) {
        stamp();
        Serial.print("Driver GSTAT:");
        if (drv.gstat & GSTAT_RESET)   Serial.print(" RESET (driver restarted)");
        if (drv.gstat & GSTAT_DRV_ERR) Serial.print(" DRV_ERR (shut down)");
        if (drv.gstat & GSTAT_UV_CP)   Serial.print(" UV_CP (supply undervoltage)");
        Serial.println();
        stepperClearDriverGstat(drv.gstat & 0x07);
    }

    uint32_t f = faultBits(drv.drvStatus);
    if (f != drvFaults) {
        stamp();
        Serial.print("Driver flags: ");
        printFaults(f);
        Serial.println();
    }
    drvFaults = f;
}

// -----------------------------------------------------------------------------
// LCD: "DIAG" and the uptime on line 1, a fixed pattern on line 2. Garbling
// shows as anything other than exactly this.
// -----------------------------------------------------------------------------
static void lcdUptime() {
    char buf[17];
    snprintf(buf, sizeof(buf), "DIAG  up %6lus", (unsigned long)(millis() / 1000));
    lcd.setCursor(0, 0);
    lcd.print(buf);
}

static void lcdDraw() {
    lcd.init();
    lcd.backlight();
    lcdUptime();
    lcd.setCursor(0, 1);
    lcd.print("0123456789ABCDEF");
}

// -----------------------------------------------------------------------------
// Report
// -----------------------------------------------------------------------------
static void resetHallWindow() {
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        hallMin[i] = 0xFFFF;
        hallMax[i] = 0;
        hallTrigs[i] = 0;
    }
}

static void report() {
    reportNo++;
    Serial.println();
    Serial.printf("==== diag report %lu (%lu s) ====\n",
                  (unsigned long)reportNo, (unsigned long)(millis() / 1000));

    Serial.printf("I2C    SDA %s  SCL %s (at boot)  LCD 0x27 %s  Audio 0x0A %s  failed pings %lu\n",
                  sdaHigh ? "HIGH" : "LOW", sclHigh ? "HIGH" : "LOW",
                  lcdOk ? "OK" : "MISSING", audioOk ? "OK" : "MISSING",
                  (unsigned long)i2cErrors);

    if (!drvAnswering) {
        Serial.println("Driver NOT answering");
    } else {
        Serial.printf("Driver OK  GCONF 0x%03lX (%s)  flags: ",
                      (unsigned long)drv.gconf, drvGconfOk ? "as set" : "LOST");
        printFaults(drvFaults);
        Serial.println();
    }
    Serial.printf("Motor  %s  %.1f RPM  %u mA  ",
                  stepperRunning() ? "running" : "stopped",
                  stepperCurrentRPM(), (unsigned)stepperCurrentMa());
    if (chopSpread)                  Serial.println("SpreadCycle");
    else if (chopHybridRpm > 0.0f)   Serial.printf("hybrid above %.1f RPM\n", chopHybridRpm);
    else                             Serial.println("StealthChop");

    Serial.println("Hall   raw  base   min   max  trig");
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        Serial.printf("  %u  %5u %5u %5u %5u %5u\n", i + 1,
                      hallRead(i), cfg.hallBaseline[i],
                      hallMin[i] == 0xFFFF ? 0 : hallMin[i], hallMax[i], hallTrigs[i]);
    }
    resetHallWindow();
}

static void beep() {
    audioNoteOnFreq(LAYER_A, BEEP_NOTE, BEEP_VEL, BEEP_HZ);
    beepOffMs = millis() + BEEP_MS;
    beeping = true;
}

// -----------------------------------------------------------------------------
// Commands
// -----------------------------------------------------------------------------
static void help() {
    Serial.println();
    Serial.println("Diagnostics commands:");
    Serial.println("  help                 this list");
    Serial.println("  spin <rpm>           run the platter (negative = reverse)");
    Serial.println("  stop                 stop the platter");
    Serial.println("  current <mA>         motor current (max 1700)");
    Serial.println("  chop stealth         StealthChop (quiet)");
    Serial.println("  chop spread          SpreadCycle");
    Serial.println("  chop hybrid <rpm>    StealthChop, SpreadCycle above <rpm>");
    Serial.println("  beep                 play the beep");
    Serial.println("  lcd                  re-init the LCD");
    Serial.println("  hall <1-8>           stream one sensor (ms raw), any key stops");
    Serial.println("  exit                 reboot to normal");
}

static void command() {
    char* cmd = strtok(line, " ");
    char* arg = strtok(nullptr, " ");
    char* arg2 = strtok(nullptr, " ");
    if (!cmd) return;

    if (!strcmp(cmd, "help")) {
        help();
    } else if (!strcmp(cmd, "spin") && arg) {
        float rpm = strtof(arg, nullptr);
        if (fabsf(rpm) < MIN_RPM || fabsf(rpm) > MAX_RPM) {
            Serial.printf("RPM must be %.0f to %.0f (either sign)\n", MIN_RPM, MAX_RPM);
            return;
        }
        stepperStart(rpm);
        Serial.printf("Spinning at %.1f RPM\n", rpm);
    } else if (!strcmp(cmd, "stop")) {
        stepperStop();
        Serial.println("Stopping");
    } else if (!strcmp(cmd, "current") && arg) {
        long ma = strtol(arg, nullptr, 10);
        if (ma < 1 || ma > MAX_CURRENT_MA) {
            Serial.printf("Current must be 1 to %u mA\n", (unsigned)MAX_CURRENT_MA);
            return;
        }
        stepperSetCurrentMa((uint16_t)ma);
        Serial.printf("Current %ld mA\n", ma);
    } else if (!strcmp(cmd, "chop") && arg) {
        bool spread = false;
        float hybrid = 0.0f;
        if (!strcmp(arg, "stealth"))      { }
        else if (!strcmp(arg, "spread"))  { spread = true; }
        else if (!strcmp(arg, "hybrid") && arg2) {
            hybrid = strtof(arg2, nullptr);
            if (hybrid <= 0.0f) { Serial.println("hybrid needs an RPM above 0"); return; }
        } else { Serial.println("chop stealth | chop spread | chop hybrid <rpm>"); return; }
        bool ok = stepperSetChopper(spread, hybrid);
        chopSpread = spread;
        chopHybridRpm = hybrid;
        Serial.println(ok ? "Chopper set" : "Chopper set, but GCONF did not verify");
    } else if (!strcmp(cmd, "beep")) {
        beep();
    } else if (!strcmp(cmd, "lcd")) {
        lcdDraw();
        Serial.println("LCD re-initialized");
    } else if (!strcmp(cmd, "hall") && arg) {
        long n = strtol(arg, nullptr, 10);
        if (n < 1 || n > NUM_HALL_SENSORS) { Serial.println("hall 1 to 8"); return; }
        streamSensor = (int8_t)(n - 1);
        Serial.printf("Streaming hall %ld (ms raw). Press any key to stop.\n", n);
    } else if (!strcmp(cmd, "exit")) {
        Serial.println("Rebooting to normal...");
        reboot();
    } else if (!strcmp(cmd, "diag")) {
        Serial.println("Already in diagnostics.");
    } else {
        Serial.println("Unknown command. Type help.");
    }
}

// -----------------------------------------------------------------------------
// Setup / loop
// -----------------------------------------------------------------------------
void diagSetup() {
    Serial.begin(115200);

    // Idle I2C lines sit HIGH on their pull-ups. LOW = something holds the bus.
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    delay(5);
    sdaHigh = digitalRead(PIN_SDA);
    sclHigh = digitalRead(PIN_SCL);

    storageLoad(cfg);
    stepperInit();                              // coils off until commanded
    stepperSetCorrection(cfg.rpmCorrection);
    stepperClearDriverGstat(0x07);              // the power-up reset flag
    hallInit();
    hallSetCalibration(cfg.hallBaseline, cfg.hallNoise, cfg.hallThreshold);
    hallSetPolarity(cfg.magnetPolarity);

    audioInit(DIAG_VOLUME, false);
    Voice v = voiceGet((uint8_t)VoiceId::Piano);
    v.waveform        = voiceWaveform(Wave::Sine);
    v.attackMs        = 5;
    v.decayMs         = 0;
    v.sustain         = 1.0f;
    v.releaseMs       = 20;
    v.harmonicsEdited = false;
    v.filter.cutoff   = VOICE_FILTER_OFF;
    audioSetVoice(LAYER_A, v);

    lcdDraw();
    checkI2c();
    checkDriver();
    resetHallWindow();

    help();
    lastCheckMs = lastReportMs = millis();
}

void diagLoop() {
    stepperUpdate();
    hallUpdate();
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint16_t r = hallRead(i);
        if (r < hallMin[i]) hallMin[i] = r;
        if (r > hallMax[i]) hallMax[i] = r;
        if (hallTrigger(i) != HallPole::None) hallTrigs[i]++;
    }

    if (beeping && (int32_t)(millis() - beepOffMs) >= 0) {
        audioNoteOff(LAYER_A, BEEP_NOTE);
        beeping = false;
    }

    if (streamSensor >= 0) {
        if (Serial.available()) {
            while (Serial.available()) Serial.read();
            streamSensor = -1;
            Serial.println("Stream stopped.");
            lastReportMs = millis();
        } else if (micros() - lastStreamUs >= STREAM_PERIOD_US) {
            lastStreamUs = micros();
            Serial.printf("%lu %u\n", (unsigned long)millis(), hallRead(streamSensor));
        }
    } else if (readLine()) {
        command();
    }

    uint32_t now = millis();
    if (now - lastCheckMs >= CHECK_MS) {
        lastCheckMs = now;
        checkI2c();
        checkDriver();
        if (lcdOk) lcdUptime();
    }
    // No reports or beeps while streaming, so the stream stays clean.
    if (streamSensor < 0 && now - lastReportMs >= REPORT_MS) {
        lastReportMs = now;
        report();
        beep();
    }
}
