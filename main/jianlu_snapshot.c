// main/jianlu_snapshot.c —— 见 jianlu_snapshot.h。
#include "jianlu_snapshot.h"

#include <string.h>

uint32_t jianlu_crc32(const void *data, size_t len)
{
    const uint8_t *bytes = data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return ~crc;
}

// ---- 小端定长读写(记录体本身按内存布局,头部显式编码)----
typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t rec_size;
    uint32_t crc32;      // 对 count 条记录体
} snap_header_t;

size_t jianlu_snapshot_encode(const jianlu_store_t *store, uint8_t *buf, size_t cap)
{
    if (cap < sizeof(snap_header_t)) return 0;
    // 语音占位卡是本地状态,不进快照
    uint8_t kept[JIANLU_MAX_RECORDS];
    int n = 0;
    for (int i = 0; i < store->count; i++) {
        if (!store->records[i].voice_placeholder) kept[n++] = (uint8_t)i;
    }
    size_t body = (size_t)n * sizeof(jianlu_record_t);
    if (cap < sizeof(snap_header_t) + body) return 0;
    uint8_t *dst = buf + sizeof(snap_header_t);
    for (int i = 0; i < n; i++) {
        memcpy(dst, &store->records[kept[i]], sizeof(jianlu_record_t));
        dst += sizeof(jianlu_record_t);
    }
    snap_header_t h = {
        .magic = JIANLU_SNAP_MAGIC,
        .version = JIANLU_SNAP_VERSION,
        .count = (uint8_t)n,
        .rec_size = sizeof(jianlu_record_t),
        .crc32 = jianlu_crc32(buf + sizeof(snap_header_t), body),
    };
    memcpy(buf, &h, sizeof(h));
    return sizeof(h) + body;
}

bool jianlu_snapshot_decode(const uint8_t *buf, size_t len, jianlu_store_t *store)
{
    if (buf == NULL || store == NULL || len < sizeof(snap_header_t)) return false;
    snap_header_t h;
    memcpy(&h, buf, sizeof(h));
    if (h.magic != JIANLU_SNAP_MAGIC || h.version != JIANLU_SNAP_VERSION) return false;
    if (h.rec_size != sizeof(jianlu_record_t) || h.count > JIANLU_MAX_RECORDS) return false;
    size_t body = (size_t)h.count * sizeof(jianlu_record_t);
    if (len < sizeof(h) + body) return false;
    if (h.crc32 != jianlu_crc32(buf + sizeof(h), body)) return false;

    memcpy(store->records, buf + sizeof(h), body);
    store->count = h.count;
    store->selected = 0;
    store->view = JIANLU_VIEW_READY;
    store->offline = true;
    store->error[0] = '\0';
    return true;
}

// ---- 待同步队列 ----
void jianlu_syncq_init(jianlu_syncq_t *q)
{
    memset(q, 0, sizeof(*q));
}

bool jianlu_syncq_contains(const jianlu_syncq_t *q, const char *id)
{
    if (id == NULL) return false;
    for (int i = 0; i < q->count; i++) {
        if (strcmp(q->ids[i], id) == 0) return true;
    }
    return false;
}

bool jianlu_syncq_add(jianlu_syncq_t *q, const char *id)
{
    if (id == NULL || id[0] == '\0') return false;
    if (jianlu_syncq_contains(q, id)) return true;
    if (q->count >= JIANLU_SYNCQ_MAX) {
        memmove(q->ids, q->ids + 1, sizeof(q->ids[0]) * (JIANLU_SYNCQ_MAX - 1));
        q->count = JIANLU_SYNCQ_MAX - 1;
    }
    jianlu_utf8_copy(q->ids[q->count], JIANLU_ID_LEN, id, JIANLU_ID_LEN - 1);
    q->count++;
    return true;
}

bool jianlu_syncq_remove(jianlu_syncq_t *q, const char *id)
{
    if (id == NULL) return false;
    for (int i = 0; i < q->count; i++) {
        if (strcmp(q->ids[i], id) != 0) continue;
        memmove(q->ids[i], q->ids[i + 1],
                sizeof(q->ids[0]) * (size_t)(q->count - i - 1));
        q->count--;
        return true;
    }
    return false;
}

typedef struct {
    uint32_t magic;
    uint8_t count;
    uint8_t reserved[3];
    uint32_t crc32;
} syncq_header_t;

size_t jianlu_syncq_encode(const jianlu_syncq_t *q, uint8_t *buf, size_t cap)
{
    size_t body = (size_t)q->count * JIANLU_ID_LEN;
    if (cap < sizeof(syncq_header_t) + body) return 0;
    syncq_header_t h = {
        .magic = JIANLU_SYNCQ_MAGIC,
        .count = (uint8_t)q->count,
        .crc32 = jianlu_crc32(q->ids, body),
    };
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), q->ids, body);
    return sizeof(h) + body;
}

bool jianlu_syncq_decode(const uint8_t *buf, size_t len, jianlu_syncq_t *q)
{
    if (buf == NULL || q == NULL || len < sizeof(syncq_header_t)) return false;
    syncq_header_t h;
    memcpy(&h, buf, sizeof(h));
    if (h.magic != JIANLU_SYNCQ_MAGIC || h.count > JIANLU_SYNCQ_MAX) return false;
    size_t body = (size_t)h.count * JIANLU_ID_LEN;
    if (len < sizeof(h) + body) return false;
    if (h.crc32 != jianlu_crc32(buf + sizeof(h), body)) return false;
    memcpy(q->ids, buf + sizeof(h), body);
    q->count = h.count;
    return true;
}
