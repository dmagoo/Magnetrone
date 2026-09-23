#pragma once
#include <stdint.h>

void     hallInit();
void     hallUpdate();
uint16_t hallRead(uint8_t index);           // raw ADC value, index 0-7

// Which pole of a magnet fired a trigger. Normal is the orientation set by
// hallSetPolarity(); Reversed is a magnet flipped the other way up.
enum class HallPole : uint8_t { None, Normal, Reversed };

// The pole that fired on this cycle's rising edge, or None. One cycle only.
HallPole hallTrigger(uint8_t index);
// Per-sensor baselines, one shared threshold.
//
// The baselines have to be per-sensor: the eight rest levels span roughly 150
// counts, and a single average leaves the outliers permanently further from
// their own rest value than HALL_REARM_LEVEL. Such a sensor can never re-arm,
// so it fires once and then stays silent for the rest of the session.
//
// The threshold stays shared because it is not measurable per-sensor: one
// magnet sits on one track, so calibration only ever sees a real hit on the
// single sensor that magnet passes.
void     hallSetCalibration(const uint16_t* baselines, uint16_t threshold);

// Which way a passing magnet pushes the sensor output: +1 or -1. A trigger in
// this direction is HallPole::Normal, the opposite one HallPole::Reversed.
//
// Both directions fire. A disc magnet's field reverses at its edges, so a pass
// reads fringe / face / fringe with the fringes opposite in sign to the face,
// and accepting both signs is only safe if the fringes stay under the
// threshold. Measured 2026-09-23 with test_hall_range on the hand-turned
// platter: fringes are 5-9% of the face on all eight sensors (76-129 counts
// against faces of 1166-1453), under both the calibrated threshold (50% of
// peak) and HALL_THRESHOLD_DEFAULT (200).
void     hallSetPolarity(int8_t polarity);

// Signed deviation from baseline for a sensor, as of the last hallUpdate().
// The sign is the magnet's orientation.
int16_t  hallDeviation(uint8_t index);
