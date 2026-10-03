// main/jianlu_provision.c —— 见 jianlu_provision.h。
// 生命周期管理移植自仓库 demo/blufi-provisioning 分支的 demo_blufi.c。
#include "jianlu_provision.h"

#include <string.h>

#include "esp_blufi.h"
#include "esp_blufi_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "jianlu_blufi_security.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "jianlu_prov";

static jianlu_prov_cb_t s_cb;
static void *s_cb_user;
static char s_dev_name[19];          // "BLUFI_XIAONUO_XXXX"
static char s_ssid[33];
static char s_pass[65];
static SemaphoreHandle_t s_host_stopped;
static bool s_host_initialized;
static bool s_host_running;
static bool s_gatt_initialized;
static bool s_btc_initialized;
static bool s_profile_initialized;
static bool s_ble_connected;
static bool s_wifi_connected;        // 应用通过 report_wifi 告知
static bool s_wifi_ready;            // Wi-Fi 协议栈已初始化(本应用配网与联网分离,恒 false)

void jianlu_provision_set_wifi_ready(bool ready)
{
    s_wifi_ready = ready;
}

static void post(jianlu_prov_ev_t ev, const char *ssid, const char *pass,
                 const char *data)
{
    if (s_cb) s_cb(ev, ssid, pass, data, s_cb_user);
}

void jianlu_provision_report_wifi(bool connected)
{
    s_wifi_connected = connected;
    wifi_mode_t mode = WIFI_MODE_STA;
    esp_wifi_get_mode(&mode);
    esp_blufi_extra_info_t info = { 0 };
    if (s_ssid[0] != '\0') {
        info.sta_ssid = (uint8_t *)s_ssid;
        info.sta_ssid_len = strlen(s_ssid);
    }
    esp_blufi_send_wifi_conn_report(mode,
        connected ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_CONN_FAIL,
        0, &info);
}

static void blufi_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: %d", reason);
    post(JIANLU_PROV_FAILED, NULL, NULL, NULL);
}

static void blufi_sync(void)
{
    int rc = esp_blufi_profile_init();
    if (rc == 0) {
        s_profile_initialized = true;
    } else {
        ESP_LOGE(TAG, "blufi profile init 失败: %d", rc);
        post(JIANLU_PROV_FAILED, NULL, NULL, NULL);
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    if (s_host_stopped) xSemaphoreGive(s_host_stopped);
    nimble_port_freertos_deinit();
}

static void blufi_event(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    switch (event) {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        esp_blufi_adv_start_with_name(s_dev_name);
        ESP_LOGI(TAG, "BLUFI 广播中: %s", s_dev_name);
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        s_ble_connected = true;
        esp_blufi_adv_stop();
        if (jianlu_blufi_security_init() != 0) {
            post(JIANLU_PROV_FAILED, NULL, NULL, NULL);
            break;
        }
        post(JIANLU_PROV_BLE_CONNECT, NULL, NULL, NULL);
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        s_ble_connected = false;
        jianlu_blufi_security_deinit();
        esp_blufi_adv_start_with_name(s_dev_name);
        post(JIANLU_PROV_BLE_DISCONNECT, NULL, NULL, NULL);
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        if (s_wifi_ready) esp_wifi_set_mode(WIFI_MODE_STA);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        if (param->sta_ssid.ssid_len >= sizeof(s_ssid)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memcpy(s_ssid, param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        s_ssid[param->sta_ssid.ssid_len] = '\0';
        break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        if (param->sta_passwd.passwd_len >= sizeof(s_pass)) {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        memcpy(s_pass, param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        s_pass[param->sta_passwd.passwd_len] = '\0';
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        if (s_ssid[0] == '\0') {
            esp_blufi_send_error_info(ESP_BLUFI_DATA_FORMAT_ERROR);
            break;
        }
        post(JIANLU_PROV_GOT_WIFI, s_ssid, s_pass, NULL);
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        if (s_wifi_ready) esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS:
        jianlu_provision_report_wifi(s_wifi_connected);
        break;
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA:
        if (param->custom_data.data && param->custom_data.data_len > 0) {
            // data 非 NUL 结尾,回调方按 C 串处理 → 拷到有界缓冲
            static char s_custom[160];
            int n = param->custom_data.data_len;
            if (n >= (int)sizeof(s_custom)) n = sizeof(s_custom) - 1;
            memcpy(s_custom, param->custom_data.data, n);
            s_custom[n] = '\0';
            post(JIANLU_PROV_CUSTOM_DATA, NULL, NULL, s_custom);
        }
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        esp_blufi_send_error_info(param->report_error.state);
        break;
    default:
        break;   // BSSID/WIFI_LIST 等选配事件不处理
    }
}

static esp_blufi_callbacks_t s_callbacks = {
    .event_cb = blufi_event,
    .negotiate_data_handler = jianlu_blufi_negotiate,
    .encrypt_func = jianlu_blufi_encrypt,
    .decrypt_func = jianlu_blufi_decrypt,
    .checksum_func = jianlu_blufi_checksum,
};

esp_err_t jianlu_provision_start(jianlu_prov_cb_t cb, void *user)
{
    if (s_host_initialized) return ESP_ERR_INVALID_STATE;
    s_cb = cb;
    s_cb_user = user;
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    s_wifi_connected = false;

    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_dev_name, sizeof(s_dev_name), "BLUFI_XIAONUO_%02X%02X",
             mac[4], mac[5]);

    esp_err_t err = esp_blufi_register_callbacks(&s_callbacks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_callbacks 失败: %s", esp_err_to_name(err));
        return err;
    }
    err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败: %s(空闲堆 %u)",
                 esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
        return err;
    }
    s_host_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (!s_host_stopped) return ESP_ERR_NO_MEM;

    ble_hs_cfg.reset_cb = blufi_reset;
    ble_hs_cfg.sync_cb = blufi_sync;
    ble_hs_cfg.gatts_register_cb = esp_blufi_gatt_svr_register_cb;
    if (esp_blufi_gatt_svr_init() != 0) return ESP_FAIL;
    s_gatt_initialized = true;
    if (ble_svc_gap_device_name_set(s_dev_name) != 0) return ESP_FAIL;
    esp_blufi_btc_init();
    s_btc_initialized = true;
    err = esp_nimble_enable(host_task);
    if (err == ESP_OK) s_host_running = true;
    return err;
}

esp_err_t jianlu_provision_stop(void)
{
    if (!s_host_initialized) return ESP_OK;
    s_cb = NULL;
    s_cb_user = NULL;
    s_ble_connected = false;
    jianlu_blufi_security_deinit();

    if (s_profile_initialized) esp_blufi_adv_stop();
    if (s_gatt_initialized) {
        esp_blufi_gatt_svr_deinit();
        s_gatt_initialized = false;
    }
    bool host_stopped = !s_host_running;
    if (s_host_running) {
        int rc = nimble_port_stop();
        if (rc == 0) {
            xSemaphoreTake(s_host_stopped, portMAX_DELAY);
            host_stopped = true;
        } else {
            ESP_LOGE(TAG, "nimble_port_stop 失败: %d", rc);
        }
    }
    if (host_stopped) nimble_port_deinit();
    s_host_running = false;
    if (s_profile_initialized) {
        esp_blufi_profile_deinit();
        s_profile_initialized = false;
    }
    if (s_btc_initialized) {
        esp_blufi_btc_deinit();
        s_btc_initialized = false;
    }
    s_host_initialized = false;
    if (s_host_stopped) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    ESP_LOGI(TAG, "BLUFI 已停止,NimBLE 内存释放");
    return ESP_OK;
}

const char *jianlu_provision_device_name(void)
{
    return s_dev_name;
}
