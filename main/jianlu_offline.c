// main/jianlu_offline.c —— 见 jianlu_offline.h。
#include "jianlu_offline.h"

#include <stdio.h>

#include "esp_log.h"

static const char *TAG = "jianlu_off";

#define SNAP_PATH  "/voicefs/list.snap"
#define SYNCQ_PATH "/voicefs/sync.q"
#define SNAP_BUF   (sizeof(jianlu_record_t) * JIANLU_MAX_RECORDS + 16)
#define SYNCQ_BUF  (JIANLU_ID_LEN * JIANLU_SYNCQ_MAX + 16)

// 读写共用一块静态缓冲(省 6.8KB .bss):调用方都在应用/网络任务串行路径上。
static uint8_t s_snap_buf[SNAP_BUF];

esp_err_t jianlu_offline_save_snapshot(const jianlu_store_t *store)
{
    size_t n = jianlu_snapshot_encode(store, s_snap_buf, sizeof(s_snap_buf));
    if (n == 0) return ESP_ERR_INVALID_SIZE;
    FILE *f = fopen(SNAP_PATH, "wb");
    if (f == NULL) return ESP_FAIL;
    size_t w = fwrite(s_snap_buf, 1, n, f);
    fclose(f);
    if (w != n) return ESP_FAIL;
    ESP_LOGI(TAG, "快照已保存(%d 条)", store->count);
    return ESP_OK;
}

bool jianlu_offline_load_snapshot(jianlu_store_t *store)
{
    FILE *f = fopen(SNAP_PATH, "rb");
    if (f == NULL) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = false;
    if (size > 0 && (size_t)size <= sizeof(s_snap_buf) &&
        fread(s_snap_buf, 1, (size_t)size, f) == (size_t)size) {
        ok = jianlu_snapshot_decode(s_snap_buf, (size_t)size, store);
    }
    fclose(f);
    if (ok) {
        ESP_LOGI(TAG, "快照已加载(%d 条,离线模式)", store->count);
    } else {
        ESP_LOGW(TAG, "快照缺失或损坏");
    }
    return ok;
}

esp_err_t jianlu_offline_load_syncq(jianlu_syncq_t *q)
{
    jianlu_syncq_init(q);
    static uint8_t buf[SYNCQ_BUF];
    FILE *f = fopen(SYNCQ_PATH, "rb");
    if (f == NULL) return ESP_OK;   // 无文件 = 空队列
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size > 0 && (size_t)size <= sizeof(buf) &&
        fread(buf, 1, (size_t)size, f) == (size_t)size) {
        jianlu_syncq_decode(buf, (size_t)size, q);
    }
    fclose(f);
    return ESP_OK;
}

esp_err_t jianlu_offline_save_syncq(const jianlu_syncq_t *q)
{
    static uint8_t buf[SYNCQ_BUF];
    size_t n = jianlu_syncq_encode(q, buf, sizeof(buf));
    if (n == 0) return ESP_ERR_INVALID_SIZE;
    FILE *f = fopen(SYNCQ_PATH, "wb");
    if (f == NULL) return ESP_FAIL;
    size_t w = fwrite(buf, 1, n, f);
    fclose(f);
    return w == n ? ESP_OK : ESP_FAIL;
}
