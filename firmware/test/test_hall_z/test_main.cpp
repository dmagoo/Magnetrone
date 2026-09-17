// test_hall_z -- source-impedance check for the hall divider network
//
// HOW TO RUN:
//   pio test -e teensy41_test_hall_z
//
// WHY:
//   Sensor 7 measures 1.6 V at the Teensy pin with a multimeter but analogRead()
//   returns ~1500 (about 1.2 V), while the identical sensors return ~2100. A
//   10 Mohm meter cannot see a cracked solder joint that adds tens of kohms in
//   series, but the ADC can: the sample-and-hold capacitor has to be charged
//   through that resistance in a few microseconds.
//
// WHAT IT DOES:
//   For every sensor, three readings:
//     single  -- one analogRead after the mux has just been parked on ANOTHER
//                channel. This is the worst case and matches normal operation.
//     burst   -- 100 back-to-back reads of the same channel. The S/H cap is
//                already charged from the previous read, so a high source
//                impedance largely stops mattering.
//     slow    -- maximum averaging and the longest conversion the core allows.
//
//   A healthy sensor reads about the same all three ways. A sensor behind a
//   resistive joint reads low on "single" and climbs on "burst" and "slow".
//
// READING THE RESULT:
//   burst - single of roughly 0..30 counts  -> source impedance is fine
//   burst - single of hundreds of counts    -> excess series resistance

#include <Arduino.h>
#include <unity.h>
#include "pins.h"
#include "config.h"

static const int HALL_PINS[NUM_HALL_SENSORS] = {
    PIN_HALL_1, PIN_HALL_2, PIN_HALL_3, PIN_HALL_4,
    PIN_HALL_5, PIN_HALL_6, PIN_HALL_7, PIN_HALL_8
};

// "single" mirrors hall.cpp exactly: one sweep across all eight channels in
// order, one analogRead each. Reading the channels round-robin like this is
// what the real firmware does, so it is the reading that actually matters.
// (An earlier version parked the mux on a fixed channel between reads. That
// produced 4095 on every healthy channel -- an artefact of the back-to-back
// conversions, not a real measurement -- so it is gone.)
static void sweepSingle(int out[NUM_HALL_SENSORS]) {
    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        out[i] = analogRead(HALL_PINS[i]);
    }
}

static int readBurst(int pin) {
    int v = 0;
    for (int i = 0; i < 100; i++) v = analogRead(pin);
    return v;
}

static int readSlow(int pin) {
    analogReadAveraging(32);
    for (int i = 0; i < 8; i++) analogRead(pin);
    int v = analogRead(pin);
    analogReadAveraging(4);
    return v;
}

// Drives each hall pin as a digital output, high then low, and reads it back
// with the ADC. This needs no meter and no probing.
//
// If a pin reads near 4095 driven high and near 0 driven low, then the pad and
// the ADC behind it both work, and any bad sensor reading must be the signal
// failing to reach the pad. If a pin's readings barely move, that pin itself is
// damaged.
//
// Driving a pin that is wired to the hall divider is safe: the divider is 7.5k
// and 15k, so the Teensy sources or sinks well under a milliamp.
static void pinDriveCheck() {
    Serial.println();
    Serial.println("pin drive check (drives each pin, reads it back)");
    Serial.println("sensor  driven HIGH   driven LOW    verdict");
    Serial.println("-------------------------------------------------");

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        int pin = HALL_PINS[i];

        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        delay(2);
        int hi = analogRead(pin);

        digitalWrite(pin, LOW);
        delay(2);
        int lo = analogRead(pin);

        // Leave it back the way the sensors need it.
        pinMode(pin, INPUT);
        delay(2);

        const char* verdict = (hi > 3500 && lo < 500) ? "pin ok" : "PIN NOT RESPONDING";

        char line[80];
        snprintf(line, sizeof(line), "  #%d      %5d        %5d       %s",
                 i + 1, hi, lo, verdict);
        Serial.println(line);
    }

    Serial.println();
    Serial.println("All pins ok means every pad and ADC works, and a bad sensor");
    Serial.println("reading is the signal not reaching the pad.");
    Serial.println();
    // The hall pins were just driven, so give the dividers a moment to settle
    // back to their real voltages before anything measures them again.
    delay(50);
}

// The measurement itself, with no Unity calls in it. loop() calls this
// directly. It must NOT contain TEST_PASS()/TEST_ASSERT: those longjmp back
// into RUN_TEST's stack frame, which no longer exists once setup() has
// returned, and the jump lands on garbage. That is a hard fault (IACCVIOL,
// "executing from address 0x0") and the board reboots every cycle.
static void measureSourceImpedance() {
    analogReadResolution(HALL_ADC_BITS);
    analogReadAveraging(4);

    Serial.println();
    Serial.println("sensor  single  burst   slow    burst-single  verdict");
    Serial.println("------------------------------------------------------");

    int worst = 0;
    int worstIdx = 0;

    int singles[NUM_HALL_SENSORS];
    sweepSingle(singles);

    for (int i = 0; i < NUM_HALL_SENSORS; i++) {
        int single = singles[i];
        int burst  = readBurst(HALL_PINS[i]);
        int slow   = readSlow(HALL_PINS[i]);
        int gap    = burst - single;

        const char* verdict = (gap > 150) ? "HIGH IMPEDANCE" : "ok";
        if (gap > worst) { worst = gap; worstIdx = i; }

        char line[96];
        snprintf(line, sizeof(line), "  #%d    %5d   %5d   %5d   %8d      %s",
                 i + 1, single, burst, slow, gap, verdict);
        Serial.println(line);
    }

    Serial.println();
    Serial.print("largest gap: sensor #");
    Serial.print(worstIdx + 1);
    Serial.print("  (");
    Serial.print(worst);
    Serial.println(" counts)");
    Serial.println();
    Serial.println("If one sensor's gap is hundreds of counts while the rest are");
    Serial.println("near zero, that sensor has excess series resistance -- a bad");
    Serial.println("joint. If every gap is near zero, the fault is elsewhere.");
}

// Unity wrapper: safe to use TEST_* here, inside RUN_TEST.
void test_source_impedance() {
    measureSourceImpedance();
    TEST_PASS();
}

// The crash report is only readable once, at boot, and the serial monitor is
// almost never attached that early. So capture it into a buffer and reprint it
// on every cycle -- no race, nothing to scroll back for.
static String crashText;

// Counts boots within this power cycle. Plain statics, so this resets to 1 on
// every reboot -- it cannot tell you the board rebooted. The crash report and
// its timestamp are what prove that. (Earlier attempts used DMAMEM, which the
// startup code zeroes, and a .noinit section, which is not in Teensy's linker
// script and stopped the board booting at all.)
static uint32_t bootCount;
static uint32_t bootMagic;

static void printBanner() {
    Serial.println();
    Serial.println("==========================================================");
    Serial.print(  "  test_hall_z   built ");
    Serial.print(__DATE__);
    Serial.print(" ");
    Serial.println(__TIME__);
    Serial.print(  "  boots since power-up: ");
    Serial.println(bootCount);
    Serial.println("==========================================================");
    if (crashText.length()) {
        Serial.println("---- CRASH REPORT FROM LAST BOOT ----");
        Serial.print(crashText);
        Serial.println("-------------------------------------");
    } else {
        Serial.println("(no crash report stored)");
    }
}

void setup() {
    // bootMagic is uninitialised on a cold power-up, so seed the counter then.
    if (bootMagic != 0xB007C0DE) { bootMagic = 0xB007C0DE; bootCount = 0; }
    bootCount++;

    if (CrashReport) {
        crashText = "";
        // CrashReport prints to any Print target; capture it into the string.
        struct Capture : public Print {
            String* out;
            size_t write(uint8_t c) override { *out += (char)c; return 1; }
        } cap;
        cap.out = &crashText;
        CrashReport.printTo(cap);
    }

    Serial.begin(115200);
    delay(1500);   // give the USB serial monitor time to attach

    printBanner();

    UNITY_BEGIN();
    RUN_TEST(test_source_impedance);
    UNITY_END();
}

// Reprint every few seconds so the result is still there when a serial monitor
// is attached after the upload finishes.
void loop() {
    delay(3000);
    printBanner();
    pinDriveCheck();
    measureSourceImpedance();
}
