// main/jianlu_level.h —— PCM 音量电平映射(纯 C,host 可测)。
//
// 把 16bit PCM 块的 RMS 映射为 0..100 的驱动条高度,供录音声波动画使用。
// 整数运算,无浮点。
#pragma once

#include <stddef.h>
#include <stdint.h>

// silence → 0;RMS 约 150 以下视为背景噪声压到 0;RMS ≥ 3000 顶到 100。
uint8_t jianlu_level_from_pcm(const int16_t *pcm, size_t samples);
