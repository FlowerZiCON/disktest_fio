#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 MOONLIGHT
"""Generate a translation template from src/uitext.def.

Prints one line per translatable entry:

    key<TAB>中文原文

The key is the constant name in src/uitext.def, the source text is the C++ string
literal with its escapes intact (\\n stays \\n).  Feed it to a translator, then save
the result as resources/i18n/<lang>.tsv and run `disktest.exe --i18n-check`.

Usage:
    python tools/make_i18n_template.py > resources/i18n/xx.tsv
"""

import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEF = os.path.join(ROOT, "src", "uitext.def")

ENTRY = re.compile(r"UI_TEXT\((\w+)\s*,")


def read_literal_block(text, i):
    """Read the macro arguments starting at i (just past the first '(').

    Returns (body, next_index).  Stops at the matching ')' of UI_TEXT( ... ),
    skipping string literals so commas/parens inside them do not count.
    """
    depth = 1
    start = i
    while i < len(text):
        c = text[i]
        if c == '"':
            i += 1
            while i < len(text):
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start:i], i + 1
        i += 1
    raise ValueError("unbalanced parentheses")


def literals_of(body):
    """Join the C++ string literals in body, keeping their escape sequences."""
    out = []
    for match in re.finditer(r'"((?:[^"\\]|\\.)*)"', body, re.S):
        out.append(match.group(1))
    return "".join(out)


def main():
    raw = io.open(DEF, encoding="utf-8").read()
    # 注释行先摘掉：文件头的说明里也写着 "UI_TEXT(名字, 中文原文)" 这种例子，
    # 不先摘掉就会把它当成一条真文案。
    text = "\n".join(
        "" if line.lstrip().startswith("//") else line
        for line in raw.splitlines())

    lines = []
    pos = 0
    while True:
        m = ENTRY.search(text, pos)
        if not m:
            break
        body, end = read_literal_block(text, m.end())
        lines.append("%s\t%s" % (m.group(1), literals_of(body)))
        pos = end

    out = []
    out.append("# Generated from src/uitext.def by tools/make_i18n_template.py\n")
    out.append("# key<TAB>中文原文 —— 译完另存为 resources/i18n/<lang>.tsv，"
               "再跑 disktest.exe --i18n-check\n")
    out.extend(line + "\n" for line in lines)
    # 控制台的代码页可能是 GBK：直接写字节，别让 print 去编码。
    sys.stdout.buffer.write("".join(out).encode("utf-8"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
