#!/usr/bin/env python3
"""Generate 6 uncompressed 4-bpp BFF font C arrays for LiangziUi via lv_font_conv,
and validate 100% UI CJK glyph coverage against LiangziUi.cpp."""
from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT_HEADER = ROOT / "lib" / "LiangziUi" / "src" / "LiangziFonts.h"
UI_CPP = ROOT / "lib" / "LiangziUi" / "src" / "LiangziUi.cpp"

HERO_CHARS = "梁文峰谷"

# Complete UI Chinese + symbol charset used across Portrait & Landscape pages
UI_CN_CHARS = (
    "一二三四五六日周"
    "梁文峰谷"
    "当前高峰低谷周末假日全天法定休息"
    "时间未同步等待剩余待机"
    "可用余额旧新增减赠充优先充值"
    "告警阈值正常低于停用持平水位于"
    "资金构成消耗趋势采样次监控轮询第等待"
    "今日排班工作日节假自定义官方默认"
    "连接配置打开上位机桌面端按查看系统诊断未配置出厂"
    "开启关闭持续扣费失联断线连接中无拆分字段"
    "方向自动旋屏已开固定至请"
    "刷新页前秒分时状态未知版本固件"
    "¥°·…"
)

CJK_FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyhbd.ttc",
    r"C:\Windows\Fonts\Dengb.ttf",
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\Deng.ttf",
    r"C:\Windows\Fonts\simhei.ttf",
]

FONTS = [
    {
        "name": "kFontHero36",
        "candidates": CJK_FONT_CANDIDATES,
        "size": 36,
        "range": None,
        "symbols": HERO_CHARS,
        "required": HERO_CHARS,
    },
    {
        "name": "kFontHero30",
        "candidates": CJK_FONT_CANDIDATES,
        "size": 30,
        "range": None,
        "symbols": HERO_CHARS,
        "required": HERO_CHARS,
    },
    {
        "name": "kFontCn12",
        "candidates": CJK_FONT_CANDIDATES,
        "size": 12,
        "range": "0x20-0x7E",
        "symbols": "".join(sorted(set(UI_CN_CHARS))),
        "required": "".join(chr(c) for c in range(0x21, 0x7F)) + UI_CN_CHARS,
    },
    {
        "name": "kFontMeta10",
        "candidates": [r"C:\Windows\Fonts\segoeuib.ttf", r"C:\Windows\Fonts\arialbd.ttf"],
        "size": 10,
        "range": "0x20-0x7E,0xB0,0xA5,0xB7",
        "symbols": None,
        "required": "".join(chr(c) for c in range(0x21, 0x7F)) + "¥°·",
    },
    {
        "name": "kFontNum16",
        "candidates": [r"C:\Windows\Fonts\consolab.ttf"],
        "size": 16,
        "range": None,
        "symbols": "0123456789.,:-+$¥%/ hms",
        "required": "0123456789.,:-+$¥%/hms",
    },
    {
        "name": "kFontNum22",
        "candidates": [r"C:\Windows\Fonts\consolab.ttf"],
        "size": 22,
        "range": None,
        "symbols": "0123456789.,:-+$¥ ",
        "required": "0123456789.,:-+$¥",
    },
]


def _read_u16(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 2], "little")


def _read_u32(buf: bytes, off: int) -> int:
    return int.from_bytes(buf[off : off + 4], "little")


def _map_codepoint_m5gfx(subs: list[tuple[int, int, int, int, int, int]], cdata: bytes, cp: int) -> int:
    """Exact replica of M5GFX 0.2.26 BFFfont::mapCodepointToGlyph (lgfx_fonts.cpp:1392-1493)."""
    l, r = 0, len(subs) - 1
    st = None
    while l <= r:
        m = (l + r) >> 1
        doff, rstart, rlen, goff, ecnt, fmt = subs[m]
        if cp < rstart:
            r = m - 1
        elif cp >= rstart + rlen:
            l = m + 1
        else:
            st = subs[m]
            break
    if st is None:
        return 0
    doff, rstart, rlen, goff, ecnt, fmt = st
    idx = cp - rstart
    if fmt == 0:
        if doff == 0 or doff + idx >= len(cdata):
            return 0
        gid = goff + cdata[doff + idx]
        return gid
    if fmt == 1:
        if doff == 0 or ecnt == 0:
            return 0
        cp_off = doff
        gid_off = cp_off + ecnt * 2
        if gid_off + ecnt * 2 > len(cdata):
            return 0
        lo, hi = 0, ecnt - 1
        while lo <= hi:
            mid = (lo + hi) >> 1
            delta = _read_u16(cdata, cp_off + mid * 2)
            if idx < delta:
                hi = mid - 1
            elif idx > delta:
                lo = mid + 1
            else:
                return goff + _read_u16(cdata, gid_off + mid * 2)
        return 0
    if fmt == 2:
        return goff + idx
    if fmt == 3:
        if doff == 0 or ecnt == 0 or doff + ecnt * 2 > len(cdata):
            return 0
        lo, hi = 0, ecnt - 1
        while lo <= hi:
            mid = (lo + hi) >> 1
            delta = _read_u16(cdata, doff + mid * 2)
            if idx < delta:
                hi = mid - 1
            elif idx > delta:
                lo = mid + 1
            else:
                return goff + mid
        return 0
    return 0


def normalize_and_verify_bff_for_m5gfx(raw: bytes, spec: dict) -> bytes:
    """Fix lv_font_conv record-relative cmap offsets so M5GFX 0.2.26 ties don't shift lookups by +8."""
    import struct

    buf = bytearray(raw)
    pos = 0
    cmap_pos = cmap_sz = loca_entries = 0
    while pos + 8 <= len(buf):
        sz = _read_u32(buf, pos)
        tag = bytes(buf[pos + 4 : pos + 8])
        if tag == b"cmap":
            cmap_pos, cmap_sz = pos, sz
        elif tag == b"loca":
            loca_entries = _read_u32(buf, pos + 8)
        if sz == 0:
            break
        pos += sz

    if cmap_sz < 12 or loca_entries < 2:
        raise RuntimeError(f"Missing cmap/loca table in {spec['name']}")

    csize = cmap_sz - 8
    cnt = _read_u32(buf, cmap_pos + 8)
    headers_end = 4 + cnt * 16
    subs = []
    for i in range(cnt):
        off = cmap_pos + 12 + i * 16
        doff, rstart, rlen, goff, ecnt, fmt = struct.unpack_from("<IIHHHBx", buf, off)
        subs.append([doff, rstart, rlen, goff, ecnt, fmt])

    # Normalize record-relative lv_font_conv offsets (>= headers_end + 8) to payload-relative.
    needs_norm = any(s[0] >= headers_end + 8 for s in subs)
    if needs_norm:
        for i, s in enumerate(subs):
            if s[5] == 2:
                # format 0 tiny uses no external data array; 0 ties both pv and rv cleanly
                s[0] = 0
            elif s[0] >= 8:
                s[0] -= 8
            struct.pack_into("<I", buf, cmap_pos + 12 + i * 16, s[0])

    # Verify M5GFX 0.2.26 heuristic won't double-shift
    pv = rv = 0
    for s in subs:
        doff = s[0]
        if doff == 0:
            pv += 1
            rv += 1
        else:
            if doff < csize:
                pv += 1
            if doff >= 8 and (doff - 8) < csize:
                rv += 1
    if rv > pv:
        raise RuntimeError(f"M5GFX unintended shift triggered for {spec['name']}: pv={pv}, rv={rv}")

    cdata = bytes(buf[cmap_pos + 8 : cmap_pos + cmap_sz])
    sub_tuples = [tuple(s) for s in subs]
    missing = []
    for ch in dict.fromkeys(spec["required"]):
        gid = _map_codepoint_m5gfx(sub_tuples, cdata, ord(ch))
        if gid <= 0 or gid >= loca_entries:
            missing.append(f"U+{ord(ch):04X}'{ch}'(gid={gid})")
    if missing:
        raise RuntimeError(f"M5GFX glyph lookup failed in {spec['name']}: {', '.join(missing[:12])}")
    return bytes(buf)


def run_lv_font_conv(spec: dict, ttf_path: str, out_bin: Path) -> bytes:
    cmd = [
        "npx.cmd" if os.name == "nt" else "npx",
        "--yes",
        "lv_font_conv",
        "--font", ttf_path,
        "--size", str(spec["size"]),
        "--bpp", "4",
        "--format", "bin",
        "--no-compress",
        "--no-kerning",
    ]
    if spec["range"]:
        cmd += ["--range", spec["range"]]
    if spec["symbols"]:
        cmd += ["--symbols", spec["symbols"]]
    cmd += ["-o", str(out_bin)]
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    if res.returncode != 0:
        raise RuntimeError(f"{res.stdout}\n{res.stderr}".strip())
    data = out_bin.read_bytes()
    if len(data) < 16 or data[4:8] != b"head":
        raise RuntimeError(f"Invalid BFF header for {spec['name']} (bytes[4:8]={data[4:8]!r})")
    if len(data) < 80:
        raise RuntimeError(f"Suspiciously small BFF payload ({len(data)} bytes) for {spec['name']}")
    return normalize_and_verify_bff_for_m5gfx(data, spec)


def build_font_blob(spec: dict, out_bin: Path) -> tuple[bytes, str]:
    errors = []
    for candidate in spec["candidates"]:
        if not Path(candidate).exists():
            continue
        try:
            data = run_lv_font_conv(spec, candidate, out_bin)
            return data, Path(candidate).name
        except Exception as exc:  # noqa: BLE001
            errors.append(f"{Path(candidate).name}: {exc}")
    raise RuntimeError(f"All font candidates failed for {spec['name']}:\n" + "\n".join(errors))


def strip_cpp_comments(text: str) -> str:
    text = re.sub(r"//.*?$", "", text, flags=re.MULTILINE)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return text


def check_ui_coverage() -> list[str]:
    if not UI_CPP.exists():
        return []
    src = strip_cpp_comments(UI_CPP.read_text(encoding="utf-8"))
    literals = re.findall(r'"((?:\\.|[^"\\])*)"', src)
    covered = set(UI_CN_CHARS) | set(HERO_CHARS)
    missing = set()
    for lit in literals:
        for ch in lit:
            if ord(ch) > 0x7E:
                if ch not in covered:
                    missing.add(ch)
    return sorted(missing)


def format_array(name: str, data: bytes) -> str:
    lines = [
        f"constexpr size_t {name}Size = {len(data)};",
        f"constexpr uint8_t {name}[{len(data)}] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02X}" for b in data[i : i + 16])
        comma = "," if i + 16 < len(data) else ""
        lines.append(f"  {chunk}{comma}")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    missing = check_ui_coverage()
    if missing:
        print(f"ERROR: LiangziUi.cpp contains UI characters missing from UI_CN_CHARS: {''.join(missing)}", file=sys.stderr)
        for ch in missing:
            print(f"  missing U+{ord(ch):04X} '{ch}'", file=sys.stderr)
        return 2
    if UI_CPP.exists():
        print("Coverage check PASS: 100% of non-ASCII UI literals in LiangziUi.cpp are covered.")
    if "--check-only" in sys.argv:
        return 0

    OUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    parts = [
        "// Auto-generated by tools/gen_fonts.py via lv_font_conv (4-bpp uncompressed BFF for M5GFX ft_lvgl).",
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace liangzi::fonts {",
        "",
    ]
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        for spec in FONTS:
            data, used_font = build_font_blob(spec, tmp_dir / f"{spec['name']}.bin")
            total += len(data)
            print(f"{spec['name']}: {len(data)} bytes (font={used_font}, size={spec['size']})")
            parts.append(format_array(spec["name"], data))
            parts.append("")
    parts.append("}  // namespace liangzi::fonts")
    parts.append("")
    OUT_HEADER.write_text("\n".join(parts), encoding="utf-8", newline="\n")
    print(f"Wrote {OUT_HEADER} (total font payload: {total} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
