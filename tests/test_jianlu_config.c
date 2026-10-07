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

    // 图片缓存下载决策
    // 中枢已无此图:一律失效(删缓存清版本),与版本/缓存无关
    assert(jianlu_dl_decide(false, 100, 100, true) == JIANLU_DL_INVALIDATE);
    assert(jianlu_dl_decide(false, 0, 0, false) == JIANLU_DL_INVALIDATE);
    // 有图 + 版本未变 + 缓存在:跳过(不换图秒进)
    assert(jianlu_dl_decide(true, 100, 100, true) == JIANLU_DL_SKIP);
    // 有图 + 版本变化(换图):下载
    assert(jianlu_dl_decide(true, 200, 100, true) == JIANLU_DL_NEED);
    // 首次(无版本记录):下载
    assert(jianlu_dl_decide(true, 100, 0, true) == JIANLU_DL_NEED);
    // 版本未变但缓存丢失(被清/损坏):下载
    assert(jianlu_dl_decide(true, 100, 100, false) == JIANLU_DL_NEED);

    // 失败路径视图决策表:有快照→离线模式(内容可见),无快照→错误页
    assert(jianlu_fetch_fail_view(true) == JIANLU_FAIL_OFFLINE);
    assert(jianlu_fetch_fail_view(false) == JIANLU_FAIL_ERROR);

    // Wi-Fi 断开原因分类:只有凭据/关联类失败才允许自动转配网
    assert(jianlu_wifi_reason_is_cred_error(15));    // 4-way 握手超时
    assert(jianlu_wifi_reason_is_cred_error(201));   // 找不到 AP
    assert(jianlu_wifi_reason_is_cred_error(202));   // 认证失败
    assert(jianlu_wifi_reason_is_cred_error(203));   // 关联失败
    assert(jianlu_wifi_reason_is_cred_error(204));   // 握手超时
    assert(jianlu_wifi_reason_is_cred_error(205));   // 连接失败(密码错)
    assert(!jianlu_wifi_reason_is_cred_error(200));  // beacon 超时(临时)
    assert(!jianlu_wifi_reason_is_cred_error(2));    // AP 内部原因
    assert(!jianlu_wifi_reason_is_cred_error(8));    // 正常挥手断开
    return 0;
}
