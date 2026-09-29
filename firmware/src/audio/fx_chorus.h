#pragma once
#include <Arduino.h>
#include <AudioStream.h>

// A chorus: one copy of the input, delayed by a time a sine LFO sweeps. The
// output is that copy alone (the wet signal); the layer's mixer blends it
// with the dry. The Teensy library's own chorus has only a voice count, no
// rate or depth, so this replaces it.
//
// The delay sweeps from CHORUS_BASE_MS up by the depth. At depth 0 it holds
// at the base: a fixed delayed copy, which still colours the sound.
class AudioEffectModChorus : public AudioStream {
public:
    static constexpr float CHORUS_BASE_MS      = 10.0f;
    static constexpr float CHORUS_MAX_DEPTH_MS = 5.0f;

    AudioEffectModChorus() : AudioStream(1, inputQueueArray) {}

    void rate(float hz);     // LFO rate
    void depth(float ms);    // sweep, 0 to CHORUS_MAX_DEPTH_MS

    virtual void update(void);

private:
    // A power of two above (base + max depth) in samples, plus the
    // interpolation's one extra.
    static constexpr uint16_t BUF_LEN = 1024;
    static_assert((CHORUS_BASE_MS + CHORUS_MAX_DEPTH_MS) * AUDIO_SAMPLE_RATE_EXACT / 1000.0f + 2 < BUF_LEN,
                  "chorus buffer too short for its delay");

    audio_block_t* inputQueueArray[1];
    int16_t  buf[BUF_LEN] = {};
    uint16_t head = 0;
    float    phase = 0.0f;              // LFO, 0 to 1
    volatile float phaseStep = 0.0f;    // per block
    volatile float depthSamples = 0.0f;
};
