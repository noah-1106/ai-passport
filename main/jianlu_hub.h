// main/jianlu_hub.h —— 中枢 HTTP 访问(阻塞式,只能在网络工作任务里调用)。
#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "jianlu_json.h"
#include "jianlu_nvs.h"
#include "jianlu_store.h"

// 设置/读取运行时中枢地址(mDNS 发现、NVS 缓存或 Kconfig 开发覆盖)。
// 未设置(空串)时 fetch/complete 直接返回 ESP_ERR_INVALID_STATE。
void jianlu_hub_set_base(const char *url);
const char *jianlu_hub_base(void);

// 网络任务共享刮擦缓冲(9KB):fetch 响应体、capture 响应、上传分块、
// mDNS 包都复用它——网络任务内串行使用,省数份静态缓冲。
uint8_t *jianlu_net_scratch(size_t *len);

// GET {hub}/api/profile → jianlu_profile_t
esp_err_t jianlu_hub_fetch_profile(jianlu_profile_t *out);

// 流式下载到文件(GET api_path → 写 file_path,2KB 分块)。
// 用于 avatar.raw / qrcode.raw 缓存。404/非 200 返回 ESP_ERR_NOT_FOUND。
esp_err_t jianlu_hub_download_file(const char *api_path, const char *file_path);

// 缓存文件存在且非空
bool jianlu_hub_cache_exists(const char *file_path);


// GET {hub}/api/records?status=pending&pageSize=20
// 成功时把 records 填入 store 并返回 ESP_OK;失败返回错误并把可读说明写进
// errbuf(可为 NULL)。无 PSRAM:响应收进定长静态缓冲,超长直接报错,
// 服务器侧 pageSize=20 的正常响应远在缓冲之内。
esp_err_t jianlu_hub_fetch(jianlu_store_t *store, char *errbuf, size_t errbuf_len);

// PUT {CONFIG_XIAONUO_HUB_URL}/api/records/{id},body {"status":"completed"}。
esp_err_t jianlu_hub_complete(const char *id);
