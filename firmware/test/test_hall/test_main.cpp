// test_hall -- Hall effect sensor live monitor
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_hall -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_hall
//
// WHAT IT DOES:
//   Live display of all 8 hall sensors -- deviation from baseline, bar graph,
//   ON/OFF state, range, EMA, and trigger count. Updates in place.
//   Pass a magnet over each sensor to verify response.
//
// WIRING:
//   Hall sensors connected per pins.h (analog pins 14-17, digital 38-41)

#include <Arduino.h>
#include <unity.h>
#include "pins.h"
#include "config.h"

// --- Tunable constants ---
constexpr float   EMA_ALPHA       = 0.1f;   // EMA smoothing factor (lower = smoother)
constexpr int     UPDATE_MS       = 100;    // display refresh interval
constexpr int     BAR_HALF_WIDTH  = 10;     // chars each side of center pipe
constexpr int     BAR_MAX_DEV     = 600;    // deviation that fills the bar fully

// --- Pin list in sensor order ---
static const int HALL_PINS[NUM_HALL_SENSORS] = {
    PIN_HALL_1, PIN_HALL_2, PIN_HALL_3, PIN_HALL_4,
    PIN_HALL_5, PIN_HALL_6, PIN_HALL_7, PIN_HALL_8
};

// --- Per-sensor state ---
struct SensorState {
    int   deviation  = 0;
    float ema        = 0.0f;
    int   rangeMin   = 0;
    int   rangeMax   = 0;
    bool  on         = false;
    bool  wasOn      = false;
    int   triggers   = 0;
};

static SensorState sensors[NUM_HALL_SENSORS];
static uint32_t    startMs = 0;

// --- Draw a center-anchored bar ---
static void printBar(int deviation) {
    int filled = (int)((float)abs(deviation) / BAR_MAX_DEV * BAR_HALF_WIDTH);
    filled = min(filled, BAR_HALF_WIDTH);

    Serial.print('[');
    for (int i = BAR_HALF_WIDTH - 1; i >= 0; i--) {
        Serial.print((deviation < 0 && i < filled) ? '=' : ' ');
    }
    Serial.print('|');
    for (int i = 0; i < BAR_HALF_WIDTH; i++) {
        Serial.print((deviation > 0 && i < filled) ? '=' : ' ');
    }
    Serial.print(']');
}

// --- Move cursor up N lines ---
static void cursorUp(int n) {
    Serial.print("\033[");
    Serial.print(n);
    Serial.print('A');
}

void test_hall() {
    TEST_PASS();
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    analogReadResolution(HALL_ADC_BITS);
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        pinMode(HALL_PINS[i], INPUT);
    }

    startMs = millis();

    UNITY_BEGIN();
    RUN_TEST(test_hall);
    UNITY_END();

    // Print blank lines so first cursorUp has room
    Serial.println();  // runtime line
    Serial.println();  // header
    for (int i = 0; i < NUM_HALL_SENSORS; i++) Serial.println();
}

void loop() {
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate < UPDATE_MS) return;
    lastUpdate = millis();

    // Read and update state
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        int raw = analogRead(HALL_PINS[i]);
        int dev = raw - HALL_BASELINE_DEFAULT;
        sensors[i].deviation = dev;
        sensors[i].ema = EMA_ALPHA * dev + (1.0f - EMA_ALPHA) * sensors[i].ema;
        if (dev < sensors[i].rangeMin) sensors[i].rangeMin = dev;
        if (dev > sensors[i].rangeMax) sensors[i].rangeMax = dev;
        sensors[i].on = abs(dev) > HALL_THRESHOLD_DEFAULT;
        if (sensors[i].on && !sensors[i].wasOn) sensors[i].triggers++;
        sensors[i].wasOn = sensors[i].on;
    }

    // Move cursor up to overwrite
    cursorUp(NUM_HALL_SENSORS + 2);

    // Runtime
    uint32_t elapsed = (millis() - startMs) / 1000;
    uint32_t mins = elapsed / 60;
    uint32_t secs = elapsed % 60;
    Serial.print("Runtime: ");
    if (mins < 10) Serial.print('0');
    Serial.print(mins);
    Serial.print(':');
    if (secs < 10) Serial.print('0');
    Serial.print(secs);
    Serial.println("                    \r");

    // Header
    Serial.print("HALL SENSORS  baseline:");
    Serial.print(HALL_BASELINE_DEFAULT);
    Serial.print("  threshold:+/-");
    Serial.print(HALL_THRESHOLD_DEFAULT);
    Serial.println("          \r");

    // Sensor rows
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        SensorState& s = sensors[i];

        Serial.print('#');
        Serial.print(i + 1);
        Serial.print(' ');
        printBar(s.deviation);

        // Deviation
        char devBuf[8];
        snprintf(devBuf, sizeof(devBuf), "%+5d", s.deviation);
        Serial.print(devBuf);

        // ON/OFF
        Serial.print(s.on ? "  ON  " : "  off ");

        // Range
        char rangeBuf[24];
        snprintf(rangeBuf, sizeof(rangeBuf), "range:[%+4d,%+4d]", s.rangeMin, s.rangeMax);
        Serial.print(rangeBuf);

        // EMA
        char emaBuf[12];
        snprintf(emaBuf, sizeof(emaBuf), "  ema:%+5d", (int)s.ema);
        Serial.print(emaBuf);

        // Trigger count
        char trigBuf[16];
        snprintf(trigBuf, sizeof(trigBuf), "  trg:%3d", s.triggers);
        Serial.print(trigBuf);

        // Raw ADC
        char rawBuf[12];
        snprintf(rawBuf, sizeof(rawBuf), "  raw:%4d", analogRead(HALL_PINS[i]));
        Serial.print(rawBuf);

        Serial.println("  \r");
    }
}
