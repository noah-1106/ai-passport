// tests/test_jianlu_netflow.c —— 联网流程状态机迁移的 host 侧测试。
#include <assert.h>

#include "jianlu_netflow.h"

static void test_first_provision_path(void) {
    // 新用户主路径:无凭据 → 配网 → 收凭据 → 连上 → 发现中枢 → 拉取 → 就绪
    jianlu_nf_t s = JIANLU_NF_CONNECTING;   // 任意初值
    assert(jianlu_netflow_event(&s, JIANLU_NF_START_NO_CREDS));
    assert(s == JIANLU_NF_PROVISIONING);
    assert(jianlu_netflow_view(s) == JIANLU_VIEW_PROVISIONING);

    assert(jianlu_netflow_event(&s, JIANLU_NF_GOT_CREDS));
    assert(s == JIANLU_NF_CONNECTING);
    assert(jianlu_netflow_event(&s, JIANLU_NF_GOT_IP));
    assert(s == JIANLU_NF_DISCOVERING);
    assert(jianlu_netflow_event(&s, JIANLU_NF_HUB_READY));
    assert(s == JIANLU_NF_LOADING);
    assert(jianlu_netflow_event(&s, JIANLU_NF_FETCH_OK));
    assert(s == JIANLU_NF_READY);
    assert(jianlu_netflow_view(s) == JIANLU_VIEW_READY);
}

static void test_kconfig_path(void) {
    // 开发路径:有凭据直连;mDNS 失败给出错
    jianlu_nf_t s = JIANLU_NF_PROVISIONING;
    assert(jianlu_netflow_event(&s, JIANLU_NF_START_WITH_CREDS));
    assert(s == JIANLU_NF_CONNECTING);
    assert(jianlu_netflow_event(&s, JIANLU_NF_GOT_IP));
    assert(jianlu_netflow_event(&s, JIANLU_NF_HUB_FAIL));
    assert(s == JIANLU_NF_ERROR);
    assert(jianlu_netflow_view(s) == JIANLU_VIEW_ERROR);
}

static void test_flaky_creds_fall_back_to_provisioning(void) {
    // NVS 凭据失效:连接屡败 → 配网态
    jianlu_nf_t s;
    jianlu_netflow_event(&s, JIANLU_NF_START_WITH_CREDS);
    assert(jianlu_netflow_event(&s, JIANLU_NF_CONNECT_FAIL));
    assert(s == JIANLU_NF_PROVISIONING);
    // 配网态下 GOT_IP(BLUFI 带来的连接成功)可直奔发现
    assert(jianlu_netflow_event(&s, JIANLU_NF_GOT_IP));
    assert(s == JIANLU_NF_DISCOVERING);
}

static void test_ready_disconnect_and_reprovision(void) {
    jianlu_nf_t s;
    jianlu_netflow_event(&s, JIANLU_NF_START_WITH_CREDS);
    jianlu_netflow_event(&s, JIANLU_NF_GOT_IP);
    jianlu_netflow_event(&s, JIANLU_NF_HUB_READY);
    jianlu_netflow_event(&s, JIANLU_NF_FETCH_OK);
    assert(s == JIANLU_NF_READY);

    // READY 掉线 → 重连;重连成功重新发现
    assert(jianlu_netflow_event(&s, JIANLU_NF_DISCONNECT));
    assert(s == JIANLU_NF_CONNECTING);
    assert(jianlu_netflow_event(&s, JIANLU_NF_GOT_IP));
    assert(s == JIANLU_NF_DISCOVERING);

    // 任意状态确认重配 → 配网态
    assert(jianlu_netflow_event(&s, JIANLU_NF_REPROVISION));
    assert(s == JIANLU_NF_PROVISIONING);
}

static void test_illegal_transitions_rejected(void) {
    jianlu_nf_t s = JIANLU_NF_READY;
    assert(!jianlu_netflow_event(&s, JIANLU_NF_GOT_CREDS));
    assert(s == JIANLU_NF_READY);
    assert(!jianlu_netflow_event(&s, JIANLU_NF_FETCH_OK));
    assert(!jianlu_netflow_event(&s, JIANLU_NF_HUB_READY));

    s = JIANLU_NF_PROVISIONING;
    assert(!jianlu_netflow_event(&s, JIANLU_NF_CONNECT_FAIL));
    assert(!jianlu_netflow_event(&s, JIANLU_NF_DISCONNECT));
    assert(s == JIANLU_NF_PROVISIONING);

    s = JIANLU_NF_LOADING;
    assert(jianlu_netflow_event(&s, JIANLU_NF_FETCH_FAIL));
    assert(s == JIANLU_NF_ERROR);
}

int main(void) {
    test_first_provision_path();
    test_kconfig_path();
    test_flaky_creds_fall_back_to_provisioning();
    test_ready_disconnect_and_reprovision();
    test_illegal_transitions_rejected();
    return 0;
}
