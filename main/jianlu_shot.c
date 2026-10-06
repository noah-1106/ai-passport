// main/jianlu_shot.c —— 见 jianlu_shot.h。
//
// 坑位对策(编号对应社区踩坑文档 docs/reference/y2lin/...):
//  1 显式安装 USB-serial-JTAG 驱动(rx256/tx1024)+ vfs_use_driver,
//    读任务低频轮询 is_driver_installed,不假设就绪
//  2 绝不 busy-loop:错误退避 200ms,任务优先级 3(LVGL 为 4),
//    无事可做必 vTaskDelay
//  3 滑动窗口子串匹配(容 Windows 吞换行),行终止符复位,匹配后清窗
//  4' 文档方案(静态全屏缓冲)在本应用装不下:DRAM 静态占用已高,
//    再加 150KB 链接溢出 85KB(实测)。改为零大内存方案:
//    包一层 LVGL flush 回调 + lv_refr_now 强制全屏重渲染,
//    渲染分带(240×N,自上而下)顺序流式发送。先干跑一轮校验
//    分带严格平铺(全宽/首尾相接/终点=屏高,数学等价于全覆盖),
//    过了才实传。注意:不留全帧缓冲、不留覆盖位图(9.6KB 静态曾把
//    堆底压穿 Wi-Fi 分配线,见下)
//  5 512 字节分块发送(tx 环形 1024),块失败放弃传输不杀任务
//  6 二进制窗口期间静音全部日志,窗口外才恢复
//  7 只读且失败静默:校验不过/锁超时一律一个字节不发
#include "jianlu_shot.h"

#include <string.h>

#include "bsp_display.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#define SHOT_CMD        "FAP_SCREENSHOT_V1"
#define SHOT_CMD_LEN    (sizeof(SHOT_CMD) - 1)
#define SHOT_W          240
#define SHOT_H          320
#define SHOT_BYTES      (SHOT_W * SHOT_H * 2)
#define SHOT_CHUNK      512          // 坑5:远小于 tx 环形 1024
#define SHOT_TASK_STACK 6144         // 流式转发无大栈需求(不留全帧);省堆

static bool s_started;

// ---- 渲染分带采集状态(flush 回调与控制同任务上下文) ----
static lv_display_flush_cb_t s_orig_flush;   // 被包裹的原回调
static bool s_stream_on;                     // true=实传 false=干跑校验
static volatile bool s_pass_ok;              // 分带至今严格平铺
static int s_next_y;                         // 期望的下一带起始行
static uint8_t s_chunk[SHOT_CHUNK];          // 坑5:发送分块缓冲
static size_t s_chunk_used;

static void chunk_flush_out(void)
{
    if (s_chunk_used == 0) return;
    // 主机慢读/USB 调度抖动会让环形缓冲短暂写满:重试几轮再判死
    int written = -1;
    for (int try = 0; try < 15 && s_pass_ok; try++) {
        written = usb_serial_jtag_write_bytes(s_chunk, (uint32_t)s_chunk_used, 1000);
        if (written > 0 && (size_t)written == s_chunk_used) break;
    }
    if (written <= 0 || (size_t)written != s_chunk_used) {
        s_pass_ok = false;   // 坑5:真拔线/断开 → 放弃传输,不杀任务
    }
    s_chunk_used = 0;
}

static void chunk_push(const uint8_t *data, size_t len)
{
    while (s_pass_ok && len > 0) {
        size_t room = SHOT_CHUNK - s_chunk_used;
        size_t take = len < room ? len : room;
        memcpy(s_chunk + s_chunk_used, data, take);
        s_chunk_used += take;
        data += take;
        len -= take;
        if (s_chunk_used == SHOT_CHUNK) chunk_flush_out();
    }
}

// 包裹 flush:带像素先取走(校验/发送),再交原回调上屏。
// LVGL 分带渲染保证自上而下逐带调用;任何非常规分带 → 判失败。
static void shot_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // 坑7:必须全宽、从期望行起、不重不漏
    if (area->x1 == 0 && area->x2 == SHOT_W - 1
        && area->y1 == s_next_y && area->y2 >= area->y1 && area->y2 < SHOT_H) {
        int rows = area->y2 - area->y1 + 1;
        if (s_stream_on) {
            chunk_push(px_map, (size_t)rows * SHOT_W * 2);
        }
        s_next_y = area->y2 + 1;
    } else {
        s_pass_ok = false;
    }

    // 原回调(esp_lvgl_port:DMA 上屏,传输完成自行 flush_ready)
    if (s_orig_flush != NULL) s_orig_flush(disp, area, px_map);
}

// 强制全屏重渲染一遍。stream=false 为干跑校验(只验分带,不发送)。
static bool render_full_pass(bool stream)
{
    lv_display_t *disp = lv_display_get_default();
    lv_obj_t *scr = lv_screen_active();
    if (disp == NULL || scr == NULL) return false;

    s_stream_on = stream;
    s_pass_ok = true;
    s_next_y = 0;
    s_chunk_used = 0;

    s_orig_flush = lv_display_get_flush_cb(disp);
    if (s_orig_flush == NULL) return false;
    lv_display_set_flush_cb(disp, shot_flush);
    lv_obj_invalidate(scr);
    lv_refr_now(disp);   // 同步全屏渲染:分带依次过 shot_flush
    lv_display_set_flush_cb(disp, s_orig_flush);
    s_orig_flush = NULL;

    if (stream) chunk_flush_out();   // 尾块
    // 平铺完备证明:band 从 0 起步、每个恰接上一个结尾(s_next_y 逻辑)、
    // 终点 == SHOT_H ⇒ 每行恰好覆盖一次,无需位图复核
    return s_pass_ok && s_next_y == SHOT_H;
}

static void send_capture(void)
{
    int64_t t0 = esp_timer_get_time();
    // 坑7:先干跑校验分带平铺,不过关一个字节都不发(主机得干净超时)
    if (!render_full_pass(false)) {
        ESP_LOGW("jianlu_shot", "分带校验失败,静默不发");
        return;
    }
    int64_t t_dry = esp_timer_get_time();

    esp_log_level_t saved = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);   // 坑6:头行首字节前静音

    char header[64];
    int n = snprintf(header, sizeof(header), "%s %d %d RGB565LE %d\n",
                     SHOT_CMD, SHOT_W, SHOT_H, SHOT_BYTES);
    bool ok = n > 0 && usb_serial_jtag_write_bytes(header, (uint32_t)n, 200) == n;
    if (ok) ok = render_full_pass(true);

    esp_log_level_set("*", saved);   // 窗口外恢复,后续日志安全
    int64_t t_end = esp_timer_get_time();
    ESP_LOGI("jianlu_shot", "耗时: 干跑 %lldms 传输 %lldms(堆 %u 最小 %u)",
             (long long)((t_dry - t0) / 1000),
             (long long)((t_end - t_dry) / 1000),
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
    if (!ok) {
        ESP_LOGW("jianlu_shot", "截屏传输未完成(主机断开?)");
    }
}

static void shot_task(void *arg)
{
    (void)arg;
    // 坑3:滑动窗口,长度恰为命令长
    static char window[SHOT_CMD_LEN];
    size_t filled = 0;

    for (;;) {
        // 坑1:驱动未就绪低频轮询,绝不假设
        if (!usb_serial_jtag_is_driver_installed()) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        uint8_t byte;
        int n = usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(200));
        if (n < 0) {
            vTaskDelay(pdMS_TO_TICKS(200));   // 坑2:持续错误退避
            continue;
        }
        if (n == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));    // 坑2:无事可做也让出
            continue;
        }

        // 坑3:行终止符复位窗口(半行残迹不跨行累积)
        if (byte == '\n' || byte == '\r') {
            filled = 0;
            continue;
        }
        if (filled == SHOT_CMD_LEN) {
            memmove(window, window + 1, SHOT_CMD_LEN - 1);
            filled = SHOT_CMD_LEN - 1;
        }
        window[filled++] = (char)byte;
        if (filled == SHOT_CMD_LEN && memcmp(window, SHOT_CMD, SHOT_CMD_LEN) == 0) {
            filled = 0;   // 坑3:匹配后清窗,残迹不再触发
            if (bsp_lvgl_lock(1000)) {
                send_capture();
                bsp_lvgl_unlock();
            }
            // 坑7:锁超时 → 静默不响应,一个字节不发
        }
    }
}

void jianlu_shot_start(void)
{
    if (s_started) return;
    s_started = true;

    // 坑1:单应用固件无 REPL,驱动没人装 → 显式安装并把控制台挪到驱动路径
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 256,
        .tx_buffer_size = 1024,
    };
    ESP_LOGI("jianlu_shot", "堆: 安装前 %u", (unsigned)esp_get_free_heap_size());
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    ESP_LOGI("jianlu_shot", "堆: 驱动后 %u", (unsigned)esp_get_free_heap_size());
    if (err != ESP_OK) {
        ESP_LOGE("jianlu_shot", "USB-serial-JTAG 驱动安装失败: %s",
                 esp_err_to_name(err));
        s_started = false;
        return;
    }
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI("jianlu_shot", "堆: vfs 后 %u", (unsigned)esp_get_free_heap_size());

    // 坑2:优先级 3,低于 LVGL(4)
    if (xTaskCreate(shot_task, "jianlu_shot", SHOT_TASK_STACK, NULL, 3, NULL)
        != pdPASS) {
        ESP_LOGE("jianlu_shot", "截屏任务创建失败");
        s_started = false;
        return;
    }
    ESP_LOGI("jianlu_shot", "截屏服务就绪(%s)", SHOT_CMD);
}
