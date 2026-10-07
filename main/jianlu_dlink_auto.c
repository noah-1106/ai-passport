// main/jianlu_dlink_auto.c —— 见 jianlu_dlink_auto.h。
#include <stddef.h>

#include "jianlu_dlink_auto.h"

jianlu_auto_action_t jianlu_auto_decide(const jianlu_auto_input_t *in)
{
    if (in == NULL) return JIANLU_AUTO_STAY;
    if (in->busy) return JIANLU_AUTO_STAY;
    if (in->mode_dwell_s < JIANLU_AUTO_MIN_DWELL_S) return JIANLU_AUTO_STAY;

    if (!in->in_ble) {
        // Wi-Fi 态:中枢持续够不着 → 换更稳的 BLE 路
        if (in->hub_unreachable_s >= JIANLU_AUTO_HUB_FAIL_TO_BLE_S) {
            return JIANLU_AUTO_TO_BLE;
        }
        return JIANLU_AUTO_STAY;
    }

    // BLE 态:手动强制 → 常驻
    if (in->ble_locked) return JIANLU_AUTO_STAY;
    // 桥已连接 → 常驻(最省电最稳)
    if (in->ble_connected) return JIANLU_AUTO_STAY;
    // 空闲够久且有 Wi-Fi 凭据 → 回 Wi-Fi 试探(人可能挪了位置)
    if (in->ble_idle_s >= JIANLU_AUTO_BLE_IDLE_TO_WIFI_S && in->wifi_has_creds) {
        return JIANLU_AUTO_TO_WIFI;
    }
    return JIANLU_AUTO_STAY;
}

jianlu_sync_act_t jianlu_dlink_sync_decide(bool dlink_mode, bool ble_connected)
{
    if (!dlink_mode) return JIANLU_SYNC_WIFI;
    return ble_connected ? JIANLU_SYNC_REQ_BLE : JIANLU_SYNC_WAIT_BLE;
}
