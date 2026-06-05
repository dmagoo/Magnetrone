#include <Arduino.h>
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
    encoderInit();
    audioInit(cfg.volume, cfg.muted);
    midiInit();
    hallInit();
    menuInit(cfg);
}

void loop() {
    encoderUpdate();
    hallUpdate();
    sequencerUpdate(cfg);
    menuUpdate(cfg);
    midiUpdate();
}
