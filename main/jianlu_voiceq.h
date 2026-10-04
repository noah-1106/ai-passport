// main/jianlu_voiceq.h —— 离线语音多槽队列(纯 C,host 可测)。
//
// 模型:移位 FIFO。槽位 1..4(文件 p1.wav..p4.wav),始终连续:
//   入队(录音):占 count+1 槽;满 4 槽拒绝(绝不静默覆盖)。
//   出队(补传成功):删除头部(p1),其余下移一格。
// 分区容量有限(576KB),槽数与容量是两道闸,本模块只管槽数。
#pragma once

#include <stdbool.h>

#define JIANLU_VOICEQ_SLOTS 4

typedef struct {
    int count;   // 已占槽数 0..JIANLU_VOICEQ_SLOTS
} jianlu_voiceq_t;

void jianlu_voiceq_init(jianlu_voiceq_t *q);

// 入队:返回新槽位号(1..SLOTS);满返回 -1。
int jianlu_voiceq_push(jianlu_voiceq_t *q);

// 头部槽位号(下次补传/最老一条);空返回 -1。
int jianlu_voiceq_head(const jianlu_voiceq_t *q);

// 出队头部(补传成功后调用);空队列返回 false。
bool jianlu_voiceq_pop(jianlu_voiceq_t *q);

// 满了吗(此时再录必须拒绝并提示)。
bool jianlu_voiceq_full(const jianlu_voiceq_t *q);
