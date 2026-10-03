// tests/test_jianlu_snapshot.c —— 快照与待同步队列编解码的 host 侧测试。
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jianlu_snapshot.h"

static void fill_store(jianlu_store_t *s, int n) {
    jianlu_store_init(s);
    jianlu_store_replace_begin(s);
    for (int i = 0; i < n; i++) {
        char id[8], title[32];
        snprintf(id, sizeof(id), "%d", 100 + i);
        snprintf(title, sizeof(title), "事项%d", i);
        jianlu_store_add(s, id, title, "摘要", JIANLU_TYPE_TODO,
                         "2026-10-04 09:00:00", "标签", NULL);
    }
    jianlu_store_replace_end(s);
}

static void test_snapshot_roundtrip(void) {
    jianlu_store_t a, b;
    fill_store(&a, 3);
    // 一条带待同步标记
    a.records[1].sync_pending = true;

    uint8_t buf[8192];
    size_t n = jianlu_snapshot_encode(&a, buf, sizeof(buf));
    assert(n > 0);

    jianlu_store_init(&b);
    assert(jianlu_snapshot_decode(buf, n, &b));
    assert(b.count == 3);
    assert(b.view == JIANLU_VIEW_READY);
    assert(b.offline);                     // 快照载入即离线模式
    assert(strcmp(b.records[0].id, "100") == 0);
    assert(strcmp(b.records[1].title, "事项1") == 0);
    assert(b.records[1].sync_pending);
    assert(strcmp(b.records[0].date, "2026-10-04") == 0);
    assert(b.records[0].tag_count == 1);
    assert(strcmp(b.records[0].tags[0], "标签") == 0);

    // 小缓冲拒绝
    assert(jianlu_snapshot_encode(&a, buf, 8) == 0);
}

static void test_snapshot_corruption_rejected(void) {
    jianlu_store_t a, b;
    fill_store(&a, 2);
    uint8_t buf[8192];
    size_t n = jianlu_snapshot_encode(&a, buf, sizeof(buf));

    // 预填:解析失败不得动 store
    fill_store(&b, 1);
    b.view = JIANLU_VIEW_CONNECTING;

    uint8_t bad[8192];
    memcpy(bad, buf, n);
    bad[0] ^= 0xFF;                                   // 魔数错
    assert(!jianlu_snapshot_decode(bad, n, &b));
    memcpy(bad, buf, n);
    bad[n - 3] ^= 0x01;                               // 内容错 → CRC 不符
    assert(!jianlu_snapshot_decode(bad, n, &b));
    assert(!jianlu_snapshot_decode(buf, n / 2, &b));  // 截断
    assert(b.count == 1);
    assert(b.view == JIANLU_VIEW_CONNECTING);
}

static void test_syncq(void) {
    jianlu_syncq_t q;
    jianlu_syncq_init(&q);
    assert(q.count == 0);

    assert(jianlu_syncq_add(&q, "7"));
    assert(jianlu_syncq_add(&q, "8"));
    assert(jianlu_syncq_contains(&q, "7"));
    assert(!jianlu_syncq_contains(&q, "9"));
    // 重复加不翻倍
    assert(jianlu_syncq_add(&q, "7"));
    assert(q.count == 2);

    // 容量:满 8 后挤掉最旧
    for (int i = 0; i < 10; i++) {
        char id[8];
        snprintf(id, sizeof(id), "%d", 200 + i);
        jianlu_syncq_add(&q, id);
    }
    assert(q.count == JIANLU_SYNCQ_MAX);
    assert(!jianlu_syncq_contains(&q, "7"));    // 最旧的被挤掉
    assert(jianlu_syncq_contains(&q, "209"));

    assert(jianlu_syncq_remove(&q, "209"));
    assert(!jianlu_syncq_contains(&q, "209"));
    assert(!jianlu_syncq_remove(&q, "nobody"));

    // 编解码往返
    uint8_t buf[512];
    size_t n = jianlu_syncq_encode(&q, buf, sizeof(buf));
    assert(n > 0);
    jianlu_syncq_t q2;
    jianlu_syncq_init(&q2);
    assert(jianlu_syncq_decode(buf, n, &q2));
    assert(q2.count == q.count);
    assert(memcmp(&q2.ids, &q.ids, (size_t)q.count * JIANLU_ID_LEN) == 0);

    // 损坏拒绝
    buf[n - 1] ^= 0x55;
    jianlu_syncq_init(&q2);
    assert(!jianlu_syncq_decode(buf, n, &q2));
    assert(q2.count == 0);
}

static void test_snapshot_excludes_voice_placeholder(void) {
    jianlu_store_t a, b;
    fill_store(&a, 2);
    jianlu_store_ensure_voice_placeholder(&a);   // 占位卡进 0 号位
    assert(a.count == 3);

    uint8_t buf[8192];
    size_t n = jianlu_snapshot_encode(&a, buf, sizeof(buf));
    assert(n > 0);

    jianlu_store_init(&b);
    assert(jianlu_snapshot_decode(buf, n, &b));
    assert(b.count == 2);   // 占位卡不进快照
    for (int i = 0; i < b.count; i++) {
        assert(!b.records[i].voice_placeholder);
    }
    assert(strcmp(b.records[0].id, "100") == 0);
}

int main(void) {
    test_snapshot_roundtrip();
    test_snapshot_corruption_rejected();
    test_syncq();
    test_snapshot_excludes_voice_placeholder();
    return 0;
}
