// main/jianlu_config.h —— 凭据与中枢地址的来源优先级(纯 C,host 可测)。
//
// 优先级(社区首配设计):
//   Wi-Fi:  NVS(BLUFI 配网写入) > Kconfig(开发期烧录) > 无 → 进入配网态
//   中枢:   Kconfig(开发覆盖) > mDNS 本次发现 > NVS(上次发现缓存) > 无 → 出错页
// Kconfig 全部留空即"出厂配网模式"。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    JIANLU_WIFI_NONE = 0,
    JIANLU_WIFI_NVS,
    JIANLU_WIFI_KCONFIG,
} jianlu_wifi_src_t;

// 启动模式决策(优先级:直连 > 强制配网 > 凭据联网 > 出厂配网)。
// 直连(BLE-only)不需要 Wi-Fi 凭据:dmode 在位时无论凭据/重配状态
// 一律进 BLE,配网页只属于"Wi-Fi 模式且无凭据"。
typedef enum {
    JIANLU_BOOT_BLE = 0,     // dmode 直连标志在位:BLE 广播,跳过一切 Wi-Fi 逻辑
    JIANLU_BOOT_PROV_FORCE,  // reprov 标志在位:用户要求重配,强制配网态
    JIANLU_BOOT_WIFI,        // 有凭据(NVS/Kconfig):正常联网路径
    JIANLU_BOOT_PROV_FRESH,  // 无凭据:出厂配网模式
} jianlu_boot_mode_t;

jianlu_boot_mode_t jianlu_config_boot_mode(bool dmode, bool reprov,
                                           jianlu_wifi_src_t wifi_src);

typedef enum {
    JIANLU_HUB_NONE = 0,
    JIANLU_HUB_KCONFIG,
    JIANLU_HUB_MDNS,
    JIANLU_HUB_NVS,
} jianlu_hub_src_t;

// 入参为 C 字符串(NULL 视为空)。返回选中的来源;调用方按来源取对应值。
jianlu_wifi_src_t jianlu_config_pick_wifi(const char *nvs_ssid, const char *kconf_ssid);

// 优先级:KCONFIG > MDNS > NVS > NONE。mdns_url 为本次发现结果(失败传 NULL)。
jianlu_hub_src_t jianlu_config_pick_hub(const char *kconf_url, const char *mdns_url,
                                        const char *nvs_url);

// ---- 图片缓存下载决策(头像/二维码共用,host 可测)----
typedef enum {
    JIANLU_DL_SKIP = 0,    // 版本未变且缓存存在:跳过下载
    JIANLU_DL_NEED,        // 需要下载(版本变化或缓存缺失)
    JIANLU_DL_INVALIDATE,  // 中枢已无此图:删缓存清版本
} jianlu_dl_action_t;

jianlu_dl_action_t jianlu_dl_decide(bool has, uint64_t version, uint64_t last_version,
                                    bool cache_exists);

// ---- 失败路径的视图决策(host 可测)----
// 正常界面内降级:拉取失败一律进离线/降级模式(READY+离线标识,
// 有快照显示快照,无快照显示空态),绝不整屏接管。
// 全屏错误页只保留给真正致命状态:Wi-Fi 初始化失败(无凭据走配网,另路径)。
typedef enum {
    JIANLU_FAIL_ERROR = 0,   // Wi-Fi 本身不可用:错误页(仅此一种)
    JIANLU_FAIL_OFFLINE,     // 网络在而中枢不可达:正常界面+离线降级
} jianlu_fail_view_t;

jianlu_fail_view_t jianlu_fetch_fail_view(bool wifi_ok);

// Wi-Fi 断开原因是否为"凭据/关联类失败"(连不上 AP)。
// 只有这类失败才允许自动转配网;beacon 超时/路由器重启等临时原因绝不触发。
bool jianlu_wifi_reason_is_cred_error(int reason);
