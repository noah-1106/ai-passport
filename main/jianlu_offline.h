// main/jianlu_offline.h —— 快照/待同步队列的 SPIFFS 落盘(voicefs 分区)。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "jianlu_snapshot.h"

// 快照:每次成功拉取后覆盖写。加载成功返回 true(无快照/损坏返回 false)。
esp_err_t jianlu_offline_save_snapshot(const jianlu_store_t *store);
bool jianlu_offline_load_snapshot(jianlu_store_t *store);

// 待同步队列:无文件时视为空队列。
esp_err_t jianlu_offline_load_syncq(jianlu_syncq_t *q);
esp_err_t jianlu_offline_save_syncq(const jianlu_syncq_t *q);
