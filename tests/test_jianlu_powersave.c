// tests/test_jianlu_powersave.c —— 省电策略状态机的 host 侧测试。
#include <assert.h>

#include "jianlu_powersave.h"

int main(void) {
    jianlu_ps_t ps;
    jianlu_ps_init(&ps);
    assert(ps.screen_on && ps.idle_s == 0);

    // 59 秒无动作
    for (int i = 0; i < JIANLU_PS_SCREEN_OFF_S - 1; i++) {
        assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_NONE);
    }
    // 第 60 秒熄屏(边沿,只报一次)
    assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_SCREEN_OFF);
    assert(!ps.screen_on);
    assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_NONE);

    // 灭屏首事件:只唤醒并被吞;随后整个唤醒手势(PRESS→CLICK)都被吞
    assert(jianlu_ps_key(&ps, false));       // PRESS(非手势收尾)→ 吞
    assert(ps.screen_on && ps.idle_s == 0);
    assert(jianlu_ps_key(&ps, false));       // 手势中间的 PRESS 重复 → 仍吞
    assert(jianlu_ps_key(&ps, true));        // CLICK(手势收尾)→ 吞,手势结束
    assert(!jianlu_ps_key(&ps, false));      // 新手势 PRESS → 放行
    assert(!jianlu_ps_key(&ps, true));       // 其 CLICK → 也放行

    // 亮屏状态按键只复位空闲,不吞
    jianlu_ps_tick(&ps);
    assert(!jianlu_ps_key(&ps, false));
    assert(ps.idle_s == 0);

    // 忙(录音/上传)不累计空闲
    jianlu_ps_set_busy(&ps, true);
    for (int i = 0; i < JIANLU_PS_LIGHT_SLEEP_S + 10; i++) {
        assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_NONE);
    }
    assert(ps.screen_on && ps.idle_s == 0);
    jianlu_ps_set_busy(&ps, false);

    // 600 秒进 light sleep(只在跨越时报一次)
    for (int i = 0; i < JIANLU_PS_SCREEN_OFF_S; i++) jianlu_ps_tick(&ps);
    assert(!ps.screen_on);
    for (int i = JIANLU_PS_SCREEN_OFF_S + 1; i < JIANLU_PS_LIGHT_SLEEP_S; i++) {
        assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_NONE);
    }
    assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_LIGHT_SLEEP);
    assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_NONE);

    // 睡醒复位:同时进入吞手势模式(睡中按下唤醒键的事件随后会到)
    jianlu_ps_woke(&ps);
    assert(ps.screen_on && ps.idle_s == 0);
    assert(jianlu_ps_key(&ps, false));       // 唤醒手势 PRESS → 吞
    assert(jianlu_ps_key(&ps, true));        // 收尾 → 吞并结束
    assert(!jianlu_ps_key(&ps, false));      // 之后正常

    // 灭屏前一键重置计时
    for (int i = 0; i < JIANLU_PS_SCREEN_OFF_S - 1; i++) jianlu_ps_tick(&ps);
    jianlu_ps_key(&ps, false);
    for (int i = 0; i < JIANLU_PS_SCREEN_OFF_S - 1; i++) {
        assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_NONE);
    }
    assert(jianlu_ps_tick(&ps) == JIANLU_PS_ACT_SCREEN_OFF);
    return 0;
}
