#pragma once
#include <stdint.h>
#include "config.h"

// The drum kit: one drum per slot, slot 0 = hall 1 (innermost track) through
// slot 7 = hall 8 (outermost). The busiest drums sit on the outer tracks, which
// have room for ~20 magnets per rev; the inner track fits only 4-5. That is
// with Low Note Inner and no shift: Track Shift rotates which sensor plays
// which slot, so a sparse drum can still be moved outward to rapid-fire it,
// and Low Note Outer flips the kit end to end (see layerDegree()).
enum class DrumSlot : uint8_t {
    Crash, LowTom, HighTom, Clap, OpenHat, Snare, Kick, ClosedHat, COUNT
};
static_assert((uint8_t)DrumSlot::COUNT == NUM_HALL_SENSORS, "one drum per sensor");

// General MIDI percussion note numbers (channel 10), per slot. Fixed: drums
// ignore root, scale, octave and the pitch offset, since moving a GM drum
// number changes which drum plays.
constexpr uint8_t KIT_GM_NOTE[NUM_HALL_SENSORS] = {
    49,   // Crash Cymbal 1
    45,   // Low Tom
    50,   // High Tom
    39,   // Hand Clap
    46,   // Open Hi-Hat
    38,   // Acoustic Snare
    36,   // Bass Drum 1
    42,   // Closed Hi-Hat
};
