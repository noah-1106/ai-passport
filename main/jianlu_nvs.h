// main/jianlu_nvs.h —— 配网数据 NVS 持久化(命名空间 "jianlu")。
//
// 布局:key 全部为字符串
//   "wifi_ssid"  BLUFI 配网写入的 SSID
//   "wifi_pass"  对应密码
//   "hub_url"    mDNS 发现/BLUFI custom data 写入的中枢地址缓存
// 凭据不进固件;Kconfig 只是开发期后备(优先级见 jianlu_config.h)。
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#define JIANLU_NVS_SSID_LEN  33
#define JIANLU_NVS_PASS_LEN  65
#define JIANLU_HUB_URL_LEN   128

typedef struct {
    char wifi_ssid[JIANLU_NVS_SSID_LEN];
    char wifi_pass[JIANLU_NVS_PASS_LEN];
    char hub_url[JIANLU_HUB_URL_LEN];
} jianlu_nvs_data_t;

// 读取全部已知 key 到 data(缺失的 key 置空串)。NVS 未初始化返回错误。
esp_err_t jianlu_nvs_load(jianlu_nvs_data_t *data);

// 单项写入(立即提交)。len 超限返回 ESP_ERR_INVALID_ARG,不截断写坏数据。
esp_err_t jianlu_nvs_save_wifi(const char *ssid, const char *pass);
esp_err_t jianlu_nvs_save_hub(const char *url);

// 重配:擦除 wifi_ssid/wifi_pass/hub_url 三个 key(其余 NVS 内容不动)。
esp_err_t jianlu_nvs_clear_provisioning(void);
