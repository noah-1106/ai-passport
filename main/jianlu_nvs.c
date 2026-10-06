// main/jianlu_nvs.c —— 见 jianlu_nvs.h。
#include "jianlu_nvs.h"

#include <string.h>

#include "esp_log.h"
#include "jianlu_theme.h"
#include "nvs.h"

static const char *TAG = "jianlu_nvs";
static const char *NS = "jianlu";

static esp_err_t open(nvs_handle_t *out, nvs_open_mode_t mode)
{
    return nvs_open(NS, mode, out);
}

static void read_str(nvs_handle_t h, const char *key, char *dst, size_t len)
{
    dst[0] = '\0';
    size_t want = len;
    if (nvs_get_str(h, key, dst, &want) != ESP_OK) dst[0] = '\0';
}

esp_err_t jianlu_nvs_load(jianlu_nvs_data_t *data)
{
    memset(data, 0, sizeof(*data));
    nvs_handle_t h;
    esp_err_t err = open(&h, NVS_READONLY);
    if (err != ESP_OK) return err;
    read_str(h, "wifi_ssid", data->wifi_ssid, sizeof(data->wifi_ssid));
    read_str(h, "wifi_pass", data->wifi_pass, sizeof(data->wifi_pass));
    read_str(h, "hub_url", data->hub_url, sizeof(data->hub_url));
    uint8_t bright = 0;
    nvs_get_u8(h, "bright", &bright);
    data->brightness = bright;
    uint8_t keepon = 0;
    nvs_get_u8(h, "keepon", &keepon);
    data->keep_on = keepon;
    uint8_t theme = 0;
    nvs_get_u8(h, "theme", &theme);
    if (theme >= JIANLU_THEME_COUNT) theme = 0;   // 越界回默认
    data->theme = theme;
    nvs_get_u64(h, "av_ver", &data->avatar_ver);
    nvs_get_u64(h, "qr_ver", &data->qrcode_ver);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t jianlu_nvs_save_brightness(uint8_t pct);

static esp_err_t nvs_save_u8(const char *key, uint8_t val)
{
    nvs_handle_t h;
    esp_err_t err = open(&h, NVS_READWRITE);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(h, key, val);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t jianlu_nvs_save_brightness(uint8_t pct)
{
    return nvs_save_u8("bright", pct);
}

esp_err_t jianlu_nvs_save_keep_on(uint8_t on)
{
    return nvs_save_u8("keepon", on);
}

esp_err_t jianlu_nvs_save_theme(uint8_t theme)
{
    return nvs_save_u8("theme", theme);
}

esp_err_t jianlu_nvs_save_image_ver(bool avatar, uint64_t ver)
{
    nvs_handle_t h;
    esp_err_t err = open(&h, NVS_READWRITE);
    if (err != ESP_OK) return err;
    err = nvs_set_u64(h, avatar ? "av_ver" : "qr_ver", ver);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

static esp_err_t save_pair(const char *key1, const char *val1,
                           const char *key2, const char *val2,
                           size_t max1, size_t max2)
{
    if ((val1 && strlen(val1) >= max1) || (val2 && strlen(val2) >= max2)) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = open(&h, NVS_READWRITE);
    if (err != ESP_OK) return err;
    if (val1) err = nvs_set_str(h, key1, val1);
    if (err == ESP_OK && val2) err = nvs_set_str(h, key2, val2);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "写 %s 失败: %s", key1, esp_err_to_name(err));
    return err;
}

esp_err_t jianlu_nvs_save_wifi(const char *ssid, const char *pass)
{
    return save_pair("wifi_ssid", ssid, "wifi_pass", pass,
                     JIANLU_NVS_SSID_LEN, JIANLU_NVS_PASS_LEN);
}

esp_err_t jianlu_nvs_save_hub(const char *url)
{
    return save_pair("hub_url", url, NULL, NULL, JIANLU_HUB_URL_LEN, 0);
}

esp_err_t jianlu_nvs_clear_provisioning(void)
{
    nvs_handle_t h;
    esp_err_t err = open(&h, NVS_READWRITE);
    if (err != ESP_OK) return err;
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    nvs_erase_key(h, "hub_url");
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "配网信息已清除");
    return err;
}
