// main/jianlu_dns.h —— 极简 mDNS 报文构建与解析(纯 C,不依赖 lwip,host 可测)。
//
// 背景:esp-mdns 组件的查询 API 在本设备/路由器组合下收不到应答(查询已
// 发出、对端已应答,经抓包证实),改为原始 UDP + 手写报文,行为完全可控可测。
// 只支持我们需要的三种记录:PTR(12)/SRV(33)/A(1),支持压缩指针(0xC0)。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define JIANLU_DNS_NAME_LEN  64
#define JIANLU_DNS_GOT_PTR   0x01
#define JIANLU_DNS_GOT_SRV   0x02
#define JIANLU_DNS_GOT_A     0x04

typedef struct {
    char ptr_target[JIANLU_DNS_NAME_LEN];   // PTR 答案:服务实例名
    uint16_t srv_port;                      // SRV 答案:端口
    char srv_target[JIANLU_DNS_NAME_LEN];   // SRV 答案:目标主机
    uint32_t a_ipv4;                        // A 答案:IPv4(网络字节序)
} jianlu_dns_t;

// 构造 DNS 查询报文(单问题,RD=0)。name 为点分域名("_xiaonuo._tcp.local")。
// qtype: 12=PTR 33=SRV 1=A。返回报文长度,buf 太小返回 0。
size_t jianlu_dns_build_query(uint8_t *buf, size_t cap, const char *name, uint16_t qtype);

// 解析 DNS 报文,提取关心的记录,返回 JIANLU_DNS_GOT_* 掩码。
// a_filter 非 NULL 时只采纳名字匹配的 A 记录(用于拿到 SRV 后二次过包);
// PTR 只采纳 qname 为 "_xiaonuo._tcp.local" 的答案。
// out 为累积式:多次调用(多个报文)可向同一 out 里补充,调用方负责先清零。
int jianlu_dns_parse(const uint8_t *pkt, size_t len, jianlu_dns_t *out,
                     const char *a_filter);
