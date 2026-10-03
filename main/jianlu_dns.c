// main/jianlu_dns.c —— 见 jianlu_dns.h。
#include "jianlu_dns.h"

#include <stdbool.h>
#include <string.h>

#define DNS_CLASS_IN 1
// QU 位(qclass 最高位):要求应答方单播直答到查询源地址。
// 本设备所在路由器(小米)不把 mDNS 组播应答转发给设备(抓包证实查询已到
// Mac、应答已发出、设备收不到);单播应答绕开组播下行路径。RFC 6762 §5.4。
#define DNS_CLASS_QU 0x8001
#define DNS_TYPE_A   1
#define DNS_TYPE_PTR 12
#define DNS_TYPE_SRV 33

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

// 把点分域名编码为 DNS 标签序列写入 buf,返回写入长度,失败返回 0。
static size_t encode_name(uint8_t *buf, size_t cap, const char *name)
{
    size_t used = 0;
    const char *p = name;
    while (*p != '\0') {
        const char *dot = strchr(p, '.');
        size_t label_len = dot ? (size_t)(dot - p) : strlen(p);
        if (label_len == 0 || label_len > 63) return 0;
        if (used + 1 + label_len + 1 > cap) return 0;
        buf[used++] = (uint8_t)label_len;
        memcpy(buf + used, p, label_len);
        used += label_len;
        p = dot ? dot + 1 : p + label_len;
    }
    if (used + 1 > cap) return 0;
    buf[used++] = 0;
    return used;
}

// 解码(可能压缩的)域名到 out;返回消耗的报文字节数(压缩指针只算 2 字节),
// 出错返回 0。深度限制防指针环。
static size_t decode_name(const uint8_t *pkt, size_t len, size_t off,
                          char *out, size_t out_len)
{
    size_t consumed = 0;
    size_t o = 0;
    size_t cur = off;
    bool jumped = false;
    for (int depth = 0; depth < 8; depth++) {
        if (cur >= len) return 0;
        uint8_t l = pkt[cur];
        if ((l & 0xC0) == 0xC0) {
            if (cur + 1 >= len) return 0;
            if (!jumped) consumed += 2;
            cur = (size_t)(((l & 0x3F) << 8) | pkt[cur + 1]);
            jumped = true;
            continue;
        }
        if (l == 0) {
            if (!jumped) consumed += 1;
            break;
        }
        if ((l & 0xC0) != 0) return 0;
        cur++;
        if (cur + l > len) return 0;
        if (o + l + 2 > out_len) return 0;
        if (o > 0) out[o++] = '.';
        memcpy(out + o, pkt + cur, l);
        o += l;
        cur += l;
        if (!jumped) consumed += 1 + l;
    }
    if (o >= out_len) return 0;
    out[o] = '\0';
    return consumed;
}

size_t jianlu_dns_build_query(uint8_t *buf, size_t cap, const char *name, uint16_t qtype)
{
    if (cap < 12 + 5) return 0;
    memset(buf, 0, 12);
    wr16(buf + 4, 1);   // qdcount
    size_t name_len = encode_name(buf + 12, cap - 12, name);
    if (name_len == 0) return 0;
    if (12 + name_len + 4 > cap) return 0;
    wr16(buf + 12 + name_len, qtype);
    wr16(buf + 12 + name_len + 2, DNS_CLASS_QU);
    return 12 + name_len + 4;
}

int jianlu_dns_parse(const uint8_t *pkt, size_t len, jianlu_dns_t *out,
                     const char *a_filter)
{
    if (pkt == NULL || out == NULL || len < 12) return 0;
    uint16_t qd = rd16(pkt + 4);
    uint16_t an = rd16(pkt + 6);
    // 只处理响应或任意含答案的报文(查询报文无答案,自然提不到东西)
    size_t off = 12;
    char name[JIANLU_DNS_NAME_LEN];
    for (uint16_t i = 0; i < qd; i++) {
        size_t n = decode_name(pkt, len, off, name, sizeof(name));
        if (n == 0 || off + n + 4 > len) return 0;
        off += n + 4;
    }

    int got = 0;
    for (uint16_t i = 0; i < an; i++) {
        size_t n = decode_name(pkt, len, off, name, sizeof(name));
        if (n == 0 || off + n + 10 > len) return got;
        off += n;
        uint16_t type = rd16(pkt + off);
        uint16_t rdlen = rd16(pkt + off + 8);
        size_t rdata = off + 10;
        if (rdata + rdlen > len) return got;

        if (type == DNS_TYPE_PTR && out->ptr_target[0] == '\0') {
            if (strcmp(name, "_xiaonuo._tcp.local") == 0) {
                if (decode_name(pkt, len, rdata, out->ptr_target,
                                sizeof(out->ptr_target)) != 0) {
                    got |= JIANLU_DNS_GOT_PTR;
                }
            }
        } else if (type == DNS_TYPE_SRV && out->srv_port == 0) {
            if (rdlen >= 7) {
                out->srv_port = rd16(pkt + rdata + 4);
                if (decode_name(pkt, len, rdata + 6, out->srv_target,
                                sizeof(out->srv_target)) != 0) {
                    got |= JIANLU_DNS_GOT_SRV;
                } else {
                    out->srv_port = 0;
                }
            }
        } else if (type == DNS_TYPE_A && out->a_ipv4 == 0) {
            if (rdlen == 4 &&
                (a_filter == NULL || strcmp(name, a_filter) == 0)) {
                out->a_ipv4 = rd32(pkt + rdata);
                got |= JIANLU_DNS_GOT_A;
            }
        }
        off = rdata + rdlen;
    }
    return got;
}
