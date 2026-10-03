// main/jianlu_voice.c —— 见 jianlu_voice.h。纯 C,host 侧测试覆盖。
#include "jianlu_voice.h"

#include <string.h>

void jianlu_voice_init(jianlu_voice_t *voice)
{
    memset(voice, 0, sizeof(*voice));
    voice->state = JIANLU_VOICE_IDLE;
}

bool jianlu_voice_event(jianlu_voice_t *voice, jianlu_voice_event_t ev)
{
    switch (ev) {
    case JIANLU_VOICE_EV_HOLD_START:
        if (voice->state != JIANLU_VOICE_IDLE) return false;
        voice->state = JIANLU_VOICE_RECORDING;
        voice->recorded_bytes = 0;
        return true;
    case JIANLU_VOICE_EV_STOP:
        if (voice->state != JIANLU_VOICE_RECORDING) return false;
        voice->state = jianlu_voice_worth_sending(voice)
                     ? JIANLU_VOICE_SENDING : JIANLU_VOICE_IDLE;
        return true;
    case JIANLU_VOICE_EV_SEND_OK:
        if (voice->state != JIANLU_VOICE_SENDING) return false;
        voice->state = JIANLU_VOICE_CONFIRM;
        return true;
    case JIANLU_VOICE_EV_SEND_FAIL:
        if (voice->state != JIANLU_VOICE_SENDING) return false;
        voice->state = JIANLU_VOICE_ERROR;
        return true;
    case JIANLU_VOICE_EV_DISMISS:
        if (voice->state != JIANLU_VOICE_CONFIRM && voice->state != JIANLU_VOICE_ERROR) {
            return false;
        }
        voice->state = JIANLU_VOICE_IDLE;
        voice->recorded_bytes = 0;
        return true;
    }
    return false;
}

bool jianlu_voice_reached_max(const jianlu_voice_t *voice)
{
    return voice->recorded_bytes >= JIANLU_VOICE_MAX_BYTES;
}

bool jianlu_voice_worth_sending(const jianlu_voice_t *voice)
{
    return voice->recorded_bytes >= JIANLU_VOICE_MIN_BYTES;
}

static void put_u32le(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)((v >> 16) & 0xFF);
    dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

void jianlu_voice_wav_header(uint8_t *header, uint32_t data_bytes)
{
    static const uint8_t tmpl[JIANLU_WAV_HEADER_LEN] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E',
        'f', 'm', 't', ' ', 16, 0, 0, 0,   // fmt 块固定 16 字节
        1, 0,                              // PCM
        1, 0,                              // 单声道
        0, 0, 0, 0,                        // 采样率(下面填)
        0, 0, 0, 0,                        // 字节率(下面填)
        2, 0,                              // block align = 16bit x 1ch
        16, 0,                             // 位深
        'd', 'a', 't', 'a', 0, 0, 0, 0,
    };
    memcpy(header, tmpl, JIANLU_WAV_HEADER_LEN);
    put_u32le(header + 4, 36 + data_bytes);                            // RIFF 总长度
    put_u32le(header + 24, JIANLU_VOICE_SAMPLE_RATE);
    put_u32le(header + 28, JIANLU_VOICE_BYTES_PER_SEC);                // 字节率
    put_u32le(header + 40, data_bytes);                                // data 长度
}

bool jianlu_voice_should_retry_pending(bool pending_file_exists, bool wifi_connected)
{
    return pending_file_exists && wifi_connected;
}
