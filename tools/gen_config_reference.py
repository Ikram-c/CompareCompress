#!/usr/bin/env python3
"""Generates docs/reference/config.md from config/config.yaml and src/core/config.cpp.

The YAML supplies every key, its default and the comment next to it; the C++
reader supplies the type and the accepted range of each key.  Running this
script after editing either file keeps the reference in step with the code:

    python3 tools/gen_config_reference.py

It needs no third-party modules (the YAML subset used by config.yaml is parsed
with a small purpose-built reader).
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
YAML_PATH = ROOT / "config" / "config.yaml"
CPP_PATH = ROOT / "src" / "core" / "config.cpp"
OUT_PATH = ROOT / "docs" / "reference" / "config.md"

# Named validation bounds used in config.cpp (kept in step by hand; the script
# fails loudly when it meets a name it does not know).
CONSTANTS = {}


def load_constants(cpp: str) -> None:
    """Collects `constexpr <type> kName = <value>;` definitions from config.cpp."""
    for m in re.finditer(r"constexpr\s+[\w:]+\s+(k\w+)\s*=\s*([^;/]+);", cpp):
        name, expr = m.group(1), m.group(2).strip()
        expr = re.sub(r"static_cast<[^>]+>", "", expr)
        expr = re.sub(r"(\d)[fuL]+\b", r"\1", expr)
        expr = expr.replace("f", "") if re.fullmatch(r"[\d.e+\-f]+", expr) else expr
        try:
            CONSTANTS[name] = eval(expr, {"__builtins__": {}}, dict(CONSTANTS))  # noqa: S307 - trusted source file
        except Exception:  # pragma: no cover - only for unexpected expressions
            pass
    CONSTANTS["kConfigSchemaVersion"] = 1  # from config.h
    CONSTANTS["max_delay"] = CONSTANTS["kMaxSeconds"]
    CONSTANTS["max_seconds"] = CONSTANTS["kMaxSeconds"]
    CONSTANTS["max_loupe_zoom"] = CONSTANTS["kMaxZoom"]


def value_of(token: str):
    """Turns a bound expression from a getter call into a number."""
    token = token.strip()
    token = re.sub(r"static_cast<[^>]+>\((.*)\)", r"\1", token)
    if token in CONSTANTS:
        return CONSTANTS[token]
    m = re.fullmatch(r"-?(k\w+)", token)
    if m and m.group(1) in CONSTANTS:
        return -CONSTANTS[m.group(1)]
    m = re.fullmatch(r"(k\w+)\s*\*\s*(k\w+)", token)
    if m:
        return CONSTANTS[m.group(1)] * CONSTANTS[m.group(2)]
    cleaned = re.sub(r"[fuL]+$", "", token)
    try:
        return float(cleaned) if "." in cleaned or "e" in cleaned else int(cleaned)
    except ValueError:
        sys.exit(f"gen_config_reference: unknown bound '{token}'")


def fmt(v) -> str:
    if isinstance(v, float):
        if v == int(v) and abs(v) < 1e15:
            return str(int(v))
        return f"{v:g}"
    return str(v)


def balanced_tail(text: str, start: int) -> str:
    """Returns the text from `start` up to the parenthesis that closes the getter call."""
    depth = 1
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[start:i]
    sys.exit("gen_config_reference: unbalanced parentheses in config.cpp")


def split_args(text: str) -> list:
    """Splits a comma-separated argument list, ignoring commas inside () and <>."""
    out, depth, current = [], 0, ""
    for ch in text:
        if ch in "(<":
            depth += 1
        elif ch in ")>":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(current)
            current = ""
        else:
            current += ch
    out.append(current)
    return out


def parse_getters(cpp: str) -> dict:
    """Maps key path -> (type, range text) from the r.get_* calls."""
    types = {"int": "integer", "long": "integer", "u64": "integer (bytes)", "size": "integer (bytes)",
             "float": "number", "double": "number", "bool": "true / false", "string": "text",
             "string_list": "list of text"}
    out = {}
    pattern = re.compile(r'r\.get_(int|long|float|double|u64|size|bool|string|string_list|enum<\w+>)\("([\w.]+)"')
    for m in pattern.finditer(cpp):
        kind, key = m.group(1), m.group(2)
        rest = balanced_tail(cpp, m.end())
        args = [a.strip() for a in split_args(rest) if a.strip()]
        if kind.startswith("enum"):
            choices = re.search(r'"([^"]+)"\s*$', rest)
            out[key] = ("one of", choices.group(1) if choices else "")
        elif kind in ("string",):
            out[key] = (types[kind], "must not be empty" if args and args[-1] == "false" else "")
        elif kind == "string_list":
            out[key] = (types[kind], f"at most {value_of(args[-1])} entries")
        elif kind == "bool":
            out[key] = (types[kind], "")
        else:
            lo, hi = value_of(args[-2]), value_of(args[-1])
            out[key] = (types[kind], f"{fmt(lo)} .. {fmt(hi)}")
    # metrics.default_scale.* is read in a loop
    scale = re.search(r'metrics\.default_scale\."\) \+ metric_key\(.*?\n\s*ok = r\.get_float\(path, [^,]+, ([^,]+), ([^)]+)\)', cpp, re.S)
    if scale:
        rng = f"{fmt(value_of(scale.group(1)))} .. {fmt(value_of(scale.group(2)))}"
        for metric in ("abs_diff", "luma_diff", "delta_e76", "delta_e2000", "ssim", "squared_error"):
            out[f"metrics.default_scale.{metric}"] = ("number", rng)
    return out


def parse_yaml(text: str):
    """Reads config.yaml into (path, default, comment) rows, stopping at `sites:`.

    A comment-only line indented as far as the previous key's comment continues
    that comment (config.yaml wraps long explanations that way)."""
    rows = []
    stack = []  # (indent, key)
    last_comment_col = None
    for raw in text.split("\n"):
        if not raw.strip():
            last_comment_col = None
            continue
        if raw.lstrip().startswith("#"):
            col = len(raw) - len(raw.lstrip(" "))
            if rows and last_comment_col is not None and col >= last_comment_col:
                path, default, comment = rows[-1]
                rows[-1] = (path, default, (comment + " " + raw.strip().lstrip("#").strip()).strip())
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        body, hash_, comment = raw.partition("#")
        last_comment_col = len(body) if hash_ else None
        body = body.rstrip()
        m = re.match(r"\s*([\w]+):\s*(.*)$", body)
        if not m:
            continue
        key, value = m.group(1), m.group(2).strip()
        while stack and stack[-1][0] >= indent:
            stack.pop()
        path = ".".join([k for _, k in stack] + [key])
        if key == "sites" and indent == 0:
            break
        if value == "":
            stack.append((indent, key))
            rows.append((path, None, comment.strip()))
        else:
            rows.append((path, value, comment.strip()))
    return rows


def main() -> None:
    cpp = CPP_PATH.read_text(encoding="utf-8")
    load_constants(cpp)
    getters = parse_getters(cpp)
    rows = parse_yaml(YAML_PATH.read_text(encoding="utf-8"))
    lines = [
        "# Configuration reference (`config.yaml`)",
        "",
        "*Generated by `tools/gen_config_reference.py` from `config/config.yaml` and `src/core/config.cpp`;",
        "edit those files, not this page.*",
        "",
        "The built-in defaults below are compiled into the executable. A `config.yaml` overrides any subset of",
        "them; the first one found is used: the file named with `--config <file>`, `config.yaml` next to the",
        "executable, the per-user file (`~/Library/Application Support/CompressCompare/config.yaml` on macOS,",
        "`~/.config/compresscompare/config.yaml` on Linux; Settings > *edit config.yaml...* creates it), and",
        "`config.yaml` in the working directory.  Maps merge key by key,",
        "scalars and lists replace the default, and the `sites` list merges by name.",
        "Every key is validated at start-up; a mistake stops the program with a message naming the file and the",
        "key. `compresscompare --check-config` validates without opening a window.",
        "See [how-to: change the configuration](../how-to/change-config.md) for the workflow.",
        "",
        "| Key | Type | Default | Accepted values | Meaning |",
        "|---|---|---|---|---|",
    ]
    for path, default, comment in rows:
        if default is None:
            lines.append(f"| **`{path}`** (section) | map | | | {comment} |")
            continue
        if path not in getters:
            sys.exit(f"gen_config_reference: {path} is in config.yaml but has no getter in config.cpp")
        kind, rng = getters[path]
        lines.append(f"| `{path}` | {kind} | `{default}` | {rng} | {comment} |")
    missing = sorted(set(getters) - {p for p, d, _ in rows if d is not None})
    if missing:
        sys.exit(f"gen_config_reference: read by config.cpp but missing from config.yaml: {missing}")
    lines += [
        "",
        "## The `sites` list",
        "",
        "`sites` is a list of maps with four keys:",
        "",
        "| Key | Type | Required | Meaning |",
        "|---|---|---|---|",
        "| `name` | text | yes | Display name; unique (case-insensitive). |",
        "| `aliases` | list of text | no | Search aliases for the site box (`ig`, `insta`, ...); lower-cased on load. At most 64. |",
        "| `hosts` | list of text | no | Host suffixes for URL auto-detection: a host matches `s` when it equals `s` or ends with `.s`. At most 64. |",
        "| `notes` | text | no | What the site typically does to uploads; shown on hover. |",
        "",
        "An override entry whose `name` matches a built-in site updates the fields it lists and keeps the",
        "others; a new name is appended to the end of the list. The list is limited to 512 entries. The built-in",
        "sites are listed in [sites.md](sites.md).",
        "",
        "## Cross-field rules",
        "",
        "Besides the per-key ranges the loader checks that `metrics.ssim.window` is odd,",
        "`metrics.histogram_fine_bins >= metrics.histogram_bins`, `heatmap.gamma` lies within",
        "`[heatmap.gamma_min, heatmap.gamma_max]`, `heatmap.manual_max_min_factor < heatmap.manual_max_max_factor`,",
        "`view.loupe.zoom` lies within `[view.loupe.min_zoom, view.loupe.max_zoom]`,",
        "`view.loupe.min_size_px <= view.loupe.max_size_px`, `app.panels.slot_thumb_min_px <= app.panels.slot_thumb_max_px`,",
        "`ffmpeg.sample_points` lies within `[ffmpeg.sample_points_min, ffmpeg.sample_points_max]` and",
        "`threading.fallback_workers <= threading.max_workers`.",
        "",
        "## Constants that are deliberately not configurable",
        "",
        "Physical and standards constants stay in the source as named `constexpr` values: the sRGB transfer",
        "curve and D65 matrix, the CIE Lab thresholds, the CIEDE2000 weights, Rec.601 luma, the ITU-T T.81",
        "Annex K quantisation tables, JPEG marker codes, the colour-map anchor colours, OpenGL enumerants and",
        "the cosmetic paddings and colours of the widgets (`src/app/ui_style.h`).",
        "",
    ]
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT_PATH.relative_to(ROOT)} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
