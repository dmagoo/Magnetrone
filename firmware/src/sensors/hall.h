#pragma once
#include <stdint.h>

void     hallInit();
void     hallUpdate();
uint16_t hallRead(uint8_t index);           // raw ADC value, index 0-7
bool     hallTriggered(uint8_t index);      // true on rising edge only, one cycle
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

// Which way a passing magnet pushes the sensor output: +1 or -1. Only that
// direction fires a note.
//
// This matters because a disc magnet's field REVERSES at its edges. A pass
// reads as fringe, then face, then fringe, with the fringe lobes opposite in
// sign to the face and strong enough to clear the threshold on their own. The
// old code compared absolute deviation, so those lobes were indistinguishable
// from a real hit and one pass fired two or three notes -- worst on the slow
// inner tracks, where the lobes are far enough apart in time to clear the
// debounce. Keying on the sign drops them entirely.
void     hallSetPolarity(int8_t polarity);

// Signed deviation from baseline for a sensor, as of the last hallUpdate().
// The sign is the magnet's orientation; a future revision uses it to let a
// flipped magnet mean something different musically.
int16_t  hallDeviation(uint8_t index);
