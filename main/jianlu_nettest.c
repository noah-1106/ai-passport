// main/jianlu_nettest.c —— 网络取证自检(一次性,取证构建专用)。
//
// 回答三个问题:超时够不够 / 内存够不够 / 连接数够不够。
// 裸 lwIP socket connect(绕开 HTTP 层),15s 超时,逐次记录
// 结果/耗时/heap,外加网关对照与压测后完整 fetch 对比。
#include "jianlu_nettest.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jianlu_hub.h"
#include "lwip/sockets.h"

static const char *TAG = "nettest";

#define NT_TIMEOUT_S 15

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// 裸 TCP connect,非阻塞+select,返回耗时 ms;失败返回 -1(errno 记录)
static int64_t probe_connect(uint32_t ip_be, uint16_t port, int *out_errno)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        *out_errno = errno;
        return -1;
    }
    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = ip_be,
    };
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int64_t t0 = now_ms();
    int r = connect(fd, (struct sockaddr *)&dst, sizeof(dst));
    bool ok = false;
    if (r == 0) {
        ok = true;
    } else if (errno == EINPROGRESS) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv = { .tv_sec = NT_TIMEOUT_S, .tv_usec = 0 };
        if (select(fd + 1, NULL, &wset, NULL, &tv) > 0) {
            int soerr = 0;
            socklen_t len = sizeof(soerr);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
            ok = (soerr == 0);
            if (!ok) *out_errno = soerr;
        } else {
            *out_errno = ETIMEDOUT;
        }
    } else {
        *out_errno = errno;
    }
    int64_t elapsed = now_ms() - t0;
    close(fd);
    return ok ? elapsed : -1;
}

static void run_series(const char *label, uint32_t ip_be, uint16_t port, int count)
{
    int ok_cnt = 0;
    int64_t durations[32] = { 0 };
    for (int i = 0; i < count && i < 32; i++) {
        int e = 0;
        int64_t ms = probe_connect(ip_be, port, &e);
        if (ms >= 0) {
            ok_cnt++;
            durations[i] = ms;
            ESP_LOGI(TAG, "[压测] %s #%d/%d OK %lldms heap=%u min=%u",
                     label, i + 1, count, (long long)ms,
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)esp_get_minimum_free_heap_size());
        } else {
            ESP_LOGW(TAG, "[压测] %s #%d/%d FAIL(%s) %lldms heap=%u min=%u",
                     label, i + 1, count, strerror(e), (long long)(-ms == -1 ? 0 : 0),
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)esp_get_minimum_free_heap_size());
            durations[i] = -1;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    // 汇总:成功耗时分布(min/max/超过1s/超过5s 的个数)
    int64_t mn = -1, mx = -1;
    int over1s = 0, over5s = 0;
    for (int i = 0; i < count && i < 32; i++) {
        if (durations[i] < 0) continue;
        if (mn < 0 || durations[i] < mn) mn = durations[i];
        if (durations[i] > mx) mx = durations[i];
        if (durations[i] > 1000) over1s++;
        if (durations[i] > 5000) over5s++;
    }
    ESP_LOGI(TAG, "[压测汇总] %s: %d/%d 成功;成功耗时 min=%lldms max=%lldms "
             ">1s=%d >5s=%d;首尾 heap %u/%u min-ever %u",
             label, ok_cnt, count, (long long)mn, (long long)mx,
             over1s, over5s,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());
}

void jianlu_nettest_run(const char *hub_base_url)
{
    ESP_LOGI(TAG, "===== 网络取证自检开始(15s 超时,500ms 间隔) =====");
    ESP_LOGI(TAG, "[基线] heap=%u min-ever=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());

    // 1) 对照:网关 5 次
    run_series("网关:53", ipaddr_addr("192.168.31.1"), 53, 5);

    // 2) 主压测:中枢 20 次
    char host[160];
    snprintf(host, sizeof(host), "%s", hub_base_url ? hub_base_url : "");
    char *h = host;
    if (strncmp(h, "http://", 7) == 0) h += 7;
    char *colon = strchr(h, ':');
    uint16_t port = 3000;
    if (colon) {
        port = (uint16_t)atoi(colon + 1);
        *colon = '\0';
    }
    ESP_LOGI(TAG, "[压测] 目标 %s:%u", h, port);
    run_series("中枢", ipaddr_addr(h), port, 20);

    // 3) 对照后再来一轮网关(区分"持续故障"与"中途恶化")
    run_series("网关:53(二)", ipaddr_addr("192.168.31.1"), 53, 5);

    // 4) 完整 HTTP fetch 对比(裸 socket vs HTTP 栈)
    ESP_LOGI(TAG, "[HTTP对比] 走 jianlu_hub_fetch 完整路径");
    static jianlu_store_t probe_store;
    char errbuf[JIANLU_ERROR_LEN] = { 0 };
    esp_err_t err = jianlu_hub_fetch(&probe_store, errbuf, sizeof(errbuf));
    ESP_LOGI(TAG, "[HTTP对比] fetch=%s(%s) heap=%u min=%u",
             esp_err_to_name(err), errbuf,
             (unsigned)esp_get_free_heap_size(),
             (unsigned)esp_get_minimum_free_heap_size());

    ESP_LOGI(TAG, "===== 网络取证自检结束 =====");
}

// app 侧一次性 boot 定时器回调:hub 就绪后自动开跑
void jianlu_nettest_boot_cb(void *arg)
{
    (void)arg;
    // 在定时器任务上下文只做转发:取证跑在独立任务里,不阻塞定时器
    if (xTaskCreate((TaskFunction_t)jianlu_nettest_task_entry,
                    "nettest", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE("nettest", "取证任务创建失败");
    }
}

void jianlu_nettest_task_entry(void *arg)
{
    (void)arg;
    jianlu_nettest_run(jianlu_hub_base());
    vTaskDelete(NULL);
}
