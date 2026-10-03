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

#include "jianlu_store.h"

typedef enum {
    JIANLU_ANIM_NONE = 0,   // 直接重排(状态页切换、首次加载)
    JIANLU_ANIM_NEXT,       // 顶卡向下一张切换(向上飞出)
    JIANLU_ANIM_PREV,       // 向上一张(顶卡直接换,飞出方向不区分)
    JIANLU_ANIM_COMPLETE,   // 完成:顶卡向右划出
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
void jianlu_ui_voice_confirm(const char *transcript, const char *reply, int new_count);
void jianlu_ui_voice_error(void);
