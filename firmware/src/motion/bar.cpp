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
    int32_t n = (int32_t)barStepsPerRev();
    int32_t m = (stepperPosition() - origin) % n;
    return (uint32_t)(m < 0 ? m + n : m);
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
    // overflows on a long run (it would after ~17 hours at MAX_RPM).
    int32_t n = (int32_t)barStepsPerRev();
    int32_t d = stepperPosition() - origin;
    if (d > n * 1000 || d < -n * 1000) origin += (d / n) * n;

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
