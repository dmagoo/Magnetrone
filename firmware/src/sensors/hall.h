#pragma once
#include <stdint.h>

void     hallInit();
void     hallUpdate();
uint16_t hallRead(uint8_t index);           // raw ADC value, index 0-7
bool     hallTriggered(uint8_t index);      // true on rising edge only, one cycle
void     hallSetCalibration(uint16_t baseline, uint16_t threshold);

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
