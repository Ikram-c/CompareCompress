#!/usr/bin/env python3
"""Reports every function body longer than a limit (Power of 10 rule 4).

    python3 tools/check_function_length.py [limit]   # default 60 lines

Scans src/ and tests/. A function is recognised by a line that is exactly `{`
(the project's brace style puts the opening brace of a function on its own
line) preceded by a signature line, and its length is the number of lines
between that brace and the matching `}` at the same indentation. Bodies of
structs, classes, enums and namespaces are skipped. Exit status is 1 when any
function exceeds the limit, so the script can run in CI. clang-tidy's
`readability-function-size` enforces the same limit during an analysed build;
this script needs nothing but Python.
"""
import glob
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_LIMIT = 60
SKIP = re.compile(r"\s*(struct|class|enum|namespace|union)\b")
SIGNATURE_END = re.compile(r"\)\s*(const)?\s*(noexcept|override|final)?\s*$")


def source_files() -> list:
    """Every C++ source and header under src/ and tests/."""
    patterns = ("src/**/*.cpp", "src/**/*.h", "tests/*.cpp", "tests/*.h")
    files = []
    for pattern in patterns:
        files += glob.glob(str(ROOT / pattern), recursive=True)
    return sorted(files)


def is_signature(line: str) -> bool:
    """True when `line` looks like the end of a function signature."""
    return bool(SIGNATURE_END.search(line.strip())) and not SKIP.match(line)


def function_name(lines: list, brace: int) -> str:
    """The signature line (first line above the brace that contains a parenthesis)."""
    k = brace - 1
    while k > 0 and "(" not in lines[k]:
        k -= 1
    return lines[k].strip()[:90]


def closing_brace(lines: list, start: int, indent: int) -> int:
    """Index of the `}` (or `};`) at `indent` that closes the block opened at `start`."""
    close = " " * indent + "}"
    j = start + 1
    while j < len(lines) and lines[j].rstrip() not in (close, close + ";"):
        j += 1
    return j


def long_functions(path: str, limit: int) -> list:
    """Returns (line number, length, signature) for every over-long function in `path`."""
    lines = pathlib.Path(path).read_text(encoding="utf-8").split("\n")
    found = []
    i = 1
    while i < len(lines):
        line = lines[i]
        if line.rstrip() == "{" and is_signature(lines[i - 1]):
            indent = len(line) - len(line.lstrip())
            end = closing_brace(lines, i, indent)
            length = end - i - 1
            if length > limit:
                found.append((i, length, function_name(lines, i)))
            i = end
        i += 1
    return found


def main() -> None:
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_LIMIT
    total = 0
    for path in source_files():
        for line, length, name in long_functions(path, limit):
            print(f"{pathlib.Path(path).relative_to(ROOT)}:{line}: {length} lines: {name}")
            total += 1
    print(f"functions over {limit} lines: {total}")
    sys.exit(1 if total else 0)


if __name__ == "__main__":
    main()
