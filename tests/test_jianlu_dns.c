// tests/test_jianlu_dns.c —— mDNS 报文构建/解析的 host 侧测试。
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "jianlu_dns.h"

// 把点分域名编成标签序列(与固件编码一致,测试夹具用)
static size_t enc(uint8_t *buf, const char *name) {
    size_t used = 0;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t l = dot ? (size_t)(dot - p) : strlen(p);
        buf[used++] = (uint8_t)l;
        memcpy(buf + used, p, l);
        used += l;
        p = dot ? dot + 1 : p + l;
    }
    buf[used++] = 0;
    return used;
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static void test_build_query(void) {
    uint8_t buf[96];
    size_t n = jianlu_dns_build_query(buf, sizeof(buf), "_xiaonuo._tcp.local", 12);
    assert(n > 12);
    // 头:qd=1,其余计数 0
    assert(buf[4] == 0 && buf[5] == 1);
    assert(buf[6] == 0 && buf[7] == 0);
    // qname 以 8 "_xiaonuo" 开头
    assert(buf[12] == 8 && memcmp(buf + 13, "_xiaonuo", 8) == 0);
    // 尾部 qtype=12 qclass=0x8001(QU 位:要求单播直答)
    assert(buf[n - 4] == 0 && buf[n - 3] == 12);
    assert(buf[n - 2] == 0x80 && buf[n - 1] == 1);
    // 小缓冲拒绝
    assert(jianlu_dns_build_query(buf, 10, "_xiaonuo._tcp.local", 12) == 0);
}

// 造一个响应头:flags 0x8400,an=ancount
static size_t resp_header(uint8_t *buf, const char *qname, uint16_t ancount) {
    memset(buf, 0, 12);
    buf[2] = 0x84;
    put16(buf + 4, 1);            // qd=1
    put16(buf + 6, ancount);
    size_t off = 12 + enc(buf + 12, qname);
    put16(buf + off, 12);         // qtype PTR
    put16(buf + off + 2, 1);
    return off + 4;
}

static void test_parse_ptr_only(void) {
    // 模拟 Mac mDNSResponder:只回 PTR,名字用压缩指针指回问题区(0xC00C)
    uint8_t pkt[256];
    size_t off = resp_header(pkt, "_xiaonuo._tcp.local", 1);
    // 答案:name=PTR 压缩指针 0xC00C,type=12,class=1,ttl=120,rdlen
    pkt[off++] = 0xC0; pkt[off++] = 0x0C;
    put16(pkt + off, 12); put16(pkt + off + 2, 1);
    pkt[off + 4] = 0; pkt[off + 5] = 0; pkt[off + 6] = 0; pkt[off + 7] = 120;
    uint8_t *rdlen_p = pkt + off + 8;
    off += 10;
    size_t rd = enc(pkt + off, "xiaonuo-hub._xiaonuo._tcp.local");
    put16(rdlen_p, (uint16_t)rd);
    off += rd;

    jianlu_dns_t res = { 0 };
    int got = jianlu_dns_parse(pkt, off, &res, NULL);
    assert((got & JIANLU_DNS_GOT_PTR) != 0);
    assert(strcmp(res.ptr_target, "xiaonuo-hub._xiaonuo._tcp.local") == 0);
    assert((got & JIANLU_DNS_GOT_SRV) == 0);
    assert((got & JIANLU_DNS_GOT_A) == 0);
}

static void test_parse_full_zeroconf_style(void) {
    // 模拟 zeroconf:一个包内 PTR + SRV + TXT + A,SRV target 压缩到后文 A 名
    uint8_t pkt[512];
    size_t off = resp_header(pkt, "_xiaonuo._tcp.local", 4);

    // PTR 答案
    pkt[off++] = 0xC0; pkt[off++] = 0x0C;
    put16(pkt + off, 12); put16(pkt + off + 2, 1);
    uint8_t *rdlen1 = pkt + off + 8;
    off += 10;
    size_t rd = enc(pkt + off, "xn-test-hub._xiaonuo._tcp.local");
    put16(rdlen1, (uint16_t)rd);
    off += rd;

    // SRV 答案:name 压缩到 PTR 目标(0xC021,假设 PTR rdata 起点 0x21)
    // 为稳妥,SRV 的 name 直接用完整编码
    off += enc(pkt + off, "xn-test-hub._xiaonuo._tcp.local");
    put16(pkt + off, 33); put16(pkt + off + 2, 1);
    uint8_t *rdlen2 = pkt + off + 8;
    off += 10;
    size_t srv_start = off;
    put16(pkt + off, 0); put16(pkt + off + 2, 0);       // prio/weight
    put16(pkt + off + 4, 3000);                          // port
    off += 6;
    rd = enc(pkt + off, "xiaonuo-mac.local");
    off += rd;
    put16(rdlen2, (uint16_t)(off - srv_start));

    // TXT 答案(应被跳过)
    off += enc(pkt + off, "xn-test-hub._xiaonuo._tcp.local");
    put16(pkt + off, 16); put16(pkt + off + 2, 1);
    put16(pkt + off + 8, 1);
    off += 10;
    pkt[off++] = 0;

    // A 答案:name 完整编码(也可压缩),192.168.31.242
    off += enc(pkt + off, "xiaonuo-mac.local");
    put16(pkt + off, 1); put16(pkt + off + 2, 1);
    put16(pkt + off + 8, 4);
    off += 10;
    pkt[off++] = 192; pkt[off++] = 168; pkt[off++] = 31; pkt[off++] = 242;

    jianlu_dns_t res = { 0 };
    int got = jianlu_dns_parse(pkt, off, &res, NULL);
    assert((got & JIANLU_DNS_GOT_PTR) != 0);
    assert((got & JIANLU_DNS_GOT_SRV) != 0);
    assert((got & JIANLU_DNS_GOT_A) != 0);
    assert(strcmp(res.ptr_target, "xn-test-hub._xiaonuo._tcp.local") == 0);
    assert(res.srv_port == 3000);
    assert(strcmp(res.srv_target, "xiaonuo-mac.local") == 0);
    // rd32 读出:192 在最高字节
    assert(res.a_ipv4 == 0xC0A81FF2u);

    // 带错误过滤名时不收 A
    jianlu_dns_t res2 = { 0 };
    got = jianlu_dns_parse(pkt, off, &res2, "other-host.local");
    assert((got & JIANLU_DNS_GOT_A) == 0);
    // 带正确过滤名时收 A
    jianlu_dns_t res3 = { 0 };
    got = jianlu_dns_parse(pkt, off, &res3, "xiaonuo-mac.local");
    assert((got & JIANLU_DNS_GOT_A) != 0);
    assert(res3.a_ipv4 == 0xC0A81FF2u);
}

static void test_parse_garbage_safe(void) {
    jianlu_dns_t res = { 0 };
    uint8_t junk[64];
    memset(junk, 0xFF, sizeof(junk));
    assert(jianlu_dns_parse(junk, sizeof(junk), &res, NULL) == 0);
    assert(jianlu_dns_parse(junk, 4, &res, NULL) == 0);
    assert(jianlu_dns_parse(NULL, 100, &res, NULL) == 0);
    // 压缩指针成环:0xC00C 处自指
    uint8_t loop_pkt[32];
    memset(loop_pkt, 0, sizeof(loop_pkt));
    put16(loop_pkt + 6, 1);              // an=1
    loop_pkt[12] = 0xC0; loop_pkt[13] = 0x0C;   // name 指针指向自己
    put16(loop_pkt + 14, 12); put16(loop_pkt + 16, 1);
    put16(loop_pkt + 22, 0);
    jianlu_dns_parse(loop_pkt, sizeof(loop_pkt), &res, NULL);   // 不死循环即胜
}

int main(void) {
    test_build_query();
    test_parse_ptr_only();
    test_parse_full_zeroconf_style();
    test_parse_garbage_safe();
    return 0;
}
