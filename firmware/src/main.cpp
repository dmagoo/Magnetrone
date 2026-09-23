#include <Arduino.h>
#include <math.h>
#include "config/storage.h"
#include "motion/stepper.h"
#include "ui/encoder.h"
#include "audio/audio.h"
#include "midi/midi.h"
#include "sensors/hall.h"
#include "menu/menu.h"
#include "sequencer/sequencer.h"

static SavedConfig cfg;

void setup() {
    storageLoad(cfg);
    stepperInit();
    stepperSetCorrection(cfg.rpmCorrection);   // measured by calibration
    encoderInit();
    audioInit(cfg.volume, cfg.muted);
    audioSetVoice(voiceGet(cfg.voice));
    midiInit();
    hallInit();
    hallSetCalibration(cfg.hallBaseline, cfg.hallThreshold);
    hallSetPolarity(cfg.magnetPolarity);
    menuInit(cfg);
}

void loop() {
    stepperUpdate();
    encoderUpdate();
    hallUpdate();
    sequencerUpdate(cfg);
    menuUpdate(cfg);
    midiUpdate();

    // Beat clock follows the platter. RPM is signed (negative = reversed), but
    // tempo is not, so the clock tracks the magnitude.
    midiClockUpdate(fabsf(cfg.rpm) * cfg.beatsPerRev, stepperRunning());
}
