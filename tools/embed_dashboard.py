"""Embed a dashboard dist/ folder into a gzipped PROGMEM header.

Usage: python tools/embed_dashboard.py <dist-dir> <output-header>
Example: python tools/embed_dashboard.py esp32/dashboard/dist esp32/UnoReceiver/DashboardAssets.h
"""
import gzip
import os
import sys

ASSETS = [
    ("dashboardHtml", "index.html"),
    ("dashboardCss", "style.css"),
    ("dashboardJs", "app.js"),
    ("dashboardReport", "report.js"),
]


def c_array(name, data):
    lines = [",".join(f"0x{b:02x}" for b in data[i:i + 20]) for i in range(0, len(data), 20)]
    return f"const uint8_t {name}[] PROGMEM = {{\n" + ",\n".join(lines) + "\n};\n"


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    dist, out = sys.argv[1], sys.argv[2]
    parts = ["#pragma once\n#include <Arduino.h>\n"]
    for name, filename in ASSETS:
        with open(os.path.join(dist, filename), "rb") as f:
            source = f.read()
        # mtime=0 keeps the output identical for identical sources.
        data = gzip.compress(source, compresslevel=9, mtime=0)
        parts.append(c_array(name, data))
        print(f"{filename}: {len(source)} -> {len(data)} bytes gzipped")
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(parts))


if __name__ == "__main__":
    main()
