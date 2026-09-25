#include "calibration.h"
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "motion/stepper.h"
#include "motion/bar.h"
#include "sensors/hall.h"
#include "config/storage.h"
#include "menu/menu.h"

// ---------------------------------------------------------------------------
// Local constants
// ---------------------------------------------------------------------------

// How long to sample sensors with the platter clear. We don't know revolutions
// at this point, so we use time: at CALIBRATION_RPM (24 RPM, 2.5 s per rev),
// 5 seconds covers two full revolutions, enough for a stable noise floor.
static const uint32_t BASELINE_SAMPLE_MS = 5000;

// How long to wait for the magnet before giving up. Detection needs three
// passes (up to three revolutions after the spin-up), about 9 s at 24 RPM.
static const uint32_t DETECT_TIMEOUT_MS = 15000;

// Backstop for waiting out the deceleration ramp at the end of calibration.
static const uint32_t STOP_TIMEOUT_MS = 6000;

// Threshold is set to this fraction of the measured peak deviation.
// 0.5 means the trigger fires at half the peak signal strength,
// giving a reliable trigger without being too sensitive to noise.
static const float THRESHOLD_FACTOR = 0.5f;

// A revolution this much shorter than expected means more than one magnet on
// the outer track (each pass looks like a revolution); this much longer, a
// pass was missed.
static const float MULTI_MAGNET_TOLERANCE = 1.3f;

// The calibration magnet goes on the start mark, on the outer track. Its
// larger circumference makes the placement, and so the bar start, more exact.
static const uint8_t OUTER_SENSOR = NUM_HALL_SENSORS - 1;

// Pins in order matching sensor index 0-7.
static const uint8_t HALL_PINS[NUM_HALL_SENSORS] = {
    PIN_HALL_1, PIN_HALL_2, PIN_HALL_3, PIN_HALL_4,
    PIN_HALL_5, PIN_HALL_6, PIN_HALL_7, PIN_HALL_8
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Read a raw 12-bit ADC value from a sensor pin.
// analogReadResolution(12) is set by hallInit() in setup().
static inline uint16_t rawRead(uint8_t sensorIndex) {
    return (uint16_t)analogRead(HALL_PINS[sensorIndex]);
}

// Absolute deviation of a reading from a baseline.
static inline uint16_t deviation(uint16_t reading, uint16_t baseline) {
    return reading > baseline ? reading - baseline : baseline - reading;
}

// ---------------------------------------------------------------------------
// Bring the platter to a stop and WAIT for it.
//
// stepperStop() only sets the target to zero -- the deceleration itself is
// serviced by stepperUpdate(), which main loop() calls. Calibration blocks, so
// without pumping it here the platter would keep turning through the trailing
// message delay and only slow down once calibration returned. Same reason the
// sampling loops call stepperUpdate().
// ---------------------------------------------------------------------------
static void stopAndSettle() {
    stepperStop();

    // Ramping CALIBRATION_RPM down takes about a second. The timeout is a
    // backstop so a wedged ramp cannot hang calibration forever.
    uint32_t deadline = millis() + STOP_TIMEOUT_MS;
    while (stepperRunning() && millis() < deadline) {
        stepperUpdate();
        delay(2);
    }
}

// ---------------------------------------------------------------------------
// Phase 1: Baseline
// ---------------------------------------------------------------------------
// With the platter clear and the motor running, sample every sensor
// repeatedly over a fixed time window. The average becomes the resting
// baseline. The motor runs so its noise is part of what is measured.
//
// Baselines are kept PER SENSOR. The eight rest levels span roughly 150 counts,
// and averaging them into one number leaves the outliers permanently further
// from their own rest value than HALL_REARM_LEVEL -- such a sensor can never
// re-arm, so it fires once and is silent from then on.

static uint16_t sampled[NUM_HALL_SENSORS];   // phase 1's result, for phase 2

void calibrationSampleBaselines() {
    menuMessage("Sampling...", "Keep it clear");
    stepperStart(CALIBRATION_RPM);

    uint32_t sum[NUM_HALL_SENSORS] = {};
    uint32_t count = 0;
    uint32_t start = millis();

    while (millis() - start < BASELINE_SAMPLE_MS) {
        stepperUpdate();   // this loop blocks main loop(); the ramp still needs servicing
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
            sum[i] += rawRead(i);
        }
        count++;
        delay(5);
    }

    if (count == 0) count = 1;
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
        sampled[i] = (uint16_t)(sum[i] / count);
    }

    stopAndSettle();
}

// ---------------------------------------------------------------------------
// Phase 2: Magnet detection
// ---------------------------------------------------------------------------
// One magnet sits on the start mark, on the outer track. Three passes of it
// over the outer sensor, each located by motor position rather than the clock:
//   1. First pass: the reference.
//   2. Second pass, one revolution later. The steps between them give the
//      belt ratio (and so the speed correction), exactly and independent of
//      the spin-up ramp. The peak seen so far sets the threshold and pole.
//   3. Third pass, at the final threshold, so the bar start is where the
//      mark's notes will actually fire: that position is the start.
// Each pass must first see the reading back near baseline (the same re-arm
// rule as hallUpdate()), so a magnet already over the sensor when the spin
// starts cannot be timed from the middle of its pass.
//
// The other sensors are watched too: a strong reading there means a magnet
// on another track, either instead of or as well as the outer one.

CalibrationStatus calibrationDetect(SavedConfig& cfg) {
    menuMessage("Searching...", "");
    stepperStart(CALIBRATION_RPM);

    const uint16_t* baselines = sampled;
    const uint32_t  nominal   = stepperStepsPerRev();

    uint8_t  pass       = 0;       // passes seen so far
    bool     armed      = false;   // outer reading has been back near baseline
    int32_t  firstPos   = 0;
    int32_t  startPos   = 0;
    uint16_t peak       = 0;       // outer sensor's highest deviation
    int8_t   peakSign   = 1;       // which way that peak pointed
    uint16_t otherPeak  = 0;       // highest deviation on any other sensor
    uint16_t threshold  = HALL_THRESHOLD_DEFAULT;
    float    correction = 1.0f;
    CalibrationStatus status = CalibrationStatus::TimeoutNoMagnet;

    uint32_t deadline = millis() + DETECT_TIMEOUT_MS;

    // No delay in this loop: the position is read when the trigger is seen,
    // so polling slower would blur the bar start.
    while (pass < 3 && status == CalibrationStatus::TimeoutNoMagnet &&
           millis() < deadline) {
        stepperUpdate();   // this loop blocks main loop(); the ramp still needs servicing
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
            uint16_t val = rawRead(i);
            uint16_t dev = deviation(val, baselines[i]);

            if (i != OUTER_SENSOR) {
                if (dev > otherPeak) otherPeak = dev;
                continue;
            }

            // The peak's SIGN says which pole is facing the sensors. The face
            // field is far stronger than the magnet's opposite-signed fringe
            // lobes, so the largest excursion is reliably the real one.
            if (pass < 2 && dev > peak) {
                peak     = dev;
                peakSign = (val >= baselines[i]) ? 1 : -1;
            }

            if (!armed) {
                if (dev < HALL_REARM_LEVEL) armed = true;
                continue;
            }
            if (dev < threshold) continue;

            int32_t pos = stepperPosition();
            armed = false;
            pass++;

            if (pass == 1) {
                firstPos = pos;
            } else if (pass == 2) {
                // Motor steps per platter revolution, measured, against the
                // nominal count from the tooth ratio.
                int32_t steps = pos - firstPos;
                correction = (steps > 0) ? (float)nominal / (float)steps : 0.0f;
                if (correction > MULTI_MAGNET_TOLERANCE ||
                    otherPeak >= (uint16_t)(peak * THRESHOLD_FACTOR)) {
                    status = CalibrationStatus::MultipleMagnets;
                } else if (correction < 1.0f / MULTI_MAGNET_TOLERANCE) {
                    pass = 3;   // a pass was missed; report no magnet
                } else {
                    threshold = (uint16_t)(peak * THRESHOLD_FACTOR);
                }
            } else {
                startPos = pos;
                status   = CalibrationStatus::Success;
            }
        }
    }

    // Nothing on the outer track, but something elsewhere: the magnet is on
    // the wrong track.
    if (pass == 0 && otherPeak >= HALL_THRESHOLD_DEFAULT) {
        status = CalibrationStatus::WrongTrack;
    }

    stopAndSettle();

    if (status != CalibrationStatus::Success) {
        // Show a brief error. The caller (menu) decides what to do next.
        switch (status) {
            case CalibrationStatus::TimeoutNoMagnet:
                menuMessage("No magnet found", "Check the mark");
                break;
            case CalibrationStatus::MultipleMagnets:
                menuMessage("Too many magnets", "Remove extras");
                break;
            case CalibrationStatus::WrongTrack:
                menuMessage("Wrong track", "Use outer track");
                break;
            default:
                break;
        }
        delay(2000);
        return status;
    }

    // Commit results to config and EEPROM.
    for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) cfg.hallBaseline[i] = baselines[i];
    cfg.hallThreshold  = threshold;
    cfg.rpmCorrection  = correction;
    cfg.magnetPolarity = peakSign;
    cfg.calibrated     = true;
    storageSave(cfg);

    // Push everything into the modules that use it, so it applies at once.
    // The phase is saved by barUpdate() now that the platter is at rest.
    stepperSetCorrection(correction);
    hallSetCalibration(baselines, threshold);
    hallSetPolarity(peakSign);
    barSetStart(startPos);

    // Report the measured belt reduction so it can be compared against the
    // assumed GEAR_RATIO: motor revs per platter rev is GEAR_RATIO / correction.
    // Tenths by integer maths, so nothing here depends on %f in snprintf.
    int  tenths = constrain((int)lroundf((float)GEAR_RATIO * 10.0f / correction), 0, 999);
    char belt[24];   // sized for any int, which silences -Wformat-truncation
    snprintf(belt, sizeof(belt), "Belt %d.%d:1", tenths / 10, tenths % 10);
    menuMessage("Calibrated!", belt);
    delay(2000);

    return CalibrationStatus::Success;
}

// ---------------------------------------------------------------------------
// Start position only
// ---------------------------------------------------------------------------
// Two passes over the outer sensor at the saved threshold, the one play uses,
// so the start is where the mark's notes fire. The first pass only checks the
// spacing: a second outer-track magnet makes the revolution come up short.
// The other tracks are ignored, so their magnets can stay.

CalibrationStatus calibrationFindStart(const SavedConfig& cfg) {
    menuMessage("Searching...", "");
    stepperStart(CALIBRATION_RPM);

    const uint32_t nominal = stepperStepsPerRev();
    uint8_t  pass     = 0;
    bool     armed    = false;
    int32_t  firstPos = 0;
    int32_t  startPos = 0;
    CalibrationStatus status = CalibrationStatus::TimeoutNoMagnet;

    uint32_t deadline = millis() + DETECT_TIMEOUT_MS;
    while (pass < 2 && millis() < deadline) {
        stepperUpdate();
        uint16_t dev = deviation(rawRead(OUTER_SENSOR), cfg.hallBaseline[OUTER_SENSOR]);
        if (!armed) {
            if (dev < HALL_REARM_LEVEL) armed = true;
            continue;
        }
        if (dev < cfg.hallThreshold) continue;

        int32_t pos = stepperPosition();
        armed = false;
        if (++pass == 1) {
            firstPos = pos;
        } else {
            float ratio = (float)(pos - firstPos) / (float)nominal;
            if (ratio < 1.0f / MULTI_MAGNET_TOLERANCE) {
                status = CalibrationStatus::MultipleMagnets;
            } else if (ratio <= MULTI_MAGNET_TOLERANCE) {
                startPos = pos;
                status   = CalibrationStatus::Success;
            }   // longer: a pass was missed; report no magnet
        }
    }

    stopAndSettle();

    switch (status) {
        case CalibrationStatus::Success:
            barSetStart(startPos);   // saved by barUpdate() at rest
            menuMessage("StartPos set", "");
            break;
        case CalibrationStatus::MultipleMagnets:
            menuMessage("Too many magnets", "Only 1 on outer");
            break;
        default:
            menuMessage("No magnet found", "Check the mark");
            break;
    }
    delay(2000);
    return status;
}
