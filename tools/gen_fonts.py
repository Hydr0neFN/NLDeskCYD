#!/usr/bin/env python3
r"""Regenerate the Noto Sans TC subsets (src/font_noto_16.c, src/font_noto_20.c).

Collects every non-ASCII character that appears in src/*.cpp (except the font
files themselves), so a new label can never ship with a missing glyph: run
this after adding or changing any CJK text.

    python tools/gen_fonts.py

Needs node (npx lv_font_conv) and C:\Windows\Fonts\NotoSansTC-VF.ttf.
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONT = r"C:\Windows\Fonts\NotoSansTC-VF.ttf"

chars = set()
for f in (ROOT / "src").glob("*.cpp"):
    chars |= {c for c in f.read_text(encoding="utf-8") if ord(c) > 0x7E}
# LVGL symbol glyphs come from Montserrat, never from this font; box-drawing
# characters only appear in comments.
chars = "".join(sorted(c for c in chars if not 0xF000 <= ord(c) <= 0xF8FF and not 0x2500 <= ord(c) <= 0x257F))
print(f"{len(chars)} glyphs: {chars}")

for size in (16, 20):
    out = ROOT / "src" / f"font_noto_{size}.c"
    subprocess.run(
        ["npx", "-y", "lv_font_conv", "--font", FONT, "--size", str(size), "--bpp", "4",
         "--format", "lvgl", "--range", "0x20-0x7E", "--symbols", chars,
         "--lv-include", "lvgl.h", "-o", str(out)],
        check=True, shell=sys.platform == "win32",
    )
    print("wrote", out.name)
