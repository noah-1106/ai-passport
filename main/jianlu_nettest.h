// main/jianlu_nettest.h —— 网络取证自检(取证构建专用,正式版不编入)。
#pragma once

void jianlu_nettest_run(const char *hub_base_url);

// 内部:boot 定时器回调与任务入口(取证构建接线用)
void jianlu_nettest_boot_cb(void *arg);
void jianlu_nettest_task_entry(void *arg);
