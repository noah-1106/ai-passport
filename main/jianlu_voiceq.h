// main/jianlu_voiceq.h —— 离线语音队列(移位 FIFO,纯 C,host 可测)。
//
// 模型:槽位 1..N(文件 p1.wav..pN.wav),始终连续:
//   入队(录音):占 count+1 槽,记录时间戳;不设条数上限(空间不足由
//     文件系统层拦截),JIANLU_VOICEQ_SLOTS 只是防失控硬顶。
//   出队(补传成功):删除头部(p1),其余下移一格,时间戳同步移动。
// 元数据(每槽录音时刻 epoch,0=未对时)可编解码落盘。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JIANLU_VOICEQ_SLOTS   64
#define JIANLU_VOICEQ_MAGIC   0x4A4C5651u   // "JLVQ"
#define JIANLU_VOICEQ_VERSION 1

typedef struct {
    int count;                               // 已占槽数 0..SLOTS
    uint32_t ts[JIANLU_VOICEQ_SLOTS];        // 每槽录音时刻(epoch 秒,0=未知)
} jianlu_voiceq_t;

void jianlu_voiceq_init(jianlu_voiceq_t *q);

// 入队:返回新槽位号;ts 为该槽时间戳(0=未对时)。硬顶返回 -1。
int jianlu_voiceq_push(jianlu_voiceq_t *q, uint32_t ts);

// 头部槽位号(下次补传/最老一条);空返回 -1。
int jianlu_voiceq_head(const jianlu_voiceq_t *q);

// 出队头部(时间戳同步下移);空队列返回 false。
bool jianlu_voiceq_pop(jianlu_voiceq_t *q);

// 删除任意槽(时间戳同步下移);越界返回 false。
bool jianlu_voiceq_remove_at(jianlu_voiceq_t *q, int slot);

// 编解码元数据(供 SPIFFS 侧车文件);decode 校验失败返回 false 且不动 q。
size_t jianlu_voiceq_encode(const jianlu_voiceq_t *q, uint8_t *buf, size_t cap);
bool jianlu_voiceq_decode(const uint8_t *buf, size_t len, jianlu_voiceq_t *q);
