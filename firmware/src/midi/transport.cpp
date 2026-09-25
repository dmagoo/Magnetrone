#include "transport.h"
#include "midi.h"
#include "motion/stepper.h"
#include "motion/bar.h"

constexpr uint32_t PPQN            = 24;
constexpr uint32_t TICKS_PER_16TH  = 6;
constexpr uint32_t SPP_LIMIT       = 16384;   // Song Position Pointer is 14 bits

static bool     rolling     = false;   // Start/Continue sent, clock flowing
static bool     everStarted = false;   // Start the first time, Continue after
static bool     holdFirst   = false;   // no clock until the tick at firstTick
static uint32_t firstTick   = 0;
static uint32_t lastTick    = 0;       // tick within the bar at the last update
static uint32_t songTick    = 0;       // song position of the next clock
static uint32_t lastTpr     = 0;
static int32_t  lastPos     = 0;
static int8_t   dir         = 0;       // direction of motion, 0 until it moves

static void halt() {
    if (rolling) {
        midiStop();
        rolling = false;
    }
}

// Where the platter is in the bar, in clock ticks, as the receiver sees it:
// always forward in time, whichever way the platter turns.
static uint32_t tickInBar(uint32_t tpr) {
    uint32_t n = barStepsPerRev();
    uint32_t p = barPhase();
    if (dir < 0) p = (n - p) % n;
    return (uint32_t)((uint64_t)p * tpr / n);
}

static void start(uint32_t t, uint32_t tpr) {
    lastTick = t;
    if (barKnown()) {
        // The next 16th after now. It may be the next bar's downbeat.
        uint32_t w    = (t / TICKS_PER_16TH + 1) * TICKS_PER_16TH;
        uint32_t bars = songTick / tpr;
        if (w >= tpr) { w -= tpr; bars++; }
        uint32_t target = bars * tpr + w;
        if (target < songTick) target += tpr;              // never jump backwards
        if (target / TICKS_PER_16TH >= SPP_LIMIT) target = w;  // wrap to the top

        midiSongPosition((uint16_t)(target / TICKS_PER_16TH));
        midiContinue();
        songTick  = target;
        firstTick = w;
        holdFirst = true;
    } else {
        if (everStarted) midiContinue();
        else             midiStart();
        holdFirst = false;
    }
    everStarted = true;
    rolling     = true;
}

void transportUpdate(uint8_t beatsPerRev) {
    int32_t pos   = stepperPosition();
    int32_t delta = pos - lastPos;
    lastPos = pos;

    // Jogging to find the start is not playing.
    if (!stepperRunning() || stepperJogging()) {
        halt();
        dir = 0;   // the next start may go either way
        return;
    }

    if (delta != 0) {
        int8_t d = (delta > 0) ? 1 : -1;
        if (d != dir) { halt(); dir = d; }   // reversed: resynchronise
    }
    if (dir == 0) return;                    // started but not moved yet

    uint32_t tpr = PPQN * beatsPerRev;
    if (tpr == 0) return;
    if (tpr != lastTpr) { halt(); lastTpr = tpr; }   // Beats/Rev changed

    uint32_t t = tickInBar(tpr);
    if (!rolling) { start(t, tpr); return; }

    // A slow loop (an EEPROM write, an LCD redraw) can cross a few ticks at
    // once; they go out as a short burst, keeping the count right. Half a bar
    // or more means a long block, where the count is ambiguous: resynchronise.
    uint32_t crossed = (t + tpr - lastTick) % tpr;
    if (crossed >= tpr / 2) { halt(); return; }   // restarts next loop

    for (uint32_t k = 0; k < crossed; k++) {
        lastTick = (lastTick + 1) % tpr;
        if (holdFirst) {
            if (lastTick != firstTick) continue;
            holdFirst = false;
        }
        midiClock();
        songTick++;
    }
}
