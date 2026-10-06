// main/jianlu_shot.h —— FAP_SCREENSHOT_V1 串口截屏协议(社区标准,只读)。
//
// 主机经 USB-CDC 发 ASCII 行 "FAP_SCREENSHOT_V1\n",设备回
// "FAP_SCREENSHOT_V1 <w> <h> RGB565LE <bytes>\n" + 精确 <bytes> 个
// 小端 RGB565 像素。实现按 docs/reference/y2lin/serial-screenshot-protocol.md
// 的踩坑清单逐条执行(驱动安装/退避/子串匹配/静态缓冲/分块/日志静音)。
#pragma once

// 启动截屏服务任务(幂等)。UI/LVGL 就绪后调用一次。
void jianlu_shot_start(void);
