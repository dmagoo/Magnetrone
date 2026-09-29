#pragma once
#include <stdint.h>
#include "config/storage.h"

// =============================================================================
// Bar start -- where a bar begins on the platter.
//
// One platter revolution is one bar. The start is the moment the start mark
// on the platter passes the sensor arm. It is kept as the motor position at
// which that happens, so the phase is exact: a GT2 belt cannot slip, so steps
// per revolution never changes. Lost steps (a stall, a bump) would jump it.
//
// The motor is unpowered at boot, so the position does not survive a power
// cycle on its own. The phase is saved whenever the platter comes to rest and
// invalidated when it starts, so after a power cut mid-spin the start is
// unknown rather than wrong. A platter turned by hand while off goes unseen.
// =============================================================================

void     barInit(const SavedConfig& cfg);   // restore the saved phase, if valid
void     barUpdate(SavedConfig& cfg);       // call every loop: saves / invalidates the phase

bool     barKnown();
uint32_t barStepsPerRev();

// Steps past the start mark, 0 to barStepsPerRev()-1, counted in the forward
// direction. Only meaningful when barKnown().
uint32_t barPhase();

// The same, for the platter at motor position `pos` rather than now.
uint32_t barPhaseAt(int32_t pos);

// The start mark passed the arm at motor position `pos`.
void     barSetStart(int32_t pos);

// Forget the start (Reset Cal). Writes nothing; the caller saves cfg.
void     barForget(SavedConfig& cfg);
