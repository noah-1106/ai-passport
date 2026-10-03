// main/jianlu_voice.h —— 语音记录核心逻辑(纯 C,不依赖 ESP-IDF/LVGL)。
//
// 包含三块可 host 测试的纯逻辑:
//   1. 录音状态机:IDLE → RECORDING → SENDING → CONFIRM/ERROR → IDLE
//   2. WAV 头构建(44 字节,PCM 16kHz 16bit 单声道)
//   3. 录音长度边界:太短取消、到上限自动停止
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JIANLU_VOICE_SAMPLE_RATE   16000
#define JIANLU_VOICE_BYTES_PER_SEC (JIANLU_VOICE_SAMPLE_RATE * 2)  // 16bit 单声道
#define JIANLU_VOICE_MAX_SEC       15
#define JIANLU_VOICE_MAX_BYTES     (JIANLU_VOICE_MAX_SEC * JIANLU_VOICE_BYTES_PER_SEC)
// 短于 0.5s 视为误触,丢弃不上传
#define JIANLU_VOICE_MIN_BYTES     (JIANLU_VOICE_BYTES_PER_SEC / 2)
#define JIANLU_WAV_HEADER_LEN      44

typedef enum {
    JIANLU_VOICE_IDLE = 0,   // 无语音活动
    JIANLU_VOICE_RECORDING,  // 按住 OK 录音中
    JIANLU_VOICE_SENDING,    // 松开,上传中
    JIANLU_VOICE_CONFIRM,    // 上传成功,展示 transcript/reply
    JIANLU_VOICE_ERROR,      // 上传失败(文件保留待补传)
} jianlu_voice_state_t;

typedef enum {
    JIANLU_VOICE_EV_HOLD_START = 0,  // OK 长按触发,开始录音
    JIANLU_VOICE_EV_STOP,            // 松开或到达时长上限,录音结束
    JIANLU_VOICE_EV_SEND_OK,         // 上传成功
    JIANLU_VOICE_EV_SEND_FAIL,       // 上传失败
    JIANLU_VOICE_EV_DISMISS,         // 按键或超时关闭确认/错误页
} jianlu_voice_event_t;

typedef struct {
    jianlu_voice_state_t state;
    size_t recorded_bytes;   // RECORDING 中累计的 PCM 字节数
} jianlu_voice_t;

void jianlu_voice_init(jianlu_voice_t *voice);

// 状态机事件。返回 true 表示状态发生了变化,调用方应刷新 UI。
// HOLD_START:仅 IDLE 接受,进入 RECORDING 并清零计数。
// STOP:仅 RECORDING 接受;录音不足下限则直接回 IDLE(取消,调用方删文件),
//      否则进入 SENDING(调用方上传)。录音中达到上限时调用方先发 STOP。
// SEND_OK→CONFIRM,SEND_FAIL→ERROR(仅 SENDING 接受)。
// DISMISS:CONFIRM/ERROR → IDLE。
bool jianlu_voice_event(jianlu_voice_t *voice, jianlu_voice_event_t ev);

// 录音中累计字节:返回 true 表示已到上限,调用方应停止并按 STOP 处理。
bool jianlu_voice_reached_max(const jianlu_voice_t *voice);

// STOP 后是否值得上传(达到最短长度)。
bool jianlu_voice_worth_sending(const jianlu_voice_t *voice);

// 构建 44 字节 WAV 头(PCM,16kHz,16bit,单声道)。header 至少 44 字节。
// data_bytes 为 PCM 数据长度。字段值由本模块常量决定,便于 host 测试断言。
void jianlu_voice_wav_header(uint8_t *header, uint32_t data_bytes);

// 离线队列决策:上传失败后文件保留;下次联网时若存在 pending 文件应先补传。
// 这里是纯判断:state 为 ERROR 或有 pending 标记时允许补传。
bool jianlu_voice_should_retry_pending(bool pending_file_exists, bool wifi_connected);
