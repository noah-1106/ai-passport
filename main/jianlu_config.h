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
// 拉取失败/发现失败:有快照必走离线模式(内容可见+离线标识),
// 错误信息只在离线模式内提示;无快照才落全屏错误页。
typedef enum {
    JIANLU_FAIL_ERROR = 0,   // 无内容可示:错误页
    JIANLU_FAIL_OFFLINE,     // 有历史快照:离线模式
} jianlu_fail_view_t;

jianlu_fail_view_t jianlu_fetch_fail_view(bool snapshot_ok);
