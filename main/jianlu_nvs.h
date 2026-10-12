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
    uint8_t brightness;      // 25/50/75/100;0=未设置
    uint8_t keep_on;         // 屏幕常亮 0/1
    uint8_t theme;           // 色彩主题 0..3;0=墨夜(默认)
    uint64_t avatar_ver;     // 已缓存头像的中枢版本(0=未缓存)
    uint64_t qrcode_ver;
    uint8_t reprov;          // 强制配网标志:置位后开机必进配网态(即使有凭据)
    uint8_t dmode;           // 当前处于 BLE 直连态(自动引擎也会写;cmode 语义见 jianlu_config.h)
    uint8_t dlock;           // 废弃:旧版"手动强制 BLE"锁,仅用于迁移到 cmode
    uint8_t cmode;           // 连接方式 0=自动 1=Wi-Fi 中枢 2=蓝牙直连(jianlu_cmode_t)
    uint8_t cmode_pend;      // 待验证切换:回滚目标档位(0xFF=无;Wi-Fi→蓝牙须重启验证)
    uint8_t cmode_fail;      // 回滚后待展示的失败原因码(0=无;进 Wi-Fi 态提示后清零)
} jianlu_nvs_data_t;

// 读取全部已知 key 到 data(缺失的 key 置空串)。NVS 未初始化返回错误。
esp_err_t jianlu_nvs_load(jianlu_nvs_data_t *data);

// 单项写入(立即提交)。len 超限返回 ESP_ERR_INVALID_ARG,不截断写坏数据。
esp_err_t jianlu_nvs_save_wifi(const char *ssid, const char *pass);
esp_err_t jianlu_nvs_save_hub(const char *url);
esp_err_t jianlu_nvs_save_brightness(uint8_t pct);
esp_err_t jianlu_nvs_save_keep_on(uint8_t on);
esp_err_t jianlu_nvs_save_theme(uint8_t theme);
// 图片缓存版本记录(下载成功后写新版本;失效时写 0)
esp_err_t jianlu_nvs_save_image_ver(bool avatar, uint64_t ver);

// 重配:擦除 wifi_ssid/wifi_pass/hub_url 三个 key(其余 NVS 内容不动)。
esp_err_t jianlu_nvs_clear_provisioning(void);
// 强制配网标志(重配确认时置 1;BLUFI 收到新凭据时清 0)。
// 语义:重配 = 强制走配网,即使 Kconfig/NVS 还有旧凭据。
esp_err_t jianlu_nvs_save_reprov(uint8_t on);
// BLE 直连态标志(设置页切换/自动引擎写入,重启生效)
esp_err_t jianlu_nvs_save_dmode(uint8_t on);
// 连接方式(0=自动 1=Wi-Fi 2=蓝牙直连;设置页三档,重启生效)
esp_err_t jianlu_nvs_save_cmode(uint8_t cmode);
// 待验证切换的回滚档位(0xFF=无)与回滚后的失败原因码(0=无)
esp_err_t jianlu_nvs_save_cmode_pend(uint8_t pend);
esp_err_t jianlu_nvs_save_cmode_fail(uint8_t reason);

// 配网成功落盘:凭据写入 + 强制标志清除,同一事务(单次 commit)。
// 修复"两笔独立提交间掉电/失败 → 有凭据但标志在位 → 永远强制配网"。
esp_err_t jianlu_nvs_save_provisioned(const char *ssid, const char *pass);
