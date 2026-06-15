// test_stepper -- TMC2209 (UART) bring-up + smooth motion reference.
//
// This file is intentionally a CLEAN REFERENCE for the eventual OO motor wrapper.
// The multi-session debugging scaffolding has been removed; what remains is the
// known-good driver bring-up and the smooth-motion pattern the wrapper should mirror.
//
// HOW TO RUN:
//   VS Code: PlatformIO sidebar -> teensy41_test_stepper -> Advanced -> Test
//   Terminal: pio test -e teensy41_test_stepper
//
// HARD-WON LESSONS baked into this file (see docs/motor_debug.md for the full story):
//   1. The driver browns out and RESETS to factory defaults when the motor spins
//      unless there is enough bulk capacitance at the driver (VM/VIO). With the cap
//      in place the config survives motion. If config mysteriously reverts, suspect
//      the cap first.
//   2. The TMCStepper getter functions echo the library's SHADOW, not the chip. Only
//      RAW reads (driver.GCONF()/CHOPCONF()) tell the truth, and only AT REST (reads
//      taken during/after motion are corrupted by motor noise).
//   3. Write GCONF as ONE raw word (setGconfVerified), never via the per-bit setters:
//      they each do read-modify-write through the shadow and clobber each other.
//   4. Single-wire UART writes are unacknowledged and can silently drop -> verify
//      critical writes by reading back (setGconfVerified / setMicrostepsVerified).
//   5. Configure ONLY at rest; never talk to the driver over UART mid-motion.
//   6. Accelerate in step-RATE space, not period space, for even (jerk-free) accel.
//
// WIRING (per pins.h):
//   - BigTreeTech TMC2209 V1.3: STEP -> PIN_STEP (3), DIR -> PIN_DIR_TMP (6),
//     EN -> PIN_ENABLE (5, active low).
//   - UART: the TMC2209 has ONE UART pin (PDN_UART). Land BOTH Teensy lines on the
//     driver's "RX" pad: Teensy TX (35) through a 1k resistor, Teensy RX (34) direct.
//     (The "TX" pad did not work on this board.) Pins 34/35 are Serial8. Common GND.
//   - MS1, MS2 tied to GND -> UART address 0. VM = 24V, VIO = 3.3V, bulk cap at driver.
//   - NOTE: PIN_DIR_TMP (6) is a temporary rewire for the burnt PIN_DIR (2).

#include <Arduino.h>
#include <unity.h>
#include <TMCStepper.h>
#include "pins.h"

// --- TMC2209 hardware constants ---
#define R_SENSE          0.11f   // BigTreeTech TMC2209 sense resistor
#define DRIVER_ADDRESS   0b00    // MS1 + MS2 both tied low -> UART address 0
#define RUN_CURRENT_MA   900     // RMS. 1500 over-temped on a bare heatsink; 900 runs cool.
#define MICROSTEPS       8       // native; interpolation smooths to 256 regardless

// GCONF (register 0x00) is written as one raw word. Bits we set:
//   bit0 I_scale_analog = 0  -> coil current from UART rms_current(), NOT the VREF pot
//   bit2 en_spreadcycle      -> 0 = StealthChop (quiet), 1 = SpreadCycle (more torque)
//   bit6 pdn_disable    = 1  -> required for UART
//   bit7 mstep_reg_select=1  -> microsteps come from the UART register, not the MS pins
//   bit8 multistep_filt = 1  -> step pulse filtering
#define GCONF_STEALTHCHOP  0x1C0   // quiet; what this device uses
#define GCONF_SPREADCYCLE  0x1C4   // louder, more high-speed torque (not needed here)

static TMC2209Stepper driver(&Serial8, R_SENSE, DRIVER_ADDRESS);

// Step pulse period in microseconds (smaller = faster). STEP_DELAY_FAST_US is the
// SOFT SPEED CAP: keep it above the stall period with margin. STEP_DELAY_SLOW_US is
// the gentle start/idle speed.
#define STEP_DELAY_SLOW_US   500
#define STEP_DELAY_FAST_US    20   // soft cap (top speed); tune to stay below the stall

// Soft start: a stepper can't reliably catch a fast first step from a dead stop
// (intermittent startup stall), and StealthChop's current regulation needs a moment
// to settle. So begin at a very slow pull-in speed and hold briefly before ramping.
#define STEP_DELAY_START_US  1500  // gentle pull-in speed from standstill
#define START_DWELL_MS       300   // hold at pull-in speed to let the rotor lock in

static int currentDelayUs = STEP_DELAY_SLOW_US;   // current step period the ramp is at

#define RUNTAG "test42"
static void tlog(const char* msg) { Serial.print(RUNTAG); Serial.print(" "); Serial.println(msg); }
static void indent()              { Serial.print("      "); }

// ---- verified writes (single-wire UART writes can silently drop) ----------------
// Force the whole GCONF in one raw write, then confirm by reading it back. Returns
// attempts taken (0 = never stuck). Use this, not the per-bit GCONF setters.
static int setGconfVerified(uint32_t want) {
    for (int attempt = 1; attempt <= 8; attempt++) {
        driver.GCONF(want);
        if (driver.GCONF() == want) return attempt;
    }
    return 0;
}
// Same idea for the microstep (MRES) field in CHOPCONF.
static int setMicrostepsVerified(int ms) {
    for (int attempt = 1; attempt <= 5; attempt++) {
        driver.microsteps(ms);
        if ((int)driver.microsteps() == ms) return attempt;
    }
    return 0;
}

// One-time RAW readback to confirm settings actually reached the chip. Call only at
// rest. (TMCStepper getters echo the shadow; the raw GCONF()/CHOPCONF() reads here
// are the trustworthy ones.)
static void printConfig() {
    uint32_t gconf = driver.GCONF();
    tlog("--- config readback (raw, at rest) ---");
    indent(); Serial.print("version=0x");       Serial.println(driver.version(), HEX);  // expect 0x21
    indent(); Serial.print("GCONF=0x");          Serial.println(gconf, HEX);
    indent(); Serial.print("  I_scale_analog="); Serial.print((gconf >> 0) & 1);  // want 0 (UART current)
    Serial.print(" spreadcycle=");               Serial.print((gconf >> 2) & 1);  // 0=StealthChop
    Serial.print(" pdn_disable=");               Serial.print((gconf >> 6) & 1);  // want 1
    Serial.print(" mstep_reg_sel=");             Serial.println((gconf >> 7) & 1); // want 1
    indent(); Serial.print("CHOPCONF=0x");       Serial.println(driver.CHOPCONF(), HEX);
    indent(); Serial.print("Iset(mA)=");         Serial.println(driver.rms_current());
    indent(); Serial.print("microsteps=");       Serial.println(driver.microsteps());
    indent(); Serial.print("TPWMTHRS=");         Serial.print(driver.TPWMTHRS());
    Serial.print(" TCOOLTHRS=");                 Serial.print(driver.TCOOLTHRS());
    Serial.print(" SGTHRS=");                    Serial.println(driver.SGTHRS());
}

// ---- motion ---------------------------------------------------------------------
// Generate step pulses at a fixed period for a duration. This is the raw stepping
// primitive; the wrapper will likely drive STEP from a timer/interrupt instead so it
// never has to block. Returns the pulse count produced.
static uint32_t stepForMs(uint32_t durationMs, int delayUs) {
    uint32_t start = millis();
    uint32_t steps = 0;
    while (millis() - start < durationMs) {
        digitalWrite(PIN_STEP, HIGH);
        delayMicroseconds(delayUs / 2);
        digitalWrite(PIN_STEP, LOW);
        delayMicroseconds(delayUs / 2);
        steps++;
    }
    return steps;
}

// Accelerate in step-RATE (frequency) space, not period space, so the speed changes
// by an equal amount each tick -> constant, even acceleration (no worse-at-high-speed
// lurch). RAMP_FREQ_STEP_SPS per RAMP_DWELL_MS sets the accel rate; lower = gentler.
// This is the "ease into a speed" behavior the wrapper's slew limiter will implement.
#define RAMP_DWELL_MS        20    // ms held at each step-rate increment
#define RAMP_FREQ_STEP_SPS   120   // steps/sec change per increment (~6000 steps/s^2)

static void rampTo(int targetUs) {
    long f   = 1000000L / currentDelayUs;    // current step rate (steps/s)
    long ft  = 1000000L / targetUs;          // target step rate
    int  dir = (ft > f) ? 1 : -1;
    while ((dir > 0 && f < ft) || (dir < 0 && f > ft)) {
        f += dir * RAMP_FREQ_STEP_SPS;
        if ((dir > 0 && f > ft) || (dir < 0 && f < ft)) f = ft;   // don't overshoot
        currentDelayUs = 1000000L / f;
        stepForMs(RAMP_DWELL_MS, currentDelayUs);
    }
    currentDelayUs = targetUs;
}

// Representative run: one continuous accel from slow to the soft-cap speed, hold,
// decel. NO UART reads or prints during motion, so stepping never pauses (pausing to
// print is what caused the print-synced hitches). This is the motion profile the
// wrapper should reproduce when easing to a target speed.
// One direction: soft start, ramp to soft cap, hold, ramp back down to slow.
static void rampUpHoldDown() {
    currentDelayUs = STEP_DELAY_START_US;           // gentle pull-in speed
    stepForMs(START_DWELL_MS, STEP_DELAY_START_US); // hold so the rotor locks in before accel
    rampTo(STEP_DELAY_FAST_US);                     // smooth continuous accel to the soft cap
    stepForMs(3000, STEP_DELAY_FAST_US);            // hold top speed 3s
    rampTo(STEP_DELAY_SLOW_US);                     // smooth continuous decel
}

static void smoothRun() {
    tlog("smooth run: forward, then reverse (no mid-motion UART/printing)");

    digitalWrite(PIN_DIR_TMP, HIGH);   // forward
    rampUpHoldDown();

    delay(300);                        // let momentum settle before reversing
    digitalWrite(PIN_DIR_TMP, LOW);    // reverse
    rampUpHoldDown();

    tlog("--- run complete ---");
    delay(2000);
}

// ---- UART sanity test (the only UNITY assertion) --------------------------------
void test_stepper() {
    uint8_t ver = driver.version();
    char msg[64];
    snprintf(msg, sizeof(msg), "TMC2209 UART: expected 0x21, got 0x%02X", ver);
    TEST_ASSERT_EQUAL_MESSAGE(0x21, ver, msg);
}

void setup() {
    Serial.begin(115200);
    delay(2000);  // wait for monitor to attach

    pinMode(PIN_STEP,    OUTPUT);
    pinMode(PIN_DIR_TMP, OUTPUT);
    pinMode(PIN_ENABLE,  OUTPUT);
    digitalWrite(PIN_ENABLE, LOW);   // active low: LOW = driver enabled
    digitalWrite(PIN_STEP,   LOW);
    digitalWrite(PIN_DIR_TMP, HIGH);

    // ---- driver bring-up (the wrapper's init() should mirror this order) ----
    Serial8.begin(115200);
    driver.begin();
    int gconfTries = setGconfVerified(GCONF_STEALTHCHOP);  // StealthChop + UART current/microsteps
    driver.toff(5);                  // enable the chopper (required in any mode)
    driver.tbl(2);                   // chopper blank time (only matters in SpreadCycle)
    driver.hstrt(4);                 // SpreadCycle hysteresis (unused in StealthChop, harmless)
    driver.hend(0);                  //   "
    driver.rms_current(RUN_CURRENT_MA);
    setMicrostepsVerified(MICROSTEPS);
    driver.intpol(true);             // interpolate to 256 microsteps: smooth + quiet
    driver.pwm_autoscale(true);      // required for StealthChop current regulation
    driver.TPWMTHRS(0);              // no StealthChop<->SpreadCycle hybrid (pure StealthChop)
    driver.TCOOLTHRS(0);             // CoolStep off
    driver.SGTHRS(0);                // StallGuard off

    printConfig();
    indent(); Serial.print("GCONF write stuck after tries="); Serial.println(gconfTries); // 0 = failed

    UNITY_BEGIN();
    RUN_TEST(test_stepper);
    UNITY_END();
}

void loop() {
    smoothRun();
}
