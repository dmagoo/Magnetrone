#pragma once
#include <stdint.h>

struct EncoderEvent {
    int8_t menuDelta;       // menu encoder turn
    bool   menuPressed;

    int8_t speedDelta;      // speed encoder turn
    bool   speedPressed;

    int8_t volumeDelta;     // volume encoder turn
    bool   volumePressed;

    int8_t auxDelta;        // aux encoder turn -- drives the Aux Fn modulation
    bool   auxPressed;      // aux button: always "back" in the Aux Fn flow
};

void         encoderInit();
void         encoderUpdate();
EncoderEvent encoderEvents();   // returns events since last call, clears them
bool         encoderTakeMenuPress();  // clears and returns only a pending menu press
