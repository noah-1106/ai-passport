// tests/test_jianlu_voiceq.c —— 语音队列(移位 FIFO + 时间戳)的 host 侧测试。
#include <assert.h>
#include <string.h>

#include "jianlu_voiceq.h"

int main(void) {
    jianlu_voiceq_t q;
    jianlu_voiceq_init(&q);
    assert(q.count == 0);
    assert(jianlu_voiceq_head(&q) == -1);
    assert(!jianlu_voiceq_pop(&q));

    // 入队带时间戳
    assert(jianlu_voiceq_push(&q, 1000) == 1);
    assert(jianlu_voiceq_push(&q, 2000) == 2);
    assert(jianlu_voiceq_push(&q, 3000) == 3);
    assert(q.count == 3);
    assert(q.ts[0] == 1000 && q.ts[1] == 2000 && q.ts[2] == 3000);

    // 出队头部:时间戳同步下移
    assert(jianlu_voiceq_pop(&q));
    assert(q.count == 2);
    assert(q.ts[0] == 2000 && q.ts[1] == 3000);

    // 任意槽删除
    assert(jianlu_voiceq_remove_at(&q, 2));
    assert(q.count == 1);
    assert(q.ts[0] == 2000);
    assert(!jianlu_voiceq_remove_at(&q, 0));
    assert(!jianlu_voiceq_remove_at(&q, 5));

    // 编解码往返(含时间戳)
    jianlu_voiceq_push(&q, 4000);
    jianlu_voiceq_push(&q, 5000);
    uint8_t buf[8 + JIANLU_VOICEQ_SLOTS * 4];
    size_t n = jianlu_voiceq_encode(&q, buf, sizeof(buf));
    assert(n > 0);
    jianlu_voiceq_t q2;
    jianlu_voiceq_init(&q2);
    assert(jianlu_voiceq_decode(buf, n, &q2));
    assert(q2.count == q.count);
    assert(memcmp(&q2, &q, sizeof(q)) == 0);

    // 损坏拒绝
    buf[0] ^= 0xFF;
    jianlu_voiceq_init(&q2);
    assert(!jianlu_voiceq_decode(buf, n, &q2));
    buf[0] ^= 0xFF;
    assert(!jianlu_voiceq_decode(buf, 3, &q2));

    // 排空
    while (jianlu_voiceq_pop(&q)) {}
    assert(q.count == 0);
    assert(jianlu_voiceq_head(&q) == -1);
    return 0;
}
