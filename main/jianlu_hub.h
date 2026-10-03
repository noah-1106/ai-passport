// main/jianlu_hub.h —— 中枢 HTTP 访问(阻塞式,只能在网络工作任务里调用)。
#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "jianlu_store.h"

// GET {CONFIG_XIAONUO_HUB_URL}/api/records?status=pending&pageSize=20
// 成功时把 records 填入 store 并返回 ESP_OK;失败返回错误并把可读说明写进
// errbuf(可为 NULL)。无 PSRAM:响应收进定长静态缓冲,超长直接报错,
// 服务器侧 pageSize=20 的正常响应远在缓冲之内。
esp_err_t jianlu_hub_fetch(jianlu_store_t *store, char *errbuf, size_t errbuf_len);

// PUT {CONFIG_XIAONUO_HUB_URL}/api/records/{id},body {"status":"completed"}。
esp_err_t jianlu_hub_complete(const char *id);
