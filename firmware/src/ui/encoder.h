#pragma once
#include <stdint.h>

struct EncoderEvent {
    int8_t menuDelta;       // menu encoder turn
    bool   menuPressed;

    int8_t speedDelta;      // speed encoder turn
    bool   speedPressed;

    int8_t volumeDelta;     // volume encoder turn
    bool   volumePressed;
};

void         encoderInit();
void         encoderUpdate();
EncoderEvent encoderEvents();   // returns events since last call, clears them
