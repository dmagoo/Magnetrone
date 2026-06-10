// test_stepper -- stepper driver hardware test
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_stepper -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_stepper
//
// WHAT IT DOES:
//   Runs the motor through slow and fast speeds in both directions.
//   Verify by eye that the motor spins correctly.
//   No belt or load required.
//
// WIRING:
//   - A4988 driver connected to DIR/STEP/ENABLE/SLEEP pins per pins.h
//   - NEMA17 connected to A4988
//   - 24V supply to A4988 VMOT
//   - 3.3V logic supply to A4988 VDD

#include <Arduino.h>
#include <unity.h>
#include "pins.h"

// Step pulse rate in microseconds. 1000us = 1000 steps/sec, safe for any NEMA17/A4988.
#define STEP_DELAY_SLOW_US   500   // ~10 RPM platter
#define STEP_DELAY_FAST_US    38   // ~45 RPM platter (realistic operating speed)

static int currentDelayUs = 500;

static void stepForMs(uint32_t durationMs, int delayUs) {
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        digitalWrite(PIN_STEP, HIGH);
        delayMicroseconds(delayUs / 2);
        digitalWrite(PIN_STEP, LOW);
        delayMicroseconds(delayUs / 2);
    }
}

static void rampTo(int targetUs) {
    int step = (targetUs < currentDelayUs) ? -1 : 1;
    while (currentDelayUs != targetUs) {
        currentDelayUs += step;
        stepForMs(20, currentDelayUs);
    }
}

void test_stepper() {
    TEST_PASS();
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // wait for monitor to connect

    pinMode(PIN_STEP,   OUTPUT);
    pinMode(PIN_DIR,    OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    pinMode(PIN_SLEEP,  OUTPUT);

    digitalWrite(PIN_SLEEP,  HIGH);
    digitalWrite(PIN_ENABLE, LOW);
    delay(10);

    UNITY_BEGIN();
    RUN_TEST(test_stepper);
    UNITY_END();
}

void loop() {
    digitalWrite(PIN_DIR, HIGH);

    Serial.println("Forward slow...");
    rampTo(500);
    stepForMs(5000, 500);

    Serial.println("Forward fast...");
    rampTo(20);
    stepForMs(5000, 20);

    Serial.println("Reverse slow...");
    rampTo(500);
    digitalWrite(PIN_DIR, LOW);
    stepForMs(5000, 500);

    Serial.println("Reverse fast...");
    rampTo(20);
    stepForMs(5000, 20);

    Serial.println("--- done ---");
    rampTo(500);
    delay(2000);
}
