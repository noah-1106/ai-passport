// tests/test_jianlu_modal.c —— 模态覆盖层按键路由表的 host 侧测试。
// 回归:提示页期间 OK 的 PRESS 不得关页(真机曾穿透成"跳过",CLICK 落到
// 列表层误触回放)。
#include <assert.h>

#include "jianlu_modal.h"

int main(void) {
    // 无模态:全部放行
    for (int b = 0; b < 3; b++) {
        for (int e = 0; e < 4; e++) {
            assert(jianlu_modal_key(JIANLU_MODAL_NONE, b, e) == JIANLU_MODAL_PASS);
        }
    }

    // 提示页:PRESS 全部吞掉(关键回归)
    assert(jianlu_modal_key(JIANLU_MODAL_SYNC_PROMPT, JIANLU_BTN_OK,
                            JIANLU_EV_PRESS) == JIANLU_MODAL_SWALLOW);
    assert(jianlu_modal_key(JIANLU_MODAL_SYNC_PROMPT, JIANLU_BTN_UP,
                            JIANLU_EV_PRESS) == JIANLU_MODAL_SWALLOW);
    // 双击/长按吞掉
    assert(jianlu_modal_key(JIANLU_MODAL_SYNC_PROMPT, JIANLU_BTN_OK,
                            JIANLU_EV_DOUBLE) == JIANLU_MODAL_SWALLOW);
    assert(jianlu_modal_key(JIANLU_MODAL_SYNC_PROMPT, JIANLU_BTN_UP,
                            JIANLU_EV_LONG) == JIANLU_MODAL_SWALLOW);
    // OK 单击 → 开始同步;UP/DOWN 单击 → 跳过
    assert(jianlu_modal_key(JIANLU_MODAL_SYNC_PROMPT, JIANLU_BTN_OK,
                            JIANLU_EV_CLICK) == JIANLU_MODAL_START_SYNC);
    assert(jianlu_modal_key(JIANLU_MODAL_SYNC_PROMPT, JIANLU_BTN_UP,
                            JIANLU_EV_CLICK) == JIANLU_MODAL_SKIP);
    assert(jianlu_modal_key(JIANLU_MODAL_SYNC_PROMPT, JIANLU_BTN_DOWN,
                            JIANLU_EV_CLICK) == JIANLU_MODAL_SKIP);

    // 汇总页:OK 单击返回,其余全吞
    assert(jianlu_modal_key(JIANLU_MODAL_SUMMARY, JIANLU_BTN_OK,
                            JIANLU_EV_CLICK) == JIANLU_MODAL_DISMISS);
    assert(jianlu_modal_key(JIANLU_MODAL_SUMMARY, JIANLU_BTN_OK,
                            JIANLU_EV_PRESS) == JIANLU_MODAL_SWALLOW);
    assert(jianlu_modal_key(JIANLU_MODAL_SUMMARY, JIANLU_BTN_UP,
                            JIANLU_EV_CLICK) == JIANLU_MODAL_SWALLOW);
    assert(jianlu_modal_key(JIANLU_MODAL_SUMMARY, JIANLU_BTN_DOWN,
                            JIANLU_EV_LONG) == JIANLU_MODAL_SWALLOW);
    return 0;
}
