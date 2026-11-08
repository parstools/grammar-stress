#!/usr/bin/env python3
"""Group syntax errors by the words around them, split into headers and own code.

usage: report_errors.py RESULTS.TSV ROOT
"""
import collections
import os
import re
import sys

results, root = sys.argv[1:3]
MARKER = re.compile(r'# (\d+) "([^"]*)"((?: \d)*)')
rows = [line.rstrip("\n").split("\t") for line in open(results, errors="replace")][1:]
categories = collections.Counter(row[1] for row in rows)
print(dict(categories))
per_std = collections.Counter()
for row in rows:
    per_std[(row[0].split("/")[0], row[1])] += 1
for std in sorted({row[0].split("/")[0] for row in rows}):
    total = sum(v for (s, _), v in per_std.items() if s == std)
    print(f"{std:8} total={total:6} parsed={per_std[(std, 'parsed')]:6} rejected={per_std[(std, 'rejected')]:6}")
where = collections.Counter()
own_words = collections.Counter()
system_words = collections.Counter()
samples = {}
for row in rows:
    if row[1] != "rejected" or not row[2]:
        continue
    lines = open(os.path.join(root, row[0]), errors="replace").read().split("\n")
    number = int(row[2]) - 1
    depth = 0
    for line in lines[:number]:
        match = MARKER.match(line)
        if match:
            flags = match.group(3).split()
            if "1" in flags and "3" in flags:
                depth += 1
            elif "2" in flags and depth > 0:
                depth -= 1
    kind = "system" if depth else "own"
    where[kind] += 1
    line = lines[number]
    column = int(row[3]) - 1
    before = re.findall(r"[A-Za-z_]\w*|\S", line[:column])[-1:] or [""]
    key = (before[0] if before[0].startswith("__") or before[0] in ("asm", "inline", "typeof", "restrict") else "") or row[4]
    (system_words if depth else own_words)[key] += 1
    samples.setdefault(key, f"{row[0][:42]}: {line.strip()[:95]}")
print(dict(where))
print("system headers:", system_words.most_common(18))
print("own code:", own_words.most_common(34))
for key, _ in (system_words + own_words).most_common(26):
    print(f"[{key}] {samples[key]}")
