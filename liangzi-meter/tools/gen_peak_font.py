#!/usr/bin/env python3
"""生成"梁文峰/梁文谷"36px 抗锯齿 RGB565 位图（pushImage 用）。

用法:
    python tools/gen_peak_font.py [输出头文件路径]

原理:
    - 微软雅黑 (msyh.ttc) 2x 超采样渲染 → LANCZOS 降采样到 36px/字
    - 背景 = 屏幕背景色 CLR_BG, 字形 = 峰谷目标色 (黄/绿), 抗锯齿像素自动插值
    - 输出 const uint16_t RGB565 数组, 固件 pushImage 直接推屏

改字号/颜色/文案后重跑本脚本即可, 无需改固件其他部分。
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "C:/Windows/Fonts/msyh.ttc"  # 微软雅黑
CELL = 36          # 显示字号（像素）
SS = 2             # 超采样倍数
BG = (10, 12, 16)  # 屏幕背景 CLR_BG

# 文案 -> (目标色 RGB, 变量名)
GLYPHS = {
    "梁文峰": ((255, 200, 50), "PEAK_WENFENG"),  # 高峰期：黄
    "梁文谷": ((64, 208, 120), "PEAK_WENGU"),    # 非高峰期：绿
}


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def render_rgb565(text: str, fg: tuple[int, int, int]) -> list[int]:
    w = len(text) * CELL
    big = Image.new("RGB", (w * SS, CELL * SS), BG)
    d = ImageDraw.Draw(big)
    font = ImageFont.truetype(FONT_PATH, CELL * SS)
    d.text((0, 0), text, font=font, fill=fg)
    small = big.resize((w, CELL), Image.Resampling.LANCZOS)
    return [rgb565(*small.getpixel((x, y))) for y in range(CELL) for x in range(w)]


def main() -> None:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "sticks3" / "peak_font.h"
    out.parent.mkdir(parents=True, exist_ok=True)

    lines: list[str] = []
    lines.append("// 峰谷大字位图（微软雅黑 2x 超采样抗锯齿渲染，RGB565，背景=CLR_BG）")
    lines.append("// 由 tools/gen_peak_font.py 生成 —— 请勿手改，重新生成请运行该脚本")
    lines.append("")

    for text, (fg, var) in GLYPHS.items():
        data = render_rgb565(text, fg)
        w = len(text) * CELL
        lines.append(f"// {text} ({w}x{CELL})")
        lines.append(f"const uint16_t {var}[{w * CELL}] = {{")
        for i in range(0, len(data), 12):
            lines.append("  " + ",".join(f"0x{v:04X}" for v in data[i:i + 12]) + ",")
        lines.append("};")
        lines.append("")

    lines.append(f"#define PEAK_W {3 * CELL}")
    lines.append(f"#define PEAK_H {CELL}")
    out.write_text("\n".join(lines), encoding="utf-8")
    print(f"OK -> {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
