// main/jianlu_app.c —— 见 jianlu_app.h。
//
// 按键映射(阶段 5):
//   UP/DOWN 单击   卡片堆切换顶卡(飞卡过渡)
//   OK 单击        完成顶卡(划线 → PUT → 飞出);语音确认/失败页=关闭
//   OK 双击        手动刷新(原"OK 长按"让位给按住说话)
//   OK 按住 ≥500ms 按住说话:松开结束并上传;15s 上限自动结束
#include "jianlu_app.h"

#include <string.h>

#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "jianlu_capture.h"
#include "jianlu_hub.h"
#include "jianlu_ui.h"
#include "jianlu_voice.h"

static const char *TAG = "jianlu";

// 临时链路自检:置 1 后,Wi-Fi 连通即自动录 3s 并上传(不等按键),
// 用于无人值守验证 录音→上传→中枢→解析 链路。验证完必须改回 0。
#define VOICE_LINK_SELFTEST 0

#define EVENT_QUEUE_DEPTH 16
#define JOB_QUEUE_DEPTH   4
#define WIFI_RETRY_MS     5000
#define REC_RELEASE_MV    2000   // 松开判定:高于 OK 键窗口上限(1900),松开约 3300
#define CONFIRM_TIMEOUT_MS 6000

typedef enum {
    EV_KEY = 0,          // 按键
    EV_WIFI_CONNECTED,   // 拿到 IP,可以拉清单/补传
    EV_WIFI_DISCONNECTED,// 掉线,稍后重连
    EV_FETCH_DONE,       // 网络任务:拉取结束(arg1=esp_err)
    EV_COMPLETE_DONE,    // 网络任务:完成上报结束(arg1=esp_err,id 在 payload)
    EV_REC_TICK,         // 录音任务:进度(arg1=已录秒数)
    EV_REC_DONE,         // 录音任务:结束(arg1=esp_err,arg2=PCM 字节数)
    EV_CAPTURE_DONE,     // 网络任务:语音上传结束(arg1=esp_err,arg2=是否 pending 补传)
    EV_CONFIRM_TIMEOUT,  // 确认页自动关闭
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
    JOB_CAPTURE,         // id 字段复用为 use_pending 标志("1"=补传)
} job_type_t;

typedef struct {
    job_type_t type;
    char id[JIANLU_ID_LEN];
} net_job_t;

typedef enum {
    REC_CMD_START = 0,
} rec_cmd_type_t;

static jianlu_store_t s_store;
// 拉取结果先落在这里,由应用任务在 EV_FETCH_DONE 里一次性提交,
// 避免网络任务与应用任务并发读写 s_store。
static jianlu_store_t s_fetch_store;
static char s_fetch_err[JIANLU_ERROR_LEN];
static jianlu_voice_t s_voice;
static jianlu_capture_result_t s_capture_result;
static QueueHandle_t s_ev_queue;
static QueueHandle_t s_job_queue;
static QueueHandle_t s_rec_queue;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_confirm_timer;
static volatile bool s_wifi_connected;
static volatile bool s_recording;   // 录音任务运行中(用于松开轮询与日志)

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

// ---------------------------------------------------------------------------
// Wi-Fi(STA,凭据来自 Kconfig)
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
        wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        post_event(EV_WIFI_DISCONNECTED, 0, 0, NULL);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        post_event(EV_WIFI_CONNECTED, 0, 0, NULL);
    }
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

    wifi_config_t sta = { 0 };
    jianlu_utf8_copy((char *)sta.sta.ssid, sizeof(sta.sta.ssid),
                     CONFIG_XIAONUO_WIFI_SSID, sizeof(sta.sta.ssid) - 1);
    jianlu_utf8_copy((char *)sta.sta.password, sizeof(sta.sta.password),
                     CONFIG_XIAONUO_WIFI_PASSWORD, sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_set_config(WIFI_IF_STA, &sta);
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

// 录音任务上下文,每写一块(64ms)调一次:松开返回 false;每秒投一次进度。
static bool rec_poll(void *user)
{
    rec_poll_state_t *st = user;
    st->bytes += JIANLU_REC_CHUNK_BYTES;
    int sec = (int)(st->bytes / JIANLU_VOICE_BYTES_PER_SEC);
    if (sec != st->last_sec) {
        st->last_sec = sec;
        post_event(EV_REC_TICK, sec, 0, NULL);
    }
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
        if (xQueueReceive(s_rec_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        s_poll_state.bytes = 0;
        s_poll_state.last_sec = 0;
        s_recording = true;
        size_t pcm_bytes = 0;
        esp_err_t err = jianlu_capture_record(rec_poll, &s_poll_state, &pcm_bytes);
        s_recording = false;
        post_event(EV_REC_DONE, (int32_t)err, (int32_t)pcm_bytes, NULL);
    }
}

// ---------------------------------------------------------------------------
// 网络工作任务:唯一执行阻塞 HTTP 的地方
// ---------------------------------------------------------------------------
static void net_task(void *arg)
{
    (void)arg;
    net_job_t job;
    for (;;) {
        if (xQueueReceive(s_job_queue, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job.type == JOB_FETCH) {
            s_fetch_err[0] = '\0';
            esp_err_t err = jianlu_hub_fetch(&s_fetch_store,
                                             s_fetch_err, sizeof(s_fetch_err));
            post_event(EV_FETCH_DONE, (int32_t)err, 0, NULL);
        } else if (job.type == JOB_COMPLETE) {
            esp_err_t err = jianlu_hub_complete(job.id);
            post_event(EV_COMPLETE_DONE, (int32_t)err, 0, job.id);
        } else {
            bool use_pending = job.id[0] == '1';
            esp_err_t err = jianlu_capture_upload(use_pending, &s_capture_result);
            post_event(EV_CAPTURE_DONE, (int32_t)err, use_pending ? 1 : 0, NULL);
        }
    }
}

// ---------------------------------------------------------------------------
// 应用任务:改 store/语音状态机、刷 UI、派单
// ---------------------------------------------------------------------------
static void ui_refresh(jianlu_anim_t anim)
{
    if (bsp_lvgl_lock(500)) {
        jianlu_ui_refresh(&s_store, anim);
        bsp_lvgl_unlock();
    }
}

static void request_fetch(void)
{
    net_job_t job = { .type = JOB_FETCH };
    (void)xQueueSend(s_job_queue, &job, 0);
}

static void request_capture(bool use_pending)
{
    net_job_t job = { .type = JOB_CAPTURE, .id = { use_pending ? '1' : '0', '\0' } };
    (void)xQueueSend(s_job_queue, &job, 0);
}

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

static void voice_ui(jianlu_voice_state_t state, int elapsed)
{
    if (!bsp_lvgl_lock(500)) return;
    switch (state) {
    case JIANLU_VOICE_RECORDING: jianlu_ui_voice_recording(elapsed); break;
    case JIANLU_VOICE_SENDING:   jianlu_ui_voice_sending(); break;
    case JIANLU_VOICE_CONFIRM:
        jianlu_ui_voice_confirm(s_capture_result.transcript,
                                s_capture_result.reply,
                                s_capture_result.new_count);
        break;
    case JIANLU_VOICE_ERROR:     jianlu_ui_voice_error(); break;
    default:                     jianlu_ui_voice_idle(); break;
    }
    bsp_lvgl_unlock();
}

// 语音确认/失败页关闭:回列表并刷新
static void voice_dismiss(void)
{
    if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_DISMISS)) {
        if (s_confirm_timer) esp_timer_stop(s_confirm_timer);
        voice_ui(s_voice.state, 0);
        if (s_wifi_connected) {
            jianlu_store_set_view(&s_store, JIANLU_VIEW_LOADING, NULL);
            ui_refresh(JIANLU_ANIM_NONE);
            request_fetch();
        }
    }
}

static void on_key_event(const app_ev_t *ev)
{
    // 语音流程中的按键优先于清单操作
    if (s_voice.state != JIANLU_VOICE_IDLE) {
        if (ev->btn == BSP_BTN_OK && ev->btn_ev == BSP_BTN_CLICK) voice_dismiss();
        return;   // 录音/上传中忽略其余按键
    }

    if (ev->btn == BSP_BTN_OK && ev->btn_ev == BSP_BTN_LONG) {
        // 按住说话开始(松开由录音任务轮询 ADC 判定)
        if (CONFIG_XIAONUO_HUB_URL[0] == '\0') return;   // 未配置中枢,语音无意义
        if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_HOLD_START)) {
            ESP_LOGI(TAG, "开始录音(按住说话)");
            voice_ui(s_voice.state, 0);
            rec_cmd_type_t cmd = REC_CMD_START;
            (void)xQueueSend(s_rec_queue, &cmd, 0);
        }
        return;
    }
    if (ev->btn == BSP_BTN_OK && ev->btn_ev == BSP_BTN_DOUBLE) {
        // 双击刷新(原 OK 长按让位给按住说话)
        if (CONFIG_XIAONUO_WIFI_SSID[0] == '\0') {
            jianlu_store_set_view(&s_store, JIANLU_VIEW_NO_CONFIG, NULL);
        } else if (s_wifi_connected) {
            jianlu_store_set_view(&s_store, JIANLU_VIEW_LOADING, NULL);
            request_fetch();
        } else {
            jianlu_store_set_view(&s_store, JIANLU_VIEW_CONNECTING, NULL);
            wifi_connect();
        }
        ui_refresh(JIANLU_ANIM_NONE);
        return;
    }
    if (ev->btn_ev != BSP_BTN_CLICK) return;
    if (s_store.view != JIANLU_VIEW_READY || s_store.count == 0) return;

    if (ev->btn == BSP_BTN_UP || ev->btn == BSP_BTN_DOWN) {
        jianlu_store_move(&s_store, ev->btn == BSP_BTN_UP ? -1 : 1);
        ui_refresh(ev->btn == BSP_BTN_UP ? JIANLU_ANIM_PREV : JIANLU_ANIM_NEXT);
    } else if (ev->btn == BSP_BTN_OK) {
        const jianlu_record_t *rec = jianlu_store_selected(&s_store);
        if (rec == NULL || rec->completing) return;
        char id[JIANLU_ID_LEN];
        jianlu_utf8_copy(id, sizeof(id), rec->id, sizeof(id) - 1);
        jianlu_store_set_completing(&s_store, id, true);
        ui_refresh(JIANLU_ANIM_NONE);   // 先划线,PUT 成功后再飞出
        net_job_t job = { .type = JOB_COMPLETE };
        jianlu_utf8_copy(job.id, sizeof(job.id), id, sizeof(job.id) - 1);
        (void)xQueueSend(s_job_queue, &job, 0);
    }
}

static void app_task(void *arg)
{
    (void)arg;
    app_ev_t ev;
    for (;;) {
        if (xQueueReceive(s_ev_queue, &ev, portMAX_DELAY) != pdTRUE) continue;
        switch (ev.type) {
        case EV_KEY:
            on_key_event(&ev);
            break;
        case EV_WIFI_CONNECTED:
            ESP_LOGI(TAG, "Wi-Fi 已连接,开始拉取简录");
            jianlu_store_set_view(&s_store, JIANLU_VIEW_LOADING, NULL);
            ui_refresh(JIANLU_ANIM_NONE);
            // 有遗留录音先补传,再拉清单(网络任务串行执行)
            if (jianlu_voice_should_retry_pending(jianlu_capture_pending_exists(),
                                                  true)) {
                ESP_LOGI(TAG, "发现待补传录音,先补传");
                request_capture(true);
            }
            request_fetch();
#if VOICE_LINK_SELFTEST
            if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_HOLD_START)) {
                ESP_LOGI(TAG, "[SELFTEST] 自动开始录音 3s");
                rec_cmd_type_t cmd = REC_CMD_START;
                (void)xQueueSend(s_rec_queue, &cmd, 0);
            }
#endif
            break;
        case EV_WIFI_DISCONNECTED:
            ESP_LOGW(TAG, "Wi-Fi 掉线,%dms 后重连", WIFI_RETRY_MS);
            if (s_store.view != JIANLU_VIEW_NO_CONFIG) {
                jianlu_store_set_view(&s_store, JIANLU_VIEW_CONNECTING, NULL);
                ui_refresh(JIANLU_ANIM_NONE);
            }
            schedule_wifi_retry();
            break;
        case EV_FETCH_DONE:
            // 提交网络任务写好的结果;s_store 只在本任务里变更
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                memcpy(&s_store, &s_fetch_store, sizeof(s_store));
                jianlu_store_set_view(&s_store, JIANLU_VIEW_READY, NULL);
            } else {
                jianlu_store_set_view(&s_store, JIANLU_VIEW_ERROR, s_fetch_err);
            }
            ui_refresh(JIANLU_ANIM_NONE);
            break;
        case EV_COMPLETE_DONE:
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                jianlu_store_remove(&s_store, ev.id);
                ui_refresh(JIANLU_ANIM_COMPLETE);   // 顶卡飞出
            } else {
                jianlu_store_set_completing(&s_store, ev.id, false);
                ui_refresh(JIANLU_ANIM_NONE);
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
                jianlu_capture_discard();   // 误触(太短)或录音失败
                voice_ui(JIANLU_VOICE_IDLE, 0);
                break;
            }
            ESP_LOGI(TAG, "录音 %d 字节,开始上传", (int)s_voice.recorded_bytes);
            voice_ui(s_voice.state, 0);
            request_capture(false);
            break;
        }
        case EV_CAPTURE_DONE: {
            bool use_pending = ev.arg2 == 1;
            if ((esp_err_t)ev.arg1 == ESP_OK) {
                jianlu_capture_delete(use_pending);
                if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_SEND_OK)) {
                    voice_ui(s_voice.state, 0);
                    schedule_confirm_dismiss();
                }
            } else {
                if (jianlu_voice_event(&s_voice, JIANLU_VOICE_EV_SEND_FAIL)) {
                    voice_ui(s_voice.state, 0);
                    schedule_confirm_dismiss();
                }
                // pending 补传失败:静默,文件继续保留等下次联网
            }
            break;
        }
        case EV_CONFIRM_TIMEOUT:
            voice_dismiss();
            break;
        }
    }
}

void jianlu_app_start(void)
{
    jianlu_store_init(&s_store);
    jianlu_voice_init(&s_voice);

    s_ev_queue = xQueueCreate(EVENT_QUEUE_DEPTH, sizeof(app_ev_t));
    s_job_queue = xQueueCreate(JOB_QUEUE_DEPTH, sizeof(net_job_t));
    s_rec_queue = xQueueCreate(1, sizeof(rec_cmd_type_t));
    if (!s_ev_queue || !s_job_queue || !s_rec_queue) {
        ESP_LOGE(TAG, "队列创建失败,应用无法启动");
        return;
    }

    if (jianlu_capture_init() != ESP_OK) {
        ESP_LOGW(TAG, "语音存储不可用,语音记录将失败");
    }

    if (xTaskCreate(net_task, "jianlu_net", 6144, NULL, 4, NULL) != pdPASS ||
        xTaskCreate(rec_task, "jianlu_rec", 4096, NULL, 6, NULL) != pdPASS ||
        xTaskCreate(app_task, "jianlu", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "任务创建失败,应用无法启动");
        return;
    }

    if (CONFIG_XIAONUO_WIFI_SSID[0] == '\0' || CONFIG_XIAONUO_HUB_URL[0] == '\0') {
        ESP_LOGW(TAG, "Wi-Fi 凭据或中枢地址未配置(Kconfig),进入未配置态");
        jianlu_store_set_view(&s_store, JIANLU_VIEW_NO_CONFIG, NULL);
    } else {
        jianlu_store_set_view(&s_store, JIANLU_VIEW_CONNECTING, NULL);
        esp_err_t err = wifi_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi 初始化失败: %s", esp_err_to_name(err));
            jianlu_store_set_view(&s_store, JIANLU_VIEW_ERROR, "Wi-Fi 初始化失败");
        }
    }

    esp_err_t btn_err = bsp_button_init(on_key, NULL);
    if (btn_err != ESP_OK) {
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(btn_err));
    }
    ui_refresh(JIANLU_ANIM_NONE);
}
