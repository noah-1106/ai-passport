// main/jianlu_powersave.c —— 见 jianlu_powersave.h。
#include "jianlu_powersave.h"

void jianlu_ps_init(jianlu_ps_t *ps)
{
    ps->idle_s = 0;
    ps->screen_on = true;
    ps->busy = false;
    ps->eat_gesture = false;
}

uint8_t jianlu_ps_tick(jianlu_ps_t *ps)
{
    if (ps->busy || ps->keep_on) {
        ps->idle_s = 0;
        return JIANLU_PS_ACT_NONE;
    }
    ps->idle_s++;
    if (ps->screen_on && ps->idle_s >= JIANLU_PS_SCREEN_OFF_S) {
        ps->screen_on = false;
        if (ps->idle_s >= JIANLU_PS_LIGHT_SLEEP_S) {
            return JIANLU_PS_ACT_LIGHT_SLEEP;   // 已超过更深阈值,直接睡
        }
        return JIANLU_PS_ACT_SCREEN_OFF;
    }
    if (!ps->screen_on && ps->idle_s == JIANLU_PS_LIGHT_SLEEP_S) {
        return JIANLU_PS_ACT_LIGHT_SLEEP;
    }
    return JIANLU_PS_ACT_NONE;
}

bool jianlu_ps_key(jianlu_ps_t *ps, bool gesture_end)
{
    ps->idle_s = 0;
    if (!ps->screen_on) {
        // 灭屏首事件:唤醒并吞掉,且吞到本手势结束
        ps->screen_on = true;
        ps->eat_gesture = true;
        return true;
    }
    if (ps->eat_gesture) {
        if (gesture_end) ps->eat_gesture = false;
        return true;
    }
    return false;
}

void jianlu_ps_set_keep_on(jianlu_ps_t *ps, bool keep_on)
{
    ps->keep_on = keep_on;
    if (keep_on) {
        ps->idle_s = 0;
        ps->screen_on = true;   // 开常亮立即亮屏
    }
}

void jianlu_ps_set_busy(jianlu_ps_t *ps, bool busy)
{
    ps->busy = busy;
    if (busy) ps->idle_s = 0;
}

void jianlu_ps_woke(jianlu_ps_t *ps)
{
    ps->idle_s = 0;
    ps->screen_on = true;
    ps->eat_gesture = true;   // 睡中按下唤醒键的手势,事件会随后到来,吞掉
}
