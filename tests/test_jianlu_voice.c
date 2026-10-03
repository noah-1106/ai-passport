// tests/test_jianlu_voice.c —— 语音状态机与 WAV 头构建的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_voice.h"

static void test_happy_path(void) {
    jianlu_voice_t v;
    jianlu_voice_init(&v);
    assert(v.state == JIANLU_VOICE_IDLE);

    // IDLE 下只有 HOLD_START 被接受
    assert(!jianlu_voice_event(&v, JIANLU_VOICE_EV_STOP));
    assert(!jianlu_voice_event(&v, JIANLU_VOICE_EV_SEND_OK));
    assert(!jianlu_voice_event(&v, JIANLU_VOICE_EV_DISMISS));

    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_HOLD_START));
    assert(v.state == JIANLU_VOICE_RECORDING);
    assert(v.recorded_bytes == 0);

    // 录了 3 秒,松开 → SENDING
    v.recorded_bytes = 3 * JIANLU_VOICE_BYTES_PER_SEC;
    assert(!jianlu_voice_reached_max(&v));
    assert(jianlu_voice_worth_sending(&v));
    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_STOP));
    assert(v.state == JIANLU_VOICE_SENDING);

    // SENDING 中不再接受录音类事件
    assert(!jianlu_voice_event(&v, JIANLU_VOICE_EV_HOLD_START));
    assert(!jianlu_voice_event(&v, JIANLU_VOICE_EV_STOP));

    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_SEND_OK));
    assert(v.state == JIANLU_VOICE_CONFIRM);
    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_DISMISS));
    assert(v.state == JIANLU_VOICE_IDLE);
}

static void test_too_short_cancelled(void) {
    jianlu_voice_t v;
    jianlu_voice_init(&v);
    jianlu_voice_event(&v, JIANLU_VOICE_EV_HOLD_START);
    // 0.2 秒:低于 0.5s 下限,STOP 直接回 IDLE(调用方丢弃文件)
    v.recorded_bytes = JIANLU_VOICE_BYTES_PER_SEC / 5;
    assert(!jianlu_voice_worth_sending(&v));
    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_STOP));
    assert(v.state == JIANLU_VOICE_IDLE);
}

static void test_max_cap(void) {
    jianlu_voice_t v;
    jianlu_voice_init(&v);
    jianlu_voice_event(&v, JIANLU_VOICE_EV_HOLD_START);
    v.recorded_bytes = JIANLU_VOICE_MAX_BYTES;
    assert(jianlu_voice_reached_max(&v));
    assert(jianlu_voice_worth_sending(&v));
    jianlu_voice_event(&v, JIANLU_VOICE_EV_STOP);
    assert(v.state == JIANLU_VOICE_SENDING);
}

static void test_send_fail_then_dismiss(void) {
    jianlu_voice_t v;
    jianlu_voice_init(&v);
    jianlu_voice_event(&v, JIANLU_VOICE_EV_HOLD_START);
    v.recorded_bytes = JIANLU_VOICE_BYTES_PER_SEC;
    jianlu_voice_event(&v, JIANLU_VOICE_EV_STOP);
    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_SEND_FAIL));
    assert(v.state == JIANLU_VOICE_ERROR);
    // ERROR 中不能重新开始录音,只能先关闭
    assert(!jianlu_voice_event(&v, JIANLU_VOICE_EV_HOLD_START));
    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_DISMISS));
    assert(v.state == JIANLU_VOICE_IDLE);
    // 关闭后可以再次录音
    assert(jianlu_voice_event(&v, JIANLU_VOICE_EV_HOLD_START));
}

static void test_wav_header(void) {
    uint8_t h[JIANLU_WAV_HEADER_LEN];
    uint32_t data = 32000;   // 1 秒
    jianlu_voice_wav_header(h, data);

    assert(memcmp(h, "RIFF", 4) == 0);
    assert(memcmp(h + 8, "WAVE", 4) == 0);
    assert(memcmp(h + 12, "fmt ", 4) == 0);
    assert(memcmp(h + 36, "data", 4) == 0);

    // RIFF 总长度 = 36 + data
    assert(h[4] == (uint8_t)((36 + data) & 0xFF));
    assert(h[5] == (uint8_t)(((36 + data) >> 8) & 0xFF));
    // PCM / 单声道 / 16bit
    assert(h[20] == 1 && h[21] == 0);
    assert(h[22] == 1 && h[23] == 0);
    assert(h[34] == 16 && h[35] == 0);
    // 采样率 16000 = 0x3E80,字节率 32000 = 0x7D00
    assert(h[24] == 0x80 && h[25] == 0x3E && h[26] == 0 && h[27] == 0);
    assert(h[28] == 0x00 && h[29] == 0x7D && h[30] == 0 && h[31] == 0);
    // block align = 2
    assert(h[32] == 2 && h[33] == 0);
    // data 长度
    assert(h[40] == (uint8_t)(data & 0xFF));
    assert(h[41] == (uint8_t)((data >> 8) & 0xFF));
    assert(h[42] == 0 && h[43] == 0);
}

static void test_pending_retry_decision(void) {
    assert(jianlu_voice_should_retry_pending(true, true));
    assert(!jianlu_voice_should_retry_pending(true, false));
    assert(!jianlu_voice_should_retry_pending(false, true));
    assert(!jianlu_voice_should_retry_pending(false, false));
}

int main(void) {
    test_happy_path();
    test_too_short_cancelled();
    test_max_cap();
    test_send_fail_then_dismiss();
    test_wav_header();
    test_pending_retry_decision();
    return 0;
}
