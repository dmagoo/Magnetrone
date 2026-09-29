#pragma once
#include <Arduino.h>
#include <AudioStream.h>

// An echo: the input delayed by a set time, with part of each echo fed back
// in so it repeats and fades. The output is the echoes alone (the wet
// signal); the layer's mixer blends them with the dry.
//
// The buffer is the caller's, fixed in size at build time, so one that does
// not fit fails the build instead of misbehaving on the board. It keeps
// running at Mix 0, so echoes fade out and come back naturally rather than
// cutting off or replaying stale audio.
class AudioEffectFbDelay : public AudioStream {
public:
    AudioEffectFbDelay() : AudioStream(1, inputQueueArray) {}

    // The buffer, `len` samples. Cleared here: RAM2 is not cleared at boot.
    void begin(int16_t* buf, uint32_t len);

    void delayMs(float ms);          // clamped to what the buffer holds
    void feedback(float amount);     // 0 to 1; the caller keeps it below 1

    virtual void update(void);

private:
    audio_block_t* inputQueueArray[1];
    int16_t*          buf = nullptr;
    uint32_t          len = 0;
    uint32_t          head = 0;
    volatile uint32_t delaySamples = 1;
    volatile int32_t  fbQ15 = 0;     // feedback, Q15
};
