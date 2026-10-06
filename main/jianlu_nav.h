// main/jianlu_nav.h —— 页面导航状态机(纯 C,host 可测)。
//
// 页面:主页(HOME,开机默认)/ 简录(JIANLU,卡片堆)/ 二维码(QR)/ 设置(SETTINGS)。
// 规则(v2 产品定义):
//   主页:UP/DOWN 移菜单焦点(三格循环),OK 单击进入,OK 双击无操作
//   子页:OK 双击回主页
//   设置页:UP/DOWN 移焦点(四行循环),OK 单击 = 触发当前行动作
//           (亮度行 OK 循环调档 25/50/75/100,避免"亮度行吃掉 UP/DOWN
//           移不出焦点"的死角)
//   简录页:UP/DOWN/OK 由卡片堆逻辑处理(本模块 NO_CHANGE 放行)
// 全局手势(OK 长按说话、UP 长按重配)在应用层先行拦截,不进本模块。
#pragma once

typedef enum {
    JIANLU_PAGE_HOME = 0,
    JIANLU_PAGE_JIANLU,
    JIANLU_PAGE_QR,
    JIANLU_PAGE_SETTINGS,
    JIANLU_PAGE_COUNT,
} jianlu_page_t;

typedef struct {
    jianlu_page_t page;
    int home_focus;      // 0 简录 1 二维码 2 设置
    int settings_focus;  // 0 亮度 1 立即同步 2 关于 3 重新配网
} jianlu_nav_t;

typedef enum {
    JIANLU_NAV_NO_CHANGE = 0,
    JIANLU_NAV_FOCUS_CHANGED,   // 焦点移动(重绘当前页)
    JIANLU_NAV_PAGE_CHANGED,    // nav->page 已切换
    JIANLU_NAV_SETTINGS_ACTION, // 设置页 OK(读 settings_focus 决定动作)
} jianlu_nav_result_t;

// btn/ev 取值与 bsp_button.h 一致(0=UP 1=DOWN 2=OK;0=PRESS 1=CLICK
// 2=DOUBLE 3=LONG)。本模块只关心 CLICK 与 OK 双击。
void jianlu_nav_init(jianlu_nav_t *nav);
jianlu_nav_result_t jianlu_nav_key(jianlu_nav_t *nav, int btn, int ev);

// 主页菜单焦点 → 目标页面(主页三格 = 简录/二维码/设置)
jianlu_page_t jianlu_nav_home_target(int focus);

#define JIANLU_HOME_ITEMS     3
#define JIANLU_SETTINGS_ITEMS 6
#define JIANLU_SETTINGS_ROW_BRIGHTNESS 0   // 屏幕亮度(OK 循环调档)
#define JIANLU_SETTINGS_ROW_THEME     1   // 色彩主题(OK 循环切换)
#define JIANLU_SETTINGS_ROW_KEEPON    2   // 屏幕常亮(OK 开关)
#define JIANLU_SETTINGS_ROW_SYNC      3   // 立即同步
#define JIANLU_SETTINGS_ROW_REPROV    4   // 重新配网
#define JIANLU_SETTINGS_ROW_ABOUT     5   // 关于(最后一项)
