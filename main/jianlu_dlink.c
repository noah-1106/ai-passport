// main/jianlu_dlink.c —— 见 jianlu_dlink.h。
#include "jianlu_dlink.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "jianlu_capture.h"
#include "jianlu_voiceq.h"
#include "jianlu_dlink_codec.h"
#include "jianlu_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "jianlu_dl";

// 标准 Nordic Uart Service UUID
// 6E400001-B5A3-F393-E0A9-E50E24DCCA9E(服务) 0002(RX 写) 0003(TX 通知)


static char s_dev_name[19];
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_val_handle;
static bool s_tx_subscribed;

// 行缓冲(RX 写可能分片)
static char s_line[JIANLU_DL_LINE_MAX + 1];
static size_t s_line_used;

// 应用层命令结果(事件)与清单互斥访问(直连态由 dlink 事件任务处理,
// store 与 UI 刷新通过回调交给应用)
typedef void (*jianlu_dlink_ui_cb)(void);
static jianlu_dlink_ui_cb s_ui_refresh;

// ---- 语音槽信息(来自 capture 队列)----
static int s_slots[JIANLU_VOICEQ_SLOTS];

// 由应用注入:清单指针 + 刷新回调
static jianlu_store_t *s_store;
static struct {
    char ids[JIANLU_VOICEQ_SLOTS][JIANLU_ID_LEN];
    int count;
} s_pdone;   // 待同步勾选(桥 plist 上报成功即清)
static void (*s_on_pdone)(const char *id);

void jianlu_dlink_bind(jianlu_store_t *store, jianlu_dlink_ui_cb refresh)
{
    s_store = store;
    s_ui_refresh = refresh;
}

void jianlu_dlink_set_pdone_cb(void (*cb)(const char *id))
{
    s_on_pdone = cb;
}

// 供应用把离线勾选转成直连待同步列表(进入直连时调用)
void jianlu_dlink_set_pending(const char (*ids)[JIANLU_ID_LEN], int n)
{
    s_pdone.count = n > JIANLU_VOICEQ_SLOTS ? JIANLU_VOICEQ_SLOTS : n;
    for (int i = 0; i < s_pdone.count; i++) {
        jianlu_utf8_copy(s_pdone.ids[i], JIANLU_ID_LEN, ids[i],
                         JIANLU_ID_LEN - 1);
    }
}

// ---- TX:发一行 ----
// ATT 通知单帧上限 MTU-3,超长会被 NimBLE 截断(实测语音分块行 ~284 字节,
// 接收端拼出半行 JSON)。这里按 MTU-3 自行分片逐帧通知,行尾 '\n' 随最后
// 一片;L2CAP 层保序可靠,接收端按 '\n' 重组即可。
// 返回 false = 组包/入队失败(发送方应稍后重试整行)。
static bool send_line_ex(const char *line)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_tx_subscribed) return false;
    uint16_t mtu = ble_att_mtu(s_conn_handle);
    size_t chunk = mtu > 3 ? (size_t)(mtu - 3) : 20;
    size_t len = strlen(line);
    for (size_t off = 0; off < len; off += chunk) {
        size_t take = len - off > chunk ? chunk : len - off;
        struct os_mbuf *om = ble_hs_mbuf_from_flat((const uint8_t *)line + off,
                                                   take);
        if (om == NULL) return false;
        int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
        if (rc != 0) return false;
    }
    return true;
}

static void send_line(const char *line)
{
    if (!send_line_ex(line)) {
        ESP_LOGW(TAG, "通知发送失败(连接断开或队列满)");
    }
}

// ---- 语音流发送任务 ----
// vget 的分块流在此发送:NimBLE 主机任务只投递请求,本任务按行发送并在
// 队列满时 sleep 重试(mbuf 释放由主机事件循环驱动,这里睡着它才能跑)。
// 语音行走 indication(ACK 流控):notify 无流控,持续灌会把 ACL 队列
// 撑死(实测 19 块后链路楔死 6s+);indicate 一问一答,速率由对端决定。
static volatile int s_stream_slot;
static TaskHandle_t s_stream_task;

// 发一行(逐 MTU-3 分片,indication,上一条未 ACK 时等)
static void stream_send_line(const char *line)
{
    uint16_t mtu = ble_att_mtu(s_conn_handle);
    size_t chunk = mtu > 3 ? (size_t)(mtu - 3) : 20;
    size_t len = strlen(line);
    for (size_t off = 0; off < len; off += chunk) {
        size_t take = len - off > chunk ? chunk : len - off;
        for (int tries = 0; tries < 600; tries++) {
            if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
            struct os_mbuf *om = ble_hs_mbuf_from_flat(
                (const uint8_t *)line + off, take);
            if (om != NULL) {
                int rc = ble_gatts_indicate_custom(s_conn_handle,
                                                   s_tx_val_handle, om);
                if (rc == 0) break;   // 已入队,ACK 异步回来
                if (om != NULL) os_mbuf_free_chain(om);
            }
            vTaskDelay(pdMS_TO_TICKS(15));   // EBUSY:上一条未 ACK,等
        }
    }
}

static void stream_task(void *arg)
{
    (void)arg;
    static char out[JIANLU_DL_LINE_MAX + 1];   // 2K 行缓冲:栈上放必溢出
    static uint8_t raw[JIANLU_DL_VOICE_CHUNK];
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int slot = s_stream_slot;
        if (slot < 1) continue;
        char path[32];
        snprintf(path, sizeof(path), "/voicefs/p%d.wav", slot);
        FILE *f = fopen(path, "rb");
        if (f == NULL) {
            send_line("{\"r\":\"bad\"}\n");
            continue;
        }
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 44, SEEK_SET);   // 跳过 WAV 头:桥端自行封装,只要 PCM
        long body = fsize - 44;
        int total = (int)((body + JIANLU_DL_VOICE_CHUNK - 1)
                          / JIANLU_DL_VOICE_CHUNK);
        int seq = 0;
        for (; seq < total; seq++) {
            size_t want = (size_t)body - (size_t)seq * JIANLU_DL_VOICE_CHUNK;
            if (want > JIANLU_DL_VOICE_CHUNK) want = JIANLU_DL_VOICE_CHUNK;
            if (fread(raw, 1, want, f) != want) break;
            size_t n = jianlu_dlink_build_vchunk(out, sizeof(out), slot, seq,
                                                 total, raw, want);
            if (n == 0) break;
            out[n] = '\n';
            out[n + 1] = '\0';
            if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) break;
            stream_send_line(out);
        }
        fclose(f);
        if (seq >= total && s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            send_line("\n");   // 收尾空行,兜底行终止
        }
        ESP_LOGI(TAG, "语音流完成 slot=%d %d/%d 块", slot, seq, total);
    }
}

// 主机任务里调用:请求 TX 任务流出一个语音槽(任务懒创建,堆紧)
static void stream_request(int slot)
{
    if (s_stream_task == NULL) {
        if (xTaskCreate(stream_task, "dl_tx", 3072, NULL, 4,
                        &s_stream_task) != pdPASS) {
            ESP_LOGE(TAG, "TX 任务创建失败,语音槽 %d 无法发送", slot);
            return;
        }
    }
    s_stream_slot = slot;
    xTaskNotifyGive(s_stream_task);
}

static void reply(const char *line)
{
    send_line(line);
    send_line("\n");   // 确保分片边界后仍有行终止(单通知通常已含 \n,幂等)
}

// ---- 命令处理(在 NimBLE 主机任务)----
static void handle_line(const char *line, size_t len)
{
    jianlu_dl_msg_t msg;
    static char out[JIANLU_DL_LINE_MAX + 1];
    ESP_LOGI(TAG, "RX 行(%u): %.60s", (unsigned)len, line);

    if (!jianlu_dlink_parse(line, len, &msg)) {
        size_t n = jianlu_dlink_build_bad(out, sizeof(out));
        if (n) reply(out);
        return;
    }
    switch (msg.cmd) {
    case JIANLU_DL_CMD_RESET:
        if (s_store != NULL) {
            jianlu_store_replace_begin(s_store);
            jianlu_store_replace_end(s_store);
            if (s_ui_refresh) s_ui_refresh();
        }
        reply("{\"r\":\"ok\"}");
        break;
    case JIANLU_DL_CMD_RECORDS:
        if (s_store != NULL) {
            if (msg.seq == 0) jianlu_store_replace_begin(s_store);
            int added = jianlu_dlink_records_into_store(msg.records_json,
                                                        msg.records_json_len,
                                                        s_store);
            if (added < 0) {
                reply("{\"r\":\"bad\"}");
                break;
            }
            if (msg.seq + 1 >= msg.total) {
                jianlu_store_replace_end(s_store);
                if (s_ui_refresh) s_ui_refresh();
                size_t n = snprintf(out, sizeof(out),
                                    "{\"r\":\"ok\",\"cnt\":%d}", s_store->count);
                if (n && n < sizeof(out)) reply(out);
            } else {
                reply("{\"r\":\"ok\"}");
            }
        }
        break;
    case JIANLU_DL_CMD_PLIST: {
        size_t n = jianlu_dlink_build_plist(out, sizeof(out),
                                            s_pdone.ids, s_pdone.count);
        if (n) reply(out);
        break;
    }
    case JIANLU_DL_CMD_PDONE: {
        // 桥确认该勾选已 PUT 成功:清出待同步表
        for (int i = 0; i < s_pdone.count; i++) {
            if (strcmp(s_pdone.ids[i], msg.id) == 0) {
                memmove(s_pdone.ids[i], s_pdone.ids[i + 1],
                        (size_t)(s_pdone.count - i - 1) * sizeof(s_pdone.ids[0]));
                s_pdone.count--;
                break;
            }
        }
        if (s_on_pdone) s_on_pdone(msg.id);   // 应用侧出队+持久化+UI(NimBLE 上下文,只投事件)
        reply("{\"r\":\"ok\"}");
        break;
    }
    case JIANLU_DL_CMD_VLIST: {
        int total = jianlu_capture_queue_count();
        if (total > JIANLU_VOICEQ_SLOTS) total = JIANLU_VOICEQ_SLOTS;
        for (int i = 0; i < total; i++) s_slots[i] = i + 1;
        size_t n = jianlu_dlink_build_vlist(out, sizeof(out), s_slots, total);
        if (n) reply(out);
        break;
    }
    case JIANLU_DL_CMD_VGET:
    case JIANLU_DL_CMD_VDEL: {
        int slot = msg.slot;
        if (slot < 1 || slot > jianlu_capture_queue_count()) {
            reply("{\"r\":\"bad\"}");
            break;
        }
        if (msg.cmd == JIANLU_DL_CMD_VDEL) {
            reply("{\"r\":\"ok\"}");   // 删除在全部分块送达后由桥再确认一次
            break;
        }
        // vget:文件分块流交给独立 TX 任务。不能在本任务(NimBLE 主机任务)
        // 里循环发送:mgbu 的释放依赖主机事件循环,回调里 sleep 会把 ACL 发送
        // 队列撑死(rc=6 ENOMEM,实测 500+ 分块全部失败)。
        stream_request(slot);
        break;
    }
    default:
        reply("{\"r\":\"bad\"}");
        break;
    }
}

static void line_feed(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char ch = (char)data[i];
        if (ch == '\n' || ch == '\r') {
            if (s_line_used > 0) {
                s_line[s_line_used] = '\0';
                handle_line(s_line, s_line_used);
                s_line_used = 0;
            }
        } else if (s_line_used < JIANLU_DL_LINE_MAX) {
            s_line[s_line_used++] = ch;
        } else {
            s_line_used = 0;   // 超长行丢弃
        }
    }
}

static const ble_uuid128_t NUS_SVC = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_RX = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_TX = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);
static int rx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                     void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        static uint8_t wr[2048];
        size_t take = om_len > sizeof(wr) ? sizeof(wr) : om_len;
        if (ble_hs_mbuf_to_flat(ctxt->om, wr, sizeof(wr), NULL) != 0) take = 0;
        if (take > 0) line_feed(wr, take);
        return 0;
    }
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int tx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                     void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        // 真实数据走 notify;读到的是空串
        struct os_mbuf *om = ble_hs_mbuf_from_flat((const uint8_t *)"", 0);
        if (om == NULL) return BLE_ATT_ERR_INSUFFICIENT_RES;
        int rc = os_mbuf_append(ctxt->om, "", 0);
        os_mbuf_free_chain(om);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static const struct ble_gatt_svc_def SVC[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &NUS_RX.u,
                .access_cb = rx_access,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &NUS_TX.u,
                .access_cb = tx_access,
                .val_handle = &s_tx_val_handle,
                // notify 走短回复;indicate(ACK 流控)给语音流用
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
            },
            { 0 },
        },
    },
    { 0 },
};
// ---- GATT ----
// ---- GAP ----
static void start_advertising(void);   // 断开后重启广播用(完整字段+参数+日志)

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_line_used = 0;
            ESP_LOGI(TAG, "桥已连接 handle=%u", s_conn_handle);
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_tx_subscribed = false;
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "桥断开 reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_tx_subscribed = false;
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_val_handle) {
            s_tx_subscribed = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "TX 订阅=%d", s_tx_subscribed);
            if (s_tx_subscribed && s_store != NULL) {
                char out[96];
                int n2 = snprintf(out, sizeof(out),
                                  "{\"r\":\"ready\",\"cnt\":%d}\n", s_store->count);
                if (n2 > 0 && (size_t)n2 < sizeof(out)) send_line(out);
            }
        }
        return 0;
    default:
        return 0;
    }
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = { 0 };
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)s_dev_name;
    fields.name_len = (uint8_t)strlen(s_dev_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params params = { 0 };
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                               &params, gap_event, NULL);
    if (rc == 0) ESP_LOGI(TAG, "直连广播中: %s", s_dev_name);
    else ESP_LOGE(TAG, "广播启动失败 rc=%d", rc);
}

static void on_sync(void)
{
    // buddy/demo 同款注册顺序:先 count_cfg 扩 GATT 资源池再 add,
    // 否则 add_svcs 必然 ENOMEM(rc=6)
    int rc = ble_gatts_reset();
    ble_svc_gatt_init();   // void 返回
    if (rc == 0) rc = ble_gatts_count_cfg(SVC);
    if (rc == 0) rc = ble_gatts_add_svcs(SVC);
    if (rc == 0) rc = ble_gatts_start();
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT 注册失败 rc=%d", rc);
        return;
    }
    ble_svc_gap_device_name_set(s_dev_name);
    start_advertising();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();   // 返回即主机停止
    nimble_port_freertos_deinit();
}

bool jianlu_dlink_start(void)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_dev_name, sizeof(s_dev_name), "XIAONUO_%02X%02X", mac[4], mac[5]);

    // NimBLE 引导(与 jianlu_provision 同栈,但不经 BLUFI profile)
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init 失败: %s(空闲堆 %u)",
                 esp_err_to_name(err), (unsigned)esp_get_free_heap_size());
        return false;
    }
    ble_hs_cfg.reset_cb = NULL;   // 直连不加密配对,复位即重新广播
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    err = esp_nimble_enable(host_task);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_nimble_enable 失败: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool jianlu_dlink_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool jianlu_dlink_send_line(const char *line)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_tx_subscribed) return false;
    char out[128];
    int n = snprintf(out, sizeof(out), "%s\n", line);
    if (n <= 0 || (size_t)n >= sizeof(out)) return false;
    return send_line_ex(out);
}
