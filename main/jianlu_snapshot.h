// main/jianlu_snapshot.h —— 清单快照与待同步队列的编解码(纯 C,host 可测)。
//
// 快照:魔数+版本+条数+记录数组(直接按 jianlu_record_t 内存布局,同一
// 固件自产自读,版本号与尺寸守卫布局漂移)+ CRC32 防半个文件。
// 待同步队列:离线勾选的记录 id,定长 8 槽,last-write-wins,满了丢最旧。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jianlu_store.h"

#define JIANLU_SNAP_MAGIC   0x4A4C5350u   // "JLSP"
#define JIANLU_SNAP_VERSION 1
#define JIANLU_SYNCQ_MAGIC  0x4A4C5351u   // "JLSQ"
#define JIANLU_SYNCQ_MAX    8

uint32_t jianlu_crc32(const void *data, size_t len);

// 编码清单记录到 buf;返回长度,cap 不足返回 0。offline 标志不入快照
// (是否离线由加载方决定)。
size_t jianlu_snapshot_encode(const jianlu_store_t *store, uint8_t *buf, size_t cap);

// 解码快照到 store:替换清单,置 view=READY、offline=true。校验失败返回
// false 且 store 不变。
bool jianlu_snapshot_decode(const uint8_t *buf, size_t len, jianlu_store_t *store);

typedef struct {
    char ids[JIANLU_SYNCQ_MAX][JIANLU_ID_LEN];
    int count;
} jianlu_syncq_t;

void jianlu_syncq_init(jianlu_syncq_t *q);
// 已在队列里返回 true(不重复加);满了挤掉最旧的一条再放新(新的优先同步)。
bool jianlu_syncq_add(jianlu_syncq_t *q, const char *id);
bool jianlu_syncq_remove(jianlu_syncq_t *q, const char *id);
bool jianlu_syncq_contains(const jianlu_syncq_t *q, const char *id);

size_t jianlu_syncq_encode(const jianlu_syncq_t *q, uint8_t *buf, size_t cap);
bool jianlu_syncq_decode(const uint8_t *buf, size_t len, jianlu_syncq_t *q);
