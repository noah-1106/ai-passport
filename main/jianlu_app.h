// main/jianlu_app.h —— 小诺简录应用胶水层:按键事件、Wi-Fi、网络工作任务与 UI 的串联。
//
// 线程模型(遵守仓库运行时不变量):
//   按键回调(esp_timer 任务)  → 只 xQueueSend,不碰 LVGL、不阻塞
//   Wi-Fi 事件回调(系统事件任务) → 只 xQueueSend
//   应用任务(jianlu)          → 改 store、持 bsp_lvgl_lock 刷 UI、向工作任务派单
//   网络任务(jianlu_net)      → 唯一执行阻塞 HTTP 的地方,完成后回投结果事件
#pragma once

// 由 app_main 调用:创建内部队列/任务并连接 Wi-Fi(凭据为空时进入未配置态)。
// 显示与 LVGL 必须已就绪(jianlu_ui_create 之前或之后调用均可)。
void jianlu_app_start(void);
