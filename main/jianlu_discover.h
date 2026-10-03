// main/jianlu_discover.h —— mDNS 发现局域网内的小诺中枢(_xiaonuo._tcp)。
#pragma once

#include <stddef.h>

#include "esp_err.h"

// 阻塞式查询(在网络工作任务里调用):取第一个 _xiaonuo._tcp 实例,
// 解析出 IPv4 后组成 "http://<ip>:<port>" 写入 url。
// 找到返回 ESP_OK;超时/无服务返回 ESP_ERR_NOT_FOUND。
esp_err_t jianlu_discover_hub(char *url, size_t url_len, int timeout_ms);
