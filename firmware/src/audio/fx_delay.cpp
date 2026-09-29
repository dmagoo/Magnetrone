#include "fx_delay.h"
#include <string.h>

void AudioEffectFbDelay::begin(int16_t* b, uint32_t n) {
    __disable_irq();
    buf  = b;
    len  = n;
    head = 0;
    __enable_irq();
    memset(b, 0, n * sizeof(int16_t));
}

void AudioEffectFbDelay::delayMs(float ms) {
    float s = ms * AUDIO_SAMPLE_RATE_EXACT / 1000.0f;
    uint32_t n = (s < 1.0f) ? 1 : (uint32_t)s;
    if (len && n > len - 1) n = len - 1;
    delaySamples = n;
}

void AudioEffectFbDelay::feedback(float amount) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 0.99f) amount = 0.99f;
    fbQ15 = (int32_t)(amount * 32768.0f);
}

void AudioEffectFbDelay::update(void) {
    audio_block_t* in = receiveReadOnly(0);
    if (!buf || len < 2) {
        if (in) release(in);
        return;
    }
    audio_block_t* out = allocate();
    if (!out) {
        if (in) release(in);
        return;
    }

    const uint32_t d  = delaySamples;
    const int32_t  fb = fbQ15;
    uint32_t rd = (head + len - d) % len;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        int32_t echo = buf[rd];
        int32_t x    = (in ? in->data[i] : 0) + ((echo * fb) >> 15);
        if (x >  32767) x =  32767;
        if (x < -32768) x = -32768;
        buf[head]    = (int16_t)x;
        out->data[i] = (int16_t)echo;
        if (++head == len) head = 0;
        if (++rd   == len) rd   = 0;
    }

    if (in) release(in);
    transmit(out);
    release(out);
}
