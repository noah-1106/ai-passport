// main/jianlu_modal.h —— 模态覆盖层按键路由表(纯 C,host 可测)。
//
// 背景(真机事故):提示页只过滤了 CLICK,OK 的 PRESS 事件穿透分支把
// 提示当"其他键"关掉,随后的 CLICK 落到列表层触发了回放。
// 规则:PRESS 一律吞掉不关页;动作只在手势收尾(CLICK)判定。
#pragma once

// 模态页类型
typedef enum {
    JIANLU_MODAL_NONE = 0,     // 无模态,事件放行到列表层
    JIANLU_MODAL_SYNC_PROMPT,  // 「发现离线内容」:OK 开始同步,其他键跳过
    JIANLU_MODAL_SUMMARY,      // 「同步完成/中断」:OK 返回,其余吞掉
    JIANLU_MODAL_INFO,         // 信息页(关于等):OK 单击关闭,其余吞掉
} jianlu_modal_t;

// 按键与事件(与 bsp_button.h 的取值一致,纯模块不依赖 BSP 头)
#define JIANLU_BTN_UP    0
#define JIANLU_BTN_DOWN  1
#define JIANLU_BTN_OK    2
#define JIANLU_EV_PRESS  0
#define JIANLU_EV_CLICK  1
#define JIANLU_EV_DOUBLE 2
#define JIANLU_EV_LONG   3

typedef enum {
    JIANLU_MODAL_PASS = 0,      // 无模态或放行(交给列表层)
    JIANLU_MODAL_SWALLOW,       // 吞掉(PRESS/DOUBLE/LONG 等)
    JIANLU_MODAL_START_SYNC,    // 提示页 OK 单击:开始同步
    JIANLU_MODAL_SKIP,          // 提示页其他键单击:跳过/稍后
    JIANLU_MODAL_DISMISS,       // 汇总页 OK 单击:返回
} jianlu_modal_act_t;

jianlu_modal_act_t jianlu_modal_key(jianlu_modal_t modal, int btn, int ev);
