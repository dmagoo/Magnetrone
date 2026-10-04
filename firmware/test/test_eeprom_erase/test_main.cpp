// test_eeprom_erase -- wipe the whole EEPROM, then a hardware hello world
//
// HOW TO RUN (from the firmware/ folder):
//   pio test -e teensy41_test_eeprom_erase
//
// WHAT IT DOES:
//   1. Writes 0xFF (the erased value) to every EEPROM byte and reads them all
//      back. Every saved setting, scene, custom voice and calibration is gone;
//      the main firmware starts from factory defaults on its next boot and
//      needs a full calibration. Reflash the main firmware afterwards.
//   2. Scans the I2C bus and prints what answers on Serial. Expect the LCD
//      (0x27) and the audio shield's SGTL5000 (0x0A). Both share SDA 18 / SCL 19,
//      so if neither answers, suspect the bus (shield seating, wiring).
//   3. Loops forever: "Hello" on the LCD, a short beep, and the I2C scan
//      again (so a serial monitor opened late still sees it). The LCD's second
//      line counts the loops. The motor is left disabled: grinding it on USB
//      power alone proves nothing.

#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Audio.h>
#include <LiquidCrystal_I2C.h>
#include <TMCStepper.h>
#include <unity.h>
#include "pins.h"

// --- LCD (address as config.h's LCD_I2C_ADDRESS) ---
static LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- Audio: one sine into the shield ---
static AudioSynthWaveformSine sine;
static AudioOutputI2S         i2sOut;
static AudioConnection        patchL(sine, 0, i2sOut, 0);
static AudioConnection        patchR(sine, 0, i2sOut, 1);
static AudioControlSGTL5000   sgtl5000;

// --- TMC2209 (as test_stepper_wiring) ---
#define R_SENSE          0.11f
#define DRIVER_ADDRESS   0b00
#define RUN_CURRENT_MA   900
#define MICROSTEPS       8
#define GCONF_STEALTHCHOP  0x1C0

static TMC2209Stepper driver(&Serial8, R_SENSE, DRIVER_ADDRESS);

void test_eeprom_erase() {
    const int len = EEPROM.length();
    Serial.print("Erasing "); Serial.print(len); Serial.println(" bytes");

    for (int i = 0; i < len; i++) EEPROM.update(i, 0xFF);

    int bad = 0;
    for (int i = 0; i < len; i++) if (EEPROM.read(i) != 0xFF) bad++;
    Serial.print("Bytes not erased: "); Serial.println(bad);
    TEST_ASSERT_EQUAL_MESSAGE(0, bad, "Some EEPROM bytes did not erase");
}

static void i2cScan() {
    Serial.println("I2C scan (expect 0x0A SGTL5000, 0x27 LCD):");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print("  found 0x"); Serial.println(addr, HEX);
            found++;
        }
    }
    if (!found) Serial.println("  nothing answered");
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    UNITY_BEGIN();
    RUN_TEST(test_eeprom_erase);
    UNITY_END();

    Wire.begin();
    i2cScan();

    lcd.init();
    lcd.backlight();

    AudioMemory(8);
    sgtl5000.enable();
    sgtl5000.volume(0.5f);
    sine.frequency(440);
    sine.amplitude(0.0f);

    pinMode(PIN_STEP,   OUTPUT);
    pinMode(PIN_DIR,    OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, HIGH);   // active low: driver off, motor left alone
    digitalWrite(PIN_STEP,   LOW);
    digitalWrite(PIN_DIR,    HIGH);

    Serial8.begin(115200);
    driver.begin();
    driver.GCONF(GCONF_STEALTHCHOP);
    driver.toff(5);
    driver.rms_current(RUN_CURRENT_MA);
    driver.microsteps(MICROSTEPS);
    driver.intpol(true);
    driver.pwm_autoscale(true);
    Serial.print("TMC2209 version 0x"); Serial.print(driver.version(), HEX);
    Serial.println(" (expect 0x21)");
}

void loop() {
    static uint32_t count = 0;
    count++;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Hello, EEPROM");
    lcd.setCursor(0, 1);
    lcd.print("erased  loop ");
    lcd.print(count);

    Serial.print("Loop "); Serial.print(count); Serial.println(": beep");
    i2cScan();

    sine.amplitude(0.5f);
    delay(300);
    sine.amplitude(0.0f);

    delay(1500);
}
