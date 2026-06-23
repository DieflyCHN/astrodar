#!/usr/bin/env python3
"""Convert the official ELP82B text tables into a C++ embedded resource."""

from pathlib import Path
import sys


def main() -> None:
    source = Path(sys.argv[1])
    output = Path(sys.argv[2])
    parts = [
        '#include "elp82b_embedded.hpp"\n\n',
        '#include <string>\n\n',
        'std::string_view embeddedELP82BFile(int fileNumber) {\n',
        '    switch (fileNumber) {\n',
    ]
    for number in range(1, 37):
        text = (source / f"ELP{number}.txt").read_text(encoding="ascii")
        chunks = []
        while text:
            end = min(len(text), 60000)
            if end < len(text):
                end = text.rfind("\n", 0, end) + 1
            chunks.append(text[:end])
            text = text[end:]
        parts.append(f"    case {number}: {{\n")
        parts.append("        static const std::string value =\n")
        for index, chunk in enumerate(chunks):
            delimiter = f"ELP{number}_{index}"
            if f"){delimiter}\"" in chunk:
                raise RuntimeError(f"raw-string delimiter collision in ELP{number}")
            prefix = "std::string(" if index == 0 else " + "
            suffix = ")" if index == 0 else ""
            parts.append(f'        {prefix}R"{delimiter}({chunk}){delimiter}"{suffix}\n')
        parts.append("        ;\n        return value;\n    }\n")
    parts += ['    default: return {};\n', '    }\n', '}\n']
    output.write_text("".join(parts), encoding="utf-8")


if __name__ == "__main__":
    main()
