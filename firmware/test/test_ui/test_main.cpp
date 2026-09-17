// test_ui -- LCD + encoder bring-up test for the assembled PCB
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_ui -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_ui
//
// WHAT IT DOES:
//   1. Scans the I2C bus and prints every address found. Asserts the LCD
//      backpack (LCD_I2C_ADDRESS) answered.
//   2. Writes a banner to the LCD and blinks the backlight, so you can confirm
//      the display and the UI board's level shifters by eye.
//   3. Loop: prints every encoder detent and button press to USB serial AND
//      mirrors the three encoder counts on the LCD, so you can turn each knob
//      and watch its number move.
//
// NOTE: this test deliberately touches nothing but I2C and the encoder pins.
// It does not compile src/, so it does not talk to the SGTL5000, the TMC2209
// or the MIDI UART. A hang here means power, solder or the I2C bus -- not the
// application firmware.

#include <Arduino.h>
#include <unity.h>
#include <Wire.h>
#include <Encoder.h>
#include <LiquidCrystal_I2C.h>
#include "pins.h"
#include "config.h"

static LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

static Encoder encMenu (PIN_MENU_A,  PIN_MENU_B);
static Encoder encSpeed(PIN_SPEED_A, PIN_SPEED_B);
static Encoder encVol  (PIN_VOL_A,   PIN_VOL_B);

// True once the I2C scan has seen the LCD; the loop skips LCD writes if not,
// so a missing display still lets the encoder half of the test run.
static bool lcdPresent = false;

static bool i2cAck(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

void test_i2c_scan() {
    Serial.println("-- I2C scan --");
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2cAck(addr)) {
            Serial.print("   device at 0x");
            Serial.println(addr, HEX);
            found++;
        }
    }
    Serial.print("   ");
    Serial.print(found);
    Serial.println(" device(s) found");
    Serial.println("   (expect 0x27 LCD backpack, 0x0A SGTL5000 if the shield is on)");

    lcdPresent = i2cAck(LCD_I2C_ADDRESS);
    TEST_ASSERT_TRUE_MESSAGE(lcdPresent, "LCD backpack did not ACK on the I2C bus");
}

void test_lcd_banner() {
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Magnetrone");
    lcd.setCursor(0, 1);
    lcd.print("UI test");
    delay(1000);

    // Blink the backlight three times -- proves the PCF8574 backlight line.
    for (uint8_t i = 0; i < 3; i++) {
        lcd.noBacklight();
        delay(200);
        lcd.backlight();
        delay(200);
    }
    TEST_PASS();
}

// Reports a press on an active-low button, debounced by edge detection only.
static bool pressed(int pin, bool& lastState) {
    bool cur = !digitalRead(pin);
    bool edge = cur && !lastState;
    lastState = cur;
    return edge;
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
    delay(200);

    pinMode(PIN_MENU_BTN,  INPUT_PULLUP);
    pinMode(PIN_SPEED_BTN, INPUT_PULLUP);
    pinMode(PIN_VOL_BTN,   INPUT_PULLUP);

    UNITY_BEGIN();
    RUN_TEST(test_i2c_scan);
    if (lcdPresent) RUN_TEST(test_lcd_banner);
    UNITY_END();

    Serial.println();
    Serial.println("-- interactive: turn each encoder, press each button --");
}

void loop() {
    static long lastMenu = 0, lastSpeed = 0, lastVol = 0;
    static bool btnMenuLast = false, btnSpeedLast = false, btnVolLast = false;
    static uint32_t lastDrawMs = 0;
    static bool dirty = true;

    long m = encMenu.read()  / 4;
    long s = encSpeed.read() / 4;
    long v = encVol.read()   / 4;

    if (m != lastMenu) {
        Serial.print("MENU  "); Serial.print(m - lastMenu > 0 ? "+" : "-");
        Serial.print("  count="); Serial.println(m);
        lastMenu = m; dirty = true;
    }
    if (s != lastSpeed) {
        Serial.print("SPEED "); Serial.print(s - lastSpeed > 0 ? "+" : "-");
        Serial.print("  count="); Serial.println(s);
        lastSpeed = s; dirty = true;
    }
    if (v != lastVol) {
        Serial.print("VOL   "); Serial.print(v - lastVol > 0 ? "+" : "-");
        Serial.print("  count="); Serial.println(v);
        lastVol = v; dirty = true;
    }

    if (pressed(PIN_MENU_BTN,  btnMenuLast))  { Serial.println("MENU  button"); dirty = true; }
    if (pressed(PIN_SPEED_BTN, btnSpeedLast)) { Serial.println("SPEED button"); dirty = true; }
    if (pressed(PIN_VOL_BTN,   btnVolLast))   { Serial.println("VOL   button"); dirty = true; }

    // Mirror the counts on the LCD, rate limited so I2C traffic stays sane.
    if (lcdPresent && dirty && millis() - lastDrawMs >= 100) {
        lastDrawMs = millis();
        dirty = false;
        char line0[17];
        char line1[17];
        snprintf(line0, sizeof(line0), "M%-4ld S%-4ld V%-3ld", lastMenu, lastSpeed, lastVol);
        snprintf(line1, sizeof(line1), "btn %c%c%c        ",
                 digitalRead(PIN_MENU_BTN)  ? '-' : 'M',
                 digitalRead(PIN_SPEED_BTN) ? '-' : 'S',
                 digitalRead(PIN_VOL_BTN)   ? '-' : 'V');
        lcd.setCursor(0, 0); lcd.print(line0);
        lcd.setCursor(0, 1); lcd.print(line1);
    }
}
