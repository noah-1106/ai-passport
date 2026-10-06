// main/jianlu_hub.c —— 见 jianlu_hub.h。
#include "jianlu_hub.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "jianlu_json.h"
#include "jianlu_nvs.h"
#include "jianlu_timefmt.h"

static const char *TAG = "jianlu_hub";

// 9KB 静态缓冲:12 条记录(标题 96B + 摘要 128B + JSON 开销)正常 <7KB。
// 不走堆,避免和 Wi-Fi/LVGL 抢碎片;也是全网络任务的共享刮擦区。
#define HUB_BODY_SIZE (9 * 1024)
#define HUB_TIMEOUT_MS 8000

static char s_body[HUB_BODY_SIZE];
static size_t s_body_len;
static bool s_body_overflow;
static char s_base[JIANLU_HUB_URL_LEN];
static char s_resp_date[40];   // 响应 Date 头(对时用)

uint8_t *jianlu_net_scratch(size_t *len)
{
    *len = HUB_BODY_SIZE;
    return (uint8_t *)s_body;
}

void jianlu_hub_set_base(const char *url)
{
    jianlu_utf8_copy(s_base, sizeof(s_base), url, sizeof(s_base) - 1);
}

const char *jianlu_hub_base(void)
{
    return s_base;
}

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->header_key != NULL &&
        strcasecmp(evt->header_key, "Date") == 0 && evt->header_value != NULL) {
        jianlu_utf8_copy(s_resp_date, sizeof(s_resp_date), evt->header_value,
                         sizeof(s_resp_date) - 1);
        return ESP_OK;
    }
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->data == NULL) return ESP_OK;
    if (s_body_len + (size_t)evt->data_len > sizeof(s_body)) {
        s_body_overflow = true;
        return ESP_FAIL;
    }
    memcpy(s_body + s_body_len, evt->data, (size_t)evt->data_len);
    s_body_len += (size_t)evt->data_len;
    return ESP_OK;
}

static void set_err(char *errbuf, size_t errbuf_len, const char *msg)
{
    if (errbuf != NULL && errbuf_len > 0) {
        jianlu_utf8_copy(errbuf, errbuf_len, msg, errbuf_len - 1);
    }
}

esp_err_t jianlu_hub_fetch(jianlu_store_t *store, char *errbuf, size_t errbuf_len)
{
    if (s_base[0] == '\0') {
        set_err(errbuf, errbuf_len, "中枢地址未设置");
        return ESP_ERR_INVALID_STATE;
    }
    char url[160];
    int n = snprintf(url, sizeof(url), "%s/api/records?status=pending&pageSize=%d",
                     s_base, JIANLU_MAX_RECORDS);
    if (n <= 0 || (size_t)n >= sizeof(url)) {
        set_err(errbuf, errbuf_len, "中枢地址过长");
        return ESP_ERR_INVALID_ARG;
    }

    s_body_len = 0;
    s_body_overflow = false;
    s_resp_date[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_http_event,
        .timeout_ms = HUB_TIMEOUT_MS,
        .buffer_size = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        set_err(errbuf, errbuf_len, "HTTP 初始化失败");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "拉取失败: %s", esp_err_to_name(err));
        set_err(errbuf, errbuf_len,
                s_body_overflow ? "响应过大" : "连不上中枢");
        return s_body_overflow ? ESP_ERR_INVALID_SIZE : err;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "拉取失败: HTTP %d", status);
        set_err(errbuf, errbuf_len, "中枢返回错误");
        return ESP_ERR_INVALID_RESPONSE;
    }

    int count = jianlu_json_parse_records(s_body, s_body_len, store);
    if (count < 0) {
        ESP_LOGW(TAG, "JSON 解析失败(%u 字节)", (unsigned)s_body_len);
        set_err(errbuf, errbuf_len, "数据格式不对");
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "拉到 %d 条简录", count);
    // 顺手对时:中枢 Date 头 → 本机时间(录音时间戳用)
    if (s_resp_date[0] != '\0') {
        uint32_t epoch = jianlu_time_parse_http_date(s_resp_date);
        if (jianlu_time_is_valid(epoch)) {
            struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "已对时: %s", s_resp_date);
        }
    }
    return ESP_OK;
}

esp_err_t jianlu_hub_complete(const char *id)
{    if (s_base[0] == '\0') return ESP_ERR_INVALID_STATE;
    char url[192];
    int n = snprintf(url, sizeof(url), "%s/api/records/%s", s_base, id);
    if (n <= 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_ARG;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = HUB_TIMEOUT_MS,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;

    static const char BODY[] = "{\"status\":\"completed\"}";
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, BODY, (int)(sizeof(BODY) - 1));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "完成上报失败 id=%s: %s", id, esp_err_to_name(err));
        return err;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGW(TAG, "完成上报失败 id=%s: HTTP %d", id, status);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t jianlu_hub_fetch_profile(jianlu_profile_t *out)
{
    if (s_base[0] == '\0') return ESP_ERR_INVALID_STATE;
    char url[160];
    int n = snprintf(url, sizeof(url), "%s/api/profile", s_base);
    if (n <= 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_ARG;

    s_body_len = 0;
    s_body_overflow = false;
    s_resp_date[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_http_event,
        .timeout_ms = HUB_TIMEOUT_MS,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "资料拉取失败: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) return ESP_ERR_INVALID_RESPONSE;
    if (jianlu_json_parse_profile(s_body, s_body_len, out) != 0) {
        ESP_LOGW(TAG, "资料 JSON 解析失败");
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "资料: \"%s\" avatar=%d qr=%d", out->nickname,
             (int)out->has_avatar, (int)out->has_qrcode);
    return ESP_OK;
}

bool jianlu_hub_cache_exists(const char *file_path)
{
    FILE *f = fopen(file_path, "rb");
    if (f == NULL) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size > 0;
}

esp_err_t jianlu_hub_download_file(const char *api_path, const char *file_path)
{
    if (s_base[0] == '\0') return ESP_ERR_INVALID_STATE;
    char url[192];
    int n = snprintf(url, sizeof(url), "%s%s", s_base, api_path);
    if (n <= 0 || (size_t)n >= sizeof(url)) return ESP_ERR_INVALID_ARG;

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HUB_TIMEOUT_MS,
        .buffer_size = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;

    size_t scratch_len = 0;
    uint8_t *chunk = jianlu_net_scratch(&scratch_len);
    if (scratch_len < 2048) {
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    int status = 0;
    FILE *f = NULL;
    size_t got_total = 0;
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status != 200) {
            err = ESP_ERR_NOT_FOUND;
        } else {
            f = fopen(file_path, "wb");
            if (f == NULL) {
                err = ESP_FAIL;
            } else {
                for (;;) {
                    int r = esp_http_client_read(client, (char *)chunk, 2048);
                    if (r < 0) { err = ESP_FAIL; break; }
                    if (r == 0) break;
                    if (fwrite(chunk, 1, (size_t)r, f) != (size_t)r) {
                        err = ESP_FAIL;
                        break;
                    }
                    got_total += (size_t)r;
                }
                fclose(f);
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "下载失败 %s: %s", api_path, esp_err_to_name(err));
        if (f != NULL || got_total > 0) remove(file_path);   // 半成品不留
        if (f == NULL && got_total == 0 && err == ESP_ERR_NOT_FOUND) {
            remove(file_path);   // 404:清掉过期缓存,避免误用
        }
        return err;
    }
    ESP_LOGI(TAG, "下载完成 %s → %s(%u 字节)", api_path, file_path,
             (unsigned)got_total);
    return ESP_OK;
}

