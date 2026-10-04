// test_i2c_audio -- is the LCD / audio shield bus alive?
//
// HOW TO RUN (from the firmware/ folder, 24 V not needed):
//   %USERPROFILE%\.platformio\penv\Scripts\pio.exe test -e teensy41_test_i2c_audio
//   %USERPROFILE%\.platformio\penv\Scripts\pio.exe device monitor
//
// Touches nothing else: no EEPROM, no motor. Safe to run any time.
//
// WHAT IT DOES:
//   At startup, reads SDA (18) and SCL (19) as plain inputs. Both should be
//   HIGH (pulled up). A LOW line is shorted or held down.
//   Then, every 2 seconds, forever, prints a report like:
//
//     ==== check 12 ====
//     SDA line at startup : HIGH (ok)
//     SCL line at startup : HIGH (ok)
//     LCD        0x27     : OK
//     Audio chip 0x0A     : MISSING
//     Other devices       : none
//     Beep                : skipped (audio chip missing)
//
//   and beeps if the audio chip answers. The LCD, if it answers, shows the
//   same two results. Open the monitor whenever; the report repeats.
//
//   The LCD and the audio chip share SDA 18 / SCL 19. Unplug the LCD to test
//   the shield alone, or remove the shield to test the LCD alone.

#include <Arduino.h>
#include <Wire.h>
#include <Audio.h>
#include <LiquidCrystal_I2C.h>
#include <unity.h>

constexpr uint8_t ADDR_LCD   = 0x27;   // config.h LCD_I2C_ADDRESS
constexpr uint8_t ADDR_AUDIO = 0x0A;   // SGTL5000
constexpr int     PIN_SDA    = 18;
constexpr int     PIN_SCL    = 19;

static LiquidCrystal_I2C lcd(ADDR_LCD, 16, 2);

static AudioSynthWaveformSine sine;
static AudioOutputI2S         i2sOut;
static AudioConnection        patchL(sine, 0, i2sOut, 0);
static AudioConnection        patchR(sine, 0, i2sOut, 1);
static AudioControlSGTL5000   sgtl5000;

static bool sdaHigh = false;
static bool sclHigh = false;
static bool lcdReady   = false;   // initialized since it last answered
static bool audioReady = false;   // enabled since it last answered

static bool answers(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// Unity needs at least one test; this one just records the line levels.
void test_bus_lines() {
    pinMode(PIN_SDA, INPUT);
    pinMode(PIN_SCL, INPUT);
    delay(5);
    sdaHigh = digitalRead(PIN_SDA);
    sclHigh = digitalRead(PIN_SCL);
    TEST_ASSERT_TRUE_MESSAGE(sdaHigh && sclHigh, "SDA or SCL is LOW at startup");
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_bus_lines);
    UNITY_END();

    Wire.begin();
    AudioMemory(8);
    sine.frequency(440);
    sine.amplitude(0.0f);
}

void loop() {
    static uint32_t check = 0;
    check++;

    bool lcdOk   = answers(ADDR_LCD);
    bool audioOk = answers(ADDR_AUDIO);

    // (Re)initialize a device whenever it comes back, so an intermittent
    // connection recovers instead of staying dead until the next reset.
    if (lcdOk && !lcdReady) { lcd.init(); lcd.backlight(); }
    lcdReady = lcdOk;
    if (audioOk && !audioReady) audioOk = sgtl5000.enable() && sgtl5000.volume(0.5f);
    audioReady = audioOk;

    Serial.println();
    Serial.print("==== check "); Serial.print(check); Serial.println(" ====");
    Serial.print("SDA line at startup : "); Serial.println(sdaHigh ? "HIGH (ok)" : "LOW (PROBLEM)");
    Serial.print("SCL line at startup : "); Serial.println(sclHigh ? "HIGH (ok)" : "LOW (PROBLEM)");
    Serial.print("LCD        0x27     : "); Serial.println(lcdOk   ? "OK" : "MISSING");
    Serial.print("Audio chip 0x0A     : "); Serial.println(audioOk ? "OK" : "MISSING");

    Serial.print("Other devices       : ");
    int others = 0;
    for (uint8_t a = 1; a < 127; a++) {
        if (a == ADDR_LCD || a == ADDR_AUDIO) continue;
        if (answers(a)) { Serial.print("0x"); Serial.print(a, HEX); Serial.print(" "); others++; }
    }
    Serial.println(others ? "" : "none");

    if (lcdOk) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("LCD OK  #"); lcd.print(check);
        lcd.setCursor(0, 1);
        lcd.print(audioOk ? "Audio OK" : "Audio MISSING");
    }

    if (audioOk) {
        Serial.println("Beep                : playing");
        sine.amplitude(0.5f);
        delay(300);
        sine.amplitude(0.0f);
    } else {
        Serial.println("Beep                : skipped (audio chip missing)");
    }

    delay(2000);
}
