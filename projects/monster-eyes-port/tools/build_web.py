"""Inline web/ into a single gzipped PROGMEM page header.

Runs as a PlatformIO `pre:` script and as a standalone `python` invocation.
The device serves one document because its web server is polled from the
render loop; separate .css/.js requests would stall the eye animation.
"""
from pathlib import Path
import gzip
import re

if "__file__" in globals():
    ROOT = Path(__file__).resolve().parents[1]
else:
    Import("env")  # noqa: F821  (injected by PlatformIO)
    ROOT = Path(env["PROJECT_DIR"])  # noqa: F821
WEB = ROOT / "web"
OUT = ROOT / "generated"

LINK = re.compile(r'[ \t]*<link rel=stylesheet href=style\.css>\n')
SCRIPT = re.compile(r'[ \t]*<script src=app\.js></script>\n')


def inline(html, pattern, replacement, what):
    html, count = pattern.subn(lambda _: replacement, html)
    if count != 1:
        raise ValueError(f"index.html must reference {what} exactly once, found {count}")
    return html


def collapse(text):
    """Strip indentation and blank lines. No minification: gzip does that work,
    and a hand-rolled minifier would eventually break a template literal."""
    lines = (line.rstrip() for line in text.splitlines())
    return "\n".join(line.lstrip() for line in lines if line.strip())


html = (WEB / "index.html").read_text()
html = inline(html, LINK, "<style>\n" + collapse((WEB / "style.css").read_text()) + "\n</style>\n", "style.css")
html = inline(html, SCRIPT, "<script>\n" + collapse((WEB / "app.js").read_text()) + "\n</script>\n", "app.js")
html = collapse(html) + "\n"

raw = html.encode()
packed = gzip.compress(raw, 9, mtime=0)
rows = "\n".join(
    "  " + ",".join(f"0x{b:02x}" for b in packed[i:i + 16]) + ","
    for i in range(0, len(packed), 16)
)

OUT.mkdir(exist_ok=True)
(OUT / "web_page.h").write_text(
    "// Generated from web/ by tools/build_web.py. DO NOT EDIT.\n"
    "#pragma once\n"
    "#include <Arduino.h>\n"
    "const uint8_t WEB_PAGE_GZ[] PROGMEM = {\n"
    f"{rows}\n"
    "};\n"
    f"const size_t WEB_PAGE_GZ_LEN = {len(packed)};\n"
)
print(f"Generated web_page.h: {len(raw)} bytes inlined, {len(packed)} bytes gzipped")
