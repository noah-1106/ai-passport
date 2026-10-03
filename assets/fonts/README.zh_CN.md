<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 小诺简录字体资产

## NotoSansCJKsc-Regular.otf

- **来源**：[notofonts/noto-cjk](https://github.com/notofonts/noto-cjk) `Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`
- **许可**：SIL Open Font License 1.1（OFL），可随固件再分发，需保留许可文本：<https://openfontlicense.org>

## jianlu_font_16.c（构建产物，纳入版本管理）

16px LVGL 应用字体，覆盖 GB2312 汉字全集 + ASCII + 全角标点 + ·—（`jianlu_charset.txt`，7048 字），
用于显示语音识别产生的任意中文内容（内置 `lv_font_source_han_sans_sc_16_cjk` 仅 1187 字，不可用）。

复现命令（转换器固定版本 `lv_font_conv@1.5.3`，`npm install lv_font_conv@1.5.3`）：

```bash
lv_font_conv \
  --font assets/fonts/NotoSansCJKsc-Regular.otf \
  --symbols "$(cat assets/fonts/jianlu_charset.txt)" \
  --size 16 --bpp 4 --format lvgl --no-compress \
  --lv-font-name jianlu_font_16 --lv-include lvgl.h \
  --output assets/fonts/jianlu_font_16.c
```

`jianlu_charset.txt` 生成脚本见仓库历史（GB2312 0xB0-0xF7 汉字区 + 0xA1/0xA3 符号区 + 可打印 ASCII）。
