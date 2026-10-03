// tests/test_jianlu_level.c —— PCM 电平映射的 host 侧测试。
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "jianlu_level.h"

int main(void) {
    int16_t buf[1024];

    // 静音 → 0
    memset(buf, 0, sizeof(buf));
    assert(jianlu_level_from_pcm(buf, 1024) == 0);

    // 背景噪声以下(RMS≈100)→ 0
    for (int i = 0; i < 1024; i++) buf[i] = (i % 2) ? 100 : -100;
    assert(jianlu_level_from_pcm(buf, 1024) == 0);

    // 中等语音(RMS≈1500)→ 约 45
    for (int i = 0; i < 1024; i++) buf[i] = (i % 2) ? 1500 : -1500;
    uint8_t mid = jianlu_level_from_pcm(buf, 1024);
    assert(mid > 35 && mid < 60);

    // 大音量(RMS≈6000)→ 封顶 100
    for (int i = 0; i < 1024; i++) buf[i] = (i % 2) ? 6000 : -6000;
    assert(jianlu_level_from_pcm(buf, 1024) == 100);

    // 满幅不失控(int16 极值)
    for (int i = 0; i < 1024; i++) buf[i] = (i % 2) ? 32767 : -32768;
    assert(jianlu_level_from_pcm(buf, 1024) == 100);

    // 空输入安全
    assert(jianlu_level_from_pcm(NULL, 0) == 0);
    return 0;
}
