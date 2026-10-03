// main/jianlu_netflow.h —— 联网流程状态机(纯 C,host 可测)。
//
// 首配主路径:PROVISIONING(BLUFI 广播等凭据)→ CONNECTING → DISCOVERING
// (mDNS 找中枢)→ LOADING → READY。任何阶段收到 REPROVISION(用户在 UI
// 确认重配)都回 PROVISIONING。连接屡败(NVS 凭据失效)也回 PROVISIONING。
#pragma once

#include <stdbool.h>

#include "jianlu_store.h"

typedef enum {
    JIANLU_NF_PROVISIONING = 0,
    JIANLU_NF_CONNECTING,
    JIANLU_NF_DISCOVERING,
    JIANLU_NF_LOADING,
    JIANLU_NF_READY,
    JIANLU_NF_ERROR,
} jianlu_nf_t;

typedef enum {
    JIANLU_NF_START_NO_CREDS = 0,  // 启动:无 Wi-Fi 凭据
    JIANLU_NF_START_WITH_CREDS,    // 启动:有凭据(NVS 或 Kconfig)
    JIANLU_NF_GOT_CREDS,           // BLUFI 收到新凭据
    JIANLU_NF_GOT_IP,              // Wi-Fi 拿到 IP
    JIANLU_NF_CONNECT_FAIL,        // 连接失败达到阈值
    JIANLU_NF_DISCONNECT,          // READY 后掉线
    JIANLU_NF_HUB_READY,           // 中枢地址已确定(Kconfig/mDNS/NVS)
    JIANLU_NF_HUB_FAIL,            // 找不到中枢
    JIANLU_NF_FETCH_OK,            // 首拉成功
    JIANLU_NF_FETCH_FAIL,          // 首拉失败
    JIANLU_NF_REPROVISION,         // 用户确认重配
} jianlu_nf_ev_t;

// 迁移;非法迁移保持原状态并返回 false。
bool jianlu_netflow_event(jianlu_nf_t *state, jianlu_nf_ev_t ev);

// 状态 → UI 视图。
jianlu_view_t jianlu_netflow_view(jianlu_nf_t state);
