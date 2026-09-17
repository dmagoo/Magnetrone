#pragma once
#include <Arduino.h>
#include <IntervalTimer.h>
#include <TMCStepper.h>

// =============================================================================
// Platter -- RPM-aware controller for the turntable platter (TMC2209 + NEMA17).
//
// The caller works entirely in platter RPM (signed: + / - sets direction). This
// class hides step periods, microstepping, acceleration, the TMC2209 UART
// bring-up, and the soft-start / idle / resume behaviour. It is open-loop and
// slew-limited: the newest target supersedes the previous one, so twiddling a
// knob just re-aims at the latest value.
//
// NAMING: the public surface is platter-referenced (RPM, stepsPerPlatterRev),
// so the class is "Platter", not "Motor" -- the motor/driver is an internal
// detail. If the raw TMC2209 layer is ever split out, THAT lower object would
// be the "StepperDriver"; this stays the platter.
//
// SINGLE INSTANCE: stepping is serviced by an IntervalTimer whose ISR must be a
// free/static function, so this class keeps a static self-pointer. One Platter
// at a time. (Documented limitation, fine for this rig.)
//
// USAGE
//   Platter platter(Platter::defaultConfig());
//   platter.begin();                 // TMC2209 bring-up, leaves at rest
//   ...
//   void loop() {
//       platter.update();            // MUST be called every loop: services ramp
//       ...
//       platter.setRPM(45.0f);       // aim; spins up from rest automatically
//       platter.adjustRPM(+rpmStep); // encoder detent
//       platter.stop();              // knob press: latch speed, ramp to rest
//       platter.resume();            // knob press again: back to latched speed
//   }
//
// -----------------------------------------------------------------------------
// UNCERTAINTIES / GUESSES (review these) -- WHAT and WHY:
//
//  * microsteps/stepsPerPlatterRev default = 8 / 17,600, NOT config.h's 16 /
//    35,200. WHY: config.h still describes the retired A4988; the verified
//    TMC2209 reference (test_stepper) settled on 8 microsteps. I did not edit
//    config.h (out of scope / no go). The caller passes the real value in.
//
//  * rpmStep default = 1.0 RPM. WHY: no detent-size constant exists in config.h.
//    A guess; probably belongs in config.h later.
//
//  * pullInRPM default = 2.5 RPM. WHY: derived from the reference's 1500 us
//    pull-in period at 8 microsteps (~2.3 RPM at the platter). It is below
//    minRPM on purpose -- pull-in is an internal sub-minimum start speed.
//
//  * accel = 6000 steps/s^2. WHY: matches the reference ramp (120 steps/s per
//    20 ms). Even, jerk-free accel in frequency space.
//
//  * Reversal while spinning ramps DOWN THROUGH ZERO, flips DIR at rest, then
//    does a fresh pull-in before ramping up. WHY: DIR must only change at rest
//    (motor noise + missed steps otherwise). Costs a moment on reverse; safe.
//
//  * stop() leaves the driver ENABLED (holding torque) at rest. WHY: a platter
//    should hold position, not freewheel. (Freewheel is a deferred item.)
//
//  * maxRPM clamp = config.h MAX_RPM (120). WHY: the 20 us hardware step cap
//    allows ~170 RPM, but config.h limits to 120 and the doc says top speed is
//    mechanically chaotic anyway. Clamp to the conservative config value.
// =============================================================================

class Platter {
public:
    // All tunables in one struct: cleaner than a 10-argument constructor, and
    // every field is something that has a config.h default and/or an EEPROM
    // value, so the caller fills it from those two sources.
    struct Config {
        float    startRPM;            // boot/first-run speed (EEPROM rpm or DEFAULT_RPM)
        float    minRPM;              // commanded-speed floor
        float    maxRPM;              // commanded-speed ceiling (soft cap)
        float    rpmStep;             // detent size for adjustRPM nudges
        uint32_t stepsPerPlatterRev;  // CALIBRATABLE: usteps for one platter rev
        float    accelStepsPerSec2;   // ramp rate, frequency space (even accel)
        float    pullInRPM;           // gentle pull-in speed from a standstill
        uint16_t startDwellMs;        // hold at pull-in so the rotor locks before ramping
        uint16_t runCurrentMa;        // TMC2209 RMS current
        uint8_t  microsteps;          // native microstep count (interpolated to 256)
    };

    // Defaults sourced from config.h where a constant exists; see UNCERTAINTIES
    // for the fields that deviate (microsteps, stepsPerPlatterRev) or are guesses.
    static Config defaultConfig();

    explicit Platter(const Config& cfg);

    // --- lifecycle ---
    void begin();   // TMC2209 UART bring-up (mirrors the verified reference); at rest
    void update();  // call EVERY loop iteration: advances the non-blocking ramp

    // --- speed control (signed RPM: sign = direction) ---
    void  setRPM(float rpm);      // aim at target; spins up from rest if needed
    void  adjustRPM(float delta); // signed nudge by delta, clamped
    float commandedRPM() const;   // where it is heading (signed)
    float currentRPM() const;     // where the ramp is right now (signed)

    // --- stop / resume (the knob-press toggle) ---
    void stop();                  // latch current speed, ramp to rest, hold
    void resume();                // ramp back to the latched speed
    bool isStopped() const;       // explicit state (RPM 0 is NOT this)

    // --- direction ---
    void reverse();               // flip sign of the target (ramps through zero)
    bool isReversed() const;

    // --- enable (coil energise) ---
    void enable();
    void disable();
    bool isEnabled() const;

    // --- calibration support (the routine lives OUTSIDE; these are its helpers) ---
    // Primitive speed rung beneath setRPM: commands raw step rate, bypassing the
    // RPM<->steps math (which depends on the very number calibration is finding).
    void setStepRate(float stepsPerSec);

    void     setStepsPerPlatterRev(uint32_t steps); // apply a calibrated value (also boot-load)
    uint32_t stepsPerPlatterRev() const;

    // Measurement: caller drives the speed sweep and reports each confirmed
    // magnet hit (one hit = one platter revolution). The Platter owns the step
    // count, so it does the counting; calibration never sees stepper internals.
    void     beginCalibration();
    void     recordCalibrationEvent();
    uint32_t endCalibration();      // returns measured stepsPerPlatterRev; does NOT apply it

private:
    // TMC2209 hardware constants (BigTreeTech V1.3, MS1/MS2 low -> UART addr 0).
    static constexpr float    kRSense          = 0.11f;
    static constexpr uint8_t  kDriverAddress   = 0b00;
    // GCONF written as ONE raw verified word: I_scale_analog=0, StealthChop,
    // pdn_disable=1, mstep_reg_select=1, multistep_filt=1. (See reference.)
    static constexpr uint32_t kGconfStealthChop = 0x1C0;

    // ISR plumbing (single-instance).
    static Platter*  instance_;
    static void      stepISR();

    // verified UART writes (single-wire writes can silently drop).
    int  setGconfVerified(uint32_t want);
    int  setMicrostepsVerified(int ms);

    // internals
    float rpmToStepRate(float rpm) const;   // |steps/s| from |RPM|
    float stepRateToRpm(float rate) const;
    void  applyStepRate(float stepsPerSec); // (re)program the step timer
    void  applyDirection();                 // write DIR pin (only safe at rest)
    void  beginFromRest();                  // pull-in + dwell, then ramp to target

    enum class Phase : uint8_t { Idle, PullInDwell, Running };

    const Config     cfg_;
    uint32_t         stepsPerRev_;          // runtime-mutable (calibration writes it); seeded from cfg_
    TMC2209Stepper   driver_;
    IntervalTimer    stepTimer_;

    volatile bool     stepPinState_ = false;
    volatile uint32_t stepCount_    = 0;    // step pulses emitted (for calibration)

    Phase    phase_           = Phase::Idle;
    bool     timerRunning_    = false;
    bool     enabled_         = false;
    bool     stopped_         = false;
    bool     reversed_        = false;      // direction currently applied to DIR
    bool     pendingReversed_ = false;      // direction requested, applied at rest

    float    currentStepRate_ = 0.0f;       // |steps/s| the ramp is at now
    float    targetStepRate_  = 0.0f;       // |steps/s| the ramp is heading to
    float    resumeRPM_       = 0.0f;       // signed speed latched at stop()

    uint32_t dwellStartMs_    = 0;
    uint32_t lastUpdateUs_    = 0;

    // calibration accumulators
    bool     calHasBaseline_  = false;
    uint32_t calLastCount_    = 0;
    uint32_t calSumSteps_     = 0;
    uint16_t calIntervals_    = 0;
};
