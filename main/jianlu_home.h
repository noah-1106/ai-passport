// main/jianlu_home.h —— 主页视图模型装配(纯 C,host 可测)。
//
// 把 store(清单)+ 待同步数 + 时间装配成主页要画的内容。
// 近期待办 = 清单里第一张 todo 类型且非占位卡的记录。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "jianlu_store.h"

typedef struct {
    char time_hhmm[8];   // "HH:mm" 或 "--:--"(未对时)
    bool offline;        // 离线标识
    int pending_sync;    // 待同步条数(语音队列+离线勾选),0 时隐藏通知条
    bool has_todo;
    char todo_title[JIANLU_TITLE_LEN];
    char todo_date[JIANLU_DATE_LEN];  // 月-日
} jianlu_home_model_t;

// epoch 为 UTC 秒(0/无效 → time_hhmm 显示 "--:--")。
void jianlu_home_build(const jianlu_store_t *store, int pending_sync,
                       uint32_t epoch, jianlu_home_model_t *out);

// 清单里第一张 todo 类型的真实记录(跳过语音占位卡);无则 NULL。
const jianlu_record_t *jianlu_store_first_todo(const jianlu_store_t *store);
