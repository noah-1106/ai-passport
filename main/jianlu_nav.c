// main/jianlu_nav.c —— 见 jianlu_nav.h。
#include "jianlu_nav.h"

#define BTN_UP 0
#define BTN_DOWN 1
#define BTN_OK 2
#define EV_CLICK 1
#define EV_DOUBLE 2

void jianlu_nav_init(jianlu_nav_t *nav)
{
    nav->page = JIANLU_PAGE_HOME;
    nav->home_focus = 0;
    nav->settings_focus = 0;
}

jianlu_page_t jianlu_nav_home_target(int focus)
{
    switch (focus) {
    case 0:  return JIANLU_PAGE_JIANLU;
    case 1:  return JIANLU_PAGE_QR;
    default: return JIANLU_PAGE_SETTINGS;
    }
}

jianlu_nav_result_t jianlu_nav_key(jianlu_nav_t *nav, int btn, int ev)
{
    if (ev != EV_CLICK && ev != EV_DOUBLE) return JIANLU_NAV_NO_CHANGE;

    switch (nav->page) {
    case JIANLU_PAGE_HOME:
        if (ev == EV_CLICK && btn == BTN_UP) {
            nav->home_focus = (nav->home_focus + JIANLU_HOME_ITEMS - 1)
                              % JIANLU_HOME_ITEMS;
            return JIANLU_NAV_FOCUS_CHANGED;
        }
        if (ev == EV_CLICK && btn == BTN_DOWN) {
            nav->home_focus = (nav->home_focus + 1) % JIANLU_HOME_ITEMS;
            return JIANLU_NAV_FOCUS_CHANGED;
        }
        if (ev == EV_CLICK && btn == BTN_OK) {
            nav->page = jianlu_nav_home_target(nav->home_focus);
            if (nav->page == JIANLU_PAGE_SETTINGS) nav->settings_focus = 0;
            return JIANLU_NAV_PAGE_CHANGED;
        }
        return JIANLU_NAV_NO_CHANGE;

    case JIANLU_PAGE_SETTINGS:
        if (ev == EV_DOUBLE && btn == BTN_OK) {
            nav->page = JIANLU_PAGE_HOME;
            return JIANLU_NAV_PAGE_CHANGED;
        }
        if (ev == EV_CLICK && (btn == BTN_UP || btn == BTN_DOWN)) {
            int dir = btn == BTN_UP ? -1 : 1;
            nav->settings_focus = (nav->settings_focus + JIANLU_SETTINGS_ITEMS
                                   + dir) % JIANLU_SETTINGS_ITEMS;
            return JIANLU_NAV_FOCUS_CHANGED;
        }
        if (ev == EV_CLICK && btn == BTN_OK) {
            return JIANLU_NAV_SETTINGS_ACTION;
        }
        return JIANLU_NAV_NO_CHANGE;

    case JIANLU_PAGE_QR:
        if (ev == EV_DOUBLE && btn == BTN_OK) {
            nav->page = JIANLU_PAGE_HOME;
            return JIANLU_NAV_PAGE_CHANGED;
        }
        return JIANLU_NAV_NO_CHANGE;

    case JIANLU_PAGE_JIANLU:
    default:
        // 简录页:OK 双击回主页,其余按键放行给卡片堆
        if (ev == EV_DOUBLE && btn == BTN_OK) {
            nav->page = JIANLU_PAGE_HOME;
            return JIANLU_NAV_PAGE_CHANGED;
        }
        return JIANLU_NAV_NO_CHANGE;
    }
}
