// main/jianlu_capture.c —— 见 jianlu_capture.h。
#include "jianlu_capture.h"

#include <stdio.h>
#include <string.h>

#include "bsp_audio.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_task_wdt.h"
#include "jianlu_hub.h"
#include "jianlu_voice.h"

static const char *TAG = "jianlu_cap";

#define REC_PATH     "/voicefs/rec.wav"
#define PENDING_PATH "/voicefs/pending.wav"
#define HTTP_CHUNK   2048          // 上传分块
#define RESP_SIZE    (4 * 1024)    // capture 响应缓冲(transcript+reply+records)
#define CAPTURE_TIMEOUT_MS 30000   // ASR+LLM 在服务端,给足余量

static bool s_mounted;
static bool s_audio_ready;

// 录音与回放共用的 PCM 块(同一任务内串行,互不重叠)
static uint8_t s_pcm_chunk[JIANLU_REC_CHUNK_BYTES];

esp_err_t jianlu_capture_init(void)
{
    if (s_mounted) return ESP_OK;
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/voicefs",
        .partition_label = "voicefs",
        .max_files = 2,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "voicefs 挂载失败: %s", esp_err_to_name(err));
        return err;
    }
    s_mounted = true;
    size_t total = 0, used = 0;
    esp_spiffs_info("voicefs", &total, &used);
    ESP_LOGI(TAG, "voicefs 挂载: %u/%u 字节已用", (unsigned)used, (unsigned)total);
    return ESP_OK;
}

bool jianlu_capture_pending_exists(void)
{
    if (!s_mounted) return false;
    FILE *f = fopen(PENDING_PATH, "r");
    if (f == NULL) return false;
    // 校验 WAV 头:voicefs 区域首次使用可能挂着旧数据的"垃圾文件",
    // 不是 RIFF/WAVE 的直接清掉,避免把垃圾补传给中枢。
    char head[12] = { 0 };
    bool valid = fread(head, 1, sizeof(head), f) == sizeof(head)
              && memcmp(head, "RIFF", 4) == 0
              && memcmp(head + 8, "WAVE", 4) == 0;
    fclose(f);
    if (!valid) {
        ESP_LOGW(TAG, "pending 文件头无效,已删除(可能来自旧分区数据)");
        remove(PENDING_PATH);
        return false;
    }
    return true;
}

static esp_err_t audio_prepare(void)
{
    if (!s_audio_ready) {
        ESP_LOGD(TAG, "rec: bsp_audio_init 开始(堆 %u, 最大块 %u)",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size());
        esp_err_t err = bsp_audio_init();
        ESP_LOGD(TAG, "rec: bsp_audio_init → %s", esp_err_to_name(err));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "音频初始化失败: %s", esp_err_to_name(err));
            return err;
        }
        s_audio_ready = true;
    }
    // 录音任务独占音频,无并发格式切换;bsp_audio_set_format 同格式调用是廉价的。
    ESP_LOGD(TAG, "rec: set_format 开始");
    esp_err_t err = bsp_audio_set_format(JIANLU_VOICE_SAMPLE_RATE, 16, 1);
    ESP_LOGD(TAG, "rec: set_format → %s", esp_err_to_name(err));
    return err;
}

esp_err_t jianlu_capture_prepare_audio(void)
{
    return audio_prepare();
}

esp_err_t jianlu_capture_record(jianlu_rec_poll_t poll, void *user, size_t *pcm_bytes)
{
    *pcm_bytes = 0;
    ESP_LOGD(TAG, "rec: audio_prepare 前");
    esp_err_t err = audio_prepare();
    if (err != ESP_OK) return err;
    ESP_LOGD(TAG, "rec: 格式就绪,打开文件");

    FILE *f = fopen(REC_PATH, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "录音文件创建失败");
        return ESP_FAIL;
    }
    uint8_t header[JIANLU_WAV_HEADER_LEN];
    jianlu_voice_wav_header(header, 0);   // 占位,结束后回填
    if (fwrite(header, 1, sizeof(header), f) != sizeof(header)) {
        fclose(f);
        return ESP_FAIL;
    }

    size_t total = 0;
    bool stop = false;
    ESP_LOGD(TAG, "rec: 进入读循环");
    while (!stop && total < JIANLU_VOICE_MAX_BYTES) {
        err = bsp_audio_read(s_pcm_chunk, sizeof(s_pcm_chunk));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2S 读失败: %s", esp_err_to_name(err));
            break;
        }
        if (fwrite(s_pcm_chunk, 1, sizeof(s_pcm_chunk), f) != sizeof(s_pcm_chunk)) {
            ESP_LOGW(TAG, "录音写文件失败");
            err = ESP_FAIL;
            break;
        }
        total += sizeof(s_pcm_chunk);
        if ((total & 0xFFFF) == 0) ESP_LOGD(TAG, "rec: %u 字节", (unsigned)total);
        if (poll != NULL && !poll(user, s_pcm_chunk, sizeof(s_pcm_chunk))) stop = true;
    }
    fflush(f);

    if (err == ESP_OK && total > 0) {
        jianlu_voice_wav_header(header, (uint32_t)total);
        if (fseek(f, 0, SEEK_SET) == 0 &&
            fwrite(header, 1, sizeof(header), f) == sizeof(header)) {
            *pcm_bytes = total;
        } else {
            err = ESP_FAIL;
        }
    }
    fclose(f);
    ESP_LOGI(TAG, "录音结束: %u 字节 PCM%s", (unsigned)total,
             stop ? "(松开)" : (total >= JIANLU_VOICE_MAX_BYTES ? "(到上限)" : ""));
    return err;
}

void jianlu_capture_discard(void)
{
    remove(REC_PATH);
}

esp_err_t jianlu_capture_play_pending(volatile bool *stop_flag, size_t *played_bytes)
{
    if (played_bytes) *played_bytes = 0;
    esp_err_t err = audio_prepare();
    if (err != ESP_OK) return err;

    FILE *f = fopen(PENDING_PATH, "rb");
    if (f == NULL) return ESP_ERR_NOT_FOUND;
    // 跳过 WAV 头(本应用自产,固定 44 字节)
    if (fseek(f, JIANLU_WAV_HEADER_LEN, SEEK_SET) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    size_t played = 0;
    for (;;) {
        if (stop_flag != NULL && *stop_flag) break;
        size_t got = fread(s_pcm_chunk, 1, sizeof(s_pcm_chunk), f);
        if (got == 0) break;
        err = bsp_audio_write(s_pcm_chunk, got);
        esp_task_wdt_reset();   // 回放全程挂狗,逐块喂
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "回放写 I2S 失败: %s", esp_err_to_name(err));
            break;
        }
        played += got;
    }
    fclose(f);
    if (played_bytes) *played_bytes = played;
    ESP_LOGI(TAG, "回放结束: %u 字节%s", (unsigned)played,
             (stop_flag != NULL && *stop_flag) ? "(按键停止)" : "");
    return err;
}

static const char *upload_path(bool use_pending)
{
    return use_pending ? PENDING_PATH : REC_PATH;
}

esp_err_t jianlu_capture_upload(bool use_pending, jianlu_capture_result_t *result)
{
    if (jianlu_hub_base()[0] == '\0') return ESP_ERR_INVALID_STATE;
    const char *path = upload_path(use_pending);
    FILE *f = fopen(path, "rb");
    if (f == NULL) return ESP_ERR_NOT_FOUND;
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= JIANLU_WAV_HEADER_LEN) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    char capture_url[JIANLU_HUB_URL_LEN + 24];
    snprintf(capture_url, sizeof(capture_url), "%s/api/device/capture",
             jianlu_hub_base());
    esp_http_client_config_t config = {
        .url = capture_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CAPTURE_TIMEOUT_MS,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(client, "Content-Type", "audio/wav");

    size_t scratch_len = 0;
    uint8_t *scratch = jianlu_net_scratch(&scratch_len);   // 上传块与响应共用,时序不重叠
    if (scratch_len < RESP_SIZE + 16) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    char *resp = (char *)scratch;
    size_t resp_len = 0;
    ESP_LOGD(TAG, "up: open(len=%ld)", fsize);
    esp_err_t err = esp_http_client_open(client, fsize);
    ESP_LOGD(TAG, "up: open → %s", esp_err_to_name(err));
    if (err == ESP_OK) {
        size_t sent = 0;
        while (sent < (size_t)fsize) {
            size_t want = (size_t)fsize - sent;
            if (want > HTTP_CHUNK) want = HTTP_CHUNK;
            size_t got = fread(scratch, 1, want, f);
            if (got == 0) { err = ESP_FAIL; break; }
            int written = esp_http_client_write(client, (const char *)scratch, (int)got);
            ESP_LOGD(TAG, "up: chunk %u→%d", (unsigned)sent, written);
            if (written <= 0) { err = ESP_FAIL; break; }   // 0=超时未发,<=0 都视为失败
            sent += (size_t)written;
        }
        ESP_LOGD(TAG, "up: 发送 %u/%ld → %s", (unsigned)sent, fsize,
                 esp_err_to_name(err));
    }
    fclose(f);

    int status = 0;
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "up: 等待应答…");
        int64_t clen = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        ESP_LOGD(TAG, "up: HTTP %d (clen=%lld)", status, (long long)clen);
        if (clen > 0) {
            // esp_http_client_read 单次可能只给一部分,循环读满
            while (resp_len < RESP_SIZE - 1) {
                int r = esp_http_client_read(client, resp + resp_len,
                                             RESP_SIZE - 1 - resp_len);
                if (r <= 0) break;
                resp_len += (size_t)r;
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "上传失败: %s", esp_err_to_name(err));
        goto fail;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "上传失败: HTTP %d", status);
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }
    resp[resp_len] = '\0';
    if (jianlu_json_parse_capture(resp, resp_len, result) != 0) {
        ESP_LOGW(TAG, "capture 响应解析失败");
        err = ESP_ERR_INVALID_RESPONSE;
        goto fail;
    }
    ESP_LOGI(TAG, "上传成功: transcript=\"%s\" 新增 %d 条",
             result->transcript, result->new_count);
    return ESP_OK;

fail:
    // 传输层失败(连不上中枢)才保留 pending 待补传;
    // 中枢已应答的失败(如静音被 ASR 拒收的 502)重传无意义,直接删除。
    if (status == 0 && !use_pending) {
        rename(REC_PATH, PENDING_PATH);
    } else {
        remove(upload_path(use_pending));
    }
    return err;
}

void jianlu_capture_delete(bool use_pending)
{
    remove(upload_path(use_pending));
}
