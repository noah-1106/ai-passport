// main/jianlu_pager.c —— 见 jianlu_pager.h。
#include "jianlu_pager.h"

#include <string.h>

// UTF-8 解码下一个码点并前进指针(容错:非法字节按 1 字节前进)
static uint32_t utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)(*p);
    uint32_t cp;
    int extra;
    if (s[0] < 0x80) {
        cp = s[0];
        extra = 0;
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = s[0] & 0x1F; extra = 1;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = s[0] & 0x0F; extra = 2;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = s[0] & 0x07; extra = 3;
    } else {
        (*p)++;
        return 0xFFFD;
    }
    for (int i = 1; i <= extra; i++) {
        if ((s[i] & 0xC0) != 0x80) { extra = i - 1; break; }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    *p += extra + 1;
    return cp;
}

static int char_units(uint32_t cp)
{
    return cp < 0x80 ? 1 : 2;
}

// 从 offset 开始切 rows 行,返回这些行占用的字节数(不超过文本末尾)。
// 规则:遇 '\n' 换行;行内单元超 cols 时在该字符之前折行——引发折行的
// 字符属于下一页,不计入本页(不在多字节字符中间切)。
static size_t consume_lines(const char *text, size_t offset, int cols, int rows)
{
    size_t pos = offset;
    int line = 0;
    int units = 0;
    while (line < rows && text[pos] != '\0') {
        const char *before = text + pos;
        uint32_t cp = utf8_next(&before);
        if (cp == '\n') {
            line++;
            units = 0;
            pos = (size_t)(before - text);
            continue;
        }
        int u = char_units(cp);
        if (units + u > cols) {
            line++;
            if (line >= rows) break;   // 引发折行的字符归下一页
            units = u;
            if (units > cols) units = cols;   // 单字符超宽:强行占满,防死循环
        } else {
            units += u;
        }
        pos = (size_t)(before - text);
    }
    return pos - offset;
}

int jianlu_pager_count_pages(const char *text, int cols, int rows)
{
    if (text == NULL || text[0] == '\0' || cols <= 0 || rows <= 0) return 0;
    int pages = 0;
    size_t offset = 0;
    size_t len = strlen(text);
    while (offset < len && pages < JIANLU_PAGER_MAX_PAGES) {
        pages++;
        offset += consume_lines(text, offset, cols, rows);
    }
    // 超出 MAX_PAGES 的内容并入最后一页(调用方决定如何提示)
    return pages;
}

void jianlu_pager_init(jianlu_pager_t *p, const char *text, int cols, int rows)
{
    memset(p, 0, sizeof(*p));
    if (text == NULL || text[0] == '\0' || cols <= 0 || rows <= 0) return;
    p->text = text;
    size_t offset = 0;
    size_t len = strlen(text);
    while (offset < len && p->pages < JIANLU_PAGER_MAX_PAGES) {
        p->page_start[p->pages] = offset;
        p->pages++;
        offset += consume_lines(text, offset, cols, rows);
    }
}

bool jianlu_pager_next(jianlu_pager_t *p)
{
    if (p->page + 1 >= p->pages) return false;
    p->page++;
    return true;
}

bool jianlu_pager_prev(jianlu_pager_t *p)
{
    if (p->page <= 0) return false;
    p->page--;
    return true;
}

size_t jianlu_pager_page_text(const jianlu_pager_t *p, char *buf, size_t cap)
{
    if (cap == 0) return 0;
    buf[0] = '\0';
    if (p->pages == 0 || p->page < 0 || p->page >= p->pages) return 0;
    size_t start = p->page_start[p->page];
    size_t end;
    if (p->page + 1 < p->pages) {
        end = p->page_start[p->page + 1];
    } else {
        end = strlen(p->text);
    }
    size_t n = end - start;
    if (n >= cap) n = cap - 1;
    memcpy(buf, p->text + start, n);
    buf[n] = '\0';
    return n;
}
