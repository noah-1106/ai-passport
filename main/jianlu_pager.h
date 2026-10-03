// main/jianlu_pager.h —— 小屏分页文本查看器核心(纯 C,host 可测)。
//
// 为"确认页长回复"与将来的"简录详情页"设计的可复用组件:
// 按显示单元折行(CJK=2 单元,ASCII=1 单元,近似 16px 字体下 8px/单元),
// 每页 rows 行,记录各页起始字节偏移。UP/DOWN 翻页,边界不循环。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JIANLU_PAGER_MAX_PAGES 8

typedef struct {
    const char *text;                       // 全文(调用方保证生命周期)
    size_t page_start[JIANLU_PAGER_MAX_PAGES]; // 各页起始字节偏移
    int pages;
    int page;                               // 当前页 0..pages-1
} jianlu_pager_t;

// 切页:cols 为每行显示单元数(24 单元≈12 汉字),rows 为每页行数。
// text 为 NULL/空串 → pages=0。
void jianlu_pager_init(jianlu_pager_t *p, const char *text, int cols, int rows);

// 翻页;到顶/到底返回 false(不循环)。
bool jianlu_pager_next(jianlu_pager_t *p);
bool jianlu_pager_prev(jianlu_pager_t *p);

// 当前页文本(不含页码)拷入 buf,返回长度。pages==0 时为空串。
size_t jianlu_pager_page_text(const jianlu_pager_t *p, char *buf, size_t cap);

// 文本在指定 cols/rows 下的总页数(不修改 p)。用于"是否放得下"预判。
int jianlu_pager_count_pages(const char *text, int cols, int rows);
