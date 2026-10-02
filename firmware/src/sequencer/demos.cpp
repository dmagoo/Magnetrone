#include "demos.h"
#include "audio/voice.h"

#define DEMOS_DATA
#include "demos_data.h"   // DEMO_NAMES[] and demoApply()

const char* demoName(uint8_t i) {
    return (i < DEMO_COUNT) ? DEMO_NAMES[i] : "";
}

void demoBuild(uint8_t i, Scene& s, VoiceSlot voices[NUM_LAYERS]) {
    s = storageFactoryScene();
    for (uint8_t l = 0; l < NUM_LAYERS; l++) voices[l] = VoiceSlot{};
    if (i < DEMO_COUNT) demoApply(i, s, voices);
}
