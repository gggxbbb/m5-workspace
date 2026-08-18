#!/usr/bin/env python3
"""生成"梁文峰/梁文谷"稀疏前景像素表（drawPixel 渲染用）。

用法:
    python tools/gen_peak_font.py [输出头文件路径]

原理:
    - 微软雅黑 (msyh.ttc) 2x 超采样渲染，anchor='mm' 居中 + textbbox 裁剪，
      保证字形完整且垂直居中（修复旧版 text((0,0)) 导致只显示上半截）。
    - 输出稀疏像素表：仅非背景像素 (x, y, rgb565)，固件 drawPixel 循环渲染。
    - 优势：绕开 pushImage 的 uint16_t 字节序问题（ESP32 小端 vs ST7789 大端）；
      背景由 canvas.fillScreen 统一管，字形像素直接覆盖。
    - 抗锯齿像素是字形色与 BG(8,12,16) 的混合，只要 canvas 背景仍是 CLR_BG 就匹配。

改字号/颜色/文案后重跑本脚本即可。
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "C:/Windows/Fonts/msyh.ttc"  # 微软雅黑
CELL = 36          # 显示字号（像素，高度）
SS = 2             # 超采样倍数
BG = (10, 12, 16)  # 屏幕背景 CLR_BG（必须与固件 CLR_BG 一致）

# 文案 -> (目标色 RGB, 变量名)
GLYPHS = {
    "梁文峰": ((255, 200, 50), "PEAK_WENFENG"),  # 高峰期：黄
    "梁文谷": ((64, 208, 120), "PEAK_WENGU"),    # 非高峰期：绿
}


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def render_pixels(text: str, fg: tuple[int, int, int]) -> tuple[list[tuple[int, int, int]], int, int]:
    """渲染文案，返回 (前景像素列表 [(x, y, rgb565)], 宽, 高)。"""
    font = ImageFont.truetype(FONT_PATH, CELL * SS)

    # 大画布渲染，用 anchor='mm' 让字形 metric 中心对齐画布中心
    tmp_w = len(text) * CELL * SS * 2
    tmp_h = CELL * SS * 3
    cx, cy = tmp_w // 2, tmp_h // 2
    tmp = Image.new("RGB", (tmp_w, tmp_h), BG)
    d = ImageDraw.Draw(tmp)
    d.text((cx, cy), text, font=font, fill=fg, anchor="mm")

    # textbbox 拿字形实际范围（含抗锯齿），裁掉空白
    bbox = d.textbbox((cx, cy), text, font=font, anchor="mm")
    glyph = tmp.crop(bbox)
    gw, gh = glyph.size  # 超采样尺寸

    # 等比缩放到 CELL 高，宽度按比例
    target_h = CELL
    target_w = max(1, int(gw * target_h / gh))
    # 宽度限制：不超过 len(text)*CELL（3 字默认 108px）
    cell_w = len(text) * CELL
    if target_w > cell_w:
        target_w = cell_w
        target_h = max(1, int(gh * target_w / gw))
    small = glyph.resize((target_w, target_h), Image.Resampling.LANCZOS)

    # 居中放置到 cell 画布
    final = Image.new("RGB", (cell_w, CELL), BG)
    ox = (cell_w - target_w) // 2
    oy = (CELL - target_h) // 2
    final.paste(small, (ox, oy))

    # 提取前景像素（非 BG）。用 565 量化值比较，避免超采样降采样后边界像素
    # 的 RGB 元组与 BG 微差但量化后恰好等于 BG565 的泄漏。
    bg565 = rgb565(*BG)
    pixels: list[tuple[int, int, int]] = []
    for y in range(CELL):
        for x in range(cell_w):
            c = final.getpixel((x, y))
            c565 = rgb565(*c)
            if c565 != bg565:
                pixels.append((x, y, c565))
    return pixels, cell_w, CELL


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "sticks3" / "peak_font.h"
    out.parent.mkdir(parents=True, exist_ok=True)

    lines: list[str] = []
    lines.append("// 峰谷大字前景像素表（稀疏，仅非背景像素，drawPixel 渲染）")
    lines.append("// 由 tools/gen_peak_font.py 生成 —— 请勿手改，重新生成请运行该脚本")
    lines.append("// 背景=CLR_BG 由 canvas.fillScreen 统一填充；字形像素 drawPixel 覆盖，避开 pushImage 字节序问题")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("struct PeakPixel { uint8_t x; uint8_t y; uint16_t c; };")
    lines.append("")

    for text, (fg, var) in GLYPHS.items():
        pixels, w, h = render_pixels(text, fg)
        lines.append(f"// {text} ({w}x{h}, {len(pixels)} 前景像素)")
        lines.append(f"const struct PeakPixel {var}[] = {{")
        # 每行 12 个像素，紧凑输出
        for i in range(0, len(pixels), 12):
            chunk = pixels[i:i + 12]
            lines.append("  " + "".join(f"{{{x},{y},0x{c:04X}}}," for x, y, c in chunk))
        lines.append("};")
        lines.append(f"const int {var}_N = sizeof({var}) / sizeof(struct PeakPixel);")
        lines.append("")

    lines.append(f"#define PEAK_W {3 * CELL}")
    lines.append(f"#define PEAK_H {CELL}")
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"OK -> {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
