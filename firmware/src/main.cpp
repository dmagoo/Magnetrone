#include <Arduino.h>
#include "config/storage.h"
#include "motion/stepper.h"
#include "ui/encoder.h"
#include "audio/audio.h"
#include "midi/midi.h"
#include "midi/transport.h"
#include "midi/midi_in.h"
#include "motion/bar.h"
#include "sensors/hall.h"
#include "menu/menu.h"
#include "sequencer/sequencer.h"
#include "sequencer/layers.h"
#include "sequencer/scenes.h"
#include "diag/diag.h"

static SavedConfig cfg;
static bool diagMode = false;

void setup() {
    diagMode = diagWanted();
    if (diagMode) { diagSetup(); return; }

    storageLoad(cfg);
    stepperInit();
    stepperSetCorrection(cfg.rpmCorrection);   // measured by calibration
    barInit(cfg);
    encoderInit();
    audioInit(cfg.volume, cfg.muted);
    midiInit();
    layersApply(cfg);
    hallInit();
    hallSetCalibration(cfg.hallBaseline, cfg.hallNoise, cfg.hallThreshold);
    hallSetPolarity(cfg.magnetPolarity);
    scenesInit(cfg);
    menuInit(cfg);
}

void loop() {
    if (diagMode) { diagLoop(); return; }

    stepperUpdate();
    encoderUpdate();
    hallUpdate();
    sequencerUpdate(cfg);
    menuUpdate(cfg);
    midiInUpdate(cfg);
    barUpdate(cfg);
    scenesUpdate(cfg);
    layersUpdate(cfg);   // Sync delay times follow the speed

    // Beat clock follows the platter's position, so it tracks the real speed.
    transportUpdate(cfg.beatsPerRev);

    diagPollSerial(cfg);   // "diag" and "scenes" in the serial monitor
}
