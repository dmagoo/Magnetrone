#include "bar.h"
#include "stepper.h"

static bool    known     = false;
static int32_t origin    = 0;       // motor position of the start mark
static bool    restSaved = true;    // the phase at this rest is already in cfg

void barInit(const SavedConfig& cfg) {
    known = cfg.barPhaseValid;
    // The position counts from 0 at power-up, so put the origin where the
    // saved phase says the platter is.
    origin    = known ? -cfg.barPhase : 0;
    restSaved = true;
}

bool barKnown() {
    return known;
}

uint32_t barStepsPerRev() {
    return stepperStepsPerRev();
}

uint32_t barPhase() {
    return barPhaseAt(stepperPosition());
}

uint32_t barPhaseAt(int32_t pos) {
    int32_t n = (int32_t)barStepsPerRev();
    int32_t m = (pos - origin) % n;
    return (uint32_t)(m < 0 ? m + n : m);
}

int32_t barRevAt(int32_t pos) {
    // Whole revolutions past the start mark, rounded down, so it steps at
    // the mark in either direction. Before the start is known the origin is
    // where the platter sat at power-up, which is as good as any.
    int32_t n = (int32_t)barStepsPerRev();
    int32_t d = pos - origin;
    return (d >= 0) ? d / n : -((-d + n - 1) / n);
}

void barSetStart(int32_t pos) {
    origin    = pos;
    known     = true;
    restSaved = false;   // save the new phase at the next rest
}

void barForget(SavedConfig& cfg) {
    known             = false;
    restSaved         = true;
    cfg.barPhase      = 0;
    cfg.barPhaseValid = false;
}

void barUpdate(SavedConfig& cfg) {
    // Keep the origin near the position so the int32 difference never
    // overflows on a long run (it would after ~17 hours at MAX_RPM). It moves
    // by a whole number of TURN_CYCLES_LCM revolutions, so no layer's turn
    // (barRevAt()) changes.
    int32_t n = (int32_t)barStepsPerRev();
    int32_t d = stepperPosition() - origin;
    if (d > n * 1000 || d < -n * 1000) origin += (d / (TURN_CYCLES_LCM * n)) * TURN_CYCLES_LCM * n;

    if (!known) return;

    if (stepperRunning()) {
        // Moving: whatever is saved is about to be stale. One write per start.
        if (cfg.barPhaseValid) {
            cfg.barPhaseValid = false;
            storageSave(cfg);
        }
        restSaved = false;
    } else if (!restSaved) {
        cfg.barPhase      = (int32_t)barPhase();
        cfg.barPhaseValid = true;
        storageSave(cfg);
        restSaved = true;
    }
}
