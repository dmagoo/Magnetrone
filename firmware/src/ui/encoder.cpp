#include "encoder.h"
#include <Arduino.h>
#include <Encoder.h>
#include "pins.h"

static Encoder encMenu  (PIN_MENU_A,  PIN_MENU_B);
static Encoder encSpeed (PIN_SPEED_A, PIN_SPEED_B);
static Encoder encVol   (PIN_VOL_A,   PIN_VOL_B);

static long lastMenu  = 0;
static long lastSpeed = 0;
static long lastVol   = 0;

static EncoderEvent pending = {};

static bool readButton(int pin, bool& lastState) {
    bool cur = !digitalRead(pin);  // active low
    bool pressed = cur && !lastState;
    lastState = cur;
    return pressed;
}

static bool btnMenuLast  = false;
static bool btnSpeedLast = false;
static bool btnVolLast   = false;

void encoderInit() {
    pinMode(PIN_MENU_BTN,  INPUT_PULLUP);
    pinMode(PIN_SPEED_BTN, INPUT_PULLUP);
    pinMode(PIN_VOL_BTN,   INPUT_PULLUP);
}

void encoderUpdate() {
    long m = encMenu.read()  / 4;
    long s = encSpeed.read() / 4;
    long v = encVol.read()   / 4;

    // Clamp accumulated deltas to [-127, 127] between reads.
    pending.menuDelta   = (int8_t)constrain(pending.menuDelta   + (m - lastMenu),  -127, 127);
    pending.speedDelta  = (int8_t)constrain(pending.speedDelta  + (s - lastSpeed), -127, 127);
    pending.volumeDelta = (int8_t)constrain(pending.volumeDelta + (v - lastVol),   -127, 127);

    lastMenu  = m;
    lastSpeed = s;
    lastVol   = v;

    if (readButton(PIN_MENU_BTN,  btnMenuLast))  pending.menuPressed  = true;
    if (readButton(PIN_SPEED_BTN, btnSpeedLast)) pending.speedPressed = true;
    if (readButton(PIN_VOL_BTN,   btnVolLast))   pending.volumePressed = true;
}

EncoderEvent encoderEvents() {
    EncoderEvent e = pending;
    pending = {};
    return e;
}
