// main/jianlu_app.c —— 见 jianlu_app.h。
//
// 按键映射(阶段 6:首配流程):
//   UP/DOWN 单击   卡片堆切换顶卡
//   OK 单击        完成顶卡;语音确认/失败页=关闭;重配确认页=确认
//   OK 双击        手动刷新
//   OK 按住 ≥500ms 按住说话(松开结束并上传,15s 上限)
//   UP 长按        重新配网:弹确认页,OK 确认后清除 NVS 凭据回配网态
//
// 联网主路径(见 jianlu_netflow.h):
//   无凭据 → PROVISIONING(BLUFI 广播)→ 收凭据存 NVS → CONNECTING
//   → GOT_IP → DISCOVERING(mDNS _xiaonuo._tcp)→ 确定中枢 → LOADING → READY
#include "jianlu_app.h"

#include <string.h>
#include <sys/time.h>

#include "bsp_button.h"
#include "bsp_display.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "jianlu_capture.h"
#include "jianlu_config.h"
#include "jianlu_discover.h"
#include "jianlu_home.h"
#include "jianlu_hub.h"
#include "jianlu_level.h"
#include "jianlu_modal.h"
#include "jianlu_nav.h"
#include "jianlu_netflow.h"
#include "jianlu_nvs.h"
#include "jianlu_offline.h"
#include "jianlu_powersave.h"
#include "jianlu_provision.h"
#include "jianlu_shot.h"
#include "jianlu_theme.h"
#include "jianlu_timefmt.h"
#include "jianlu_tone.h"
#include "jianlu_ui.h"
#include "jianlu_voice.h"

static const char *TAG = "jianlu";

// 临时链路自检:置 1 后,Wi-Fi 连通即自动录 3s 并上传(不等按键),
// 用于无人值守验证 录音→上传→中枢→解析 链路。验证完必须改回 0。
#define VOICE_LINK_SELFTEST 0
// 临时导航自检:置 1 后,首次拉取成功后按脚本注入按键事件,覆盖
// 主页菜单/三子页/亮度/双击返回/全局按住说话/简录页翻卡。验证完必须改回 0。
#define NAV_LINK_SELFTEST 1

#define EVENT_QUEUE_DEPTH 16
#define JOB_QUEUE_DEPTH   4
#define WIFI_RETRY_MS     5000
#define WIFI_FAIL_TO_PROVISION 6   // 连续失败次数上限,超过转入配网态
#define MDNS_TIMEOUT_MS   6000
#define REC_RELEASE_MV    2000   // 松开判定:高于 OK 键窗口上限(1900),松开约 3300
#define CONFIRM_TIMEOUT_MS 6000

typedef enum {
    EV_KEY = 0,          // 按键
    EV_WIFI_CONNECTED,   // 拿到 IP
    EV_WIFI_DISCONNECTED,// 掉线
    EV_FETCH_DONE,       // 网络任务:拉取结束(arg1=esp_err)
    EV_COMPLETE_DONE,    // 网络任务:完成上报结束(arg1=esp_err,id 在 payload)
    EV_DISCOVER_DONE,    // 网络任务:mDNS 发现结束(arg1=esp_err)
    EV_PROV,             // BLUFI 事件(arg1=jianlu_prov_ev_t,凭据在静态缓冲)
    EV_REC_TICK,         // 录音任务:进度(arg1=已录秒数)
    EV_REC_DONE,         // 录音任务:结束(arg1=esp_err,arg2=PCM 字节数)
    EV_CAPTURE_DONE,     // 网络任务:语音上传结束(arg1=esp_err,arg2=是否 pending 补传)
    EV_PLAY_DONE,        // 录音任务:本地回放结束(arg1=esp_err)
    EV_SYNC_DONE,        // 网络任务:待同步回放结束(arg1=esp_err,arg2=剩余条数)
    EV_OFFLINE_RETRY,    // 离线模式定时重试拉取
    EV_CONFIRM_TIMEOUT,  // 语音确认页自动关闭
    EV_PROFILE_DONE,     // 网络任务:资料拉取(arg1=esp_err)
    EV_AVATAR_DONE,      // 网络任务:头像下载(arg1=esp_err)
    EV_QR_DONE,          // 网络任务:二维码下载(arg1=esp_err)
} app_ev_type_t;

typedef struct {
    app_ev_type_t type;
    int32_t arg1;
    int32_t arg2;
    bsp_btn_t btn;
    bsp_btn_ev_t btn_ev;
    char id[JIANLU_ID_LEN];
} app_ev_t;

typedef enum {
    JOB_FETCH = 0,
    JOB_COMPLETE,
    JOB_CAPTURE,         // 语音队列:上传队头一条
    JOB_DISCOVER,        // mDNS 找中枢
    JOB_SYNC,            // 离线待同步队列回放
    JOB_DIAG,            // 网络诊断:网关/公网/中枢 三路 TCP 探针
    JOB_PROFILE,         // 拉资料(昵称/签名/有无图)
    JOB_AVATAR,          // 下载头像缓存
    JOB_QRCODE,          // 下载二维码缓存
} job_type_t;

typedef struct {
    job_type_t type;
    char id[JIANLU_ID_LEN];
} net_job_t;

typedef enum {
    REC_CMD_START = 0,
    REC_CMD_PLAY,        // 回放 pending.wav
} rec_cmd_type_t;

static jianlu_store_t s_store;
// 拉取进行中标志:网络任务直接写 s_store(省一个 5KB 副本),期间应用任务
// 不读清单(UI 刷新跳过清单区、按键清单操作被忽略),完成后由 EV_FETCH_DONE 复位。
static volatile bool s_fetch_busy;
static char s_fetch_err[JIANLU_ERROR_LEN];
static jianlu_voice_t s_voice;
static jianlu_capture_result_t s_capture_result;
static jianlu_nf_t s_nf = JIANLU_NF_CONNECTING;
static jianlu_nvs_data_t s_nvs;
static jianlu_syncq_t s_syncq;
static jianlu_ps_t s_ps;
static char s_mdns_url[JIANLU_HUB_URL_LEN];
static int s_connect_fails;
static char s_prev_top_id[JIANLU_ID_LEN];
// 显式同步:提示页 → OK 逐条上传(进度)→ 汇总页
static bool s_sync_prompt;       // 「发现离线内容」提示页显示中
static bool s_summary_shown;     // 汇总页显示中
static bool s_drain_active;      // 补传进行中(屏蔽按键)
static int s_drain_total;        // 开始时 语音队列+离线勾选 总数
static int s_drain_done;         // 已处理条数
static int s_drain_new_cards;    // 累计新增卡片数
static int s_drain_syncq_total;  // 开始时离线勾选数
static int s_drain_syncq_done;   // 已同步勾选项数
// v2:导航与资料
static jianlu_nav_t s_nav;
static jianlu_profile_t s_profile;      // 应用任务所有
static jianlu_profile_t s_profile_tmp;  // 网络任务写,EV_PROFILE_DONE 时提交
static int s_brightness = 100;          // 25/50/75/100
static bool s_keep_on;                  // 屏幕常亮
static bool s_info_shown;               // 「关于」信息页显示中

#define AVATAR_PATH "/voicefs/avatar.raw"
#define QRCODE_PATH "/voicefs/qrcode.raw"
static bool s_prov_running;
static bool s_reprov_confirm;        // 重配确认页显示中
static bool s_screen_off;            // 背光已熄(省电)
static QueueHandle_t s_ev_queue;
static QueueHandle_t s_job_queue;
static QueueHandle_t s_rec_queue;

// 前向声明(定义在下方)
static void reprov_overlay_show(void);
static void drain_start(void);
static void network_diag(void);   // 定义在文件尾(诊断段)
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_offline_timer;
static esp_timer_handle_t s_confirm_timer;

#if NAV_LINK_SELFTEST
// 导航自检脚本:{btn, ev},每 1.2s 注入一个
typedef struct { bsp_btn_t btn; bsp_btn_ev_t ev; } nav_test_key_t;
static const nav_test_key_t NAV_TEST_SCRIPT[] = {
    { BSP_BTN_DOWN, BSP_BTN_CLICK },  // 焦点1
    { BSP_BTN_DOWN, BSP_BTN_CLICK },  // 焦点2(设置)
    { BSP_BTN_OK,   BSP_BTN_CLICK },  // 进设置(焦点0)
    { BSP_BTN_DOWN, BSP_BTN_CLICK },  // 行1 主题
    { BSP_BTN_DOWN, BSP_BTN_CLICK },  // 行2 常亮
    { BSP_BTN_DOWN, BSP_BTN_CLICK },  // 行3 立即同步
    { BSP_BTN_DOWN, BSP_BTN_CLICK },  // 行4 重新配网
    { BSP_BTN_OK,   BSP_BTN_CLICK },  // 弹确认页
    { BSP_BTN_OK,   BSP_BTN_CLICK },  // 确认 → 重启进配网态
};
#define NAV_TEST_STEPS (sizeof(NAV_TEST_SCRIPT) / sizeof(NAV_TEST_SCRIPT[0]))
static esp_timer_handle_t s_nav_test_timer;
static int s_nav_test_step;

static void nav_test_tick(void *arg)
{
    (void)arg;
    if (s_nav_test_step >= (int)NAV_TEST_STEPS) {
        if (s_nav_test_timer) {
            esp_timer_stop(s_nav_test_timer);
            esp_timer_delete(s_nav_test_timer);
            s_nav_test_timer = NULL;
        }
        ESP_LOGI(TAG, "[NAVTEST] 脚本完成");
        return;
    }
    const nav_test_key_t *k = &NAV_TEST_SCRIPT[s_nav_test_step];
    ESP_LOGI(TAG, "[NAVTEST] step %d: btn=%d ev=%d", s_nav_test_step,
             (int)k->btn, (int)k->ev);
    s_nav_test_step++;
    if (!s_ev_queue) return;
    app_ev_t msg = { .type = EV_KEY, .btn = k->btn, .btn_ev = k->ev };
    (void)xQueueSend(s_ev_queue, &msg, 0);
}

static void nav_test_start(void);

#if NAV_LINK_SELFTEST
static void nav_test_boot_cb(void *arg)
{
    (void)arg;
    if (s_nav_test_step == 0 && s_nav_test_timer == NULL) nav_test_start();
}
#endif

static void nav_test_start(void)
{
    ESP_LOGI(TAG, "[NAVTEST] 开始脚本化按键注入");
    esp_timer_create_args_t args = {
        .callback = nav_test_tick,
        .name = "nav_test",
    };
    if (esp_timer_create(&args, &s_nav_test_timer) == ESP_OK) {
        esp_timer_start_periodic(s_nav_test_timer, 15000ULL * 1000);
    }
}
#endif
static volatile bool s_wifi_connected;
static volatile bool s_playing;      // 本地回放中
static volatile bool s_play_stop;    // 任意键请求停止回放
static int s_play_slot;              // 回放目标槽位 1..4

// BLUFI 回调 → 事件 的载荷(单生产者单消费者,经队列串行化,静态即可)
static char s_prov_ssid[JIANLU_NVS_SSID_LEN];
static char s_prov_pass[JIANLU_NVS_PASS_LEN];
static char s_prov_data[160];

// ---------------------------------------------------------------------------
// 事件投递(中断/回调上下文安全:只入队)
// ---------------------------------------------------------------------------
static void post_event(app_ev_type_t type, int32_t arg1, int32_t arg2, const char *id)
{
    if (!s_ev_queue) return;
    app_ev_t ev = { .type = type, .arg1 = arg1, .arg2 = arg2 };
    if (id) jianlu_utf8_copy(ev.id, sizeof(ev.id), id, sizeof(ev.id) - 1);
    (void)xQueueSend(s_ev_queue, &ev, 0);
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_ev_queue) return;
    app_ev_t msg = { .type = EV_KEY, .btn = btn, .btn_ev = ev };
    (void)xQueueSend(s_ev_queue, &msg, 0);
}

// BLUFI 回调(btc/NimBLE 上下文):拷载荷 + 入队,不做任何重活。
static void on_prov_event(jianlu_prov_ev_t ev, const char *ssid,
                          const char *pass, const char *data, void *user)
{
    (void)user;
    if (ssid) jianlu_utf8_copy(s_prov_ssid, sizeof(s_prov_ssid), ssid, sizeof(s_prov_ssid) - 1);
    if (pass) jianlu_utf8_copy(s_prov_pass, sizeof(s_prov_pass), pass, sizeof(s_prov_pass) - 1);
    if (data) jianlu_utf8_copy(s_prov_data, sizeof(s_prov_data), data, sizeof(s_prov_data) - 1);
    post_event(EV_PROV, (int32_t)ev, 0, NULL);
}

// ---------------------------------------------------------------------------
// Wi-Fi(STA;凭据来自 NVS/Kconfig/BLUFI,运行时下发)
// ---------------------------------------------------------------------------
static void wifi_connect(void)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "Wi-Fi connect 调用失败: %s", esp_err_to_name(err));
    }
}

static void wifi_retry_cb(void *arg)
{
    (void)arg;
    wifi_connect();
}

static void schedule_wifi_retry(void)
{
    if (s_retry_timer) {
        esp_timer_stop(s_retry_timer);
        esp_timer_start_once(s_retry_timer, WIFI_RETRY_MS * 1000ULL);
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        // 协议栈启动;有凭据才拨号(配网态下 BLUFI 会代为触发)
        wifi_config_t cfg = { 0 };
        esp_wifi_get_config(WIFI_IF_STA, &cfg);
        if (cfg.sta.ssid[0] != '\0') wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        post_event(EV_WIFI_DISCONNECTED, 0, 0, NULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        post_event(EV_WIFI_CONNECTED, 0, 0, NULL);
    }
}

static esp_err_t wifi_set_creds(const char *ssid, const char *pass)
{
    wifi_config_t sta = { 0 };
    jianlu_utf8_copy((char *)sta.sta.ssid, sizeof(sta.sta.ssid), ssid,
                     sizeof(sta.sta.ssid) - 1);
    jianlu_utf8_copy((char *)sta.sta.password, sizeof(sta.sta.password), pass,
                     sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    return esp_wifi_set_config(WIFI_IF_STA, &sta);
}

static esp_err_t wifi_start(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK) return err;
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                              on_wifi_event, NULL, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                              on_wifi_event, NULL, NULL);
    if (err != ESP_OK) return err;
    // 凭据由本应用自管(NVS/Kconfig/BLUFI),不让 IDF 另存一份造成双事实源
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    esp_timer_create_args_t timer_args = {
        .callback = wifi_retry_cb,
        .name = "wifi_retry",
    };
    return esp_timer_create(&timer_args, &s_retry_timer);
}

// ---------------------------------------------------------------------------
// 录音任务:等开始命令 → 阻塞录音(松开/上限停止)→ 投 EV_REC_DONE
// ---------------------------------------------------------------------------
typedef struct {
    size_t bytes;
    int last_sec;
} rec_poll_state_t;

static rec_poll_state_t s_poll_state;

// 录音任务上下文,每写一块(64ms)调一次:松开返回 false;每秒投一次进度;
// 顺手算电平驱动 UI 声波。
static bool rec_poll(void *user, const uint8_t *pcm, size_t len)
{
    rec_poll_state_t *st = user;
    st->bytes += JIANLU_REC_CHUNK_BYTES;
    int sec = (int)(st->bytes / JIANLU_VOICE_BYTES_PER_SEC);
    if (sec != st->last_sec) {
        st->last_sec = sec;
        post_event(EV_REC_TICK, sec, 0, NULL);
    }
    jianlu_ui_voice_set_level(
        jianlu_level_from_pcm((const int16_t *)pcm, len / sizeof(int16_t)));
    esp_task_wdt_reset();   // 录音循环每块喂一次狗
#if VOICE_LINK_SELFTEST
    return st->bytes < 3 * JIANLU_VOICE_BYTES_PER_SEC;   // 固定录 3s
#else
    return bsp_button_read_mv() < REC_RELEASE_MV;   // 按住时约 595mV
#endif
}

static void rec_task(void *arg)
{
    (void)arg;
    rec_cmd_type_t cmd;
    for (;;) {
        // 空闲等命令时不挂看门狗(永久阻塞是正常态);
        // 只在真正干活(录音/回放)期间挂狗,卡死才会被抓。
        if (xQueueReceive(s_rec_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        esp_task_wdt_add(NULL);
        if (cmd == REC_CMD_PLAY) {
            size_t played = 0;
            s_play_stop = false;
            esp_err_t err = jianlu_capture_play_slot(s_play_slot, &s_play_stop, &played);
            post_event(EV_PLAY_DONE, (int32_t)err, (int32_t)played, NULL);
        } else {
            s_poll_state.bytes = 0;
            s_poll_state.last_sec = 0;
            size_t pcm_bytes = 0;
            esp_err_t err = jianlu_capture_record(rec_poll, &s_poll_state, &pcm_bytes);
            post_event(EV_REC_DONE, (int32_t)err, (int32_t)pcm_bytes, NULL);
        }
        esp_task_wdt_reset();
        esp_task_wdt_delete(NULL);
    }
}

// ---------------------------------------------------------------------------
// 网络工作任务:唯一执行阻塞 HTTP/mDNS 的地方
// ---------------------------------------------------------------------------
static void net_task(void *arg)
{
    (void)arg;
    net_job_t job;
    bool ps_off = false;   // 有作业时关 Wi-Fi 省电(证据:MIN_MODEM 下 SYN
                           // 间歇丢失,ps=1 rssi=-45 仍 HTTP_CONNECT 失败)
    for (;;) {
        // 空闲等任务时不挂狗;执行作业期间挂狗(HTTP/mDNS 可能长达 30s+,
        // 超时 40s 覆盖,真卡死才触发)。空闲 5s 恢复省电,功耗不受损。
        if (xQueueReceive(s_job_queue, &job, pdMS_TO_TICKS(5000)) != pdTRUE) {
            if (ps_off) {
                esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                ps_off = false;
            }
            continue;
        }
        if (!ps_off) {
            esp_wifi_set_ps(WIFI_PS_NONE);
            ps_off = true;
        }
        esp_task_wdt_add(NULL);
        if (job.type == JOB_FETCH) {
            s_fetch_err[0] = '\0';
            s_fetch_busy = true;
            esp_err_t err = jianlu_hub_fetch(&s_store,
                                             s_fetch_err, sizeof(s_fetch_err));
            if (err == ESP_OK) jianlu_offline_save_snapshot(&s_store);
            s_fetch_busy = false;
            post_event(EV_FETCH_DONE, (int32_t)err, 0, NULL);
        } else if (job.type == JOB_COMPLETE) {
            esp_err_t err = jianlu_hub_complete(job.id);
            post_event(EV_COMPLETE_DONE, (int32_t)err, 0, job.id);
        } else if (job.type == JOB_DISCOVER) {
            s_mdns_url[0] = '\0';
            esp_err_t err = jianlu_discover_hub(s_mdns_url, sizeof(s_mdns_url),
                                                MDNS_TIMEOUT_MS);
            post_event(EV_DISCOVER_DONE, (int32_t)err, 0, NULL);
        } else if (job.type == JOB_SYNC) {
            // 离线待同步回放:新的优先;成功的出队,失败的留队列(last-write-wins)
            jianlu_syncq_t q;
            jianlu_offline_load_syncq(&q);
            int total = q.count;
            for (int i = q.count - 1; i >= 0; i--) {
                if (jianlu_hub_complete(q.ids[i]) == ESP_OK) {
                    jianlu_syncq_remove(&q, q.ids[i]);
                }
            }
            esp_err_t err = jianlu_offline_save_syncq(&q);
            ESP_LOGI(TAG, "待同步回放: %d 条剩 %d 条", total, q.count);
            post_event(EV_SYNC_DONE, (int32_t)err, q.count, NULL);
        } else if (job.type == JOB_DIAG) {
            network_diag();
        } else if (job.type == JOB_PROFILE) {
            esp_err_t err = jianlu_hub_fetch_profile(&s_profile_tmp);
            post_event(EV_PROFILE_DONE, (int32_t)err, 0, NULL);
        } else if (job.type == JOB_AVATAR) {
            esp_err_t err = jianlu_hub_download_file("/api/profile/avatar.raw",
                                                     AVATAR_PATH);
            post_event(EV_AVATAR_DONE, (int32_t)err, 0, NULL);
        } else if (job.type == JOB_QRCODE) {
            esp_err_t err = jianlu_hub_download_file("/api/profile/qrcode.raw",
                                                     QRCODE_PATH);
            post_event(EV_QR_DONE, (int32_t)err, 0, NULL);
        } else {
            // 语音队列:只打头(FIFO);队列循环由应用任务按结果驱动
            esp_err_t err = jianlu_capture_upload_head(&s_capture_result);
            post_event(EV_CAPTURE_DONE, (int32_t)err, 0, NULL);
        }
        esp_task_wdt_reset();
        esp_task_wdt_delete(NULL);
    }
}

// ---------------------------------------------------------------------------
// 应用任务:状态机迁移、刷 UI、派单
// ---------------------------------------------------------------------------
static void ui_refresh(jianlu_anim_t anim)
{
    if (s_fetch_busy) return;   // 网络任务正在重写清单,跳过本次刷新
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_refresh(&s_store, anim);
        bsp_lvgl_unlock();
    }
}

// 状态机 → 视图 → 屏幕
static void apply_view(jianlu_anim_t anim)
{
    jianlu_store_set_view(&s_store, jianlu_netflow_view(s_nf), NULL);
    ui_refresh(anim);
}

static void set_error(const char *msg)
{
    s_nf = JIANLU_NF_ERROR;
    jianlu_store_set_view(&s_store, JIANLU_VIEW_ERROR, msg);
    ui_refresh(JIANLU_ANIM_NONE);
}

static void request_fetch(void)
{
    if (!s_job_queue) return;
    // 记下当前顶卡,EV_FETCH_DONE 用来判定"新卡入堆"动画
    const jianlu_record_t *top = jianlu_store_selected(&s_store);
    s_prev_top_id[0] = '\0';
    if (top) jianlu_utf8_copy(s_prev_top_id, sizeof(s_prev_top_id),
                              top->id, sizeof(s_prev_top_id) - 1);
    net_job_t job = { .type = JOB_FETCH };
    (void)xQueueSend(s_job_queue, &job, 0);
}

static void request_capture(void)
{
    if (!s_job_queue) return;
    net_job_t job = { .type = JOB_CAPTURE };
    (void)xQueueSend(s_job_queue, &job, 0);
}

static void request_discover(void)
{
    if (!s_job_queue) return;
    net_job_t job = { .type = JOB_DISCOVER };
    (void)xQueueSend(s_job_queue, &job, 0);
}

// 图片缓存统一决策:版本变了/缓存缺失才下载;中枢已无此图则删缓存清版本。
// EV_PROFILE_DONE 与进入二维码页共用,保证"不换图秒进"。
static void request_job(job_type_t type);   // 前向声明(定义在下方)

// 在途标记:下载进行中不再重复排队(进二维码页与 profile 到达可能撞车)
static uint8_t s_dl_inflight;   // bit0=avatar bit1=qrcode

static void maybe_download_images(void)
{
    jianlu_dl_action_t a = jianlu_dl_decide(
        s_profile.has_avatar, s_profile.avatar_version, s_nvs.avatar_ver,
        jianlu_hub_cache_exists(AVATAR_PATH));
    if (a == JIANLU_DL_NEED && !(s_dl_inflight & 1)) {
        s_dl_inflight |= 1;
        request_job(JOB_AVATAR);
    } else if (a == JIANLU_DL_INVALIDATE) {
        remove(AVATAR_PATH);
        s_nvs.avatar_ver = 0;
        jianlu_nvs_save_image_ver(true, 0);
    }

    a = jianlu_dl_decide(s_profile.has_qrcode, s_profile.qrcode_version,
                         s_nvs.qrcode_ver, jianlu_hub_cache_exists(QRCODE_PATH));
    if (a == JIANLU_DL_NEED && !(s_dl_inflight & 2)) {
        s_dl_inflight |= 2;
        request_job(JOB_QRCODE);
    } else if (a == JIANLU_DL_INVALIDATE) {
        remove(QRCODE_PATH);
        s_nvs.qrcode_ver = 0;
        jianlu_nvs_save_image_ver(false, 0);
        if (s_nav.page == JIANLU_PAGE_QR && bsp_lvgl_lock(500)) {
            jianlu_ui_qr_set(false, &s_profile);
            bsp_lvgl_unlock();
        }
    }
}

static void request_job(job_type_t type)
{
    if (!s_job_queue) return;
    net_job_t job = { .type = type };
    (void)xQueueSend(s_job_queue, &job, 0);
}

// 主页内容装配并刷新(资料 + 清单 + 待同步数 + 时间 + 头像缓存状态)
static void refresh_home(void)
{
    jianlu_home_model_t model;
    jianlu_home_build(&s_store,
                      jianlu_capture_queue_count() + s_syncq.count,
                      (uint32_t)time(NULL), &model);
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_home_set(&s_profile, &model,
                           jianlu_hub_cache_exists(AVATAR_PATH));
        bsp_lvgl_unlock();
    }
}

// 页面切换(v2 导航):进简录自动拉取(fresh);进二维码有网即刷新缓存
static void switch_page(void)
{
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_show_page((jianlu_ui_page_t)s_nav.page);
        bsp_lvgl_unlock();
    }
    switch (s_nav.page) {
    case JIANLU_PAGE_JIANLU:
        if (s_wifi_connected && jianlu_hub_base()[0] != '\0' && !s_fetch_busy) {
            s_nf = JIANLU_NF_LOADING;
            apply_view(JIANLU_ANIM_NONE);
            request_fetch();
        }
        break;
    case JIANLU_PAGE_QR:
        if (bsp_lvgl_lock(500)) {
            jianlu_ui_qr_set(jianlu_hub_cache_exists(QRCODE_PATH), &s_profile);
            bsp_lvgl_unlock();
        }
        if (s_wifi_connected && jianlu_hub_base()[0] != '\0') {
            maybe_download_images();   // 版本未变则跳过,秒进
        }
        break;
    case JIANLU_PAGE_SETTINGS:
        if (bsp_lvgl_lock(500)) {
            jianlu_ui_settings_set(s_nav.settings_focus, s_brightness,
                                   s_keep_on, jianlu_ui_theme());
            bsp_lvgl_unlock();
        }
        break;
    default:   // 主页
        // 有网就顺带刷新资料(Web 端可能刚改过昵称/头像);失败用现有资料,不阻塞渲染
        if (s_wifi_connected && jianlu_hub_base()[0] != '\0') {
            request_job(JOB_PROFILE);
        }
        refresh_home();
        if (bsp_lvgl_lock(500)) {
            jianlu_ui_home_focus(s_nav.home_focus);
            bsp_lvgl_unlock();
        }
        break;
    }
    ui_refresh(JIANLU_ANIM_NONE);
}

// 亮度档:25/50/75/100 循环
static const int BRIGHTNESS_LEVELS[] = { 25, 50, 75, 100 };
#define BRIGHTNESS_LEVELS_N 4

static void settings_refresh(void)
{
    if (s_nav.page == JIANLU_PAGE_SETTINGS && bsp_lvgl_lock(500)) {
        jianlu_ui_settings_set(s_nav.settings_focus, s_brightness, s_keep_on,
                               jianlu_ui_theme());
        bsp_lvgl_unlock();
    }
}

static void brightness_apply(int pct)
{
    s_brightness = pct;
    bsp_display_backlight((uint8_t)pct);
    jianlu_nvs_save_brightness((uint8_t)pct);
    settings_refresh();
}

static void brightness_adjust(int dir)
{
    int idx = 0;
    for (int i = 0; i < BRIGHTNESS_LEVELS_N; i++) {
        if (BRIGHTNESS_LEVELS[i] >= s_brightness) { idx = i; break; }
        idx = i;
    }
    idx = (idx + dir + BRIGHTNESS_LEVELS_N) % BRIGHTNESS_LEVELS_N;
    brightness_apply(BRIGHTNESS_LEVELS[idx]);
    ESP_LOGI(TAG, "亮度 %d%%", s_brightness);
}

static void show_about(void)
{
    char sha[32];
    char ver[64];
    esp_err_t err = esp_app_get_elf_sha256(sha, sizeof(sha));
    if (err == ESP_OK) {
        snprintf(ver, sizeof(ver), "小诺简录\n固件 %02x%02x%02x%02x\nESP-IDF 5.5.3",
                 sha[0], sha[1], sha[2], sha[3]);
    } else {
        snprintf(ver, sizeof(ver), "小诺简录\nESP-IDF 5.5.3");
    }
    s_info_shown = true;
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_about(ver);
        bsp_lvgl_unlock();
    }
}

static void keepon_toggle(void)
{
    s_keep_on = !s_keep_on;
    jianlu_ps_set_keep_on(&s_ps, s_keep_on);
    jianlu_nvs_save_keep_on(s_keep_on ? 1 : 0);
    if (s_keep_on) {
        bsp_display_backlight((uint8_t)s_brightness);   // 开常亮立即点亮
        s_screen_off = false;
    }
    settings_refresh();
    ESP_LOGI(TAG, "屏幕常亮: %s", s_keep_on ? "开" : "关");
}

static void settings_action(void)
{
    switch (s_nav.settings_focus) {
    case JIANLU_SETTINGS_ROW_BRIGHTNESS:
        brightness_adjust(1);   // OK 也循环升档
        break;
    case JIANLU_SETTINGS_ROW_THEME: {
        int next = (jianlu_ui_theme() + 1) % JIANLU_THEME_COUNT;
        jianlu_nvs_save_theme((uint8_t)next);
        jianlu_ui_set_theme(next);   // 整屏重建,允许短暂重绘
        switch_page();               // 重建后恢复当前页内容
        ESP_LOGI(TAG, "色彩主题: %s", jianlu_theme_name(next));
        break;
    }
    case JIANLU_SETTINGS_ROW_KEEPON:
        keepon_toggle();
        break;
    case JIANLU_SETTINGS_ROW_SYNC:
        if (s_wifi_connected && jianlu_hub_base()[0] != '\0') {
            request_job(JOB_PROFILE);   // 顺带拉资料(昵称/头像可能刚改)
        }
        if (jianlu_capture_queue_count() + s_syncq.count > 0) {
            ESP_LOGI(TAG, "设置页触发立即同步");
            drain_start();
        } else {
            if (s_wifi_connected && jianlu_hub_base()[0] != '\0') {
                request_fetch();
            }
            if (bsp_lvgl_lock(500)) {
                jianlu_ui_overlay_flash("已是最新", jianlu_ui_accent(), "没有待同步内容");
                bsp_lvgl_unlock();
            }
        }
        break;
    case JIANLU_SETTINGS_ROW_ABOUT:
        show_about();
        break;
    case JIANLU_SETTINGS_ROW_REPROV:
        reprov_overlay_show();
        break;
    }
}

// 网络/录音任务延迟到联网后再创建:配网期 Wi-Fi(67KB)与 NimBLE(~40KB)
// 必须共存,堆预算容不下这两个任务的栈。幂等。
static bool ensure_worker_tasks(void)
{
    if (s_job_queue) return true;
    s_job_queue = xQueueCreate(JOB_QUEUE_DEPTH, sizeof(net_job_t));
    s_rec_queue = xQueueCreate(1, sizeof(rec_cmd_type_t));
    if (!s_job_queue || !s_rec_queue) return false;
    if (xTaskCreate(net_task, "jianlu_net", 5120, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(rec_task, "jianlu_rec", 3072, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "网络/录音任务创建失败");
        return false;
    }
    ESP_LOGI(TAG, "网络/录音任务已创建(堆 %u)", (unsigned)esp_get_free_heap_size());
    return true;
}

static void enter_provisioning(void)
{
    if (s_retry_timer) esp_timer_stop(s_retry_timer);
    if (!s_prov_running) {
        esp_err_t err = jianlu_provision_start(on_prov_event, NULL);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "BLUFI 启动失败: %s", esp_err_to_name(err));
            set_error("配网模块启动失败");
            return;
        }
        s_prov_running = true;
    }
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_provision_info(jianlu_provision_device_name(), false);
        bsp_lvgl_unlock();
    }
    apply_view(JIANLU_ANIM_NONE);
}

static void stop_provisioning(void)
{
    if (!s_prov_running) return;
    if (s_wifi_connected) jianlu_provision_report_wifi(true);
    jianlu_provision_stop();
    s_prov_running = false;
}

static esp_err_t wifi_teardown(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    return esp_wifi_deinit();
}

// 中枢地址确定:设置 → (mDNS 结果存 NVS)→ LOADING。
// 有离线内容(语音队列/离线勾选)时显式提示,不自动补传。
static void hub_ready(const char *url, jianlu_hub_src_t src)
{
    jianlu_hub_set_base(url);
    if (src == JIANLU_HUB_MDNS) jianlu_nvs_save_hub(url);
    ESP_LOGI(TAG, "中枢: %s(来源 %d)", url, (int)src);
    jianlu_netflow_event(&s_nf, JIANLU_NF_HUB_READY);
    apply_view(JIANLU_ANIM_NONE);

    int pending = jianlu_capture_queue_count() + s_syncq.count;
    if (pending > 0 && !s_drain_active) {
        s_sync_prompt = true;
        char body[48];
        snprintf(body, sizeof(body), "共 %d 条待同步", pending);
        if (bsp_lvgl_lock(500)) {
            jianlu_ui_overlay("发现离线内容", jianlu_ui_accent(), body,
                              "OK 开始同步 · 其他键跳过");
            bsp_lvgl_unlock();
        }
        ESP_LOGI(TAG, "发现 %d 条离线内容,等待用户确认同步", pending);
    }
    request_fetch();   // 清单照常拉取(提示页覆盖其上)
    request_job(JOB_PROFILE);   // 资料(昵称/签名/头像/二维码清单)
    request_job(JOB_DIAG);      // 网络诊断矩阵(每开机一次)
}

// OK 确认开始补传:先语音队列(逐条进度),再离线勾选,最后汇总
static void drain_start(void)
{
    s_sync_prompt = false;
    s_drain_active = true;
    s_drain_done = 0;
    s_drain_new_cards = 0;
    s_drain_syncq_total = s_syncq.count;
    s_drain_syncq_done = 0;
    s_drain_total = jianlu_capture_queue_count() + s_syncq.count;
    ESP_LOGI(TAG, "开始同步 %d 条离线内容", s_drain_total);
    if (jianlu_capture_queue_count() > 0) {
        request_capture();
    } else if (s_syncq.count > 0) {
        net_job_t job = { .type = JOB_SYNC };
        (void)xQueueSend(s_job_queue, &job, 0);
    }
}

static void drain_progress_show(const char *transcript)
{
    char body[96];
    if (transcript != NULL && transcript[0] != '\0') {
        char prefix[32];
        jianlu_utf8_copy(prefix, sizeof(prefix), transcript, 24);
        snprintf(body, sizeof(body), "正在识别 %d/%d:%s",
                 s_drain_done, s_drain_total, prefix);
    } else {
        snprintf(body, sizeof(body), "正在识别 %d/%d…",
                 s_drain_done, s_drain_total);
    }
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_overlay("正在同步", jianlu_ui_accent(), body, "请稍候");
        bsp_lvgl_unlock();
    }
}

static void drain_summary_show(void)
{
    s_drain_active = false;
    s_summary_shown = true;
    jianlu_tone_play(JIANLU_TONE_SUCCESS);
    char body[96];
    snprintf(body, sizeof(body), "新增 %d 张卡片 · %d 条勾选已同步",
             s_drain_new_cards, s_drain_syncq_done);
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_overlay("同步完成", jianlu_ui_accent(), body, "OK 返回");
        bsp_lvgl_unlock();
    }
    ESP_LOGI(TAG, "同步完成: 新增 %d 张卡片,勾选 %d 条",
             s_drain_new_cards, s_drain_syncq_done);
}

static void drain_abort_show(void)
{
    s_drain_active = false;
    s_summary_shown = true;   // OK 返回逻辑同汇总页
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_overlay("同步中断", 0xD96A5A,
                          "网络不通,剩余内容已保留", "OK 返回");
        bsp_lvgl_unlock();
    }
}

static void offline_retry_cb(void *arg)
{
    (void)arg;
    post_event(EV_OFFLINE_RETRY, 0, 0, NULL);
}

// 队列录音 ⇄ 清单顶部占位卡(每条一卡,可单独回放)同步;
// 标题带录音时刻(已对时)或"待同步"(未对时)
static void sync_voice_placeholder(void)
{
    int n = jianlu_store_set_voice_placeholders(&s_store,
                                                jianlu_capture_queue_count());
    for (int i = 0; i < n; i++) {
        uint32_t ts = jianlu_capture_slot_ts(i + 1);
        char title[JIANLU_TITLE_LEN];
        if (jianlu_time_is_valid(ts)) {
            char when[16];
            jianlu_time_format_mmdd_hhmm(ts, when, sizeof(when));
            snprintf(title, sizeof(title), "离线语音 %s", when);
        } else {
            snprintf(title, sizeof(title), "离线语音 · 待同步");
        }
        jianlu_utf8_copy(s_store.records[i].title, JIANLU_TITLE_LEN, title,
                         JIANLU_TITLE_LEN - 1);
    }
}

// 进入离线模式(快照可用时):装快照、定期重试;无快照返回 false
static bool enter_offline(void)
{
    if (!jianlu_offline_load_snapshot(&s_store)) return false;
    sync_voice_placeholder();
    s_nf = JIANLU_NF_READY;
    ui_refresh(JIANLU_ANIM_NONE);
    if (!s_offline_timer) {
        esp_timer_create_args_t args = {
            .callback = offline_retry_cb,
            .name = "offline_retry",
        };
        if (esp_timer_create(&args, &s_offline_timer) != ESP_OK) return true;
    }
    esp_timer_stop(s_offline_timer);
    esp_timer_start_periodic(s_offline_timer, 30000ULL * 1000);
    return true;
}

static void leave_offline(void)
{
    s_store.offline = false;
    if (s_offline_timer) esp_timer_stop(s_offline_timer);
}

// GOT_IP:停 BLUFI、按优先级确定中枢
static void on_got_ip(void)
{
    s_connect_fails = 0;
    stop_provisioning();
    jianlu_netflow_event(&s_nf, JIANLU_NF_GOT_IP);
    apply_view(JIANLU_ANIM_NONE);
    ESP_LOGI(TAG, "堆: got_ip 时 %u", (unsigned)esp_get_free_heap_size());
    // 趁堆宽裕先把音频 I2S DMA 拿下(首次录音再分配可能失败)
    if (jianlu_capture_prepare_audio() != ESP_OK) {
        ESP_LOGW(TAG, "音频初始化失败,语音记录将不可用");
    }
    ESP_LOGI(TAG, "堆: 音频后 %u", (unsigned)esp_get_free_heap_size());
    if (!ensure_worker_tasks()) {
        set_error("系统资源不足");
        return;
    }

    jianlu_hub_src_t src = jianlu_config_pick_hub(CONFIG_XIAONUO_HUB_URL,
                                                  NULL, s_nvs.hub_url);
    if (src == JIANLU_HUB_KCONFIG) {
        hub_ready(CONFIG_XIAONUO_HUB_URL, src);   // 开发覆盖,跳过 mDNS
    } else {
        request_discover();   // mDNS 主路径;NVS 缓存在失败时兜底
    }
}

// ---------------------------------------------------------------------------
// 语音 UI 与确认页
// ---------------------------------------------------------------------------
static void confirm_timer_cb(void *arg)
{
    (void)arg;
    post_event(EV_CONFIRM_TIMEOUT, 0, 0, NULL);
}

static void schedule_confirm_dismiss(void)
{
    if (!s_confirm_timer) {
        esp_timer_create_args_t args = {
            .callback = confirm_timer_cb,
            .name = "confirm_dismiss",
        };
        if (esp_timer_create(&args, &s_confirm_timer) != ESP_OK) return;
    }
    esp_timer_stop(s_confirm_timer);
    esp_timer_start_once(s_confirm_timer, CONFIRM_TIMEOUT_MS * 1000ULL);
}

static bool voice_ui(jianlu_voice_state_t state, int elapsed)
{
    bool paged = false;
    if (!bsp_lvgl_lock(500)) return false;
    switch (state) {
    case JIANLU_VOICE_RECORDING: jianlu_ui_voice_recording(elapsed); break;
    case JIANLU_VOICE_SENDING:   jianlu_ui_voice_sending(); break;
    case JIANLU_VOICE_CONFIRM:
        paged = jianlu_ui_confirm_present(&s_capture_result);
        break;
    case JIANLU_VOICE_ERROR:     jianlu_ui_voice_error(); break;
    default:                     jianlu_ui_voice_idle(); break;
    }
    bsp_lvgl_unlock();
    return paged;
}

// 语音确认/失败页关闭:回列表并刷新
static void voice_dismiss(void)
{
    if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_DISMISS)) {
        if (s_confirm_timer) esp_timer_stop(s_confirm_timer);
        voice_ui(s_voice.state, 0);
        // 新鲜录音处理完,队列里还有存量 → 重新提示显式同步
        if (jianlu_capture_queue_count() > 0 && !s_drain_active) {
            s_sync_prompt = true;
            char body[48];
            snprintf(body, sizeof(body), "共 %d 条待同步",
                     jianlu_capture_queue_count() + s_syncq.count);
            if (bsp_lvgl_lock(500)) {
                jianlu_ui_overlay("发现离线内容", jianlu_ui_accent(), body,
                                  "OK 开始同步 · 其他键跳过");
                bsp_lvgl_unlock();
            }
            return;
        }
        if (s_wifi_connected && jianlu_hub_base()[0] != '\0') {
            s_nf = JIANLU_NF_LOADING;
            apply_view(JIANLU_ANIM_NONE);
            request_fetch();
        }
    }
}

// ---------------------------------------------------------------------------
// 重配(UP 长按 → 确认页 → OK 确认)
// ---------------------------------------------------------------------------
static void reprov_overlay_show(void)
{
    s_reprov_confirm = true;
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_overlay("重新配网?", jianlu_ui_accent() /* UI_ACCENT */,
                          "将清除已保存的\nWi-Fi 与中枢信息",
                          "OK 确认 · 其他键取消");
        bsp_lvgl_unlock();
    }
}

static void reprov_overlay_handle(const app_ev_t *ev)
{
    bool confirm = ev->btn == BSP_BTN_OK && ev->btn_ev == BSP_BTN_CLICK;
    s_reprov_confirm = false;
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_overlay_hide();
        bsp_lvgl_unlock();
    }
    if (!confirm) return;

    ESP_LOGI(TAG, "用户确认重配:清凭据+置强制配网标志,重启进配网态");
    jianlu_nvs_clear_provisioning();
    jianlu_nvs_save_reprov(1);   // 粘性:开机必进配网,即使 Kconfig 还有旧凭据
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

// ---------------------------------------------------------------------------
// 按键分发
// ---------------------------------------------------------------------------
static void on_key_event(const app_ev_t *ev)
{
    // 省电:唤醒键与唤醒手势的后续事件全部吞掉(灭屏按 OK 不会误触发完成)
    bool gesture_end = ev->btn_ev != BSP_BTN_PRESS;
    if (jianlu_ps_key(&s_ps, gesture_end)) {
        if (s_screen_off) {
            bsp_display_backlight(100);
            s_screen_off = false;
        }
        return;
    }
    // 回放中:任意键停止
    if (s_playing) {
        s_play_stop = true;
        return;
    }
    // 补传进行中:屏蔽按键(进度/汇总由补传流程驱动)
    if (s_drain_active) {
        return;
    }
    // 汇总/中断页 + 「发现离线内容」提示 + 「关于」:模态按键路由表(jianlu_modal)
    {
        jianlu_modal_t modal = s_sync_prompt ? JIANLU_MODAL_SYNC_PROMPT
                             : s_summary_shown ? JIANLU_MODAL_SUMMARY
                             : s_info_shown ? JIANLU_MODAL_INFO
                             : JIANLU_MODAL_NONE;
        jianlu_modal_act_t act = jianlu_modal_key(modal, (int)ev->btn,
                                                  (int)ev->btn_ev);
        if (act != JIANLU_MODAL_PASS) {
            switch (act) {
            case JIANLU_MODAL_SWALLOW:
                return;   // PRESS/双击/长按:吞掉不关页
            case JIANLU_MODAL_SKIP:
                s_sync_prompt = false;
                if (bsp_lvgl_lock(500)) {
                    jianlu_ui_overlay_hide();
                    bsp_lvgl_unlock();
                }
                return;
            case JIANLU_MODAL_START_SYNC:
                s_sync_prompt = false;
                if (bsp_lvgl_lock(500)) {
                    jianlu_ui_overlay_hide();
                    bsp_lvgl_unlock();
                }
                drain_start();
                return;
            case JIANLU_MODAL_DISMISS:
                s_summary_shown = false;
                s_info_shown = false;
                if (bsp_lvgl_lock(500)) {
                    jianlu_ui_overlay_hide();
                    bsp_lvgl_unlock();
                }
                if (s_wifi_connected && jianlu_hub_base()[0] != '\0') {
                    s_nf = JIANLU_NF_LOADING;
                    apply_view(JIANLU_ANIM_NONE);
                    request_fetch();
                }
                return;
            default:
                return;
            }
        }
    }
    // 分页查看器:UP/DOWN 翻页,OK 关闭并返回
    if (jianlu_ui_pager_is_open()) {
        if (ev->btn_ev != BSP_BTN_CLICK) return;
        if (bsp_lvgl_lock(500)) {
            if (ev->btn == BSP_BTN_UP) {
                jianlu_ui_pager_prev();
            } else if (ev->btn == BSP_BTN_DOWN) {
                jianlu_ui_pager_next();
            }
            if (ev->btn == BSP_BTN_OK) jianlu_ui_pager_close();
            bsp_lvgl_unlock();
        }
        if (ev->btn == BSP_BTN_OK) voice_dismiss();
        return;
    }
    // 重配确认页拦截一切按键
    if (s_reprov_confirm) {
        if (ev->btn_ev == BSP_BTN_CLICK) reprov_overlay_handle(ev);
        return;
    }
    // 语音流程中的按键优先于清单操作
    if (s_voice.state != JIANLU_VOICE_IDLE) {
        if (ev->btn == BSP_BTN_OK && ev->btn_ev == BSP_BTN_CLICK) voice_dismiss();
        return;   // 录音/上传中忽略其余按键
    }

    if (ev->btn == BSP_BTN_UP && ev->btn_ev == BSP_BTN_LONG) {
        reprov_overlay_show();   // 重配入口(任意视图可用)
        return;
    }
    if (ev->btn == BSP_BTN_OK && ev->btn_ev == BSP_BTN_LONG) {
        // 按住说话开始(松开由录音任务轮询 ADC 判定)
        if (jianlu_hub_base()[0] == '\0' || !s_rec_queue) return;   // 中枢未确定,语音无意义
        if (jianlu_capture_queue_full()) {
            // 队列满:拒录并提示(绝不静默覆盖最老录音)
            ESP_LOGW(TAG, "语音队列已满,拒绝录音");
            jianlu_tone_play(JIANLU_TONE_FAIL);
            if (bsp_lvgl_lock(500)) {
                jianlu_ui_overlay_flash("队列已满", 0xD96A5A, "请先联网同步语音");
                bsp_lvgl_unlock();
            }
            return;
        }
        if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_HOLD_START)) {
            ESP_LOGI(TAG, "开始录音(按住说话)");
            jianlu_tone_play(JIANLU_TONE_REC_START);
            voice_ui(s_voice.state, 0);
            rec_cmd_type_t cmd = REC_CMD_START;
            (void)xQueueSend(s_rec_queue, &cmd, 0);
        }
        return;
    }
    // 页面导航(纯路由表 jianlu_nav):简录页之外全部由它接管;
    // 进简录自动拉取(替代原 OK 双击刷新),子页 OK 双击回主页
    jianlu_nav_result_t nav = jianlu_nav_key(&s_nav, (int)ev->btn,
                                             (int)ev->btn_ev);
    switch (nav) {
    case JIANLU_NAV_FOCUS_CHANGED:
        if (bsp_lvgl_lock(500)) {
            if (s_nav.page == JIANLU_PAGE_HOME) {
                jianlu_ui_home_focus(s_nav.home_focus);
            } else if (s_nav.page == JIANLU_PAGE_SETTINGS) {
                jianlu_ui_settings_set(s_nav.settings_focus, s_brightness,
                                       s_keep_on, jianlu_ui_theme());
            }
            bsp_lvgl_unlock();
        }
        return;
    case JIANLU_NAV_PAGE_CHANGED:
        switch_page();
        return;
    case JIANLU_NAV_SETTINGS_ACTION:
        settings_action();
        return;
    default:
        break;   // NO_CHANGE:简录页交给卡片堆
    }
    if (s_nav.page != JIANLU_PAGE_JIANLU) return;
    if (ev->btn_ev != BSP_BTN_CLICK) return;
    if (s_fetch_busy) return;   // 清单重写中,忽略清单操作
    if (s_store.view != JIANLU_VIEW_READY || s_store.count == 0) return;

    if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN) {
        jianlu_store_move(&s_store, ev->btn == BSP_BTN_UP ? -1 : 1);
        ui_refresh(ev->btn == BSP_BTN_UP ? JIANLU_ANIM_PREV : JIANLU_ANIM_NEXT);
    } else if (ev->btn == BSP_BTN_OK) {
        const jianlu_record_t *rec = jianlu_store_selected(&s_store);
        if (rec == NULL) return;
        if (jianlu_store_is_voice_placeholder(rec)) {
            // 语音占位卡:OK = 本地回放对应槽位
            if (s_playing || !s_rec_queue) return;
            s_playing = true;
            s_play_slot = rec->voice_slot;
            if (bsp_lvgl_lock(500)) {
                jianlu_ui_overlay("播放中", jianlu_ui_accent(), "语音 · 未识别",
                                  "任意键停止");
                bsp_lvgl_unlock();
            }
            rec_cmd_type_t cmd = REC_CMD_PLAY;
            (void)xQueueSend(s_rec_queue, &cmd, 0);
            return;
        }
        if (rec->completing || rec->sync_pending) return;
        char id[JIANLU_ID_LEN];
        jianlu_utf8_copy(id, sizeof(id), rec->id, sizeof(id) - 1);
        if (s_store.offline) {
            // 离线勾选:记待同步队列,联网后批量 PUT(last-write-wins)
            jianlu_store_set_completing(&s_store, id, false);
            jianlu_store_set_sync_pending(&s_store, id, true);
            jianlu_syncq_add(&s_syncq, id);
            jianlu_offline_save_syncq(&s_syncq);
            ESP_LOGI(TAG, "离线勾选 id=%s,待同步 %d 条", id, s_syncq.count);
            ui_refresh(JIANLU_ANIM_NONE);
            return;
        }
        jianlu_store_set_completing(&s_store, id, true);
        ui_refresh(JIANLU_ANIM_NONE);   // 先划线,PUT 成功后再飞出
        net_job_t job = { .type = JOB_COMPLETE };
        jianlu_utf8_copy(job.id, sizeof(job.id), id, sizeof(job.id) - 1);
        (void)xQueueSend(s_job_queue, &job, 0);
    }
}

// ---------------------------------------------------------------------------
// 事件主循环
// ---------------------------------------------------------------------------
// 进 light sleep(GPIO0 低电平=任意键唤醒),返回即已醒
static void light_sleep_once(void)
{
    ESP_LOGI(TAG, "空闲 %ds,进入 light sleep", JIANLU_PS_LIGHT_SLEEP_S);
    bsp_display_backlight(0);
    // GPIO0 平时被按键 ADC 占用(数字输入缓冲关闭,读回恒低,会瞬间满足唤醒
    // 条件),先切回数字输入:外部上拉使空闲为高,按下任意键拉低触发唤醒。
    gpio_reset_pin(GPIO_NUM_0);
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    // 睡前摘狗:睡眠中无法喂,醒后第一时间挂回并喂,杜绝醒后竞态误触发
    esp_task_wdt_delete(NULL);
    esp_light_sleep_start();
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    // 醒后恢复按键 ADC(bsp_button_init 可重复调用)
    bsp_button_init(on_key, NULL);
    jianlu_ps_woke(&s_ps);
    bsp_display_backlight(100);
    s_screen_off = false;
    ESP_LOGI(TAG, "已唤醒");
    ui_refresh(JIANLU_ANIM_NONE);
}

static void app_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);
    app_ev_t ev;
    for (;;) {
        esp_task_wdt_reset();
        // 省电:忙(录音/上传/配网/回放)不累计空闲
        jianlu_ps_set_busy(&s_ps,
            s_voice.state != JIANLU_VOICE_IDLE || s_prov_running || s_playing);
        if (xQueueReceive(s_ev_queue, &ev, pdMS_TO_TICKS(1000)) != pdTRUE) {
            switch (jianlu_ps_tick(&s_ps)) {
            case JIANLU_PS_ACT_SCREEN_OFF:
                ESP_LOGI(TAG, "空闲 %ds,熄屏", JIANLU_PS_SCREEN_OFF_S);
                bsp_display_backlight(0);
                s_screen_off = true;
                break;
            case JIANLU_PS_ACT_LIGHT_SLEEP:
                light_sleep_once();
                break;
            default:
                break;
            }
            continue;
        }
        switch (ev.type) {
        case EV_KEY:
            on_key_event(&ev);
            break;
        case EV_WIFI_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi 已连接");
            on_got_ip();
#if VOICE_LINK_SELFTEST
            if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_HOLD_START)) {
                ESP_LOGI(TAG, "[SELFTEST] 自动开始录音 3s");
                rec_cmd_type_t cmd = REC_CMD_START;
                (void)xQueueSend(s_rec_queue, &cmd, 0);
            }
#endif
            break;
        case EV_WIFI_DISCONNECTED:
            if (s_nf == JIANLU_NF_READY) {
                ESP_LOGW(TAG, "Wi-Fi 掉线,%dms 后重连", WIFI_RETRY_MS);
                jianlu_netflow_event(&s_nf, JIANLU_NF_DISCONNECT);
                apply_view(JIANLU_ANIM_NONE);
                schedule_wifi_retry();
            } else if (s_nf == JIANLU_NF_PROVISIONING) {
                break;   // 配网态下 BLUFI 主导连接,不重试
            } else {
                s_connect_fails++;
                ESP_LOGW(TAG, "连接失败 %d/%d", s_connect_fails, WIFI_FAIL_TO_PROVISION);
                if (s_connect_fails >= WIFI_FAIL_TO_PROVISION) {
                    if (enter_offline()) {
                        // 有快照:路由器可能只是暂时不在,留离线模式并继续后台重连,
                        // 而不是逼用户重配(重配只在没有快照可用时发生)
                        ESP_LOGW(TAG, "连接屡败,有本地快照,进入离线模式");
                        schedule_wifi_retry();
                    } else if (jianlu_netflow_event(&s_nf, JIANLU_NF_CONNECT_FAIL)) {
                        ESP_LOGW(TAG, "连接屡败(凭据可能失效),转入配网态");
                        // 配网/联网分离:先释放 Wi-Fi 的 ~56KB,BLUFI 才有地方住
                        wifi_teardown();
                        enter_provisioning();
                    }
                } else {
                    schedule_wifi_retry();
                }
            }
            break;
        case EV_DISCOVER_DONE:
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                hub_ready(s_mdns_url, JIANLU_HUB_MDNS);
            } else if (jianlu_config_pick_hub(NULL, NULL, s_nvs.hub_url)
                       == JIANLU_HUB_NVS) {
                ESP_LOGW(TAG, "mDNS 未发现中枢,用 NVS 缓存地址兜底");
                hub_ready(s_nvs.hub_url, JIANLU_HUB_NVS);
            } else if (jianlu_fetch_fail_view(true) == JIANLU_FAIL_OFFLINE
                       && enter_offline()) {
                ESP_LOGW(TAG, "未发现中枢,快照离线模式");
            } else {
                set_error("未在局域网发现小诺中枢\n请确认中枢已启动");
            }
            break;
        case EV_FETCH_DONE:
            // 网络任务已直接更新 s_store(期间 s_fetch_busy 挡住了读方)
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                jianlu_netflow_event(&s_nf, JIANLU_NF_FETCH_OK);
                leave_offline();
                jianlu_store_set_view(&s_store, JIANLU_VIEW_READY, NULL);
                sync_voice_placeholder();
                const jianlu_record_t *after = jianlu_store_selected(&s_store);
                bool arrived = after != NULL
                            && s_prev_top_id[0] != '\0'
                            && strcmp(s_prev_top_id, after->id) != 0;
                ui_refresh(arrived ? JIANLU_ANIM_ARRIVE : JIANLU_ANIM_NONE);
                refresh_home();   // 主页待办/待同步条随之更新
            } else {
                // 策略见 jianlu_fetch_fail_view(host 测试矩阵):
                // 有快照必走离线模式,不论此前是何视图
                if (jianlu_fetch_fail_view(true) == JIANLU_FAIL_OFFLINE
                    && enter_offline()) {
                    ESP_LOGW(TAG, "中枢不可达,进入离线模式");
                } else {
                    jianlu_netflow_event(&s_nf, JIANLU_NF_FETCH_FAIL);
                    jianlu_store_set_view(&s_store, JIANLU_VIEW_ERROR, s_fetch_err);
                    ui_refresh(JIANLU_ANIM_NONE);
                }
            }
            break;
        case EV_COMPLETE_DONE:
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                jianlu_tone_play(JIANLU_TONE_COMPLETE);
                if (bsp_lvgl_lock(500)) {
                    jianlu_ui_success_flash();
                    bsp_lvgl_unlock();
                }
                jianlu_store_remove(&s_store, ev.id);
                ui_refresh(JIANLU_ANIM_COMPLETE);   // 顶卡飞出
            } else {
                jianlu_tone_play(JIANLU_TONE_FAIL);
                jianlu_store_set_completing(&s_store, ev.id, false);
                ui_refresh(JIANLU_ANIM_NONE);
            }
            break;
        case EV_PROV:
            switch ((jianlu_prov_ev_t)ev.arg1) {
            case JIANLU_PROV_BLE_CONNECT:
                if (bsp_lvgl_lock(500)) {
                    jianlu_ui_provision_info(jianlu_provision_device_name(), true);
                    bsp_lvgl_unlock();
                }
                ui_refresh(JIANLU_ANIM_NONE);
                break;
            case JIANLU_PROV_BLE_DISCONNECT:
                if (bsp_lvgl_lock(500)) {
                    jianlu_ui_provision_info(jianlu_provision_device_name(), false);
                    bsp_lvgl_unlock();
                }
                ui_refresh(JIANLU_ANIM_NONE);
                break;
            case JIANLU_PROV_GOT_WIFI:
                ESP_LOGI(TAG, "收到 Wi-Fi 凭据(ssid=%s),存 NVS 并重启进入联网",
                         s_prov_ssid);
                jianlu_nvs_save_wifi(s_prov_ssid, s_prov_pass);
                jianlu_nvs_save_reprov(0);   // 配网完成,清除强制标志
                vTaskDelay(pdMS_TO_TICKS(300));   // 让日志与 BLE 事件发完
                esp_restart();
                break;
            case JIANLU_PROV_CUSTOM_DATA:
                // 手动指定中枢后备:custom data 形如 "hub=http://192.168.1.10:3000"
                if (strncmp(s_prov_data, "hub=", 4) == 0 && s_prov_data[4] != '\0') {
                    ESP_LOGI(TAG, "收到手动中枢地址: %s", s_prov_data + 4);
                    jianlu_nvs_save_hub(s_prov_data + 4);
                    jianlu_utf8_copy(s_nvs.hub_url, sizeof(s_nvs.hub_url),
                                     s_prov_data + 4, sizeof(s_nvs.hub_url) - 1);
                } else {
                    ESP_LOGW(TAG, "忽略无法识别的自定义数据");
                }
                break;
            case JIANLU_PROV_FAILED:
                s_prov_running = false;
                set_error("配网模块异常,请重启设备");
                break;
            }
            break;
        case EV_REC_TICK:
            if (s_voice.state == JIANLU_VOICE_RECORDING) {
                voice_ui(s_voice.state, (int)ev.arg1);
            }
            break;
        case EV_REC_DONE: {
            s_voice.recorded_bytes = ev.arg2 > 0 ? (size_t)ev.arg2 : 0;
            bool send = (esp_err_t)ev.arg1 == ESP_OK;
            if (send) send = jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_STOP)
                          && s_voice.state == JIANLU_VOICE_SENDING;
            if (!send) {
                // 误触太短(文件已入槽)才需要清尾槽;拒录/读错误根本未入槽
                if ((esp_err_t)ev.arg1 == ESP_OK) jianlu_capture_discard_tail();
                sync_voice_placeholder();
                ui_refresh(JIANLU_ANIM_NONE);
                voice_ui(JIANLU_VOICE_IDLE, 0);
                break;
            }
            ESP_LOGI(TAG, "录音 %d 字节,开始上传", (int)s_voice.recorded_bytes);
            sync_voice_placeholder();   // 新录音入槽,占位卡立即可见(可回放)
            ui_refresh(JIANLU_ANIM_NONE);
            voice_ui(s_voice.state, 0);
            request_capture();
            break;
        }
        case EV_CAPTURE_DONE: {
            esp_err_t err = (esp_err_t)ev.arg1;
            sync_voice_placeholder();   // 任何结果都把队列状态落到 UI(修占位卡残影)
            bool server_reject = err == ESP_ERR_INVALID_RESPONSE
                              || err == ESP_ERR_INVALID_SIZE;
            if (err == ESP_OK) {
                if (s_drain_active) {
                    s_drain_done++;
                    s_drain_new_cards += s_capture_result.new_count;
                    drain_progress_show(s_capture_result.transcript);
                }
                if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_SEND_OK)) {
                    jianlu_tone_play(JIANLU_TONE_SUCCESS);
                    // 内容超一页进分页模式:取消自动返回,等用户翻页读完
                    if (!voice_ui(s_voice.state, 0)) {
                        schedule_confirm_dismiss();
                    } else {
                        ESP_LOGI(TAG, "确认内容超一页,进入分页查看(UP/DOWN 翻页)");
                    }
                }
            } else {
                if (s_drain_active) {
                    if (server_reject) {
                        s_drain_done++;   // 拒收也算处理过(文件已删)
                        drain_progress_show(NULL);
                    } else {
                        drain_abort_show();   // 传输层失败:保留剩余,停止本轮
                    }
                }
                if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_SEND_FAIL)) {
                    jianlu_tone_play(JIANLU_TONE_FAIL);
                    voice_ui(s_voice.state, 0);
                    schedule_confirm_dismiss();
                }
            }
            ui_refresh(JIANLU_ANIM_NONE);
            refresh_home();   // 占位卡变化 → 主页待同步条更新
            // 显式补传循环:成功/拒收 → 续下一条;空队列 → 勾选回放 → 汇总
            if (s_drain_active) {
                if (jianlu_capture_queue_count() > 0) {
                    request_capture();
                } else if (s_syncq.count > 0) {
                    net_job_t job = { .type = JOB_SYNC };
                    (void)xQueueSend(s_job_queue, &job, 0);
                } else {
                    drain_summary_show();
                }
            }
            break;
        }
        case EV_PLAY_DONE:
            s_playing = false;
            if (bsp_lvgl_lock(500)) {
                jianlu_ui_overlay_hide();
                bsp_lvgl_unlock();
            }
            ui_refresh(JIANLU_ANIM_NONE);
            break;
        case EV_SYNC_DONE:
            jianlu_offline_load_syncq(&s_syncq);   // 与文件对齐(网络任务已改)
            if (s_drain_active) {
                s_drain_syncq_done = s_drain_syncq_total - (int)ev.arg2;
                drain_summary_show();
            }
            refresh_home();
            break;
        case EV_OFFLINE_RETRY:
            if (s_store.offline && s_wifi_connected && jianlu_hub_base()[0] != '\0') {
                request_fetch();
            }
            break;
        case EV_CONFIRM_TIMEOUT:
            voice_dismiss();
            break;
        case EV_PROFILE_DONE:
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                memcpy(&s_profile, &s_profile_tmp, sizeof(s_profile));
                maybe_download_images();
            }
            if (s_nav.page == JIANLU_PAGE_HOME) refresh_home();
            break;
        case EV_AVATAR_DONE:
            s_dl_inflight &= (uint8_t)~1;
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                // 记录已下载版本:同版本不再重下(换图才会变)
                s_nvs.avatar_ver = s_profile.avatar_version;
                jianlu_nvs_save_image_ver(true, s_profile.avatar_version);
            }
            jianlu_ui_avatar_dirty();
            if (s_nav.page == JIANLU_PAGE_HOME) refresh_home();
            break;
        case EV_QR_DONE:
            s_dl_inflight &= (uint8_t)~2;
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                s_nvs.qrcode_ver = s_profile.qrcode_version;
                jianlu_nvs_save_image_ver(false, s_profile.qrcode_version);
            }
            if (s_nav.page == JIANLU_PAGE_QR && bsp_lvgl_lock(500)) {
                jianlu_ui_qr_set(jianlu_hub_cache_exists(QRCODE_PATH), &s_profile);
                bsp_lvgl_unlock();
            }
            break;
        }
    }
}

void jianlu_app_start(void)
{
    jianlu_store_init(&s_store);
    jianlu_voice_init(&s_voice);
    jianlu_ps_init(&s_ps);
    jianlu_nav_init(&s_nav);
    ESP_LOGI(TAG, "堆: 启动时 %u", (unsigned)esp_get_free_heap_size());

    s_ev_queue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(app_ev_t));
    if (!s_ev_queue) {
        ESP_LOGE(TAG, "队列创建失败,应用无法启动");
        return;
    }

    if (jianlu_capture_init() != ESP_OK) {
        ESP_LOGW(TAG, "语音存储不可用,语音记录将失败");
    }
    if (jianlu_nvs_load(&s_nvs) != ESP_OK) {
        ESP_LOGW(TAG, "NVS 读取失败,按无凭据处理");
    }
    jianlu_offline_load_syncq(&s_syncq);
    // 亮度/常亮持久化:设置页可调,开机应用
    if (s_nvs.brightness == 25 || s_nvs.brightness == 50 ||
        s_nvs.brightness == 75 || s_nvs.brightness == 100) {
        s_brightness = s_nvs.brightness;
        bsp_display_backlight((uint8_t)s_brightness);
    }
    if (s_nvs.keep_on) {
        s_keep_on = true;
        jianlu_ps_set_keep_on(&s_ps, true);
    }
    if (s_nvs.theme != 0) {
        // 开机恢复持久化主题(非默认才切换,省一次重建)
        jianlu_ui_set_theme(s_nvs.theme);
        ESP_LOGI(TAG, "主题(持久化): %s", jianlu_theme_name(s_nvs.theme));
    }

    if (xTaskCreate(app_task, "jianlu", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "任务创建失败,应用无法启动");
        return;
    }

    // 凭据优先级:NVS > Kconfig;都没有 → 出厂配网模式
    jianlu_wifi_src_t wifi_src = jianlu_config_pick_wifi(s_nvs.wifi_ssid,
                                                         CONFIG_XIAONUO_WIFI_SSID);
    const char *ssid = wifi_src == JIANLU_WIFI_NVS ? s_nvs.wifi_ssid
                     : wifi_src == JIANLU_WIFI_KCONFIG ? CONFIG_XIAONUO_WIFI_SSID
                     : "";
    const char *pass = wifi_src == JIANLU_WIFI_NVS ? s_nvs.wifi_pass
                     : wifi_src == JIANLU_WIFI_KCONFIG ? CONFIG_XIAONUO_WIFI_PASSWORD
                     : "";

    if (s_nvs.reprov) {
        // 用户主动重配:强制配网态,BLUFI 收到新凭据后才清标志回联网
        ESP_LOGI(TAG, "强制配网标志在位,进入配网模式(忽略已有凭据)");
        jianlu_netflow_event(&s_nf, JIANLU_NF_START_NO_CREDS);
        enter_provisioning();
    } else if (wifi_src != JIANLU_WIFI_NONE) {
        ESP_LOGI(TAG, "用%s的凭据连接 Wi-Fi",
                 wifi_src == JIANLU_WIFI_NVS ? "NVS" : "Kconfig");
        esp_err_t err = wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi 初始化失败: %s", esp_err_to_name(err));
            set_error("Wi-Fi 初始化失败");
            return;
        }
        ESP_LOGI(TAG, "堆: Wi-Fi 后 %u", (unsigned)esp_get_free_heap_size());
        jianlu_provision_set_wifi_ready(true);
        wifi_set_creds(ssid, pass);
        jianlu_netflow_event(&s_nf, JIANLU_NF_START_WITH_CREDS);
        apply_view(JIANLU_ANIM_NONE);
        wifi_connect();
    } else {
        // 配网/联网分离(无 PSRAM,Wi-Fi ~56KB 与 NimBLE ~40KB 无法共存):
        // 配网态完全不起 Wi-Fi,BLUFI 收到凭据存 NVS 后重启走联网路径。
        ESP_LOGI(TAG, "无 Wi-Fi 凭据,进入配网模式");
        jianlu_netflow_event(&s_nf, JIANLU_NF_START_NO_CREDS);
        enter_provisioning();
    }

    // 串口截屏服务(只读,失败不影响应用)。配网态不启动:NimBLE 占 ~80K
    // 后堆仅 ~13K,省下驱动的 1.9K 给 BLUFI 连接;配网成功重启后正常启用。
    if (!s_prov_running) jianlu_shot_start();

#if NAV_LINK_SELFTEST
    {   // 自检脚本:开机 12s 启动(不依赖网络,隔离环境也能跑)
        esp_timer_handle_t t = NULL;
        esp_timer_create_args_t args = {
            .callback = nav_test_boot_cb,
            .name = "nav_boot",
        };
        if (esp_timer_create(&args, &t) == ESP_OK) {
            esp_timer_start_once(t, 12000ULL * 1000);
        }
    }
#endif

    esp_err_t btn_err = bsp_button_init(on_key, NULL);
    if (btn_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(btn_err));
    }
}

// ---------------------------------------------------------------------------
// 网络诊断(隐藏保留):三路 TCP connect 探针 + 关联 AP 详情。
// 矩阵:网关✗=Wi-Fi 数据面;网关✓公网✓中枢✗=客户端间隔离;
//       网关✓公网✗=上网控制/IoT 网段;全✓=间歇性(需长时复现)。
// ---------------------------------------------------------------------------
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "esp_netif_types.h"

static bool tcp_probe(const char *label, uint32_t ipv4_be, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        ESP_LOGW(TAG, "[诊断] %s: socket 失败", label);
        return false;
    }
    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = ipv4_be,
    };
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int r = connect(fd, (struct sockaddr *)&dst, sizeof(dst));
    bool ok = false;
    if (r == 0) {
        ok = true;
    } else if (errno == EINPROGRESS) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wset, NULL, &tv) > 0) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
            ok = (err == 0);
        }
    }
    close(fd);
    ESP_LOGW(TAG, "[诊断] %s: %s", label, ok ? "通" : "不通");
    return ok;
}

static void network_diag(void)
{
    // 关联 AP 详情(bssid/信道/频段——band 锁定缺失时可附到同名邻居 AP)
    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        ESP_LOGW(TAG, "[诊断] AP %02X:%02X:%02X:%02X:%02X:%02X ch%u%s rssi=%d "
                 "second=%u 11b%u g%u n%u",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3],
                 ap.bssid[4], ap.bssid[5], ap.primary,
                 ap.second == WIFI_SECOND_CHAN_NONE ? "" : "+2H",
                 ap.rssi, (unsigned)ap.second,
                 !!(ap.phy_11b), !!(ap.phy_11g), !!(ap.phy_11n));
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = { 0 };
    if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        ESP_LOGW(TAG, "[诊断] 本机 " IPSTR " 掩码 " IPSTR " 网关 " IPSTR,
                 IP2STR(&ip.ip), IP2STR(&ip.netmask), IP2STR(&ip.gw));
    }

    // 诊断矩阵固定三元:网关 / 公网 DNS / 中枢
    tcp_probe("a:网关 192.168.31.1:53", ipaddr_addr("192.168.31.1"), 53);
    tcp_probe("b:公网 223.5.5.5:53", ipaddr_addr("223.5.5.5"), 53);
    {
        char hub[160];
        snprintf(hub, sizeof(hub), "%s", jianlu_hub_base());
        char *host = hub;
        if (strncmp(host, "http://", 7) == 0) host += 7;
        char *colon = strchr(host, ':');
        uint16_t port = 3000;
        if (colon) {
            port = (uint16_t)atoi(colon + 1);
            *colon = '\0';
        }
        ESP_LOGW(TAG, "[诊断] c:中枢 %s:%u", host, port);
        tcp_probe("c:中枢", ipaddr_addr(host), port);
    }
}
