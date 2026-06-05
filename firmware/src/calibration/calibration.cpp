#include "calibration.h"
#include <Arduino.h>
#include "pins.h"
#include "config.h"
#include "motion/stepper.h"
#include "sensors/hall.h"
#include "config/storage.h"
#include "menu/menu.h"

// ---------------------------------------------------------------------------
// Local constants
// ---------------------------------------------------------------------------

// How long to sample sensors with no magnets present.
// We don't know revolutions at this point, so we use time.
// At DEFAULT_RPM (45 RPM = 0.75 rev/sec), 3 seconds covers ~2 full revolutions
// which is enough to establish a stable noise floor.
static const uint32_t BASELINE_SAMPLE_MS = 3000;

// How long to wait for a magnet to be detected before giving up.
static const uint32_t DETECT_TIMEOUT_MS = 15000;

// Threshold is set to this fraction of the measured peak deviation.
// 0.5 means the trigger fires at half the peak signal strength,
// giving a reliable trigger without being too sensitive to noise.
static const float THRESHOLD_FACTOR = 0.5f;

// If the measured platter RPM exceeds the commanded RPM by more than
// this factor, it almost certainly means multiple magnets are present
// (each pass looks like a revolution to the sensor).
static const float MULTI_MAGNET_TOLERANCE = 1.3f;

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
// Phase 1: Baseline
// ---------------------------------------------------------------------------
// With no magnets present and the motor running, sample every sensor
// repeatedly over a fixed time window. Record the min, max, and running
// average for each sensor. The average becomes the resting baseline.
// The range (max - min) tells us the noise floor.
//
// We use a global average across all sensors for simplicity. Per-sensor
// baselines are a future refinement if sensors show significant variation.

static uint16_t sampleBaseline() {
    menuMessage("Sampling...", "Keep magnets off");

    uint32_t sum   = 0;
    uint32_t count = 0;
    uint32_t start = millis();

    while (millis() - start < BASELINE_SAMPLE_MS) {
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
            sum += rawRead(i);
            count++;
        }
        delay(5);
    }

    return (uint16_t)(sum / count);
}

// ---------------------------------------------------------------------------
// Phase 2: Magnet detection
// ---------------------------------------------------------------------------
// With one magnet placed on the platter, we wait for the first sensor
// to exceed a temporary working threshold (baseline + HALL_THRESHOLD_DEFAULT).
// Once a trigger is detected we:
//   - Record the timestamp (revolution start)
//   - Record the peak ADC deviation for threshold calculation
//   - Record the dwell time (how long the trigger stays active)
//     NOTE: inner track sensors will have longer dwell times because
//     their linear velocity is lower. This is expected and correct.
//   - Wait for the same sensor to trigger a second time (one full revolution)
//   - Calculate actual RPM from the revolution interval
//
// We then check whether the measured RPM is suspiciously higher than
// the commanded RPM. If so, multiple magnets are the likely cause.

static CalibrationStatus detectMagnet(
    uint16_t baseline,
    uint16_t& outThreshold,
    float&    outRpmCorrection)
{
    menuMessage("Searching...", "");

    // Working threshold for detection during this phase.
    // Will be replaced by the measured peak result at the end.
    uint16_t workingThreshold = HALL_THRESHOLD_DEFAULT;

    // Per-sensor state for revolution timing.
    // We track the sensor that first triggers and use it to time one revolution.
    int8_t  timingSensor   = -1;    // which sensor we are timing (-1 = none yet)
    bool    inTrigger      = false; // currently above threshold
    uint32_t triggerStart  = 0;     // when the current trigger began
    uint32_t revStart      = 0;     // timestamp of first trigger (revolution start)
    uint16_t peakDeviation = 0;     // highest ADC deviation seen so far
    // uint16_t dwellMs = 0;  // dwell = how long magnet was over sensor (ms)
    //                        // future: store per-sensor in EEPROM to scale note duration by magnet speed

    uint32_t deadline = millis() + DETECT_TIMEOUT_MS;

    while (millis() < deadline) {
        for (uint8_t i = 0; i < NUM_HALL_SENSORS; i++) {
            uint16_t val = rawRead(i);
            uint16_t dev = deviation(val, baseline);

            // Track the global peak regardless of which sensor reports it.
            // This gives us the strongest signal seen, which we use to
            // derive the final threshold.
            if (dev > peakDeviation) peakDeviation = dev;

            // We only time revolutions on the first sensor that triggers.
            // Once a timing sensor is chosen we ignore the others for RPM math.
            // The user is instructed to use the outer track which has the
            // shortest dwell and therefore the sharpest timing edge.

            if (timingSensor == -1) {
                // No sensor chosen yet. Waiting for the first trigger.
                if (dev >= workingThreshold) {
                    timingSensor = (int8_t)i;
                    inTrigger    = true;
                    triggerStart = millis();
                    revStart     = triggerStart;
                }
            } else if ((int8_t)i == timingSensor) {
                // We are tracking this sensor for revolution timing.
                if (inTrigger) {
                    if (dev < workingThreshold - HALL_HYSTERESIS) {
                        // Trigger just went low. Record dwell time.
                        // Dwell is how long the magnet was over the sensor.
                        // Shorter dwell = outer track = faster linear speed.
                        inTrigger = false;
                        // dwellMs = (uint16_t)(millis() - triggerStart);
                    }
                } else {
                    if (dev >= workingThreshold) {
                        // Second trigger on the same sensor = one full revolution.
                        uint32_t revolutionMs = millis() - revStart;

                        // Convert revolution period to actual RPM.
                        // revolutionMs is the time for one full platter revolution.
                        float actualRPM = 60000.0f / (float)revolutionMs;

                        // We commanded DEFAULT_RPM. The ratio of actual to
                        // commanded tells us how far off our gear ratio assumption
                        // is (or if the belt is slipping, or the tooth count differs).
                        float correctionFactor = actualRPM / DEFAULT_RPM;

                        // If the measured RPM is significantly higher than commanded,
                        // the most likely explanation is multiple magnets on this track.
                        // Each pass would appear to be a full revolution.
                        if (correctionFactor > MULTI_MAGNET_TOLERANCE) {
                            menuMessage("Too many magnets", "Remove extras");
                            delay(2000);
                            return CalibrationStatus::MultipleMagnets;
                        }

                        // Threshold is a fixed fraction of the peak deviation seen.
                        // This makes the threshold self-scaling to magnet strength.
                        outThreshold      = (uint16_t)(peakDeviation * THRESHOLD_FACTOR);
                        outRpmCorrection  = correctionFactor;
                        return CalibrationStatus::Success;
                    }
                }
            }
        }

        delay(2);
    }

    // No second trigger within the timeout window.
    return CalibrationStatus::TimeoutNoMagnet;
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

CalibrationStatus calibrationRun(SavedConfig& cfg) {
    // Run motor at our best-guess DEFAULT_RPM.
    // We do not apply rpmCorrection here because calibration IS what produces
    // the correction factor. On a first run, 1.0 is the best we have.
    stepperStart(DEFAULT_RPM);

    // Phase 1: establish baseline with no magnets.
    uint16_t baseline = sampleBaseline();

    // Phase 2: detect magnet, time one revolution, measure peak.
    uint16_t threshold     = HALL_THRESHOLD_DEFAULT;
    float    rpmCorrection = 1.0f;

    CalibrationStatus status = detectMagnet(baseline, threshold, rpmCorrection);

    stepperStop();

    if (status != CalibrationStatus::Success) {
        // Show a brief error. The caller (menu) decides what to do next.
        switch (status) {
            case CalibrationStatus::TimeoutNoMagnet:
                menuMessage("No magnet found", "Check placement");
                break;
            case CalibrationStatus::MultipleMagnets:
                // message already shown inside detectMagnet
                break;
            default:
                break;
        }
        delay(2000);
        return status;
    }

    // Commit results to config and EEPROM.
    cfg.hallBaseline   = baseline;
    cfg.hallThreshold  = threshold;
    cfg.rpmCorrection  = rpmCorrection;
    cfg.calibrated     = true;
    storageSave(cfg);

    // Push the new threshold and baseline into the hall sensor module
    // so note triggering uses calibrated values immediately.
    hallSetCalibration(baseline, threshold);

    menuMessage("Calibrated!", "");
    delay(2000);

    return CalibrationStatus::Success;
}
