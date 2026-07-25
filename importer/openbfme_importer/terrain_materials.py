"""Strict, deterministic SAGE terrain material bundle conversion.

Only the requested logical terrain symbols and their exact TGA dependency
closure are accepted.  Retail INI/TGA sources are never copied to outputs.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
from typing import Any, Iterable

from .paths import safe_relative_parts
from .profile import MAX_TERRAIN_MATERIAL_SYMBOLS, TERRAIN_MATERIAL_SYMBOL_PATTERN
from .util import write_json_atomic


MAX_TERRAIN_INI_BYTES = 4 * 1024 * 1024
MAX_TERRAIN_TEXTURE_BYTES = 256 * 1024 * 1024
MAX_TERRAIN_IMAGE_DIMENSION = 16_384
MAX_TERRAIN_IMAGE_PIXELS = 64 * 1024 * 1024
_TERRAIN_HEADER = re.compile(r"^Terrain\s+([A-Za-z0-9][A-Za-z0-9._-]{0,127})$", re.IGNORECASE)
_TEXTURE_ASSIGNMENT = re.compile(
    r'^Texture\s*=\s*(?:"([^"]+)"|([^\s]+))$', re.IGNORECASE
)
_SAFE_SOURCE_BASENAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._ -]{0,254}$")


@dataclass(frozen=True, slots=True)
class _TerrainDefinition:
    symbol: str
    texture: str
    line: int


@dataclass(frozen=True, slots=True)
class TerrainMaterialReference:
    """One requested map symbol and its exact source texture dependency."""

    requested_symbol: str
    definition_symbol: str
    texture: str


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def _safe_source_basename(value: str, label: str) -> str:
    if not _SAFE_SOURCE_BASENAME.fullmatch(value):
        raise ValueError(f"unsafe {label}: {value!r}")
    parts = safe_relative_parts(value)
    if len(parts) != 1 or parts[0] != value:
        raise ValueError(f"unsafe {label}: {value!r}")
    return value


def _strip_ini_comment(line: str) -> str:
    stripped = line.strip()
    if stripped.startswith((";", "#", "//")):
        return ""
    for marker in (";", "//"):
        position = line.find(marker)
        if position >= 0:
            line = line[:position]
    return line.strip()


def _parse_terrain_ini_bytes(
    payload: bytes, requested_keys: set[str]
) -> dict[str, _TerrainDefinition]:
    if not isinstance(payload, bytes):
        raise TypeError("terrain.ini source must be bytes")
    if len(payload) > MAX_TERRAIN_INI_BYTES:
        raise ValueError(f"terrain.ini exceeds {MAX_TERRAIN_INI_BYTES} byte limit")
    if b"\0" in payload:
        raise ValueError("terrain.ini contains a NUL byte")
    try:
        text = (
            payload.decode("utf-8-sig")
            if payload.startswith(b"\xef\xbb\xbf")
            else payload.decode("cp1252")
        )
    except UnicodeDecodeError as exc:
        raise ValueError("terrain.ini has an unsupported text encoding") from exc

    definitions: dict[str, _TerrainDefinition] = {}
    # Duplicate Terrain symbols retail authors more than once; last record wins
    # and the superseded one is recorded (see the shadowing note below).
    shadowed: list[dict[str, object]] = []
    current_symbol: str | None = None
    current_line = 0
    current_textures: list[str] = []
    for line_number, raw_line in enumerate(text.splitlines(), 1):
        line = _strip_ini_comment(raw_line)
        if not line:
            continue
        header = _TERRAIN_HEADER.fullmatch(line)
        if current_symbol is None:
            if header:
                current_symbol = header.group(1)
                current_line = line_number
                current_textures = []
                continue
            if line.casefold().startswith("terrain"):
                # Retail terrain.ini also contains non-material Terrain forms.
                # Requested symbols must use the exact material grammar; other
                # forms are ignored and can never satisfy the closure.
                continue
            continue

        if header or line.casefold().startswith("terrain"):
            if current_symbol.casefold() not in requested_keys:
                if header:
                    current_symbol = header.group(1)
                    current_line = line_number
                    current_textures = []
                else:
                    current_symbol = None
                    current_line = 0
                    current_textures = []
                continue
            raise ValueError(
                f"unterminated terrain definition {current_symbol!r} before line {line_number}"
            )
        if line.casefold() == "end":
            key = current_symbol.casefold()
            if key not in requested_keys:
                current_symbol = None
                current_line = 0
                current_textures = []
                continue
            # Retail authors the same Terrain symbol twice with DIFFERENT
            # textures: `SnowType6` at terrain.ini:2056 (TXSnow05a.tga) and
            # again at :2061 (TMSnow02a.tga), and `SandMediumType2` at :1756.
            # Failing closed here blocked six skirmish maps (grey-mountains,
            # withered-heath, tournament-gundabad, tournament-rhudaur, ...).
            # EA's prepend-based lookup and OpenSAGE's dictionary overwrite
            # both select the LAST source record for an exact duplicate name —
            # the same rule `sage_map.py` already applies to duplicate player
            # -start waypoints. Take the last record and record the shadowing
            # so the decision stays auditable rather than silent.
            if key in definitions:
                shadowed.append(
                    {
                        "symbol": current_symbol,
                        "supersededTexture": definitions[key].texture,
                        "supersededLine": definitions[key].line,
                        "line": current_line,
                    }
                )
            if len(current_textures) != 1:
                raise ValueError(
                    f"terrain definition {current_symbol!r} must have exactly one Texture"
                )
            texture = _safe_source_basename(
                current_textures[0], f"Texture filename for {current_symbol!r}"
            )
            if Path(texture).suffix.casefold() != ".tga":
                raise ValueError(
                    f"unsupported terrain texture format for {current_symbol!r}: {texture}"
                )
            definitions[key] = _TerrainDefinition(current_symbol, texture, current_line)
            current_symbol = None
            current_line = 0
            current_textures = []
            continue
        assignment = _TEXTURE_ASSIGNMENT.fullmatch(line)
        if assignment:
            current_textures.append(assignment.group(1) or assignment.group(2))

    if current_symbol is not None and current_symbol.casefold() in requested_keys:
        raise ValueError(f"unterminated terrain definition {current_symbol!r}")
    return definitions


def _parse_terrain_ini(
    path: Path, requested_keys: set[str]
) -> dict[str, _TerrainDefinition]:
    size = path.stat().st_size
    if size > MAX_TERRAIN_INI_BYTES:
        raise ValueError(f"terrain.ini exceeds {MAX_TERRAIN_INI_BYTES} byte limit")
    return _parse_terrain_ini_bytes(path.read_bytes(), requested_keys)


def _validated_symbols(value: Any) -> list[str]:
    if (
        not isinstance(value, list)
        or not 1 <= len(value) <= MAX_TERRAIN_MATERIAL_SYMBOLS
        or any(not isinstance(symbol, str) for symbol in value)
    ):
        raise ValueError(
            "sage-terrain-materials options.symbols must be an explicit array of "
            f"1..{MAX_TERRAIN_MATERIAL_SYMBOLS} strings"
        )
    result: list[str] = []
    folded: set[str] = set()
    for symbol in value:
        if not TERRAIN_MATERIAL_SYMBOL_PATTERN.fullmatch(symbol):
            raise ValueError(f"unsafe terrain material symbol: {symbol!r}")
        key = symbol.casefold()
        if key in folded:
            raise ValueError(f"duplicate terrain material symbol: {symbol!r}")
        folded.add(key)
        result.append(symbol)
    return result


def resolve_terrain_material_references(
    source: bytes, symbols: Any
) -> tuple[TerrainMaterialReference, ...]:
    """Resolve ordered map symbols without extracting or decoding texture bytes."""

    requested = _validated_symbols(symbols)
    definitions = _parse_terrain_ini_bytes(
        source, {symbol.casefold() for symbol in requested}
    )
    result: list[TerrainMaterialReference] = []
    for symbol in requested:
        definition = definitions.get(symbol.casefold())
        if definition is None:
            raise ValueError(f"unresolved terrain material symbol: {symbol}")
        result.append(
            TerrainMaterialReference(
                requested_symbol=symbol,
                definition_symbol=definition.symbol,
                texture=definition.texture,
            )
        )
    return tuple(result)


def _contained_output(root: Path, relative: str) -> Path:
    parts = safe_relative_parts(relative)
    target = (root / Path(*parts)).resolve()
    try:
        target.relative_to(root.resolve())
    except ValueError as exc:
        raise ValueError(f"terrain material output escaped its bundle: {relative!r}") from exc
    return target


def convert_terrain_materials(
    sources: Iterable[tuple[str, Path | str]],
    output_directory: Path | str,
    symbols: Any,
) -> list[Path]:
    """Convert one exact terrain.ini/TGA closure into a cooked PNG bundle."""

    requested_symbols = _validated_symbols(symbols)
    selected = list(sources)
    if not 2 <= len(selected) <= MAX_TERRAIN_MATERIAL_SYMBOLS + 1:
        raise ValueError("sage-terrain-materials requires one terrain.ini and selected TGAs")

    by_basename: dict[str, tuple[str, Path]] = {}
    seen_virtual_paths: set[str] = set()
    for raw_virtual_path, raw_source_path in selected:
        if not isinstance(raw_virtual_path, str):
            raise ValueError("terrain material source virtual path must be a string")
        virtual_parts = safe_relative_parts(raw_virtual_path)
        virtual_key = "/".join(virtual_parts).casefold()
        if virtual_key in seen_virtual_paths:
            raise ValueError(f"duplicate terrain material source: {raw_virtual_path}")
        seen_virtual_paths.add(virtual_key)
        basename = _safe_source_basename(
            virtual_parts[-1], "terrain material source basename"
        )
        basename_key = basename.casefold()
        if basename_key in by_basename:
            raise ValueError(f"duplicate terrain material source basename: {basename}")
        source_path = Path(raw_source_path)
        if not source_path.is_file() or source_path.is_symlink():
            raise ValueError(f"terrain material source is not a regular file: {basename}")
        if source_path.stat().st_size > MAX_TERRAIN_TEXTURE_BYTES:
            raise ValueError(f"terrain material source exceeds size limit: {basename}")
        by_basename[basename_key] = (basename, source_path)

    ini_sources = [item for key, item in by_basename.items() if key == "terrain.ini"]
    if len(ini_sources) != 1:
        raise ValueError("sage-terrain-materials requires exactly one terrain.ini source")
    unsupported = sorted(
        name
        for key, (name, _) in by_basename.items()
        if key != "terrain.ini" and Path(name).suffix.casefold() != ".tga"
    )
    if unsupported:
        raise ValueError(
            "unsupported terrain material source format(s): " + ", ".join(unsupported)
        )

    definitions = _parse_terrain_ini(
        ini_sources[0][1], {symbol.casefold() for symbol in requested_symbols}
    )
    resolved: list[tuple[str, _TerrainDefinition]] = []
    for symbol in requested_symbols:
        definition = definitions.get(symbol.casefold())
        if definition is None:
            raise ValueError(f"unresolved terrain material symbol: {symbol}")
        resolved.append((symbol, definition))

    required_texture_keys: list[str] = []
    for _, definition in resolved:
        key = definition.texture.casefold()
        if key not in required_texture_keys:
            required_texture_keys.append(key)
    selected_texture_keys = {key for key in by_basename if key != "terrain.ini"}
    missing = [
        definition.texture
        for _, definition in resolved
        if definition.texture.casefold() not in selected_texture_keys
    ]
    extra = sorted(
        by_basename[key][0]
        for key in selected_texture_keys - set(required_texture_keys)
    )
    if missing:
        raise ValueError("missing selected terrain texture(s): " + ", ".join(dict.fromkeys(missing)))
    if extra:
        raise ValueError("extra selected terrain texture(s): " + ", ".join(extra))

    try:
        import PIL
        from PIL import Image, UnidentifiedImageError
    except ImportError as exc:
        raise FileNotFoundError(
            "Pillow is required for deterministic terrain TGA conversion"
        ) from exc
    if PIL.__version__ != "12.2.0":
        raise RuntimeError(
            "Pillow 12.2.0 is required for deterministic terrain texture output; "
            f"found {PIL.__version__}"
        )

    output = Path(output_directory)
    if output.is_symlink():
        raise ValueError("terrain material output directory cannot be a symbolic link")
    if output.exists():
        if not output.is_dir() or any(output.iterdir()):
            raise ValueError("terrain material output directory must be empty")
    else:
        output.mkdir(parents=True)
    texture_records: dict[str, dict[str, Any]] = {}
    output_paths: list[Path] = []
    for output_index, texture_key in enumerate(required_texture_keys):
        source_name, source_path = by_basename[texture_key]
        relative_png = f"textures/{output_index:04d}.png"
        target = _contained_output(output, relative_png)
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            with Image.open(source_path) as opened:
                if opened.format != "TGA":
                    raise ValueError(
                        f"unsupported image payload for {source_name}: {opened.format or 'unknown'}"
                    )
                width, height = opened.size
                if (
                    width < 1
                    or height < 1
                    or width > MAX_TERRAIN_IMAGE_DIMENSION
                    or height > MAX_TERRAIN_IMAGE_DIMENSION
                    or width * height > MAX_TERRAIN_IMAGE_PIXELS
                ):
                    raise ValueError(
                        f"terrain texture dimensions exceed limit: {source_name} {width}x{height}"
                    )
                rgba = opened.convert("RGBA")
                try:
                    pixels = rgba.tobytes()
                finally:
                    rgba.close()
            converted = Image.frombytes("RGBA", (width, height), pixels)
            try:
                converted.save(target, format="PNG", compress_level=9, optimize=False)
            finally:
                converted.close()
        except Image.DecompressionBombError as exc:
            raise ValueError(f"terrain texture dimensions exceed limit: {source_name}") from exc
        except UnidentifiedImageError as exc:
            raise ValueError(f"unsupported image payload for {source_name}") from exc
        except OSError as exc:
            raise ValueError(f"failed to decode terrain texture {source_name}") from exc
        record = {
            "sourceFile": source_name,
            "sourceSha256": _sha256(source_path),
            "png": relative_png,
            "pngSha256": _sha256(target),
            "width": width,
            "height": height,
            "mode": "RGBA",
        }
        texture_records[texture_key] = record
        output_paths.append(target)

    materials: list[dict[str, Any]] = []
    for table_index, (requested_symbol, definition) in enumerate(resolved):
        texture = texture_records[definition.texture.casefold()]
        materials.append(
            {
                "tableIndex": table_index,
                "symbol": requested_symbol,
                "definitionSymbol": definition.symbol,
                "sourceTexture": definition.texture,
                "png": texture["png"],
                "pngSha256": texture["pngSha256"],
                "width": texture["width"],
                "height": texture["height"],
            }
        )

    manifest_path = _contained_output(output, "terrain-materials.json")
    write_json_atomic(
        manifest_path,
        {
            "schema": "openbfme.sage-terrain-materials",
            "schemaVersion": 0,
            "tableOrder": "options.symbols",
            "symbolCount": len(materials),
            "textureCount": len(texture_records),
            "definitionSource": {
                "file": ini_sources[0][0],
                "sha256": _sha256(ini_sources[0][1]),
                "packaged": False,
            },
            "materials": materials,
            "textures": [texture_records[key] for key in required_texture_keys],
        },
    )
    output_paths.append(manifest_path)
    return output_paths
