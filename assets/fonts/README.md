<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Xiaonuo Jianlu Font Assets

## NotoSansCJKsc-Regular.otf

- **Source**: [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk) `Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf`
- **License**: SIL Open Font License 1.1 (OFL), redistributable with the firmware provided the license text is retained: <https://openfontlicense.org>

## jianlu_font_16.c (build artifact, tracked in version control)

16px LVGL application font covering the full GB2312 Hanzi set plus ASCII and
full-width punctuation (`jianlu_charset.txt`, 7046 characters). Used to render
arbitrary Chinese content produced by speech recognition (the built-in
`lv_font_source_han_sans_sc_16_cjk` covers only 1187 characters and is not
sufficient).

Regeneration (converter pinned to `lv_font_conv@1.5.3`, `npm install lv_font_conv@1.5.3`):

```bash
lv_font_conv \
  --font assets/fonts/NotoSansCJKsc-Regular.otf \
  --symbols "$(cat assets/fonts/jianlu_charset.txt)" \
  --size 16 --bpp 4 --format lvgl --no-compress \
  --lv-font-name jianlu_font_16 --lv-include lvgl.h \
  --output assets/fonts/jianlu_font_16.c
```

See the repository history for the `jianlu_charset.txt` generation script
(GB2312 Hanzi blocks 0xB0-0xF7, symbol blocks 0xA1/0xA3, printable ASCII).
