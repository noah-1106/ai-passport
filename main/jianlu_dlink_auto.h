// main/jianlu_dlink_auto.h —— 双模式自动切换决策(纯 C,host 可测)。
//
// 语义(用户拍板 A 方案,BLE 优先):
//   有 Wi-Fi 凭据开机默认 Wi-Fi(现状全保留);
//   Wi-Fi 态中枢连续不可达 ≥2 分钟(且空闲)→ 自动转 BLE;
//   BLE 态桥已连接 → 常驻;BLE 空闲(无连接)≥5 分钟且有 Wi-Fi 凭据 → 回 Wi-Fi;
//   手动「直连模式」= 强制 BLE 常驻(ble_locked),自动退出被禁止;
//   每模式最短驻留 5 分钟(防抖);录音/上传等忙时不切换。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define JIANLU_AUTO_HUB_FAIL_TO_BLE_S  120   // Wi-Fi 态中枢不可达→BLE
#define JIANLU_AUTO_BLE_IDLE_TO_WIFI_S 300   // BLE 空闲→Wi-Fi(试新位置)
#define JIANLU_AUTO_MIN_DWELL_S        300   // 防抖:模式最短驻留

typedef enum {
    JIANLU_AUTO_STAY = 0,   // 保持当前模式
    JIANLU_AUTO_TO_BLE,     // 重启进 BLE 直连
    JIANLU_AUTO_TO_WIFI,    // 重启回 Wi-Fi
} jianlu_auto_action_t;

typedef struct {
    bool in_ble;            // 当前 BLE 模式
    bool ble_locked;        // 手动强制 BLE(设置页开启)
    bool ble_connected;     // BLE 态:桥已连接
    bool wifi_has_creds;    // 存有 Wi-Fi 凭据
    bool busy;              // 录音/上传/同步进行中
    uint32_t mode_dwell_s;  // 当前模式已驻留秒数
    // Wi-Fi 态:Wi-Fi 关联正常但中枢连续不可达的秒数(用户活动清零另计)
    uint32_t hub_unreachable_s;
    // BLE 态:桥未连接的连续秒数
    uint32_t ble_idle_s;
} jianlu_auto_input_t;

jianlu_auto_action_t jianlu_auto_decide(const jianlu_auto_input_t *in);
