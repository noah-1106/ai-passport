// main/jianlu_powersave.h —— 省电策略状态(纯 C,host 可测)。
//
// 规则:忙(录音/上传)时不累计空闲、不熄屏;空闲 60s 熄屏(背光,Wi-Fi 保持),
// 空闲 600s 建议进 light sleep。任意键复位空闲并点亮屏幕。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define JIANLU_PS_SCREEN_OFF_S   60
#define JIANLU_PS_LIGHT_SLEEP_S  600

#define JIANLU_PS_ACT_NONE        0
#define JIANLU_PS_ACT_SCREEN_OFF  1
#define JIANLU_PS_ACT_LIGHT_SLEEP 2

typedef struct {
    uint32_t idle_s;
    bool screen_on;
    bool busy;
    bool eat_gesture;   // 正在吞没唤醒手势的后续事件(首事件已吞)
} jianlu_ps_t;

void jianlu_ps_init(jianlu_ps_t *ps);

// 每秒一次;返回本秒建议的动作(边沿触发:熄屏/睡眠各报一次)。
uint8_t jianlu_ps_tick(jianlu_ps_t *ps);

// 按键事件输入。gesture_end: 本事件是否一个手势的收尾(CLICK/DOUBLE/LONG;
// PRESS 不是)。返回 true 表示本事件应被吞没,不交给业务层:
//   1. 灭屏时的首事件(唤醒键);
//   2. 唤醒手势的后续事件(同一个按下-抬起过程中的 PRESS/CLICK/LONG)。
// 未吞没的普通按键同时复位空闲计时。
bool jianlu_ps_key(jianlu_ps_t *ps, bool gesture_end);

// 忙状态切换(录音/上传中禁止休眠)。
void jianlu_ps_set_busy(jianlu_ps_t *ps, bool busy);

// light sleep 返回后调用:当作一次唤醒(并吞没唤醒手势)。
void jianlu_ps_woke(jianlu_ps_t *ps);
