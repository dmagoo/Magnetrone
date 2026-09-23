// test_hall_range -- Audible magnet range finder
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_hall_range -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_hall_range
//
// WHAT IT DOES:
//   Lets you find, by ear, how close a magnet has to be before it would fire a
//   note in the main app. Headphones in, hold a magnet over a sensor, move it
//   slowly in.
//
//     silence      -> nothing detected (inside the noise deadband)
//     slow clicks  -> magnet detected, still below the trigger threshold
//     fast clicks  -> getting close
//     solid tone   -> threshold crossed; this is where the app fires a note
//
//   The click-to-solid transition is the number you are measuring. Hold the
//   magnet there and read the gap off a ruler.
//
//   Each sensor has its own pitch (C major, #1 = C4 ... #8 = C5) so you can
//   tell which one is responding without looking. The magnet polarity that
//   raises the sensor output plays the note; the opposite pole plays it an
//   octave down, so a flipped magnet is audibly different.
//
//   Unlike the main app, this captures each sensor's real rest level at
//   startup instead of using the stale HALL_BASELINE_DEFAULT, so the distances
//   it reports are the post-calibration ones.
//
// WIRING:
//   Hall sensors per pins.h. Audio shield seated with the mic jack toward the
//   USB end of the Teensy. Headphones in the shield's headphone jack.
//
// SERIAL:
//   Live 8-row table: deviation, positive and negative peak hold, the smaller
//   peak as a percentage of the larger, field in Gauss, raw ADC.
//   Press any key to clear the peak-hold columns.
//
//   The two peaks are held separately so a magnet's fringe lobes are visible.
//   One pass reads fringe / face / fringe with the fringes opposite in sign, so
//   after a single clean pass the larger peak is the face and the smaller is
//   the fringe. The percentage is fringe-to-face: compare it against the
//   calibrated threshold (THRESHOLD_FACTOR, 50% of peak) to see whether
//   fringes would fire if both polarities were accepted.

#include <Arduino.h>
#include <Audio.h>
#include <Wire.h>
#include <unity.h>
#include "pins.h"
#include "config.h"

// --- Tunable constants ---
constexpr int   UPDATE_MS         = 100;  // display refresh interval
constexpr int   BAR_HALF_WIDTH    = 10;   // chars each side of center pipe
constexpr int   BAR_MAX_DEV       = 600;  // deviation that fills the bar fully

constexpr int   BASELINE_SAMPLES  = 256;  // reads per sensor when capturing rest
constexpr int   DEADBAND_FLOOR    = 10;   // counts; never go quieter than this
constexpr int   DEADBAND_NOISE_X  = 4;    // deadband = this * measured rest spread

constexpr float CLICK_HZ_MIN      = 2.0f;   // click rate at the deadband edge
constexpr float CLICK_HZ_MAX      = 25.0f;  // click rate just under threshold
constexpr float SOLID_SUSTAIN     = 0.7f;   // envelope sustain for the solid tone
constexpr float OUTPUT_VOLUME     = 0.5f;

// Scale degrees, one per sensor. C4 D4 E4 F4 G4 A4 B4 C5.
static const uint8_t SENSOR_NOTE[NUM_HALL_SENSORS] = {
    60, 62, 64, 65, 67, 69, 71, 72
};

// --- ADC counts to Gauss -------------------------------------------------
// A1301: 2.5 mV/G, ratiometric, quiescent = Vcc/2, on a 5 V supply.
// The sensor output passes a 7.5k/15k divider (ratio 2/3) into the Teensy's
// 3.3 V 12-bit ADC. One count is therefore:
//
//   (3.3 / 4095) V per count / (2/3) divider / 0.0025 V per Gauss = 0.4835 G
//
// Kept as integer math so nothing here depends on %f support in snprintf.
constexpr int32_t GAUSS_NUM = 4835;
constexpr int32_t GAUSS_DEN = 10000;

static inline int32_t countsToGauss(int32_t counts) {
    return (counts * GAUSS_NUM) / GAUSS_DEN;
}

// --- Pin list in sensor order ---
static const int HALL_PINS[NUM_HALL_SENSORS] = {
    PIN_HALL_1, PIN_HALL_2, PIN_HALL_3, PIN_HALL_4,
    PIN_HALL_5, PIN_HALL_6, PIN_HALL_7, PIN_HALL_8
};

// --- Audio chain ---------------------------------------------------------
// Mirrors src/audio/audio.cpp. Duplicated rather than shared because the test
// environments build with build_src_filter = -<*>.
static AudioSynthWaveform     osc[NUM_HALL_SENSORS];
static AudioEffectEnvelope    env[NUM_HALL_SENSORS];
static AudioMixer4            mixA;
static AudioMixer4            mixB;
static AudioMixer4            mixOut;
static AudioOutputI2S         i2sOut;
static AudioControlSGTL5000   sgtl5000;

static AudioConnection patchOscEnv0(osc[0], 0, env[0], 0);
static AudioConnection patchOscEnv1(osc[1], 0, env[1], 0);
static AudioConnection patchOscEnv2(osc[2], 0, env[2], 0);
static AudioConnection patchOscEnv3(osc[3], 0, env[3], 0);
static AudioConnection patchOscEnv4(osc[4], 0, env[4], 0);
static AudioConnection patchOscEnv5(osc[5], 0, env[5], 0);
static AudioConnection patchOscEnv6(osc[6], 0, env[6], 0);
static AudioConnection patchOscEnv7(osc[7], 0, env[7], 0);

static AudioConnection patchEnvMixA0(env[0], 0, mixA, 0);
static AudioConnection patchEnvMixA1(env[1], 0, mixA, 1);
static AudioConnection patchEnvMixA2(env[2], 0, mixA, 2);
static AudioConnection patchEnvMixA3(env[3], 0, mixA, 3);

static AudioConnection patchEnvMixB0(env[4], 0, mixB, 0);
static AudioConnection patchEnvMixB1(env[5], 0, mixB, 1);
static AudioConnection patchEnvMixB2(env[6], 0, mixB, 2);
static AudioConnection patchEnvMixB3(env[7], 0, mixB, 3);

static AudioConnection patchMixAOut(mixA, 0, mixOut, 0);
static AudioConnection patchMixBOut(mixB, 0, mixOut, 1);

static AudioConnection patchOutL(mixOut, 0, i2sOut, 0);
static AudioConnection patchOutR(mixOut, 0, i2sOut, 1);

static float midiToHz(uint8_t note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

// --- Per-sensor state ----------------------------------------------------
enum Mode : uint8_t { MODE_SILENT, MODE_CLICK, MODE_SOLID };

struct SensorState {
    int      baseline    = 0;   // captured rest level
    int      noiseP2P    = 0;   // rest spread seen during capture
    int      deadband    = DEADBAND_FLOOR;
    int      deviation   = 0;   // signed, from captured baseline
    int      peakPos     = 0;   // largest positive deviation since reset
    int      peakNeg     = 0;   // largest negative deviation since reset (<= 0)
    Mode     mode        = MODE_SILENT;
    bool     negative    = false;  // polarity currently sounding
    uint32_t lastClickMs = 0;
    int      triggers    = 0;
};

static SensorState sensors[NUM_HALL_SENSORS];
static uint32_t    startMs = 0;

// Rows the live table occupies, for cursorUp().
constexpr int TABLE_LINES = 3 + NUM_HALL_SENSORS;

// --- Capture each sensor's rest level and noise spread -------------------
static void captureBaselines() {
    Serial.println("Capturing rest baselines -- keep magnets away from the arm.");

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        int32_t sum = 0;
        int     lo  = 4095;
        int     hi  = 0;

        for (int n = 0; n < BASELINE_SAMPLES; n++) {
            int v = analogRead(HALL_PINS[i]);
            sum += v;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            delayMicroseconds(200);
        }

        SensorState& s = sensors[i];
        s.baseline = (int)(sum / BASELINE_SAMPLES);
        s.noiseP2P = hi - lo;
        s.deadband = max(DEADBAND_FLOOR, s.noiseP2P * DEADBAND_NOISE_X);

        char buf[96];
        snprintf(buf, sizeof(buf),
                 "  #%d  baseline:%4d  noise p-p:%3d  deadband:%3d (%ld G)",
                 i + 1, s.baseline, s.noiseP2P, s.deadband,
                 (long)countsToGauss(s.deadband));
        Serial.println(buf);
    }
    Serial.println();
}

// --- Audio response for one sensor --------------------------------------
static void updateVoice(int i, uint32_t now) {
    SensorState& s   = sensors[i];
    int          mag = abs(s.deviation);
    bool         neg = (s.deviation < 0);

    // Match the app's trigger semantics: fire at the threshold, release only
    // once the reading is back near baseline (HALL_REARM_LEVEL).
    Mode want;
    if (mag >= HALL_THRESHOLD_DEFAULT) {
        want = MODE_SOLID;
    } else if (s.mode == MODE_SOLID && mag >= HALL_REARM_LEVEL) {
        want = MODE_SOLID;
    } else if (mag >= s.deadband) {
        want = MODE_CLICK;
    } else {
        want = MODE_SILENT;
    }

    // Polarity picks the octave: positive plays the note, negative an octave down.
    if (want != MODE_SILENT && (neg != s.negative || s.mode == MODE_SILENT)) {
        s.negative = neg;
        osc[i].frequency(midiToHz(SENSOR_NOTE[i] - (neg ? 12 : 0)));
    }

    if (want == MODE_SOLID && s.mode != MODE_SOLID) {
        s.triggers++;
        env[i].sustain(SOLID_SUSTAIN);
        env[i].noteOn();
    } else if (want != MODE_SOLID && s.mode == MODE_SOLID) {
        env[i].noteOff();
    }

    if (want == MODE_CLICK) {
        // Click rate ramps linearly from the deadband edge up to the threshold.
        float span = (float)(HALL_THRESHOLD_DEFAULT - s.deadband);
        float frac = (span > 0.0f) ? ((float)(mag - s.deadband) / span) : 1.0f;
        frac = constrain(frac, 0.0f, 1.0f);

        float    hz       = CLICK_HZ_MIN + frac * (CLICK_HZ_MAX - CLICK_HZ_MIN);
        uint32_t periodMs = (uint32_t)(1000.0f / hz);

        if (now - s.lastClickMs >= periodMs) {
            s.lastClickMs = now;
            env[i].sustain(0.0f);   // percussive: no sustain, just a tick
            env[i].noteOn();
        }
    }

    s.mode = want;
}

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

static void drawTable() {
    uint32_t elapsed = (millis() - startMs) / 1000;
    uint32_t mins    = elapsed / 60;
    uint32_t secs    = elapsed % 60;

    char line[128];

    snprintf(line, sizeof(line),
             "Runtime: %02lu:%02lu    any key clears peaks",
             (unsigned long)mins, (unsigned long)secs);
    Serial.print(line);
    Serial.println("          \r");

    snprintf(line, sizeof(line),
             "MAGNET RANGE  threshold:+/-%d (%ld G)  solid tone = app would fire",
             HALL_THRESHOLD_DEFAULT, (long)countsToGauss(HALL_THRESHOLD_DEFAULT));
    Serial.print(line);
    Serial.println("          \r");

    Serial.print("                            dev  +peak  -peak  minor%  gauss   raw  state");
    Serial.println("          \r");

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        SensorState& s = sensors[i];

        Serial.print('#');
        Serial.print(i + 1);
        Serial.print(' ');
        printBar(s.deviation);

        const char* state = (s.mode == MODE_SOLID) ? "SOLID"
                          : (s.mode == MODE_CLICK) ? "click"
                          : "  -  ";

        // Smaller peak as a percentage of the larger: the fringe-to-face ratio
        // after a clean pass. Gauss is for the larger (face) peak.
        int major = max(s.peakPos, -s.peakNeg);
        int minor = min(s.peakPos, -s.peakNeg);
        int pct   = (major > 0) ? (minor * 100) / major : 0;
        int face  = (s.peakPos >= -s.peakNeg) ? s.peakPos : s.peakNeg;

        snprintf(line, sizeof(line), " %+5d  %+5d  %+5d  %5d%%  %+5ld  %4d  %s",
                 s.deviation, s.peakPos, s.peakNeg, pct,
                 (long)countsToGauss(face), s.baseline + s.deviation, state);
        Serial.print(line);
        Serial.println("  \r");
    }
}

void test_hall_range() {
    TEST_PASS();
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    analogReadResolution(HALL_ADC_BITS);
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        pinMode(HALL_PINS[i], INPUT);
    }

    AudioMemory(24);
    sgtl5000.enable();
    sgtl5000.volume(OUTPUT_VOLUME);

    // Gain staging matches audio.cpp: 0.25 per voice, 4 voices per sub-mixer.
    for (int i = 0; i < 4; i++) {
        mixA.gain(i, 0.25f);
        mixB.gain(i, 0.25f);
    }
    mixOut.gain(0, 1.0f);
    mixOut.gain(1, 1.0f);

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        osc[i].begin(1.0f, midiToHz(SENSOR_NOTE[i]), WAVEFORM_SINE);
        osc[i].amplitude(1.0f);   // the envelope does the gating

        env[i].attack(3);
        env[i].decay(60);
        env[i].sustain(0.0f);
        env[i].release(40);
    }

    captureBaselines();

    startMs = millis();

    UNITY_BEGIN();
    RUN_TEST(test_hall_range);
    UNITY_END();

    // Print blank lines so the first cursorUp has room
    for (int i = 0; i < TABLE_LINES; i++) Serial.println();
}

void loop() {
    uint32_t now = millis();

    // Clear peak hold on any serial input
    if (Serial.available()) {
        while (Serial.available()) Serial.read();
        for (int i = 0; i < NUM_HALL_SENSORS; i++) {
            sensors[i].peakPos = 0;
            sensors[i].peakNeg = 0;
        }
    }

    // Sensors and audio run every pass so the clicks stay on time
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        SensorState& s = sensors[i];
        s.deviation = analogRead(HALL_PINS[i]) - s.baseline;

        if (s.deviation > s.peakPos) s.peakPos = s.deviation;
        if (s.deviation < s.peakNeg) s.peakNeg = s.deviation;

        updateVoice(i, now);
    }

    // Display is rate-limited
    static uint32_t lastUpdate = 0;
    if (now - lastUpdate < UPDATE_MS) return;
    lastUpdate = now;

    cursorUp(TABLE_LINES);
    drawTable();
}
