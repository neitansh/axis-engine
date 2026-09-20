#!/usr/bin/env python3
"""Хук Claude Code: сборка движка только пресетом dev.

`make`, `ninja` и `cmake --build <каталог>` без предела параллельности
запускают сотни компиляций разом и съедают память (20.09.2026 — дважды).
Пропускается только `cmake --build --preset ...` и `cmake --preset ...`.
"""
import json
import re
import sys

command = json.load(sys.stdin).get("tool_input", {}).get("command", "")
starts = r"(?:^|[;&|(]\s*)"
forbidden = [
    re.compile(starts + r"(?:sudo\s+)?(?:make|ninja|gmake)\b"),
    re.compile(starts + r"cmake\s+--build\s+(?!--preset\b)"),
]
if any(p.search(command) for p in forbidden):
    sys.stderr.write("Сборка только пресетом: cmake --build --preset dev "
                     "(Ninja, пулы compile=8). См. память build-with-job-limit.\n")
    sys.exit(2)
