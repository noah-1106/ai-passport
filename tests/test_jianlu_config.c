// tests/test_jianlu_config.c —— 凭据/中枢来源优先级的 host 侧测试。
#include <assert.h>
#include <stddef.h>

#include "jianlu_config.h"

int main(void) {
    // Wi-Fi: NVS > Kconfig > 无
    assert(jianlu_config_pick_wifi("HomeWiFi", "DevSSID") == JIANLU_WIFI_NVS);
    assert(jianlu_config_pick_wifi("", "DevSSID") == JIANLU_WIFI_KCONFIG);
    assert(jianlu_config_pick_wifi(NULL, "DevSSID") == JIANLU_WIFI_KCONFIG);
    assert(jianlu_config_pick_wifi("", "") == JIANLU_WIFI_NONE);
    assert(jianlu_config_pick_wifi(NULL, NULL) == JIANLU_WIFI_NONE);
    assert(jianlu_config_pick_wifi("HomeWiFi", "") == JIANLU_WIFI_NVS);

    // 中枢: Kconfig > mDNS > NVS > 无
    assert(jianlu_config_pick_hub("http://dev:3000", "http://mdns:3000",
                                  "http://nvs:3000") == JIANLU_HUB_KCONFIG);
    assert(jianlu_config_pick_hub("", "http://mdns:3000",
                                  "http://nvs:3000") == JIANLU_HUB_MDNS);
    assert(jianlu_config_pick_hub("", NULL, "http://nvs:3000") == JIANLU_HUB_NVS);
    assert(jianlu_config_pick_hub(NULL, NULL, NULL) == JIANLU_HUB_NONE);
    assert(jianlu_config_pick_hub("", "", "") == JIANLU_HUB_NONE);
    return 0;
}
