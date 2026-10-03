// main/jianlu_hub.c —— 见 jianlu_hub.h。
#include "jianlu_hub.h"

#include <stdio.h>
#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "jianlu_json.h"

static const char *TAG = "jianlu_hub";

// 16KB 静态响应缓冲:20 条记录(标题 96B + 摘要 128B + JSON 开销)正常 <10KB。
// 不走堆,避免和 Wi-Fi/LVGL 抢碎片。
#define HUB_BODY_SIZE (16 * 1024)
#define HUB_TIMEOUT_MS 8000

static char s_body[HUB_BODY_SIZE];
static size_t s_body_len;
static bool s_body_overflow;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
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
    char url[160];
    int n = snprintf(url, sizeof(url), "%s/api/records?status=pending&pageSize=%d",
                     CONFIG_XIAONUO_HUB_URL, JIANLU_MAX_RECORDS);
    if (n <= 0 || (size_t)n >= sizeof(url)) {
        set_err(errbuf, errbuf_len, "中枢地址过长");
        return ESP_ERR_INVALID_ARG;
    }

    s_body_len = 0;
    s_body_overflow = false;

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
    return ESP_OK;
}

esp_err_t jianlu_hub_complete(const char *id)
{
    char url[192];
    int n = snprintf(url, sizeof(url), "%s/api/records/%s",
                     CONFIG_XIAONUO_HUB_URL, id);
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
