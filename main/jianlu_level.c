// main/jianlu_level.c —— 见 jianlu_level.h。
#include "jianlu_level.h"

#define NOISE_FLOOR_RMS 150
#define FULL_LEVEL_RMS  3000

// 整数平方根(牛顿迭代,32 位输入足够)
static uint32_t isqrt(uint32_t v)
{
    if (v == 0) return 0;
    uint32_t x = v;
    uint32_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

uint8_t jianlu_level_from_pcm(const int16_t *pcm, size_t samples)
{
    if (pcm == 0 || samples == 0) return 0;
    uint32_t acc = 0;
    for (size_t i = 0; i < samples; i++) {
        int32_t s = pcm[i];
        acc += (uint32_t)(s * s) / (uint32_t)samples;   // 防溢出:逐项均摊
    }
    uint32_t rms = isqrt(acc);
    if (rms <= NOISE_FLOOR_RMS) return 0;
    if (rms >= FULL_LEVEL_RMS) return 100;
    return (uint8_t)((rms - NOISE_FLOOR_RMS) * 100 / (FULL_LEVEL_RMS - NOISE_FLOOR_RMS));
}
