// main/jianlu_modal.c —— 见 jianlu_modal.h。
#include "jianlu_modal.h"

jianlu_modal_act_t jianlu_modal_key(jianlu_modal_t modal, int btn, int ev)
{
    switch (modal) {
    case JIANLU_MODAL_SYNC_PROMPT:
        if (ev == JIANLU_EV_PRESS || ev == JIANLU_EV_DOUBLE || ev == JIANLU_EV_LONG) {
            return JIANLU_MODAL_SWALLOW;   // 按下/双击/长按不关页
        }
        if (btn == JIANLU_BTN_OK && ev == JIANLU_EV_CLICK) {
            return JIANLU_MODAL_START_SYNC;
        }
        return JIANLU_MODAL_SKIP;          // UP/DOWN 单击:跳过
    case JIANLU_MODAL_SUMMARY:
        if (btn == JIANLU_BTN_OK && ev == JIANLU_EV_CLICK) {
            return JIANLU_MODAL_DISMISS;
        }
        return JIANLU_MODAL_SWALLOW;
    default:
        return JIANLU_MODAL_PASS;
    }
}
