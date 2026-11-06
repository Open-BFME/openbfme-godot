"""Every Godot shader must fit the smallest varying budget of the GPUs we support.

Godot 4.7 counts varyings exactly (servers/rendering/shader_language.cpp, "Too many varyings used in shader"):
the shader type's own base index (scene_shader_forward_clustered.cpp / _mobile.cpp: spatial 15; canvas 9;
sky 10; particles 10) plus every user varying's size (1 per varying whatever its width, an array its length,
mat2/mat3/mat4 2/3/4 per element). The limit is min(maxVertexOutputComponents, maxFragmentInputComponents) / 4
(rendering_device_driver_vulkan.cpp). Intel's GPUs (Arc and integrated) report 116 fragment inputs, so 29.
NVIDIA and AMD report 128 (32): a shader at 30..32 compiles there and fails on Intel, where the material
draws with nothing (Silmaril v0.6.8: the terrain at 30 drew as flat brown ground on an Arc A770).

The count is conservative: every `varying` in the shader and the files it #includes counts, including ones
inside #if branches.

Both places a shader can live are checked: .gdshader / .gdshaderinc files, and the inline shader code strings
of GDScript files (a `shader_type ...;` inside a string literal, e.g. game_options.gd's gamma shader).

Usage: python3 tools/check_shader_varyings.py [godot project dir]   (default: godot/ next to tools/)
"""
from __future__ import annotations

import ast
import re
import sys
from pathlib import Path
from typing import Callable

MAX_VARYINGS = 29  # Intel: 116 fragment input components / 4
BASE_INDEX = {"spatial": 15, "canvas_item": 9, "sky": 10, "particles": 10, "fog": 0}
MATRIX_SLOTS = {"mat2": 2, "mat3": 3, "mat4": 4}

_COMMENT = re.compile(r'"(?:[^"\\\n]|\\.)*"|//[^\n]*|/\*.*?\*/', re.S)  # strings first: "res://" is no comment
_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)
_TYPE = re.compile(r"^\s*shader_type\s+(\w+)\s*;", re.M)
# anywhere, not only at a line start: `varying float a; varying float b;` declares two (Greptile, PR #241)
_VARYING = re.compile(r"(?<![\w.])varying\s+(?:(?:flat|smooth)\s+)?(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+([^;]+);")
_NAME = re.compile(r"^\s*(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*$")

Loader = Callable[[str], str]  # project-relative posix path -> text


def strip_comments(text: str) -> str:
    return _COMMENT.sub(lambda m: m.group(0) if m.group(0).startswith('"') else "\n" * m.group(0).count("\n"), text)


def resolve(include: str, current: str) -> str:
    if include.startswith("res://"):
        return include[len("res://"):]
    parent = current.rsplit("/", 1)[0] if "/" in current else ""
    parts: list[str] = [p for p in parent.split("/") if p]
    for part in include.split("/"):
        if part == "..":
            if parts:
                parts.pop()
        elif part and part != ".":
            parts.append(part)
    return "/".join(parts)


def _varyings(path: str, code: str, load: Loader) -> list[tuple[str, str, int]]:
    """[(file, name, slots)] for `code` (displayed as `path`) and every file it includes (each file once)."""
    seen: set[str] = set()
    found: list[tuple[str, str, int]] = []

    def walk(current: str, text: str) -> None:
        if current in seen:
            return
        seen.add(current)
        text = strip_comments(text)
        for match in _VARYING.finditer(text):
            kind = match.group(1)
            for decl in match.group(2).split(","):
                name = _NAME.match(decl)
                if not name:
                    raise ValueError(f"{current}: cannot read the varying declaration `{match.group(0).strip()}`")
                count = int(name.group(2) or 1)
                found.append((current, name.group(1), count * MATRIX_SLOTS.get(kind, 1)))
        for inc in _INCLUDE.findall(text):
            path = resolve(inc, current)
            walk(path, load(path))

    walk(path, code)
    return found


def varyings(rel: str, load: Loader) -> list[tuple[str, str, int]]:
    """[(file, name, slots)] for `rel` and every file it includes (each file once)."""
    return _varyings(rel, load(rel), load)


def _shader_usage(code: str, path: str, load: Loader) -> tuple[str, int, list[tuple[str, str, int]]]:
    match = _TYPE.search(strip_comments(code))
    if not match:
        raise ValueError(f"{path}: no shader_type")
    kind = match.group(1)
    if kind not in BASE_INDEX:
        raise ValueError(f"{path}: unknown shader_type {kind}")
    found = _varyings(path, code, load)
    return kind, BASE_INDEX[kind] + sum(slots for _, _, slots in found), found


def shader_usage(rel: str, load: Loader) -> tuple[str, int, list[tuple[str, str, int]]]:
    return _shader_usage(load(rel), rel, load)


def _problem(rel: str, used: int, kind: str, base: int, found: list[tuple[str, str, int]]) -> str:
    listed = ", ".join(f"{name} ({slots}, {path})" for path, name, slots in found)
    return (f"{rel}: {used} varyings ({kind} base {base} + yours), the limit is {MAX_VARYINGS} "
            f"(Intel GPUs; NVIDIA/AMD allow 32, so it only fails there): pack them into vec4s. Yours: {listed}")


def problems(shaders: list[str], load: Loader) -> list[str]:
    out = []
    for rel in sorted(shaders):
        try:
            kind, used, found = shader_usage(rel, load)
        except (ValueError, OSError, KeyError) as exc:
            out.append(f"{rel}: cannot count varyings: {exc}")
            continue
        if used > MAX_VARYINGS:
            out.append(_problem(rel, used, kind, BASE_INDEX[kind], found))
    return out


def gdscript_strings(text: str) -> list[tuple[int, str]]:
    """[(line, decoded string)] for every string literal in GDScript source; # comments and string contents are honoured."""
    strings: list[tuple[int, str]] = []
    line = 1
    i = 0
    while i < len(text):
        c = text[i]
        if c == "#":
            while i < len(text) and text[i] != "\n":
                i += 1
        elif text.startswith('"""', i) or text.startswith("'''", i):
            quote = text[i:i + 3]
            start_line = line
            start = i
            i += 3
            while i < len(text) and not text.startswith(quote, i):
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == "\n":
                    line += 1
                i += 1
            end = min(i + 3, len(text))
            try:
                strings.append((start_line, ast.literal_eval(text[start:end])))
            except (SyntaxError, ValueError):
                strings.append((start_line, text[start + 3:i]))
            i = end
        elif c in ('"', "'"):
            quote = c
            start_line = line
            start = i
            i += 1
            while i < len(text):
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == quote:
                    break
                if text[i] == "\n":
                    line += 1
                i += 1
            end = min(i + 1, len(text))
            try:
                strings.append((start_line, ast.literal_eval(text[start:end])))
            except (SyntaxError, ValueError):
                strings.append((start_line, text[start + 1:i]))
            i = end
        else:
            if c == "\n":
                line += 1
            i += 1
    return strings


def inline_shaders(files: dict[str, str]) -> list[tuple[str, int, str]]:
    """[(file:line name, line, code)] for the shader code strings of the GDScript `files`."""
    out: list[tuple[str, int, str]] = []
    for rel in sorted(files):
        for line, code in gdscript_strings(files[rel]):
            if _TYPE.search(strip_comments(code)):
                out.append((f"{rel}:{line} (inline shader)", line, code))
    return out


def inline_problems(files: dict[str, str], load: Loader) -> list[str]:
    out = []
    for name, _line, code in inline_shaders(files):
        try:
            kind, used, found = _shader_usage(code, name, load)
        except (ValueError, OSError, KeyError) as exc:
            out.append(f"{name}: cannot count varyings: {exc}")
            continue
        if used > MAX_VARYINGS:
            out.append(_problem(name, used, kind, BASE_INDEX[kind], found))
    return out


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent / "godot"

    def load(rel: str) -> str:
        return (root / rel).read_text(encoding="utf-8")

    shaders = [p.relative_to(root).as_posix() for p in root.rglob("*.gdshader")
               if ".godot" not in p.relative_to(root).parts]
    scripts = {p.relative_to(root).as_posix(): p.read_text(encoding="utf-8") for p in root.rglob("*.gd")
               if ".godot" not in p.relative_to(root).parts}
    inline = inline_shaders(scripts)
    found = problems(shaders, load) + inline_problems(scripts, load)
    for line in found:
        print(line)
    if not found:
        # a project may have no shader files or no inline shaders (Greptile / Devin, PR #242): max() of an empty list raised
        worst = max([(shader_usage(rel, load)[1], rel) for rel in shaders] +
                    [(_shader_usage(code, name, load)[1], name) for name, _line, code in inline], default=(0, "no shaders"))
        print(f"shader varyings ok: {len(shaders)} shaders and {len(inline)} inline shaders, "
              f"the most is {worst[0]}/{MAX_VARYINGS} ({worst[1]})")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
