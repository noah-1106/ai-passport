// main/jianlu_config.c —— 见 jianlu_config.h。
#include "jianlu_config.h"

#include <stdbool.h>

const char *jianlu_cmode_name(int cmode)
{
    switch (cmode) {
    case JIANLU_CMODE_WIFI: return "Wi-Fi 中枢";
    case JIANLU_CMODE_BLE:  return "蓝牙直连";
    default:                return "自动";
    }
}

static bool nonempty(const char *s)
{
    return s != 0 && s[0] != '\0';
}

jianlu_wifi_src_t jianlu_config_pick_wifi(const char *nvs_ssid, const char *kconf_ssid)
{
    if (nonempty(nvs_ssid)) return JIANLU_WIFI_NVS;
    if (nonempty(kconf_ssid)) return JIANLU_WIFI_KCONFIG;
    return JIANLU_WIFI_NONE;
}

jianlu_boot_mode_t jianlu_config_boot_mode(jianlu_cmode_t cmode, bool dmode,
                                           bool reprov, jianlu_wifi_src_t wifi_src)
{
    if (cmode == JIANLU_CMODE_BLE) return JIANLU_BOOT_BLE;   // 直连档优先于一切
    if (cmode == JIANLU_CMODE_AUTO && dmode) return JIANLU_BOOT_BLE;  // 自动档上次切到了直连
    // Wi-Fi 档忽略 dmode(可能残留自动切换的置位)
    if (reprov) return JIANLU_BOOT_PROV_FORCE;
    if (wifi_src != JIANLU_WIFI_NONE) return JIANLU_BOOT_WIFI;
    return JIANLU_BOOT_PROV_FRESH;
}

jianlu_hub_src_t jianlu_config_pick_hub(const char *kconf_url, const char *mdns_url,
                                        const char *nvs_url)
{
    if (nonempty(kconf_url)) return JIANLU_HUB_KCONFIG;
    if (nonempty(mdns_url)) return JIANLU_HUB_MDNS;
    if (nonempty(nvs_url)) return JIANLU_HUB_NVS;
    return JIANLU_HUB_NONE;
}

jianlu_dl_action_t jianlu_dl_decide(bool has, uint64_t version, uint64_t last_version,
                                    bool cache_exists)
{
    if (!has) return JIANLU_DL_INVALIDATE;
    if (!cache_exists) return JIANLU_DL_NEED;        // 版本相同但文件没了(被清/损坏)
    if (version != last_version) return JIANLU_DL_NEED;
    return JIANLU_DL_SKIP;
}

jianlu_fail_view_t jianlu_fetch_fail_view(bool wifi_ok)
{
    return wifi_ok ? JIANLU_FAIL_OFFLINE : JIANLU_FAIL_ERROR;
}

bool jianlu_wifi_reason_is_cred_error(int reason)
{
    switch (reason) {
    case 15:   // 4-way handshake timeout(密码错)
    case 201:  // NO_AP_FOUND
    case 202:  // AUTH_FAIL
    case 203:  // ASSOC_FAIL
    case 204:  // HANDSHAKE_TIMEOUT
    case 205:  // CONNECTION_FAIL(密码错时与 15 交替出现)
        return true;
    default:
        return false;   // 200=beacon timeout 等临时原因:只重试,不配网
    }
}
