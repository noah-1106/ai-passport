// main/jianlu_provision.h —— BLUFI 配网(NimBLE):广播、收凭据、上报事件。
//
// 生命周期:进入配网态 jianlu_provision_start() → 手机 EspBlufi 连接并下发
// 凭据 → 回调 GOT_WIFI(回调在 btc/NimBLE 上下文,只能入队!）→ 应用存 NVS
// 并连接 → 拿到 IP 后 jianlu_provision_stop() 释放 NimBLE 内存。
// Wi-Fi 协议栈由应用统一管理,本模块只接管 BLE 部分。
#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef enum {
    JIANLU_PROV_BLE_CONNECT = 0,  // 手机已连上 BLE
    JIANLU_PROV_BLE_DISCONNECT,   // 手机断开
    JIANLU_PROV_GOT_WIFI,         // 收到完整 Wi-Fi 凭据(ssid/pass 参数有效)
    JIANLU_PROV_CUSTOM_DATA,      // 收到自定义数据(data 参数有效,可能含 hub=URL)
    JIANLU_PROV_FAILED,           // BLE 侧不可恢复错误
} jianlu_prov_ev_t;

// 全部参数仅在回调期间有效,需要留存必须拷贝。
typedef void (*jianlu_prov_cb_t)(jianlu_prov_ev_t ev, const char *ssid,
                                 const char *pass, const char *data, void *user);

// 启动 NimBLE + BLUFI profile 并开始广播。要求 Wi-Fi 已 esp_wifi_start。
// 重复调用返回 ESP_ERR_INVALID_STATE。
esp_err_t jianlu_provision_start(jianlu_prov_cb_t cb, void *user);

// 停止广播并释放 NimBLE。未启动时调用是空操作。
esp_err_t jianlu_provision_stop(void);

// 设备名 "BLUFI_XIAONUO_XXXX"(MAC 尾 2 字节),start 后有效。
const char *jianlu_provision_device_name(void);

// 向手机回报 Wi-Fi 连接结果(拿到 IP/连接失败时调用,未连接 BLE 时为空操作)。
void jianlu_provision_report_wifi(bool connected);

// 告知 Wi-Fi 协议栈状态:未初始化时 BLUFI 的 Wi-Fi 控制事件全部忽略
// (本应用配网/联网分离:配网态不起 Wi-Fi,拿到凭据存 NVS 后重启)。
void jianlu_provision_set_wifi_ready(bool ready);
