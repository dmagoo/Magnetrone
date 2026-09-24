#include "encoder.h"
#include <Arduino.h>
#include <Encoder.h>
#include "pins.h"

static Encoder encMenu  (PIN_MENU_A,  PIN_MENU_B);
static Encoder encSpeed (PIN_SPEED_A, PIN_SPEED_B);
static Encoder encVol   (PIN_VOL_A,   PIN_VOL_B);
static Encoder encAux   (PIN_AUX_ENC_A, PIN_AUX_ENC_B);

static long lastMenu  = 0;
static long lastSpeed = 0;
static long lastVol   = 0;
static long lastAux   = 0;

static EncoderEvent pending = {};

// Bare EC11 buttons with no RC filtering (debounce is done here in software
// instead of in hardware). A mechanical contact rattles
// for a few ms on both make and break, and every one of those edges used to
// read as a fresh press. Ignore any state change that lands inside the settle
// window; 30 ms is well past the bounce and far below a deliberate double-click.
constexpr uint16_t BUTTON_DEBOUNCE_MS = 30;

static bool readButton(int pin, bool& lastState, uint32_t& lastChangeMs) {
    bool cur = !digitalRead(pin);  // active low
    if (cur == lastState) return false;

    uint32_t now = millis();
    if (now - lastChangeMs < BUTTON_DEBOUNCE_MS) return false;  // still bouncing

    lastChangeMs = now;
    lastState    = cur;
    return cur;   // report the press edge only, not the release
}

static bool     btnMenuLast  = false;
static bool     btnSpeedLast = false;
static bool     btnVolLast   = false;
static bool     btnAuxLast   = false;
static uint32_t btnMenuMs    = 0;
static uint32_t btnSpeedMs   = 0;
static uint32_t btnVolMs     = 0;
static uint32_t btnAuxMs     = 0;

void encoderInit() {
    pinMode(PIN_MENU_BTN,  INPUT_PULLUP);
    pinMode(PIN_SPEED_BTN, INPUT_PULLUP);
    pinMode(PIN_VOL_BTN,   INPUT_PULLUP);
    pinMode(PIN_AUX_ENC_BTN, INPUT_PULLUP);
}

void encoderUpdate() {
    long m = encMenu.read()  / 4;
    long s = encSpeed.read() / 4;
    long v = encVol.read()   / 4;
    long a = encAux.read()   / 4;

    // Clamp accumulated deltas to [-127, 127] between reads.
    pending.menuDelta   = (int8_t)constrain(pending.menuDelta   + (m - lastMenu),  -127, 127);
    pending.speedDelta  = (int8_t)constrain(pending.speedDelta  + (s - lastSpeed), -127, 127);
    pending.volumeDelta = (int8_t)constrain(pending.volumeDelta + (v - lastVol),   -127, 127);
    pending.auxDelta    = (int8_t)constrain(pending.auxDelta    + (a - lastAux),   -127, 127);

    lastMenu  = m;
    lastSpeed = s;
    lastVol   = v;
    lastAux   = a;

    if (readButton(PIN_MENU_BTN,  btnMenuLast,  btnMenuMs))  pending.menuPressed   = true;
    if (readButton(PIN_SPEED_BTN, btnSpeedLast, btnSpeedMs)) pending.speedPressed  = true;
    if (readButton(PIN_VOL_BTN,   btnVolLast,   btnVolMs))   pending.volumePressed = true;
    if (readButton(PIN_AUX_ENC_BTN, btnAuxLast, btnAuxMs))   pending.auxPressed    = true;
}

EncoderEvent encoderEvents() {
    EncoderEvent e = pending;
    pending = {};
    return e;
}

// For blocking code (the welcome tune) that only listens for a menu press.
// Everything else stays pending for menuUpdate() to handle afterwards.
bool encoderTakeMenuPress() {
    bool pressed = pending.menuPressed;
    pending.menuPressed = false;
    return pressed;
}
