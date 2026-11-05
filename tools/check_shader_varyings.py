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

Usage: python3 tools/check_shader_varyings.py [godot project dir]   (default: godot/ next to tools/)
"""
from __future__ import annotations

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
_VARYING = re.compile(r"^\s*varying\s+(?:(?:flat|smooth)\s+)?(?:(?:lowp|mediump|highp)\s+)?(\w+)\s+([^;]+);", re.M)
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


def varyings(rel: str, load: Loader) -> list[tuple[str, str, int]]:
    """[(file, name, slots)] for `rel` and every file it includes (each file once)."""
    seen: set[str] = set()
    found: list[tuple[str, str, int]] = []

    def walk(path: str) -> None:
        if path in seen:
            return
        seen.add(path)
        text = strip_comments(load(path))
        for match in _VARYING.finditer(text):
            kind = match.group(1)
            for decl in match.group(2).split(","):
                name = _NAME.match(decl)
                if not name:
                    raise ValueError(f"{path}: cannot read the varying declaration `{match.group(0).strip()}`")
                count = int(name.group(2) or 1)
                found.append((path, name.group(1), count * MATRIX_SLOTS.get(kind, 1)))
        for inc in _INCLUDE.findall(text):
            walk(resolve(inc, path))

    walk(rel)
    return found


def shader_usage(rel: str, load: Loader) -> tuple[str, int, list[tuple[str, str, int]]]:
    match = _TYPE.search(strip_comments(load(rel)))
    if not match:
        raise ValueError(f"{rel}: no shader_type")
    kind = match.group(1)
    if kind not in BASE_INDEX:
        raise ValueError(f"{rel}: unknown shader_type {kind}")
    found = varyings(rel, load)
    return kind, BASE_INDEX[kind] + sum(slots for _, _, slots in found), found


def problems(shaders: list[str], load: Loader) -> list[str]:
    out = []
    for rel in sorted(shaders):
        try:
            kind, used, found = shader_usage(rel, load)
        except (ValueError, OSError, KeyError) as exc:
            out.append(f"{rel}: cannot count varyings: {exc}")
            continue
        if used > MAX_VARYINGS:
            listed = ", ".join(f"{name} ({slots}, {path})" for path, name, slots in found)
            out.append(f"{rel}: {used} varyings ({kind} base {BASE_INDEX[kind]} + yours), the limit is {MAX_VARYINGS} "
                       f"(Intel GPUs; NVIDIA/AMD allow 32, so it only fails there): pack them into vec4s. Yours: {listed}")
    return out


def main(argv: list[str]) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent / "godot"
    shaders = [p.relative_to(root).as_posix() for p in root.rglob("*.gdshader")
               if ".godot" not in p.relative_to(root).parts]
    found = problems(shaders, lambda rel: (root / rel).read_text(encoding="utf-8"))
    for line in found:
        print(line)
    if not found:
        worst = max((shader_usage(rel, lambda r: (root / r).read_text(encoding="utf-8"))[1], rel) for rel in shaders)
        print(f"shader varyings ok: {len(shaders)} shaders, the most is {worst[0]}/{MAX_VARYINGS} ({worst[1]})")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
