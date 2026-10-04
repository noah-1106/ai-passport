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
#include "jianlu_timefmt.h"
#include "jianlu_voice.h"
#include "jianlu_voiceq.h"
#include <sys/time.h>

static const char *TAG = "jianlu_cap";

#define HTTP_CHUNK   2048          // 上传分块
#define RESP_SIZE    (4 * 1024)    // capture 响应缓冲(transcript+reply+records)
#define CAPTURE_TIMEOUT_MS 30000   // ASR+LLM 在服务端,给足余量
#define QUEUE_LOW_KB 96            // 分区剩余低于此值视为队列满(约 3s 录音余量)
#define OLD_PENDING_PATH "/voicefs/pending.wav"

static bool s_mounted;
static bool s_audio_ready;
static jianlu_voiceq_t s_queue;

// 录音与回放共用的 PCM 块(同一任务内串行,互不重叠)
static uint8_t s_pcm_chunk[JIANLU_REC_CHUNK_BYTES];

static void slot_path(int slot, char *buf, size_t len)
{
    snprintf(buf, len, "/voicefs/p%d.wav", slot);
}

// 校验文件是有效 WAV(分区首次使用可能挂着旧数据的垃圾)
static bool wav_valid(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return false;
    char head[12] = { 0 };
    bool ok = fread(head, 1, sizeof(head), f) == sizeof(head)
           && memcmp(head, "RIFF", 4) == 0
           && memcmp(head + 8, "WAVE", 4) == 0;
    fclose(f);
    return ok;
}

// 删除指定槽并把后面的槽下移,保持连续(时间戳同步移动,元数据落盘)
static void save_meta(void);

static void delete_and_shift(int slot)
{
    char from[24], to[24];
    slot_path(slot, to, sizeof(to));
    remove(to);
    for (int i = slot; i < JIANLU_VOICEQ_SLOTS; i++) {
        slot_path(i + 1, from, sizeof(from));
        slot_path(i, to, sizeof(to));
        FILE *probe = fopen(from, "r");
        if (probe != NULL) {
            fclose(probe);
            rename(from, to);
        }
    }
    jianlu_voiceq_remove_at(&s_queue, slot);
    save_meta();
}

#define META_PATH "/voicefs/vq.meta"

static bool s_rebuilding;

static void save_meta(void)
{
    if (s_rebuilding) return;
    static uint8_t buf[8 + JIANLU_VOICEQ_SLOTS * 4];
    size_t n = jianlu_voiceq_encode(&s_queue, buf, sizeof(buf));
    if (n == 0) return;
    FILE *f = fopen(META_PATH, "wb");
    if (f == NULL) return;
    fwrite(buf, 1, n, f);
    fclose(f);
}

static void load_meta(void)
{
    static uint8_t buf[8 + JIANLU_VOICEQ_SLOTS * 4];
    FILE *f = fopen(META_PATH, "rb");
    if (f == NULL) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && (size_t)size <= sizeof(buf) &&
        fread(buf, 1, (size_t)size, f) == (size_t)size) {
        jianlu_voiceq_t meta = { 0 };
        if (jianlu_voiceq_decode(buf, (size_t)size, &meta) &&
            meta.count <= s_queue.count) {
            // 以磁盘文件为准,时间戳尽量对齐(数量不一致时保留前 N 个)
            memcpy(s_queue.ts, meta.ts,
                   (size_t)(meta.count < s_queue.count ? meta.count
                                                       : s_queue.count) * sizeof(uint32_t));
        }
    }
    fclose(f);
}

// 从磁盘重建队列深度(p1..pN 连续即 N;坏头文件顺带清理)
static void count_from_disk(void)
{
    s_rebuilding = true;
    jianlu_voiceq_init(&s_queue);
    char path[24];
    for (int i = 1; i <= JIANLU_VOICEQ_SLOTS; i++) {
        slot_path(i, path, sizeof(path));
        FILE *f = fopen(path, "r");
        if (f == NULL) break;
        fclose(f);
        if (!wav_valid(path)) {
            ESP_LOGW(TAG, "%s 头无效,清理", path);
            delete_and_shift(i);
            i--;   // 下移后同槽位是新文件,重查
            continue;
        }
        s_queue.count = i;
    }
    s_rebuilding = false;
    load_meta();
}

// 迁移旧版单槽 pending.wav → p1.wav
static void migrate_legacy_pending(void)
{
    FILE *f = fopen(OLD_PENDING_PATH, "r");
    if (f == NULL) return;
    fclose(f);
    char p1[24];
    slot_path(1, p1, sizeof(p1));
    FILE *probe = fopen(p1, "r");
    if (probe != NULL) {
        fclose(probe);
        ESP_LOGW(TAG, "旧 pending.wav 与 p1 并存,删除旧文件");
        remove(OLD_PENDING_PATH);
        return;
    }
    if (!wav_valid(OLD_PENDING_PATH)) {
        ESP_LOGW(TAG, "旧 pending.wav 头无效,删除");
        remove(OLD_PENDING_PATH);
        return;
    }
    if (rename(OLD_PENDING_PATH, p1) == 0) {
        ESP_LOGI(TAG, "旧版 pending.wav 已迁移为 p1.wav");
    }
}

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
    migrate_legacy_pending();
    count_from_disk();
    return ESP_OK;
}

int jianlu_capture_queue_count(void)
{
    return s_queue.count;
}

// 槽位录音时刻(0=未对时/未知)
uint32_t jianlu_capture_slot_ts(int slot)
{
    if (slot < 1 || slot > s_queue.count) return 0;
    return s_queue.ts[slot - 1];
}

bool jianlu_capture_queue_full(void)
{
    if (!s_mounted) return false;
    // 不设条数上限:只按剩余空间管理(不足约 3s 录音即视为满)
    size_t total = 0, used = 0;
    if (esp_spiffs_info("voicefs", &total, &used) != ESP_OK) return true;
    size_t free_b = total > used ? total - used : 0;
    return free_b < (size_t)QUEUE_LOW_KB * 1024;
}

static esp_err_t audio_prepare(void)
{
    if (!s_audio_ready) {
        esp_err_t err = bsp_audio_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "音频初始化失败: %s", esp_err_to_name(err));
            return err;
        }
        s_audio_ready = true;
    }
    // 录音任务独占音频,无并发格式切换;bsp_audio_set_format 同格式调用是廉价的。
    return bsp_audio_set_format(JIANLU_VOICE_SAMPLE_RATE, 16, 1);
}

esp_err_t jianlu_capture_prepare_audio(void)
{
    return audio_prepare();
}

esp_err_t jianlu_capture_record(jianlu_rec_poll_t poll, void *user, size_t *pcm_bytes)
{
    *pcm_bytes = 0;
    if (jianlu_capture_queue_full()) {
        ESP_LOGW(TAG, "队列已满,拒绝录音");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = audio_prepare();
    if (err != ESP_OK) return err;

    char path[24];
    slot_path(s_queue.count + 1, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "录音文件创建失败: %s", path);
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
    if (err == ESP_OK && total > 0) {
        // 录音时刻(未对过时为 0,占位卡回退显示"待同步")
        time_t now = time(NULL);
        uint32_t ts = jianlu_time_is_valid((uint32_t)now) ? (uint32_t)now : 0;
        jianlu_voiceq_push(&s_queue, ts);
        save_meta();
        ESP_LOGI(TAG, "录音结束: %u 字节 PCM 入槽(队列 %d)%s", (unsigned)total,
                 s_queue.count, total >= JIANLU_VOICE_MAX_BYTES ? "(到上限)" : "");
    } else {
        remove(path);   // 失败/空录音:清掉半成品,不占槽
    }
    return err;
}

void jianlu_capture_discard_tail(void)
{
    if (s_queue.count > 0) {
        ESP_LOGI(TAG, "丢弃队尾录音(队列 %d/%d)", s_queue.count, JIANLU_VOICEQ_SLOTS);
        delete_and_shift(s_queue.count);
    }
}

esp_err_t jianlu_capture_play_slot(int slot, volatile bool *stop_flag,
                                   size_t *played_bytes)
{
    if (played_bytes) *played_bytes = 0;
    esp_err_t err = audio_prepare();
    if (err != ESP_OK) return err;

    char path[24];
    slot_path(slot, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f == NULL) return ESP_ERR_NOT_FOUND;
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
    ESP_LOGI(TAG, "回放结束: 槽 %d %u 字节%s", slot, (unsigned)played,
             (stop_flag != NULL && *stop_flag) ? "(按键停止)" : "");
    return err;
}

esp_err_t jianlu_capture_upload_head(jianlu_capture_result_t *result)
{
    if (jianlu_hub_base()[0] == '\0') return ESP_ERR_INVALID_STATE;
    if (jianlu_voiceq_head(&s_queue) < 0) return ESP_ERR_NOT_FOUND;

    char path[24];
    slot_path(1, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        // 与磁盘脱节的兜底:对齐计数后按空队列报
        count_from_disk();
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= JIANLU_WAV_HEADER_LEN) {
        fclose(f);
        delete_and_shift(1);   // 坏文件清掉,可继续下一条
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
        esp_http_client_cleanup(client);
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

    if (err == ESP_OK && status == 200) {
        // 先删档再解析:任何后续崩溃都不会导致同一条录音重复上传(幂等)
        delete_and_shift(1);
        resp[resp_len] = '\0';
        if (jianlu_json_parse_capture(resp, resp_len, result) == 0) {
            ESP_LOGI(TAG, "上传成功: transcript=\"%s\" 新增 %d 条(队列剩 %d)",
                     result->transcript, result->new_count, s_queue.count);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "capture 响应解析失败");
        err = ESP_ERR_INVALID_RESPONSE;
    } else if (err == ESP_OK) {
        ESP_LOGW(TAG, "上传失败: HTTP %d", status);
        err = ESP_ERR_INVALID_RESPONSE;
    } else {
        ESP_LOGW(TAG, "上传失败: %s", esp_err_to_name(err));
    }

    // 传输层失败(连不上中枢)才保留队头,应停止本轮补传;
    // 中枢已应答的失败(拒收/解析失败)重传无意义,删除并可继续下一条。
    if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_INVALID_SIZE) {
        delete_and_shift(1);
    }
    return err;
}
