// main/jianlu_ui.h —— 小诺简录 LVGL 界面(自设计,与基线测试菜单无关)。
//
// 布局(240x320,圆角遮罩半径 30,内容避开四角):
//   顶栏:左"小诺简录",右电量%(bsp_battery_soc(),-1 时隐藏)
//   堆叠卡片:顶卡(徽标+标题+摘要+标签/日期)全尺寸展示,后方两张卡
//     依次缩小变暗、向下偏移 10px 露头,形成"一叠卡片"的纵深
//   底栏:按键提示
//   状态层:连接中/加载中/出错/空清单时替代卡片堆
//   语音层:录音/上传/确认/失败的全屏覆盖层
//
// 性能取舍:Web 版的 ±1.5° 旋转与缩放依赖 LVGL 软件变换,每次重绘都要
// 全卡仿射采样,在无 PSRAM 的 C3 上代价高;改用静态几何(偏移/缩小/变暗)
// 表达纵深,切换时只对飞出顶卡做位移+透明度过渡。
//
// 线程约定:所有函数必须在 LVGL 任务内、或持有 bsp_lvgl_lock() 时调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "jianlu_home.h"
#include "jianlu_json.h"
#include "jianlu_store.h"

// 页面(v2 导航模型)
typedef enum {
    JIANLU_UI_HOME = 0,   // 主页(开机默认)
    JIANLU_UI_JIANLU,     // 简录卡片堆
    JIANLU_UI_QR,         // 二维码
    JIANLU_UI_SETTINGS,   // 设置
} jianlu_ui_page_t;

typedef enum {
    JIANLU_ANIM_NONE = 0,   // 直接重排(状态页切换、首次加载)
    JIANLU_ANIM_NEXT,       // 顶卡向下一张切换(向上飞出)
    JIANLU_ANIM_PREV,       // 向上一张(顶卡直接换,飞出方向不区分)
    JIANLU_ANIM_COMPLETE,   // 完成:顶卡向右划出
    JIANLU_ANIM_ARRIVE,     // 新简录:顶卡从上方滑入+淡入
} jianlu_anim_t;

// 建屏并加载。LVGL 已初始化后调用一次。
void jianlu_ui_create(void);

// 按 store 当前内容刷新卡片堆/状态层。anim 见上;动画进行中再来刷新会
// 取消动画直接落终态(简单可靠优先)。
void jianlu_ui_refresh(const jianlu_store_t *store, jianlu_anim_t anim);

// ---- 语音层(覆盖在卡片堆之上;IDLE 时隐藏)----
void jianlu_ui_voice_idle(void);
void jianlu_ui_voice_recording(int elapsed_sec);
void jianlu_ui_voice_sending(void);
// 确认页呈现:new_count>0 以新卡(徽标+标题)为主,否则以 reply 为主;
// 完整内容(transcript+reply)一页放不下时进入分页查看模式。
// 返回 true = 分页模式,调用方应禁用自动返回计时(用户翻页读完,OK 返回)。
bool jianlu_ui_confirm_present(const jianlu_capture_result_t *res);
void jianlu_ui_voice_error(void);

// ---- 分页文本查看器(通用组件;后续"简录详情页"复用)----
// open 时 text 会被清洗并拷入内部缓冲(调用方无需保活)。
// UP/DOWN 翻页(边界不循环),页码指示 "当前/总页";OK 的语义由调用方定
// (通常 jianlu_ui_pager_close 后执行返回逻辑)。
void jianlu_ui_pager_open(const char *title, const char *text);
bool jianlu_ui_pager_is_open(void);
void jianlu_ui_pager_close(void);
void jianlu_ui_pager_next(void);
void jianlu_ui_pager_prev(void);

// 录音电平输入(0..100),驱动声波动画。只写 volatile,
// 可在任意任务上下文调用(录音任务每块调一次)。
void jianlu_ui_voice_set_level(uint8_t level);

// 勾选成功的对勾浮层(lv_line 画,不占字形),800ms 自动消失,不拦截按键。
// 须在 LVGL 上下文调用。
void jianlu_ui_success_flash(void);

// ---- 配网引导信息(PROVISIONING 视图的状态文案;每次进入/变化时调用)----
// dev_name 传 NULL 表示尚未生成;phone_connected 表示手机已连上 BLE。
void jianlu_ui_provision_info(const char *dev_name, bool phone_connected);

// ---- 通用覆盖层(重配确认等二次确认场景;与语音层共用一块面板)----
void jianlu_ui_overlay(const char *title, uint32_t color_hex,
                       const char *body, const char *hint);
void jianlu_ui_overlay_hide(void);
// 短暂提示浮层(1.5s 自动消失,不拦截按键),如"队列已满"。
void jianlu_ui_overlay_flash(const char *title, uint32_t color_hex, const char *body);

// 「关于」覆盖层:版本文字 + 本地 lv_qrcode 生成的作者主页二维码。
void jianlu_ui_about(const char *version_text);

// ---- 页面(v2)----
// 切换页面:主页/简录/二维码/设置。简录页内容由 jianlu_ui_refresh 刷新;
// 其余页内容用各自的 set 函数。离线标识/电量为全局顶栏。
void jianlu_ui_show_page(jianlu_ui_page_t page);

// 主页:资料(昵称/签名/有无头像)+ 视图模型(时间/离线/待同步/近期待办)
// + 菜单焦点。prof 可为 NULL(未拉到资料),avatar_ok=头像缓存可用。
void jianlu_ui_home_set(const jianlu_profile_t *prof,
                        const jianlu_home_model_t *model, bool avatar_ok);
void jianlu_ui_home_focus(int focus);   // 0 简录 1 二维码 2 设置

// 二维码页:available=有缓存图可显示,否则显示引导文案
void jianlu_ui_qr_set(bool available);

// 设置页:焦点行 + 亮度百分比 + 常亮开关
void jianlu_ui_settings_set(int focus, int brightness_pct, bool keep_on);
