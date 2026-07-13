"""Install discovery and deterministic BIG archive cataloguing."""

from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
from typing import Any, Iterable

from .big import BigArchive, BigEntry
from .big import sha256_file
from .paths import safe_relative_parts
from .util import read_json, write_json_atomic


PATCH_ARCHIVES = ("_patch106.big", "_patch105.big", "_patch104.big", "_patch103.big", "_patch102.big", "_patch101.big")
CORE_ARCHIVES = (
    "ini.big",
    "w3d.big",
    "textures0.big",
    "textures1.big",
    "textures2.big",
    "textures3.big",
    "textures4.big",
    "audio.big",
    "music.big",
    "ambientstreams.big",
    "maps.big",
    "terrain.big",
    "window.big",
)

KNOWN_SLICE_ARCHIVE_SHA256 = {
    "ini.big": "e5e5aa2be5681161c2e24daa75e9294c38cb988133cba385bc433cba30fb72ca",
    "w3d.big": "c65fc670c35a2b938720a82328559786944b9dd3a37868f218f79226db3ed87d",
    "textures1.big": "defdcbef8bbd2b8b19571079c0224b2c89494410029860685ac221df7f57457a",
    "textures2.big": "1f58a0040ff52ee40bb6cfdbd02d1ecc79bd23302058b6ea6fe9880d1963a7fd",
    "textures3.big": "bc637fd0f7a2569451e4a0023377b55bd9ceb902f2ba663eb6c81ecbf18065cc",
    "textures4.big": "cc6eb18ee6ae6e537351a8639e3d2f054666e8e7e4b35baeacf8b876e0dd653e",
    "audio.big": "53d6e1108a2855a611c9ff596765a801b9db09ee94b73cd6429846f20a5d40d4",
    "lang/englishaudio.big": "322f3fce09800f24c3cd91708ce8ab295c372dcdc2e0fc1dcbe86fdee263c073",
    "maps.big": "8ca19783c3a4969dffeb5a6493a18d6f7447a310bf8b4152b17fc7ed4307e1c8",
    "terrain.big": "bf2b3331d50b2f07138e79a3d75d9d78e6a6cf41738629bc9aa8c184e12edaba",
    "music.big": "6cc8cf58ad6b1e37ea36b4f49cae4ef3b8f6698be1b9e08614763c6dedb9ac5b",
    "lang/englishpatch105.big": "ffdc7e390e9c3f3196b105d60ec067546844a946917d2b021e616bb75ad75e56",
}


@dataclass(frozen=True, slots=True)
class ArchiveInfo:
    relative_path: str
    size: int
    mtime_ns: int
    magic: str
    header_size: int
    entry_count: int
    directory_sha256: str


@dataclass(frozen=True, slots=True)
class CatalogEntry:
    archive: str
    name: str
    offset: int
    size: int
    precedence: int

    @property
    def key(self) -> str:
        return self.name.casefold()


def archive_precedence(relative_path: str) -> tuple[int, str]:
    """Lower numbers win when duplicate virtual paths exist."""
    name = Path(relative_path).name.casefold()
    patch_names = [value.casefold() for value in PATCH_ARCHIVES]
    if name in patch_names:
        return patch_names.index(name), relative_path.casefold()
    if "patch" in name and name.endswith(".big"):
        # Language patch archives use names such as EnglishPatch105.big.
        return 50, relative_path.casefold()
    # EA loads underscore override archives before normal data archives.
    if name.startswith("_"):
        return 100, relative_path.casefold()
    return 1000, relative_path.casefold()


def _directory_sha256(
    magic: str,
    header_size: int,
    entries: Iterable[BigEntry | CatalogEntry],
    *,
    relative_path: str,
    precedence: int,
) -> str:
    """Bind BIG directory bytes and every catalog winner-affecting field."""

    digest = hashlib.sha256()
    digest.update(magic.encode("ascii"))
    digest.update(b"\0")
    digest.update(relative_path.casefold().encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(precedence).encode("ascii"))
    digest.update(b"\0")
    digest.update(str(header_size).encode("ascii"))
    digest.update(b"\n")
    for entry in entries:
        entry_archive = entry.archive if isinstance(entry, CatalogEntry) else relative_path
        entry_precedence = entry.precedence if isinstance(entry, CatalogEntry) else precedence
        digest.update(entry_archive.casefold().encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(entry_precedence).encode("ascii"))
        digest.update(b"\0")
        digest.update(entry.name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(entry.offset).encode("ascii"))
        digest.update(b":")
        digest.update(str(entry.size).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _archive_directory_sha256(
    archive: BigArchive,
    relative_path: str,
    precedence: int,
) -> str:
    return _directory_sha256(
        archive.magic,
        archive.header_size,
        archive.entries,
        relative_path=relative_path,
        precedence=precedence,
    )


class InstallCatalog:
    FORMAT = 4

    def __init__(
        self,
        install_root: Path,
        archives: tuple[ArchiveInfo, ...],
        entries: tuple[CatalogEntry, ...],
    ) -> None:
        self.install_root = install_root.resolve()
        self.archives = archives
        self.entries = entries
        by_key: dict[str, list[CatalogEntry]] = {}
        for entry in entries:
            by_key.setdefault(entry.key, []).append(entry)
        self._by_key = {
            key: tuple(sorted(values, key=lambda item: (item.precedence, item.archive.casefold())))
            for key, values in by_key.items()
        }

    @classmethod
    def build(
        cls,
        install_root: Path | str,
        *,
        archive_names: Iterable[str] | None = None,
    ) -> "InstallCatalog":
        root = Path(install_root).expanduser().resolve()
        if not root.is_dir():
            raise FileNotFoundError(f"BFME II install directory not found: {root}")
        if archive_names is None:
            archive_paths = sorted(root.rglob("*.big"), key=lambda path: str(path).casefold())
        else:
            archive_paths = []
            for name in archive_names:
                matches = sorted(root.rglob(name), key=lambda path: str(path).casefold())
                archive_paths.extend(matches)
        if not archive_paths:
            raise FileNotFoundError(f"no BIG archives found beneath {root}")

        archives: list[ArchiveInfo] = []
        entries: list[CatalogEntry] = []
        unique_paths: dict[str, Path] = {}
        for path in archive_paths:
            relative = path.relative_to(root).as_posix()
            unique_paths.setdefault(relative.casefold(), path)
        ordered = sorted(
            unique_paths.values(),
            key=lambda path: archive_precedence(path.relative_to(root).as_posix()),
        )
        for precedence, path in enumerate(ordered):
            relative = path.relative_to(root).as_posix()
            parsed = BigArchive.open(path)
            stat = path.stat()
            archives.append(
                ArchiveInfo(
                    relative_path=relative,
                    size=stat.st_size,
                    mtime_ns=stat.st_mtime_ns,
                    magic=parsed.magic,
                    header_size=parsed.header_size,
                    entry_count=len(parsed.entries),
                    directory_sha256=_archive_directory_sha256(parsed, relative, precedence),
                )
            )
            entries.extend(
                CatalogEntry(relative, item.name, item.offset, item.size, precedence)
                for item in parsed.entries
            )
        return cls(root, tuple(archives), tuple(entries))

    @classmethod
    def load(cls, path: Path | str) -> "InstallCatalog":
        value = read_json(Path(path))
        if not isinstance(value, dict):
            raise ValueError("catalog root must be an object")
        if value.get("format") != cls.FORMAT:
            raise ValueError(f"unsupported catalog format: {value.get('format')!r}")
        archives = tuple(ArchiveInfo(**item) for item in value["archives"])
        entries = tuple(CatalogEntry(**item) for item in value["entries"])
        canonical_precedence = {
            archive.relative_path.casefold(): precedence
            for precedence, archive in enumerate(
                sorted(archives, key=lambda item: archive_precedence(item.relative_path))
            )
        }
        archive_names: set[str] = set()
        for archive in archives:
            safe_relative_parts(archive.relative_path)
            key = archive.relative_path.casefold()
            if key in archive_names:
                raise ValueError(f"duplicate catalog archive: {archive.relative_path}")
            archive_names.add(key)
            if archive.size < 16 or archive.header_size < 16 or archive.entry_count < 0:
                raise ValueError(f"invalid catalog archive metadata: {archive.relative_path}")
            if not re.fullmatch(r"[0-9a-f]{64}", archive.directory_sha256.casefold()):
                raise ValueError(f"invalid catalog directory digest: {archive.relative_path}")
        for entry in entries:
            safe_relative_parts(entry.archive)
            safe_relative_parts(entry.name)
            if entry.archive.casefold() not in archive_names:
                raise ValueError(f"catalog entry refers to unknown archive: {entry.archive}")
            if entry.offset < 0 or entry.size < 0 or entry.precedence < 0:
                raise ValueError(f"invalid catalog entry metadata: {entry.name}")
            expected_precedence = canonical_precedence[entry.archive.casefold()]
            if entry.precedence != expected_precedence:
                raise ValueError(
                    "catalog entry precedence does not match canonical archive order: "
                    f"{entry.archive}:{entry.name}"
                )
        return cls(Path(value["install_root"]), archives, entries)

    def save(self, path: Path | str) -> None:
        value = {
            "format": self.FORMAT,
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "install_root": str(self.install_root),
            "archives": [asdict(item) for item in self.archives],
            "entries": [asdict(item) for item in self.entries],
        }
        write_json_atomic(Path(path), value)

    def stale_reasons(self) -> list[str]:
        reasons: list[str] = []
        canonical_precedence = {
            archive.relative_path.casefold(): precedence
            for precedence, archive in enumerate(
                sorted(self.archives, key=lambda item: archive_precedence(item.relative_path))
            )
        }
        indexed = {archive.relative_path.casefold() for archive in self.archives}
        current = {
            path.relative_to(self.install_root).as_posix().casefold()
            for path in self.install_root.rglob("*.big")
        }
        for relative in sorted(current - indexed):
            reasons.append(f"new archive: {relative}")
        for relative in sorted(indexed - current):
            reasons.append(f"missing archive: {relative}")
        for archive in self.archives:
            path = self.install_root / Path(archive.relative_path)
            if not path.is_file():
                reasons.append(f"missing archive: {archive.relative_path}")
                continue
            stat = path.stat()
            if stat.st_size != archive.size or stat.st_mtime_ns != archive.mtime_ns:
                reasons.append(f"changed archive: {archive.relative_path}")
                continue
            try:
                current_directory = _archive_directory_sha256(
                    BigArchive.open(path),
                    archive.relative_path,
                    canonical_precedence[archive.relative_path.casefold()],
                )
            except (OSError, ValueError) as exc:
                reasons.append(f"unreadable archive: {archive.relative_path}: {exc}")
                continue
            if current_directory != archive.directory_sha256:
                reasons.append(f"changed archive directory: {archive.relative_path}")
                continue
            catalog_entries = tuple(
                entry
                for entry in self.entries
                if entry.archive.casefold() == archive.relative_path.casefold()
            )
            if (
                len(catalog_entries) != archive.entry_count
                or _directory_sha256(
                    archive.magic,
                    archive.header_size,
                    catalog_entries,
                    relative_path=archive.relative_path,
                    precedence=canonical_precedence[archive.relative_path.casefold()],
                )
                != archive.directory_sha256
            ):
                reasons.append(f"catalog directory mismatch: {archive.relative_path}")
        return reasons

    def resolve_exact(self, virtual_path: str) -> CatalogEntry | None:
        values = self._by_key.get(virtual_path.replace("\\", "/").casefold(), ())
        return values[0] if values else None

    def search(self, pattern: str) -> list[CatalogEntry]:
        """Case-insensitive PurePath-style glob across selected winners."""
        normalized = pattern.replace("\\", "/").casefold()
        winners = (values[0] for values in self._by_key.values())
        return sorted(
            (entry for entry in winners if Path(entry.name.casefold()).match(normalized)),
            key=lambda item: (item.name.casefold(), item.archive.casefold()),
        )

    def open_archive_for(self, entry: CatalogEntry) -> BigArchive:
        archive = BigArchive.open(self.install_root / Path(entry.archive))
        expected = (entry.name, entry.offset, entry.size)
        actual = {(item.name, item.offset, item.size) for item in archive.entries}
        if expected not in actual:
            raise ValueError(
                f"catalog entry no longer matches the BIG directory: {entry.archive}:{entry.name}"
            )
        return archive

    def as_entry(self, entry: CatalogEntry) -> BigEntry:
        return BigEntry(entry.name, entry.offset, entry.size)


def _windows_version_info(path: Path) -> dict[str, str]:
    if os.name != "nt" or not path.is_file():
        return {}
    environment = os.environ.copy()
    environment["OPENBFME_VERSION_PATH"] = str(path)
    script = (
        "$v=(Get-Item -LiteralPath $env:OPENBFME_VERSION_PATH).VersionInfo;"
        "[pscustomobject]@{CompanyName=$v.CompanyName;FileVersion=$v.FileVersion;"
        "ProductVersion=$v.ProductVersion}|ConvertTo-Json -Compress"
    )
    try:
        result = subprocess.run(
            ["powershell.exe", "-NoLogo", "-NoProfile", "-Command", script],
            capture_output=True,
            text=True,
            check=False,
            timeout=15,
            env=environment,
        )
        return json.loads(result.stdout) if result.returncode == 0 and result.stdout.strip() else {}
    except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError):
        return {}


def doctor_install(install_root: Path | str, *, deep: bool = False) -> dict[str, Any]:
    root = Path(install_root).expanduser().resolve()
    archive_status = []
    for name in CORE_ARCHIVES:
        path = root / name
        archive_status.append(
            {"name": name, "present": path.is_file(), "size": path.stat().st_size if path.is_file() else 0}
        )
    patches = [name for name in PATCH_ARCHIVES if (root / name).is_file()]
    languages = sorted(path.name for path in (root / "lang").glob("*Audio.big")) if (root / "lang").is_dir() else []
    missing_required = [item["name"] for item in archive_status[:3] if not item["present"]]
    patch_document = root / "patch.doc"
    patch_text = patch_document.read_bytes().decode("latin-1", errors="ignore") if patch_document.is_file() else ""
    game_binary = root / "game.dat"
    version_info = _windows_version_info(game_binary)
    company_name = str(version_info.get("CompanyName", ""))
    modified_marker = "DEV!ANCE" in company_name.upper()
    result = {
        "install_root": str(root),
        "install_present": root.is_dir(),
        "executable_present": (root / "lotrbfme2.exe").is_file(),
        "patch_archives": patches,
        "declared_patch": "1.06" if "1.06" in patch_text else "unknown",
        "languages": languages,
        "executable_attestation": {
            "trusted": False,
            "modified_marker_detected": modified_marker,
            "company_name": company_name,
            "file_version": str(version_info.get("FileVersion", "")),
            "product_version": str(version_info.get("ProductVersion", "")),
            "note": "archive hashes, not executable metadata, attest importer inputs",
        },
        "archives": archive_status,
        "ready": root.is_dir() and (root / "lotrbfme2.exe").is_file() and not missing_required,
        "missing_required": missing_required,
    }
    if deep:
        attestations: list[dict[str, Any]] = []
        modified: list[str] = []
        for relative, expected in KNOWN_SLICE_ARCHIVE_SHA256.items():
            path = root / Path(relative)
            actual = sha256_file(path) if path.is_file() else ""
            matches = actual.casefold() == expected
            attestations.append(
                {
                    "archive": relative,
                    "present": path.is_file(),
                    "sha256": actual,
                    "expected_sha256": expected,
                    "matches_reference": matches,
                }
            )
            if not matches:
                modified.append(relative)
        result["archive_attestation"] = attestations
        result["modified_or_missing_slice_archives"] = modified
        result["ready"] = bool(result["ready"] and not modified)
    return result
