#include <Arduino.h>
#include "config/storage.h"
#include "motion/stepper.h"
#include "ui/encoder.h"
#include "audio/audio.h"
#include "midi/midi.h"
#include "midi/transport.h"
#include "motion/bar.h"
#include "sensors/hall.h"
#include "menu/menu.h"
#include "sequencer/sequencer.h"
#include "sequencer/layers.h"

static SavedConfig cfg;

void setup() {
    storageLoad(cfg);
    stepperInit();
    stepperSetCorrection(cfg.rpmCorrection);   // measured by calibration
    barInit(cfg);
    encoderInit();
    audioInit(cfg.volume, cfg.muted);
    midiInit();
    layersApply(cfg);
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
    barUpdate(cfg);

    // Beat clock follows the platter's position, so it tracks the real speed.
    transportUpdate(cfg.beatsPerRev);
}
