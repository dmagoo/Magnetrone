// test_stepper_wiring -- TMC2209 "hello world" wiring check.
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_stepper_wiring -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_stepper_wiring
//
// WHAT IT DOES:
//   Checks the four wires to the driver, one at a time, slowly, so that anything
//   that misbehaves is a wiring fault rather than a tuning problem. This is
//   deliberately NOT a motion test -- see test_stepper for speed and ramping.
//
//     UART   -- reads the chip version, expects 0x21
//     ENABLE -- drops the driver so the shaft goes loose, then re-engages it
//     STEP   -- exactly one revolution, slowly
//     DIR    -- exactly one revolution back the other way
//
//   Put a piece of tape on the shaft. Each cycle should return it to the same
//   place. If it drifts, steps are being lost.
//
//   Everything runs well under the stall speed on purpose, so the motor should
//   never struggle. If it does, that is a wiring or power problem.
//
// WIRING (per pins.h):
//   STEP -> 3, DIR -> 2, EN -> 5 (active low).
//   UART: the TMC2209 has ONE UART pin (PDN_UART). Both Teensy lines land on the
//   driver's "RX" pad -- TX (35) through a 1k resistor, RX (34) direct. Serial8.
//   MS1 and MS2 tied to GND -> UART address 0. VM = 24 V, VIO = 3.3 V.
//   Bulk capacitance at the driver is required, or it browns out and resets to
//   factory defaults the moment the motor spins (see docs/motor_debug.md).
//
//   NOTE: this uses PIN_DIR (GPIO 2), the real direction pin on the assembled
//   PCB. test_stepper uses PIN_DIR_TMP (GPIO 6) instead, which on this board is
//   menu_encoder_a, so direction cannot be verified there.
//
// POWER:
//   The board's +5V feeds the Teensy's VIN pad. Do not run USB and the 24 V
//   supply together unless the VUSB-VIN trace under the Teensy has been cut.

#include <Arduino.h>
#include <unity.h>
#include <TMCStepper.h>
#include "pins.h"

// --- TMC2209 hardware constants (match test_stepper) ---
#define R_SENSE          0.11f   // BigTreeTech TMC2209 sense resistor
#define DRIVER_ADDRESS   0b00    // MS1 + MS2 both tied low -> UART address 0
#define RUN_CURRENT_MA   900     // RMS. 1500 over-temped on a bare heatsink.
#define MICROSTEPS       8       // native; interpolation smooths to 256 regardless

// GCONF written as one raw word -- the per-bit setters read-modify-write through
// the library's shadow copy and clobber each other.
#define GCONF_STEALTHCHOP  0x1C0

// --- Motion, deliberately gentle ---
constexpr int      FULL_STEPS_PER_REV = 200;
constexpr int      STEPS_PER_REV      = FULL_STEPS_PER_REV * MICROSTEPS;  // 1600
constexpr uint32_t STEP_PERIOD_US     = 1000;   // 1000 steps/s -> 1.6 s per revolution
constexpr uint32_t FREE_SPIN_MS       = 4000;   // how long the shaft is left loose
constexpr uint32_t PAUSE_MS           = 1500;   // between phases

static TMC2209Stepper driver(&Serial8, R_SENSE, DRIVER_ADDRESS);

// Single-wire UART writes are unacknowledged and can silently drop, so the
// critical ones are written and read back.
static bool setGconfVerified(uint32_t want) {
    for (int attempt = 1; attempt <= 8; attempt++) {
        driver.GCONF(want);
        if (driver.GCONF() == want) return true;
    }
    return false;
}

static bool setMicrostepsVerified(int ms) {
    for (int attempt = 1; attempt <= 5; attempt++) {
        driver.microsteps(ms);
        if ((int)driver.microsteps() == ms) return true;
    }
    return false;
}

// Blocking step generator. Fine here because nothing else needs to run.
static void stepN(uint32_t count, uint32_t periodUs) {
    for (uint32_t i = 0; i < count; i++) {
        digitalWrite(PIN_STEP, HIGH);
        delayMicroseconds(periodUs / 2);
        digitalWrite(PIN_STEP, LOW);
        delayMicroseconds(periodUs / 2);
    }
}

// --- The one Unity assertion: can we talk to the chip at all? -------------
void test_stepper_uart() {
    uint8_t ver = driver.version();
    char msg[72];
    snprintf(msg, sizeof(msg),
             "TMC2209 UART: expected 0x21, got 0x%02X -- check Serial8 wiring", ver);
    TEST_ASSERT_EQUAL_MESSAGE(0x21, ver, msg);
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // wait for the monitor to attach

    pinMode(PIN_STEP,   OUTPUT);
    pinMode(PIN_DIR,    OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWrite(PIN_ENABLE, LOW);    // active low: LOW = driver enabled
    digitalWrite(PIN_STEP,   LOW);
    digitalWrite(PIN_DIR,    HIGH);

    Serial8.begin(115200);
    driver.begin();

    bool gconfOk = setGconfVerified(GCONF_STEALTHCHOP);
    driver.toff(5);                   // enable the chopper (required in any mode)
    driver.rms_current(RUN_CURRENT_MA);
    bool msOk = setMicrostepsVerified(MICROSTEPS);
    driver.intpol(true);              // interpolate to 256 microsteps: smooth + quiet
    driver.pwm_autoscale(true);       // required for StealthChop current regulation

    // Raw reads, taken at rest -- the library's getters echo its shadow copy,
    // and reads taken during motion are corrupted by motor noise.
    Serial.println();
    Serial.println("=== TMC2209 wiring check ===");
    Serial.print("  version   = 0x"); Serial.print(driver.version(), HEX);
    Serial.println(" (expect 0x21)");
    Serial.print("  GCONF     = 0x"); Serial.print(driver.GCONF(), HEX);
    Serial.println(gconfOk ? "  (verified)" : "  (WRITE FAILED)");
    Serial.print("  current   = ");   Serial.print(driver.rms_current());
    Serial.println(" mA RMS");
    Serial.print("  microstep = ");   Serial.print(driver.microsteps());
    Serial.println(msOk ? "  (verified)" : "  (WRITE FAILED)");
    Serial.println();

    UNITY_BEGIN();
    RUN_TEST(test_stepper_uart);
    UNITY_END();

    Serial.println();
    Serial.println("Put tape on the shaft. Each cycle should return it to the same spot.");
    Serial.println();
}

void loop() {
    // --- ENABLE ---
    Serial.println("EN high  -> driver released. Shaft should turn freely by hand.");
    digitalWrite(PIN_ENABLE, HIGH);
    delay(FREE_SPIN_MS);

    Serial.println("EN low   -> driver engaged. Shaft should now resist by hand.");
    digitalWrite(PIN_ENABLE, LOW);
    delay(PAUSE_MS);

    // --- STEP, forward ---
    Serial.println("DIR high -> one revolution forward.");
    digitalWrite(PIN_DIR, HIGH);
    delay(10);                  // let DIR settle before the first step edge
    stepN(STEPS_PER_REV, STEP_PERIOD_US);
    delay(PAUSE_MS);

    // --- DIR, reverse ---
    Serial.println("DIR low  -> one revolution back. Tape should return to start.");
    digitalWrite(PIN_DIR, LOW);
    delay(10);
    stepN(STEPS_PER_REV, STEP_PERIOD_US);

    Serial.println("--- cycle complete ---");
    Serial.println();
    delay(PAUSE_MS * 2);
}
