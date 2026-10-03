// main/jianlu_discover.c —— 见 jianlu_discover.h。
//
// 原始 UDP mDNS:esp-mdns 组件在本设备/路由器组合下查询无应答(抓包证实
// 查询已发出、Mac 已应答,但组件收不到),改为自管 socket + jianlu_dns 解析。
// 发现分三个阶段,各自独立截止:PTR 找实例 → SRV 拿端口/主机 → A 拿 IPv4。
#include "jianlu_discover.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "jianlu_dns.h"
#include "jianlu_hub.h"
#include "lwip/sockets.h"

static const char *TAG = "jianlu_mdns";

#define MDNS_PORT 5353
#define RECV_POLL_MS 200

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int mdns_sock_open(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -1;
    // 绑定临时端口(非 5353):按 RFC 6762 §8.1,源端口非 5353 的查询,
    // 应答方必须单播直答——绕开本路由器不向设备转发组播应答的问题。
    struct sockaddr_in sa = { 0 };
    sa.sin_family = AF_INET;
    sa.sin_port = htons(55353);   // 固定非 5353 端口:legacy unicast 规则生效且便于测试
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        close(fd);
        return -1;
    }
    // 但仍要加入组播组:实测本路由器(小米)不转发非组成员发出的组播帧,
    // 不入组则查询根本不上线。
    struct ip_mreq mreq = { 0 };
    mreq.imr_multiaddr.s_addr = htonl(0xE00000FB);   // 224.0.0.251
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
        ESP_LOGW(TAG, "加入 mDNS 组播组失败");
        close(fd);
        return -1;
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = RECV_POLL_MS * 1000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    return fd;
}

static void mdns_send_query(int fd, const char *name, uint16_t qtype)
{
    uint8_t buf[96];
    size_t n = jianlu_dns_build_query(buf, sizeof(buf), name, qtype);
    if (n == 0) return;
    struct sockaddr_in dst = { 0 };
    dst.sin_family = AF_INET;
    dst.sin_port = htons(MDNS_PORT);
    dst.sin_addr.s_addr = htonl(0xE00000FB);
    int r = sendto(fd, buf, n, 0, (struct sockaddr *)&dst, sizeof(dst));
    struct sockaddr_in local = { 0 };
    socklen_t ll = sizeof(local);
    getsockname(fd, (struct sockaddr *)&local, &ll);
    ESP_LOGD(TAG, "查询 %s type%u: sendto=%d (本地端口 %u, errno=%d)",
             name, qtype, r, (unsigned)ntohs(local.sin_port), errno);
}

// 在截止时间前收包并累积解析;每收到一个含新信息的包记日志。
static void mdns_collect(int fd, jianlu_dns_t *res, const char *a_filter,
                         int64_t deadline_ms)
{
    size_t scratch_len = 0;
    uint8_t *pkt = jianlu_net_scratch(&scratch_len);   // 网络任务共享刮擦区
    size_t cap = scratch_len < 900 ? scratch_len : 900;
    while (now_ms() < deadline_ms) {
        ssize_t n = recv(fd, pkt, cap, 0);
        if (n <= 0) continue;
        int got = jianlu_dns_parse(pkt, (size_t)n, res, a_filter);
        ESP_LOGD(TAG, "收到 %d 字节 mDNS 包,收获掩码 0x%x", (int)n, got);
    }
}

esp_err_t jianlu_discover_hub(char *url, size_t url_len, int timeout_ms)
{
    int fd = mdns_sock_open();
    if (fd < 0) {
        ESP_LOGW(TAG, "mDNS socket 创建失败");
        return ESP_FAIL;
    }

    jianlu_dns_t res = { 0 };
    int64_t t0 = now_ms();
    // 阶段预算:PTR 40%,SRV 35%,A 25%
    int64_t ptr_deadline = t0 + timeout_ms * 2 / 5;
    int64_t srv_deadline = t0 + timeout_ms * 3 / 4;
    int64_t a_deadline = t0 + timeout_ms;

    // 阶段 1:PTR(发两次防丢)
    mdns_send_query(fd, "_xiaonuo._tcp.local", 12);
    mdns_collect(fd, &res, NULL, t0 + 250);
    if (res.ptr_target[0] == '\0') {
        mdns_send_query(fd, "_xiaonuo._tcp.local", 12);
    }
    mdns_collect(fd, &res, NULL, ptr_deadline);

    // 阶段 2:SRV(PTR 应答可能已附带,如 zeroconf;Mac mDNSResponder 只回 PTR)
    if (res.ptr_target[0] != '\0' && res.srv_port == 0) {
        mdns_send_query(fd, res.ptr_target, 33);
        mdns_collect(fd, &res, NULL, srv_deadline);
    }
    // 阶段 3:A(若还没有);用 SRV 目标名过滤,避免错收同名域的其他 A
    if (res.srv_port != 0 && res.a_ipv4 == 0) {
        mdns_send_query(fd, res.srv_target, 1);
        mdns_collect(fd, &res, res.srv_target, a_deadline);
    }
    close(fd);

    if (res.ptr_target[0] == '\0') {
        ESP_LOGW(TAG, "未发现 _xiaonuo._tcp 服务");
        return ESP_ERR_NOT_FOUND;
    }
    if (res.srv_port == 0 || res.a_ipv4 == 0) {
        ESP_LOGW(TAG, "中枢服务记录不全(ptr=%s port=%u ip=%" PRIu32 ")",
                 res.ptr_target, res.srv_port, res.a_ipv4);
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t ip = res.a_ipv4;   // rd32 读出即点分 A.B.C.D 的 A 在最高字节
    int n = snprintf(url, url_len, "http://%" PRIu32 ".%" PRIu32 ".%" PRIu32 ".%" PRIu32 ":%u",
                     (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                     (ip >> 8) & 0xFF, ip & 0xFF, res.srv_port);
    if (n <= 0 || (size_t)n >= url_len) return ESP_ERR_INVALID_SIZE;
    ESP_LOGI(TAG, "发现中枢: %s(%s)", url, res.ptr_target);
    return ESP_OK;
}
