#include "fx_chorus.h"
#include <math.h>

static constexpr float MS_TO_SAMPLES = AUDIO_SAMPLE_RATE_EXACT / 1000.0f;

void AudioEffectModChorus::rate(float hz) {
    phaseStep = hz * AUDIO_BLOCK_SAMPLES / AUDIO_SAMPLE_RATE_EXACT;
}

void AudioEffectModChorus::depth(float ms) {
    if (ms < 0.0f) ms = 0.0f;
    if (ms > CHORUS_MAX_DEPTH_MS) ms = CHORUS_MAX_DEPTH_MS;
    depthSamples = ms * MS_TO_SAMPLES;
}

void AudioEffectModChorus::update(void) {
    // No input is silence going in; the buffer still plays out what it holds.
    audio_block_t* in  = receiveReadOnly(0);
    audio_block_t* out = allocate();
    if (!out) {
        if (in) release(in);
        return;
    }

    // The LFO is at most a few Hz, so it is worked out at the ends of the
    // block and interpolated across it.
    const float base  = CHORUS_BASE_MS * MS_TO_SAMPLES;
    const float depth = depthSamples;
    float p0 = phase;
    float p1 = phase + phaseStep;
    if (p1 >= 1.0f) p1 -= 1.0f;
    float d0 = base + depth * 0.5f * (1.0f - cosf(TWO_PI * p0));
    float d1 = base + depth * 0.5f * (1.0f - cosf(TWO_PI * p1));
    phase = p1;

    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        buf[head] = in ? in->data[i] : 0;
        float d    = d0 + (d1 - d0) * ((float)i / AUDIO_BLOCK_SAMPLES);
        float pos  = (float)head - d;
        if (pos < 0.0f) pos += BUF_LEN;
        uint16_t a = (uint16_t)pos;
        float    f = pos - (float)a;
        uint16_t b = (a + 1) & (BUF_LEN - 1);
        out->data[i] = (int16_t)(buf[a] + (buf[b] - buf[a]) * f);
        head = (head + 1) & (BUF_LEN - 1);
    }

    if (in) release(in);
    transmit(out);
    release(out);
}
