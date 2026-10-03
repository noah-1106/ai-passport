// main/jianlu_json.h —— 中枢响应 JSON 解析(只取需要的字段,逐条写入 store)。
#pragma once

#include <stddef.h>

#include "jianlu_store.h"

// 解析 GET /api/records 响应体 {"total":N,"records":[...]},把 records 逐条
// 填入 store(先 replace_begin,成功解析后 replace_end)。只读 id/title/summary/
// type/createdAt/tags 六个字段,其余不解析,省内存。
// 返回解析到的条数;JSON 结构错误返回 -1(此时 store 内容不变)。
int jianlu_json_parse_records(const char *body, size_t len, jianlu_store_t *store);

// POST /api/device/capture 的响应:语音识别与 AI 确认。
#define JIANLU_TRANSCRIPT_LEN 192
#define JIANLU_REPLY_LEN      192

typedef struct {
    char transcript[JIANLU_TRANSCRIPT_LEN];
    char reply[JIANLU_REPLY_LEN];
    int new_count;   // records 数组条数(无该字段为 0)
} jianlu_capture_result_t;

// 解析 {"transcript":...,"reply":...,"records":[...]}。字段缺失按空串/0 处理。
// 返回 0;JSON 结构错误返回 -1。
int jianlu_json_parse_capture(const char *body, size_t len, jianlu_capture_result_t *out);
