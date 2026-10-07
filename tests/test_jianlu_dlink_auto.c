// tests/test_jianlu_dlink_auto.c —— 自动切换决策矩阵的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_dlink_auto.h"

static jianlu_auto_input_t base_wifi(void)
{
    jianlu_auto_input_t in = {
        .in_ble = false,
        .ble_locked = false,
        .ble_connected = false,
        .wifi_has_creds = true,
        .busy = false,
        .mode_dwell_s = JIANLU_AUTO_MIN_DWELL_S,   // 默认过防抖
        .hub_unreachable_s = 0,
        .ble_idle_s = 0,
    };
    return in;
}

static void test_null_and_busy(void) {
    assert(jianlu_auto_decide(NULL) == JIANLU_AUTO_STAY);
    jianlu_auto_input_t in = base_wifi();
    in.hub_unreachable_s = 99999;   // 中枢失联再久
    in.busy = true;                 // 录音/上传中:绝不切
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);
}

static void test_dwell_debounce(void) {
    jianlu_auto_input_t in = base_wifi();
    in.hub_unreachable_s = JIANLU_AUTO_HUB_FAIL_TO_BLE_S;
    in.mode_dwell_s = JIANLU_AUTO_MIN_DWELL_S - 1;   // 驻留不足
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);
    in.mode_dwell_s = JIANLU_AUTO_MIN_DWELL_S;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_TO_BLE);
}

static void test_wifi_to_ble(void) {
    jianlu_auto_input_t in = base_wifi();
    in.hub_unreachable_s = JIANLU_AUTO_HUB_FAIL_TO_BLE_S - 1;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);
    in.hub_unreachable_s = JIANLU_AUTO_HUB_FAIL_TO_BLE_S;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_TO_BLE);
    in.hub_unreachable_s = JIANLU_AUTO_HUB_FAIL_TO_BLE_S + 60;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_TO_BLE);
    // 中枢可达(计数归零)→ 不切
    in.hub_unreachable_s = 0;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);
}

static void test_ble_stay_and_exit(void) {
    jianlu_auto_input_t in = base_wifi();
    in.in_ble = true;
    in.ble_idle_s = JIANLU_AUTO_BLE_IDLE_TO_WIFI_S + 1;

    // 手动强制 BLE:即使空闲再久也不退
    in.ble_locked = true;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);

    // 桥已连接:常驻
    in.ble_locked = false;
    in.ble_connected = true;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);

    // 空闲 + 有凭据 → 回 Wi-Fi
    in.ble_connected = false;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_TO_WIFI);

    // 空闲但无 Wi-Fi 凭据(纯 BLE 用户)→ 留在 BLE
    in.wifi_has_creds = false;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);

    // 有凭据但空闲未满 → 留在 BLE
    in.wifi_has_creds = true;
    in.ble_idle_s = JIANLU_AUTO_BLE_IDLE_TO_WIFI_S - 1;
    assert(jianlu_auto_decide(&in) == JIANLU_AUTO_STAY);
}

int main(void) {
    test_null_and_busy();
    test_dwell_debounce();
    test_wifi_to_ble();
    test_ble_stay_and_exit();
    return 0;
}
