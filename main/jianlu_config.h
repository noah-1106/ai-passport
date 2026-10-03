// main/jianlu_config.h —— 凭据与中枢地址的来源优先级(纯 C,host 可测)。
//
// 优先级(社区首配设计):
//   Wi-Fi:  NVS(BLUFI 配网写入) > Kconfig(开发期烧录) > 无 → 进入配网态
//   中枢:   Kconfig(开发覆盖) > mDNS 本次发现 > NVS(上次发现缓存) > 无 → 出错页
// Kconfig 全部留空即"出厂配网模式"。
#pragma once

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
