#include "platter.h"
#include "pins.h"
#include "config.h"

// NOTE: uses PIN_DIR (the real board's DIR pin), NOT PIN_DIR_TMP. The temporary
// pin only exists for the bring-up Teensy whose PIN_DIR was burnt out; the app
// targets the correct pin.

Platter* Platter::instance_ = nullptr;

// -----------------------------------------------------------------------------
// Construction / defaults
// -----------------------------------------------------------------------------
Platter::Config Platter::defaultConfig() {
    Config c;
    c.startRPM           = DEFAULT_RPM;                 // 45
    c.minRPM             = MIN_RPM;                     // 10
    c.maxRPM             = MAX_RPM;                     // 120 (see UNCERTAINTIES)
    c.rpmStep            = 1.0f;                        // guess; no config constant
    c.microsteps         = 8;                           // TMC2209 reference, NOT config's 16
    c.stepsPerPlatterRev = (uint32_t)MOTOR_STEPS_PER_REV * 8u * GEAR_RATIO; // 17,600
    c.accelStepsPerSec2  = 6000.0f;                     // matches reference ramp
    c.pullInRPM          = 2.5f;                        // ~reference 1500 us pull-in
    c.startDwellMs       = 300;                         // reference dwell
    c.runCurrentMa       = 900;                         // reference RMS
    return c;
}

Platter::Platter(const Config& cfg)
    : cfg_(cfg),
      stepsPerRev_(cfg.stepsPerPlatterRev),
      driver_(&Serial8, kRSense, kDriverAddress) {
    instance_ = this;
}

// -----------------------------------------------------------------------------
// ISR: toggle STEP. Two toggles = one full step pulse; count on the rising edge.
// -----------------------------------------------------------------------------
void Platter::stepISR() {
    if (!instance_) return;
    // A positioned move holds on its target: no new pulse once it is reached
    // (the pin is low, so the last pulse is complete). update() then stops.
    if (instance_->moveActive_ && !instance_->stepPinState_ &&
        instance_->position_ == instance_->moveTarget_) return;
    instance_->stepPinState_ = !instance_->stepPinState_;
    digitalWriteFast(PIN_STEP, instance_->stepPinState_);
    if (instance_->stepPinState_) {
        instance_->stepCount_++;
        // DIR only changes at rest (applyDirection), so reversed_ is stable
        // for as long as pulses are going out.
        instance_->position_ += instance_->reversed_ ? -1 : 1;
    }
}

// -----------------------------------------------------------------------------
// Verified UART writes -- single-wire writes are unacknowledged and can drop.
// -----------------------------------------------------------------------------
int Platter::setGconfVerified(uint32_t want) {
    for (int attempt = 1; attempt <= 8; attempt++) {
        driver_.GCONF(want);
        if (driver_.GCONF() == want) return attempt;
    }
    return 0;
}

int Platter::setMicrostepsVerified(int ms) {
    for (int attempt = 1; attempt <= 5; attempt++) {
        driver_.microsteps(ms);
        if ((int)driver_.microsteps() == ms) return attempt;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Bring-up -- mirrors the verified test_stepper order, at rest only.
// -----------------------------------------------------------------------------
void Platter::begin() {
    pinMode(PIN_STEP,   OUTPUT);
    pinMode(PIN_DIR,    OUTPUT);
    pinMode(PIN_ENABLE, OUTPUT);
    digitalWriteFast(PIN_STEP, LOW);
    digitalWriteFast(PIN_DIR,  HIGH);          // forward
    digitalWriteFast(PIN_ENABLE, LOW);         // active low: LOW = enabled
    enabled_  = true;
    reversed_ = false;
    pendingReversed_ = false;

    Serial8.begin(115200);
    driver_.begin();
    setGconfVerified(kGconfStealthChop);       // StealthChop + UART current/microsteps
    driver_.toff(5);                           // enable the chopper
    driver_.tbl(2);                            // (SpreadCycle-only, harmless here)
    driver_.hstrt(4);
    driver_.hend(0);
    driver_.rms_current(cfg_.runCurrentMa);
    setMicrostepsVerified(cfg_.microsteps);
    driver_.intpol(true);                      // interpolate to 256: smooth + quiet
    driver_.pwm_autoscale(true);               // required for StealthChop
    driver_.TPWMTHRS(0);                        // pure StealthChop (no hybrid)
    driver_.TCOOLTHRS(0);                        // CoolStep off
    driver_.SGTHRS(0);                           // StallGuard off

    lastUpdateUs_ = micros();
    phase_ = Phase::Idle;
}

// -----------------------------------------------------------------------------
// Unit conversions
// -----------------------------------------------------------------------------
float Platter::rpmToStepRate(float rpm) const {
    return rpm / 60.0f * (float)stepsPerRev_;
}

float Platter::stepRateToRpm(float rate) const {
    return rate * 60.0f / (float)stepsPerRev_;
}

// -----------------------------------------------------------------------------
// Step-timer programming. The ISR toggles, so the timer period is HALF the step
// period. Below ~1 step/s we treat it as stopped and end the timer.
// -----------------------------------------------------------------------------
void Platter::applyStepRate(float stepsPerSec) {
    if (stepsPerSec <= 1.0f) {
        if (timerRunning_) { stepTimer_.end(); timerRunning_ = false; }
        return;
    }
    float halfPeriodUs = 1000000.0f / stepsPerSec / 2.0f;
    if (timerRunning_) {
        stepTimer_.update(halfPeriodUs);
    } else {
        stepTimer_.begin(stepISR, halfPeriodUs);
        timerRunning_ = true;
    }
}

// DIR is only safe to change at rest (motor noise / missed steps otherwise).
void Platter::applyDirection() {
    reversed_ = pendingReversed_;
    digitalWriteFast(PIN_DIR, reversed_ ? LOW : HIGH);
}

// Soft start from a standstill: apply direction, drop to the gentle pull-in
// speed, and enter the dwell so the rotor locks before the ramp takes over.
void Platter::beginFromRest() {
    applyDirection();
    currentStepRate_ = rpmToStepRate(cfg_.pullInRPM);
    applyStepRate(currentStepRate_);
    dwellStartMs_ = millis();
    phase_ = Phase::PullInDwell;
}

// After a new target: from a standstill, pull in; otherwise let the ramp in
// update() carry it (it handles a direction flip through zero). "Standstill"
// is judged by the rate, not the phase, so a Running phase that has not yet
// settled to Idle still gets a proper pull-in.
void Platter::startOrRetarget() {
    if (phase_ != Phase::PullInDwell && currentStepRate_ <= 1.0f) {
        beginFromRest();
    } else if (phase_ == Phase::Idle) {
        phase_ = Phase::Running;
    }
}

// -----------------------------------------------------------------------------
// Speed control
// -----------------------------------------------------------------------------
void Platter::setRPM(float rpm) {
    moveActive_      = false;
    pendingReversed_ = (rpm < 0.0f);
    float mag = constrain(fabsf(rpm), cfg_.minRPM, cfg_.maxRPM);
    targetStepRate_ = rpmToStepRate(mag);
    stopped_ = false;

    startOrRetarget();
}

void Platter::adjustRPM(float delta) {
    setRPM(commandedRPM() + delta);
}

float Platter::commandedRPM() const {
    float mag = stepRateToRpm(targetStepRate_);
    return pendingReversed_ ? -mag : mag;
}

float Platter::currentRPM() const {
    float mag = stepRateToRpm(currentStepRate_);
    return reversed_ ? -mag : mag;
}

// -----------------------------------------------------------------------------
// Stop / resume
// -----------------------------------------------------------------------------
void Platter::stop() {
    moveActive_     = false;
    resumeRPM_      = commandedRPM();   // latch where we were heading (signed)
    targetStepRate_ = 0.0f;
    stopped_        = true;
    // Let the ramp settle to 0, but only if there is something to settle: a
    // platter already at rest stays Idle, or the next start would find it
    // Running at rate 0 and never pull in.
    if (phase_ == Phase::Idle && currentStepRate_ > 1.0f) phase_ = Phase::Running;
}

void Platter::resume() {
    setRPM(resumeRPM_);
}

bool Platter::isStopped() const {
    return stopped_ && currentStepRate_ <= 1.0f;
}

// -----------------------------------------------------------------------------
// Direction
// -----------------------------------------------------------------------------
void Platter::reverse() {
    setRPM(-commandedRPM());
}

bool Platter::isReversed() const { return reversed_; }

// -----------------------------------------------------------------------------
// Enable
// -----------------------------------------------------------------------------
void Platter::enable() {
    digitalWriteFast(PIN_ENABLE, LOW);
    enabled_ = true;
}

void Platter::disable() {
    digitalWriteFast(PIN_ENABLE, HIGH);
    enabled_ = false;
}

bool Platter::isEnabled() const { return enabled_; }

// -----------------------------------------------------------------------------
// Calibration support
// -----------------------------------------------------------------------------
void Platter::setStepRate(float stepsPerSec) {
    // Raw rung for the calibration sweep: command step rate directly, still
    // going through the pull-in/ramp machinery so the start is gentle.
    moveActive_      = false;
    pendingReversed_ = (stepsPerSec < 0.0f);
    targetStepRate_  = fabsf(stepsPerSec);
    stopped_         = false;
    startOrRetarget();
}

void Platter::moveBy(int32_t steps, float maxRPM) {
    if (steps == 0) return;
    moveMaxRate_     = rpmToStepRate(fabsf(maxRPM));
    pendingReversed_ = (steps < 0);
    targetStepRate_  = moveMaxRate_;
    stopped_         = false;
    moveTarget_      = position_ + steps;
    moveActive_      = true;
    startOrRetarget();
}

uint8_t Platter::driverVersion() {
    return driver_.version();
}

void Platter::setStepsPerPlatterRev(uint32_t steps) {
    if (steps > 0) stepsPerRev_ = steps;
}

uint32_t Platter::stepsPerPlatterRev() const {
    return stepsPerRev_;
}

int32_t Platter::position() const {
    return position_;   // one 32-bit read: atomic on the Cortex-M7
}

void Platter::beginCalibration() {
    calHasBaseline_ = false;
    calSumSteps_    = 0;
    calIntervals_   = 0;
}

void Platter::recordCalibrationEvent() {
    uint32_t c = stepCount_;                 // snapshot the running pulse count
    if (!calHasBaseline_) {                  // first hit: just mark the start
        calLastCount_   = c;
        calHasBaseline_ = true;
        return;
    }
    calSumSteps_ += (c - calLastCount_);     // steps over exactly one revolution
    calLastCount_ = c;
    calIntervals_++;
}

uint32_t Platter::endCalibration() {
    if (calIntervals_ == 0) return stepsPerRev_;  // no data: unchanged
    return calSumSteps_ / calIntervals_;     // measured usteps/rev; caller decides to apply
}

// -----------------------------------------------------------------------------
// Ramp service -- the non-blocking slew limiter. Called every loop.
// Acceleration happens in step-RATE (frequency) space for even, jerk-free accel.
// -----------------------------------------------------------------------------
void Platter::update() {
    uint32_t nowUs = micros();
    float dt = (nowUs - lastUpdateUs_) * 1e-6f;   // wraps ~every 71 min; benign blip
    lastUpdateUs_ = nowUs;
    if (dt <= 0.0f) return;

    // Positioned move: aim at the fastest speed that can still stop in the
    // steps left (v = sqrt(2 a d)), but never below pull-in, which it can stop
    // from dead. Arrived: stop here, holding.
    if (moveActive_) {
        int32_t left = moveTarget_ - position_;
        if (reversed_) left = -left;
        if (left <= 0) {
            moveActive_      = false;
            targetStepRate_  = 0.0f;
            currentStepRate_ = 0.0f;
            applyStepRate(0.0f);
            stopped_         = true;
            phase_           = Phase::Idle;
            return;
        }
        float v = sqrtf(2.0f * cfg_.accelStepsPerSec2 * (float)left);
        targetStepRate_ = constrain(v, rpmToStepRate(cfg_.pullInRPM), moveMaxRate_);
    }

    switch (phase_) {
        case Phase::Idle:
            return;

        case Phase::PullInDwell:
            // hold pull-in speed until the rotor has locked in
            if (millis() - dwellStartMs_ >= cfg_.startDwellMs) {
                phase_ = Phase::Running;
            }
            return;

        case Phase::Running: {
            // While a direction flip is pending we must first reach rest, so the
            // effective target is 0 until DIR can be changed safely.
            bool  flipPending = (pendingReversed_ != reversed_);
            float effTarget   = flipPending ? 0.0f : targetStepRate_;

            float maxDelta = cfg_.accelStepsPerSec2 * dt;
            if (currentStepRate_ < effTarget) {
                currentStepRate_ = min(currentStepRate_ + maxDelta, effTarget);
            } else if (currentStepRate_ > effTarget) {
                currentStepRate_ = max(currentStepRate_ - maxDelta, effTarget);
            }
            applyStepRate(currentStepRate_);

            if (currentStepRate_ <= 1.0f) {
                currentStepRate_ = 0.0f;
                applyStepRate(0.0f);
                if (flipPending) {
                    applyDirection();                 // safe now, at rest
                    if (!stopped_ && targetStepRate_ > 1.0f) {
                        beginFromRest();               // fresh pull-in the other way
                    } else {
                        phase_ = Phase::Idle;
                    }
                } else {
                    phase_ = Phase::Idle;              // reached rest (stop or 0 target)
                }
            }
            return;
        }
    }
}
