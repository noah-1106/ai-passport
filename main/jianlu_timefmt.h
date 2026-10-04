// main/jianlu_timefmt.h —— 时间解析与格式化(纯 C,host 可测)。
//
// 对时来源:中枢 HTTP 响应的 Date 头(比 SNTP 省事,每次成功拉取顺手对时)。
// 显示固定 UTC+8(用户时区),未对时(epoch 过小)由调用方回退文案。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 认为已可靠对时的下限(2025-01-01 UTC)。
#define JIANLU_TIME_VALID_EPOCH 1735689600u

// 解析 HTTP Date("Thu, 09 Oct 2026 07:00:00 GMT")为 epoch 秒;失败返回 0。
uint32_t jianlu_time_parse_http_date(const char *date);

// epoch(UTC 秒)→ 本地显示 "MM-DD HH:mm"(固定 UTC+8)。buf 至少 15 字节。
void jianlu_time_format_mmdd_hhmm(uint32_t epoch, char *buf, size_t len);

bool jianlu_time_is_valid(uint32_t epoch);
