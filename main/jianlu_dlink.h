// main/jianlu_dlink.h —— BLE 直连同步模式(NUS 服务端)。
//
// 开启方式:设置页「直连模式」→ 重启进入(与配网同构的 NVS 标志 dmode)。
// 直连态:不起 Wi-Fi(内存约束二者不能共存),NimBLE 起标准 NUS 服务,
// 广播 XIAONUO_XXXX。协议见 jianlu_dlink_codec.h;语音上传 = 桥拉取分块。
#pragma once

#include <stdbool.h>

#include "jianlu_store.h"

// 启动直连服务(幂等)。须在 Wi-Fi 未启动时调用。
// 返回 false 表示 NimBLE/GATT 初始化失败(应用仍可本地浏览清单)。
bool jianlu_dlink_start(void);

// 是否已有 BLE 中央(桥)连接
bool jianlu_dlink_connected(void);

// 向已连接的中心(电脑桥)发一行(自动 MTU 分片,行尾补 '\n')。
// 返回 false = 未连接/未订阅/组包失败。短行专用(≤126 字节)。
bool jianlu_dlink_send_line(const char *line);

// ---- 应用接线(直连态清单刷新由 BLE 命令驱动)----
void jianlu_dlink_bind(jianlu_store_t *store, void (*refresh)(void));
void jianlu_dlink_set_pending(const char (*ids)[JIANLU_ID_LEN], int n);
// 桥确认某条勾选已 PUT 成功后回调(NimBLE 主机任务上下文,只许投事件)
void jianlu_dlink_set_pdone_cb(void (*cb)(const char *id));
