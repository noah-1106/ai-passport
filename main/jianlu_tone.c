// main/jianlu_tone.c —— 见 jianlu_tone.h。
#include "jianlu_tone.h"

#include <math.h>
#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "jianlu_voice.h"   // JIANLU_VOICE_SAMPLE_RATE

static const char *TAG = "jianlu_tone";

#define SAMPLE_RATE   JIANLU_VOICE_SAMPLE_RATE   // 16000,与录音同格式免切换
#define SINE_STEPS    64
#define CHUNK_SAMPLES 512                        // 32ms 一块
#define VOLUME        55                         // 输出音量 %(留给 codec)
#define AMPLITUDE     9000                       // int16 峰值,留足余量防爆音

typedef struct {
    uint16_t freq;
    uint16_t ms;
} tone_note_t;

static const tone_note_t SEQ_REC_START[] = { {880, 80} };
static const tone_note_t SEQ_SUCCESS[]   = { {660, 80}, {0, 30}, {990, 110} };
static const tone_note_t SEQ_COMPLETE[]  = { {1320, 70} };
static const tone_note_t SEQ_FAIL[]      = { {220, 220} };

static int16_t s_sine[SINE_STEPS];
static bool s_sine_ready;
static bool s_audio_ready;
static QueueHandle_t s_queue;

static void sine_ensure(void)
{
    if (s_sine_ready) return;
    for (int i = 0; i < SINE_STEPS; i++) {
        s_sine[i] = (int16_t)(sinf(2.0f * 3.14159265f * (float)i / SINE_STEPS)
                              * AMPLITUDE);
    }
    s_sine_ready = true;
}

static esp_err_t audio_ensure(void)
{
    if (!s_audio_ready) {
        esp_err_t err = bsp_audio_init();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "音频初始化失败: %s", esp_err_to_name(err));
            return err;
        }
        s_audio_ready = true;
        bsp_audio_set_volume(VOLUME);
    }
    return bsp_audio_set_format(SAMPLE_RATE, 16, 1);
}

// 播放单音:相位累加查表;首尾 5ms 线性淡入淡出防爆音。
static void play_note(const tone_note_t *note)
{
    static int16_t chunk[CHUNK_SAMPLES];
    if (note->freq == 0) {           // 休止符
        vTaskDelay(pdMS_TO_TICKS(note->ms));
        return;
    }
    uint32_t total = (uint32_t)SAMPLE_RATE * note->ms / 1000;
    uint32_t fade = SAMPLE_RATE / 200;   // 5ms
    uint32_t phase = 0;              // 定点相位:步进 = freq * 2^16 * SINE_STEPS / SR
    uint32_t step = (uint32_t)note->freq * SINE_STEPS * 65536u / SAMPLE_RATE;
    uint32_t played = 0;
    while (played < total) {
        uint32_t n = total - played;
        if (n > CHUNK_SAMPLES) n = CHUNK_SAMPLES;
        for (uint32_t i = 0; i < n; i++) {
            int32_t s = s_sine[(phase >> 16) % SINE_STEPS];
            uint32_t idx = played + i;
            if (idx < fade) s = s * (int32_t)idx / (int32_t)fade;
            if (idx >= total - fade) s = s * (int32_t)(total - idx) / (int32_t)fade;
            chunk[i] = (int16_t)s;
            phase += step;
        }
        bsp_audio_write(chunk, n * sizeof(int16_t));
        played += n;
    }
}

static void play_seq(const tone_note_t *seq, size_t notes)
{
    if (audio_ensure() != ESP_OK) return;
    for (size_t i = 0; i < notes; i++) play_note(&seq[i]);
}

static void tone_task(void *arg)
{
    (void)arg;
    jianlu_tone_t tone;
    for (;;) {
        // 空闲等音符时不挂狗;播放期间挂狗
        if (xQueueReceive(s_queue, &tone, portMAX_DELAY) != pdTRUE) continue;
        esp_task_wdt_add(NULL);
        switch (tone) {
        case JIANLU_TONE_REC_START:
            play_seq(SEQ_REC_START, sizeof(SEQ_REC_START) / sizeof(tone_note_t));
            break;
        case JIANLU_TONE_SUCCESS:
            play_seq(SEQ_SUCCESS, sizeof(SEQ_SUCCESS) / sizeof(tone_note_t));
            break;
        case JIANLU_TONE_COMPLETE:
            play_seq(SEQ_COMPLETE, sizeof(SEQ_COMPLETE) / sizeof(tone_note_t));
            break;
        case JIANLU_TONE_FAIL:
            play_seq(SEQ_FAIL, sizeof(SEQ_FAIL) / sizeof(tone_note_t));
            break;
        }
        esp_task_wdt_reset();
        esp_task_wdt_delete(NULL);
    }
}

void jianlu_tone_play(jianlu_tone_t tone)
{
    if (!s_queue) {
        s_queue = xQueueCreate(2, sizeof(jianlu_tone_t));
        if (!s_queue) return;
        sine_ensure();
        if (xTaskCreate(tone_task, "jianlu_tone", 2048, NULL, 3, NULL) != pdPASS) {
            vQueueDelete(s_queue);
            s_queue = NULL;
            return;
        }
    }
    (void)xQueueSend(s_queue, &tone, 0);   // 满则丢弃
}
