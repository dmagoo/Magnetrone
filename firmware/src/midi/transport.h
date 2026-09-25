#pragma once
#include <stdint.h>

// =============================================================================
// MIDI beat clock and transport, driven by the platter's position.
//
// Clock ticks (24 per quarter note, beatsPerRev beats per revolution) are sent
// as the platter crosses each tick's position, not from a timer, so external
// gear follows the platter exactly, ramps included, and never drifts from it.
//
// With the bar start known, a start sends Song Position Pointer plus Continue,
// placing the receiver at the next 16th in the bar, and holds the first clock
// until the platter reaches it. So the receiver's bars land on the start mark
// even when the platter starts mid-bar. Without it, Start then Continue, as
// before. Reversing resynchronises the same way: external gear only runs
// forward, so in reverse its bar runs from the mark backwards.
// =============================================================================

void transportUpdate(uint8_t beatsPerRev);   // call every loop
