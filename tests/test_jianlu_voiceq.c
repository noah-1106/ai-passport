// tests/test_jianlu_voiceq.c —— 语音多槽队列(移位 FIFO)的 host 侧测试。
#include <assert.h>

#include "jianlu_voiceq.h"

int main(void) {
    jianlu_voiceq_t q;
    jianlu_voiceq_init(&q);
    assert(q.count == 0);
    assert(jianlu_voiceq_head(&q) == -1);
    assert(!jianlu_voiceq_full(&q));
    assert(!jianlu_voiceq_pop(&q));   // 空队列 pop 安全

    // 依次入槽 1..4
    assert(jianlu_voiceq_push(&q) == 1);
    assert(jianlu_voiceq_push(&q) == 2);
    assert(jianlu_voiceq_push(&q) == 3);
    assert(jianlu_voiceq_push(&q) == 4);
    assert(q.count == 4);
    assert(jianlu_voiceq_full(&q));
    assert(jianlu_voiceq_push(&q) == -1);   // 满:拒绝,绝不覆盖
    assert(q.count == 4);

    // FIFO:头永远是 1(最老);pop 后头部交接
    assert(jianlu_voiceq_head(&q) == 1);
    assert(jianlu_voiceq_pop(&q));
    assert(q.count == 3);
    assert(jianlu_voiceq_head(&q) == 1);   // 移位模型下头部恒为 p1
    assert(!jianlu_voiceq_full(&q));

    // 出队后再入队:新尾 = 4 号槽
    assert(jianlu_voiceq_push(&q) == 4);
    assert(q.count == 4);
    assert(jianlu_voiceq_push(&q) == -1);

    // 排空
    while (jianlu_voiceq_pop(&q)) {}
    assert(q.count == 0);
    assert(jianlu_voiceq_head(&q) == -1);
    return 0;
}
