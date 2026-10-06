// tests/test_jianlu_nav.c —— 页面导航状态机的 host 侧测试。
#include <assert.h>

#include "jianlu_nav.h"

#define UP 0
#define DOWN 1
#define OK 2
#define CLICK 1
#define DOUBLE 2
#define PRESS 0
#define LONG 3

int main(void) {
    jianlu_nav_t nav;
    jianlu_nav_init(&nav);
    assert(nav.page == JIANLU_PAGE_HOME);
    assert(nav.home_focus == 0);

    // 主页:UP 从 0 循环到 2,DOWN 正向
    assert(jianlu_nav_key(&nav, UP, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(nav.home_focus == 2);
    assert(jianlu_nav_key(&nav, DOWN, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(nav.home_focus == 0);
    assert(jianlu_nav_key(&nav, DOWN, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(nav.home_focus == 1);

    // PRESS/LONG 无操作
    assert(jianlu_nav_key(&nav, OK, PRESS) == JIANLU_NAV_NO_CHANGE);
    assert(jianlu_nav_key(&nav, OK, LONG) == JIANLU_NAV_NO_CHANGE);

    // OK 进入二维码(焦点 1)
    assert(jianlu_nav_home_target(1) == JIANLU_PAGE_QR);
    assert(jianlu_nav_key(&nav, OK, CLICK) == JIANLU_NAV_PAGE_CHANGED);
    assert(nav.page == JIANLU_PAGE_QR);

    // 二维码页:UP/DOWN/OK 单击无操作,OK 双击回主页
    assert(jianlu_nav_key(&nav, UP, CLICK) == JIANLU_NAV_NO_CHANGE);
    assert(jianlu_nav_key(&nav, OK, CLICK) == JIANLU_NAV_NO_CHANGE);
    assert(jianlu_nav_key(&nav, OK, DOUBLE) == JIANLU_NAV_PAGE_CHANGED);
    assert(nav.page == JIANLU_PAGE_HOME);

    // 进设置(焦点 2)
    nav.home_focus = 2;
    assert(jianlu_nav_key(&nav, OK, CLICK) == JIANLU_NAV_PAGE_CHANGED);
    assert(nav.page == JIANLU_PAGE_SETTINGS);
    assert(nav.settings_focus == 0);

    // 设置页:UP/DOWN 移焦点(亮度行也一样,不会卡死),OK = 动作
    assert(jianlu_nav_key(&nav, DOWN, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(nav.settings_focus == 1);
    assert(jianlu_nav_key(&nav, DOWN, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(jianlu_nav_key(&nav, DOWN, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(nav.settings_focus == 3);
    assert(jianlu_nav_key(&nav, DOWN, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(nav.settings_focus == 0);   // 循环
    assert(jianlu_nav_key(&nav, UP, CLICK) == JIANLU_NAV_FOCUS_CHANGED);
    assert(nav.settings_focus == 3);

    // OK 在设置页 = 动作
    assert(jianlu_nav_key(&nav, OK, CLICK) == JIANLU_NAV_SETTINGS_ACTION);
    // OK 双击回主页
    assert(jianlu_nav_key(&nav, OK, DOUBLE) == JIANLU_NAV_PAGE_CHANGED);
    assert(nav.page == JIANLU_PAGE_HOME);

    // 简录页:UP/DOWN/OK 单击放行(Nav 不管),双击回主页
    nav.page = JIANLU_PAGE_JIANLU;
    assert(jianlu_nav_key(&nav, UP, CLICK) == JIANLU_NAV_NO_CHANGE);
    assert(jianlu_nav_key(&nav, OK, CLICK) == JIANLU_NAV_NO_CHANGE);
    assert(jianlu_nav_key(&nav, OK, DOUBLE) == JIANLU_NAV_PAGE_CHANGED);
    assert(nav.page == JIANLU_PAGE_HOME);
    return 0;
}
