// main/jianlu_voiceq.c —— 见 jianlu_voiceq.h。
#include "jianlu_voiceq.h"

void jianlu_voiceq_init(jianlu_voiceq_t *q)
{
    q->count = 0;
}

int jianlu_voiceq_push(jianlu_voiceq_t *q)
{
    if (q->count >= JIANLU_VOICEQ_SLOTS) return -1;
    q->count++;
    return q->count;
}

int jianlu_voiceq_head(const jianlu_voiceq_t *q)
{
    return q->count > 0 ? 1 : -1;
}

bool jianlu_voiceq_pop(jianlu_voiceq_t *q)
{
    if (q->count <= 0) return false;
    q->count--;
    return true;
}

bool jianlu_voiceq_full(const jianlu_voiceq_t *q)
{
    return q->count >= JIANLU_VOICEQ_SLOTS;
}
