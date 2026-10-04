// main/jianlu_voiceq.c —— 见 jianlu_voiceq.h。
#include "jianlu_voiceq.h"

#include <string.h>

void jianlu_voiceq_init(jianlu_voiceq_t *q)
{
    memset(q, 0, sizeof(*q));
}

int jianlu_voiceq_push(jianlu_voiceq_t *q, uint32_t ts)
{
    if (q->count >= JIANLU_VOICEQ_SLOTS) return -1;
    q->ts[q->count] = ts;
    q->count++;
    return q->count;
}

int jianlu_voiceq_head(const jianlu_voiceq_t *q)
{
    return q->count > 0 ? 1 : -1;
}

bool jianlu_voiceq_pop(jianlu_voiceq_t *q)
{
    return jianlu_voiceq_remove_at(q, 1);
}

bool jianlu_voiceq_remove_at(jianlu_voiceq_t *q, int slot)
{
    if (slot < 1 || slot > q->count) return false;
    memmove(q->ts + slot - 1, q->ts + slot,
            (size_t)(q->count - slot) * sizeof(q->ts[0]));
    q->count--;
    q->ts[q->count] = 0;
    return true;
}

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    // 紧跟 count 个 uint32_t 时间戳
} voiceq_header_t;

size_t jianlu_voiceq_encode(const jianlu_voiceq_t *q, uint8_t *buf, size_t cap)
{
    size_t body = (size_t)q->count * sizeof(uint32_t);
    if (cap < sizeof(voiceq_header_t) + body) return 0;
    voiceq_header_t h = {
        .magic = JIANLU_VOICEQ_MAGIC,
        .version = JIANLU_VOICEQ_VERSION,
        .count = (uint8_t)q->count,
    };
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), q->ts, body);
    return sizeof(h) + body;
}

bool jianlu_voiceq_decode(const uint8_t *buf, size_t len, jianlu_voiceq_t *q)
{
    if (buf == NULL || q == NULL || len < sizeof(voiceq_header_t)) return false;
    voiceq_header_t h;
    memcpy(&h, buf, sizeof(h));
    if (h.magic != JIANLU_VOICEQ_MAGIC || h.version != JIANLU_VOICEQ_VERSION) return false;
    if (h.count > JIANLU_VOICEQ_SLOTS) return false;
    size_t body = (size_t)h.count * sizeof(uint32_t);
    if (len < sizeof(h) + body) return false;
    memcpy(q->ts, buf + sizeof(h), body);
    q->count = h.count;
    return true;
}
