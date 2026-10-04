// test_hall_noise -- Resting noise on every hall sensor
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_hall_noise -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_hall_noise
//
// WHAT IT DOES:
//   Measures how much each sensor's raw reading wanders with no magnet near,
//   first with the motor off, then with the platter turning at
//   CALIBRATION_RPM. The peak detector in hall.cpp fires when a reading falls
//   back from its highest value, so noise can fake a peak by the size of its
//   peak-to-peak swing. That swing sets the minimum drop.
//
//   Take every magnet off the platter first. The platter turns during the
//   second phase.
//
// WIRING:
//   Hall sensors per pins.h. Motor and TMC2209 as in test_stepper.
//
// SERIAL:
//   One table per phase, one row per sensor:
//     mean    average raw reading
//     sd      standard deviation, in tenths of a count
//     p2p     highest minus lowest reading over the whole phase
//     win     worst highest-minus-lowest inside any WINDOW_MS window, about
//             the length of one magnet pass on the outer track at 24 RPM
//             (10 mm magnet at 92.3 mm radius is 6.2 degrees, 43 ms)
//
// Unlike the other hall tests, this one builds the app's stepper module
// (build_src_filter in platformio.ini) instead of duplicating the driver setup.

#include <Arduino.h>
#include <unity.h>
#include "pins.h"
#include "config.h"
#include "motion/stepper.h"

constexpr uint32_t PHASE_MS    = 10000;  // sampling time per phase
constexpr uint32_t SPINUP_MS   = 3000;   // wait for the ramp before sampling
constexpr uint32_t WINDOW_MS   = 50;     // short window, about one pass

static const uint8_t HALL_PINS[NUM_HALL_SENSORS] = {
    PIN_HALL_1, PIN_HALL_2, PIN_HALL_3, PIN_HALL_4,
    PIN_HALL_5, PIN_HALL_6, PIN_HALL_7, PIN_HALL_8
};

struct Stats {
    uint64_t sum, sumSq;
    uint16_t lo, hi;         // whole phase
    uint16_t winLo, winHi;   // current window
    uint16_t worstWin;       // largest window swing
};

static uint16_t worstWinAll[2];   // per phase, across all sensors
static uint16_t worstP2pAll[2];

static void pumpFor(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) stepperUpdate();
}

static void runPhase(uint8_t phase, const char* name) {
    Stats s[NUM_HALL_SENSORS];
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        s[i] = {0, 0, 0xFFFF, 0, 0xFFFF, 0, 0};
    }

    uint32_t count = 0;
    uint32_t start = millis();
    uint32_t winStart = start;

    while (millis() - start < PHASE_MS) {
        stepperUpdate();   // keeps the motor turning in phase 2
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
            uint16_t v = (uint16_t)analogRead(HALL_PINS[i]);
            s[i].sum   += v;
            s[i].sumSq += (uint64_t)v * v;
            if (v < s[i].lo) s[i].lo = v;
            if (v > s[i].hi) s[i].hi = v;
            if (v < s[i].winLo) s[i].winLo = v;
            if (v > s[i].winHi) s[i].winHi = v;
        }
        count++;

        if (millis() - winStart >= WINDOW_MS) {
            for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
                uint16_t w = s[i].winHi - s[i].winLo;
                if (w > s[i].worstWin) s[i].worstWin = w;
                s[i].winLo = 0xFFFF;
                s[i].winHi = 0;
            }
            winStart = millis();
        }
    }

    char line[96];
    Serial.println();
    snprintf(line, sizeof(line), "%s: %lu samples per sensor (%lu per second)",
             name, (unsigned long)count, (unsigned long)(count * 1000 / PHASE_MS));
    Serial.println(line);
    Serial.println("hall   mean   sd(x10)   p2p   win");

    worstWinAll[phase] = 0;
    worstP2pAll[phase] = 0;
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        uint32_t mean = (uint32_t)(s[i].sum / count);
        // Variance in counts^2; integer maths, so nothing depends on %f.
        uint64_t var  = s[i].sumSq / count - (uint64_t)mean * mean;
        uint32_t sd10 = (uint32_t)lroundf(sqrtf((float)var) * 10.0f);
        uint16_t p2p  = s[i].hi - s[i].lo;
        snprintf(line, sizeof(line), "%4u   %4lu   %7lu   %3u   %3u",
                 i + 1, (unsigned long)mean, (unsigned long)sd10,
                 p2p, s[i].worstWin);
        Serial.println(line);
        if (p2p > worstP2pAll[phase]) worstP2pAll[phase] = p2p;
        if (s[i].worstWin > worstWinAll[phase]) worstWinAll[phase] = s[i].worstWin;
    }
}

void test_hall_noise() {
    runPhase(0, "Motor off");

    stepperStart(CALIBRATION_RPM);
    pumpFor(SPINUP_MS);
    runPhase(1, "Motor at CALIBRATION_RPM");
    stepperStop();
    pumpFor(SPINUP_MS);

    char line[96];
    Serial.println();
    snprintf(line, sizeof(line),
             "Worst p2p: off %u, spinning %u. Worst %lu ms window: off %u, spinning %u",
             worstP2pAll[0], worstP2pAll[1], (unsigned long)WINDOW_MS,
             worstWinAll[0], worstWinAll[1]);
    Serial.println(line);
    TEST_PASS();
}

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 4000) {}
    delay(1000);
    analogReadResolution(HALL_ADC_BITS);
    stepperInit();

    UNITY_BEGIN();
    RUN_TEST(test_hall_noise);
    UNITY_END();
}

void loop() {}
