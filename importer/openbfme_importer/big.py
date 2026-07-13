"""Safe streaming reader for EA SAGE BIGF/BIG4 archives.

This module deliberately never reads an entire archive into memory.  It also
validates every archive path before extraction so a malformed retail or test
archive cannot escape the selected cache root.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import struct
from typing import BinaryIO, Iterable, Iterator

from .paths import safe_relative_parts


SUPPORTED_MAGIC = {b"BIGF", b"BIG4"}
MAX_ENTRY_COUNT = 1_000_000
MAX_NAME_BYTES = 32_768
COPY_CHUNK = 1024 * 1024


class BigFormatError(ValueError):
    """The input is not a structurally valid BIG archive."""


class ExtractionLimitError(RuntimeError):
    """A configured extraction sandbox limit would be exceeded."""


@dataclass(frozen=True, slots=True)
class BigEntry:
    name: str
    offset: int
    size: int

    @property
    def key(self) -> str:
        return self.name.casefold()


@dataclass(frozen=True, slots=True)
class ExtractedEntry:
    entry: BigEntry
    output: Path
    sha256: str


def normalize_archive_name(raw: str) -> str:
    """Return a stable POSIX archive path or raise for an unsafe one."""
    value = raw.replace("\\", "/")
    if not value or "\x00" in value:
        raise BigFormatError("archive entry has an empty or NUL-containing path")
    try:
        parts = safe_relative_parts(value)
    except ValueError as exc:
        raise BigFormatError(f"archive entry uses an unsafe path: {raw!r}") from exc
    return "/".join(parts)


def _read_exact(stream: BinaryIO, count: int, context: str) -> bytes:
    data = stream.read(count)
    if len(data) != count:
        raise BigFormatError(f"unexpected EOF while reading {context}")
    return data


def _read_cstring(stream: BinaryIO, header_end: int) -> str:
    out = bytearray()
    while stream.tell() < header_end and len(out) <= MAX_NAME_BYTES:
        value = stream.read(1)
        if not value:
            break
        if value == b"\x00":
            try:
                decoded = out.decode("latin-1")
            except UnicodeDecodeError as exc:  # latin-1 should be total
                raise BigFormatError("invalid BIG entry name") from exc
            return normalize_archive_name(decoded)
        out.extend(value)
    if len(out) > MAX_NAME_BYTES:
        raise BigFormatError("BIG entry name exceeds safety limit")
    raise BigFormatError("unterminated BIG entry name")


class BigArchive:
    """Validated archive metadata plus bounded streaming extraction."""

    def __init__(
        self,
        path: Path,
        *,
        magic: str,
        declared_size: int,
        header_size: int,
        entries: tuple[BigEntry, ...],
    ) -> None:
        self.path = path
        self.magic = magic
        self.declared_size = declared_size
        self.header_size = header_size
        self.entries = entries

    @classmethod
    def open(cls, path: Path | str) -> "BigArchive":
        archive_path = Path(path).expanduser().resolve()
        actual_size = archive_path.stat().st_size
        if actual_size < 16:
            raise BigFormatError(f"{archive_path} is too small to be a BIG archive")
        with archive_path.open("rb") as stream:
            header = _read_exact(stream, 16, "BIG header")
            magic_raw = header[:4]
            if magic_raw not in SUPPORTED_MAGIC:
                raise BigFormatError(f"unsupported BIG magic {magic_raw!r}")
            declared_size = struct.unpack_from("<I", header, 4)[0]
            file_count = struct.unpack_from(">I", header, 8)[0]
            header_size = struct.unpack_from(">I", header, 12)[0]
            if file_count > MAX_ENTRY_COUNT:
                raise BigFormatError(f"BIG entry count {file_count} exceeds safety limit")
            if header_size < 16 or header_size > actual_size:
                raise BigFormatError(
                    f"BIG header size {header_size} is outside file size {actual_size}"
                )
            if declared_size not in (0, actual_size):
                raise BigFormatError(
                    f"BIG declared size {declared_size} does not match file size {actual_size}"
                )

            entries: list[BigEntry] = []
            for entry_number in range(file_count):
                if stream.tell() + 9 > header_size:
                    raise BigFormatError(
                        f"BIG header ended before entry {entry_number + 1}/{file_count}"
                    )
                record = _read_exact(stream, 8, f"entry {entry_number} record")
                offset, size = struct.unpack(">II", record)
                name = _read_cstring(stream, header_size)
                if offset < header_size or offset + size > actual_size:
                    raise BigFormatError(
                        f"BIG entry {name!r} range {offset}:{offset + size} is invalid"
                    )
                entries.append(BigEntry(name=name, offset=offset, size=size))
            if stream.tell() > header_size:
                raise BigFormatError("BIG directory crossed its declared header boundary")

        return cls(
            archive_path,
            magic=magic_raw.decode("ascii"),
            declared_size=declared_size or actual_size,
            header_size=header_size,
            entries=tuple(entries),
        )

    def find_exact(self, names: Iterable[str]) -> list[BigEntry]:
        wanted = {normalize_archive_name(name).casefold() for name in names}
        return [entry for entry in self.entries if entry.key in wanted]

    def iter_prefix(self, prefixes: Iterable[str]) -> Iterator[BigEntry]:
        normalized = tuple(
            normalize_archive_name(prefix.rstrip("/")).casefold() + "/"
            for prefix in prefixes
        )
        for entry in self.entries:
            if entry.key.startswith(normalized):
                yield entry

    def read_entry(self, entry: BigEntry, *, max_bytes: int) -> bytes:
        """Read one revalidated bounded entry without creating an extraction file."""

        if max_bytes < 0:
            raise ValueError("max_bytes cannot be negative")
        actual_entries = {
            (item.name, item.offset, item.size) for item in self.entries
        }
        if (entry.name, entry.offset, entry.size) not in actual_entries:
            raise BigFormatError(
                f"selected entry is not present in the reopened BIG directory: {entry.name!r}"
            )
        if entry.size > max_bytes:
            raise ExtractionLimitError(
                f"entry has {entry.size} bytes; limit is {max_bytes}"
            )
        with self.path.open("rb") as source:
            source.seek(entry.offset)
            payload = source.read(entry.size)
        if len(payload) != entry.size:
            raise BigFormatError(
                f"unexpected EOF reading {entry.name!r}: got {len(payload)} of {entry.size} bytes"
            )
        return payload

    def extract(
        self,
        entries: Iterable[BigEntry],
        output_root: Path | str,
        *,
        max_files: int = 10_000,
        max_bytes: int = 2 * 1024 * 1024 * 1024,
        overwrite: bool = False,
    ) -> list[ExtractedEntry]:
        selected = tuple(entries)
        actual_entries = {
            (entry.name, entry.offset, entry.size) for entry in self.entries
        }
        target_keys: set[str] = set()
        for entry in selected:
            if (entry.name, entry.offset, entry.size) not in actual_entries:
                raise BigFormatError(
                    f"selected entry is not present in the reopened BIG directory: {entry.name!r}"
                )
            key = entry.name.casefold()
            if key in target_keys:
                raise BigFormatError(
                    f"selection contains a case-colliding output path: {entry.name}"
                )
            target_keys.add(key)
        total_bytes = sum(entry.size for entry in selected)
        if len(selected) > max_files:
            raise ExtractionLimitError(
                f"selection has {len(selected)} files; limit is {max_files}"
            )
        if total_bytes > max_bytes:
            raise ExtractionLimitError(
                f"selection has {total_bytes} bytes; limit is {max_bytes}"
            )

        root = Path(output_root).expanduser().resolve()
        root.mkdir(parents=True, exist_ok=True)
        results: list[ExtractedEntry] = []
        with self.path.open("rb") as source:
            for entry in selected:
                relative = Path(*PurePosixPath(entry.name).parts)
                target = (root / relative).resolve()
                try:
                    target.relative_to(root)
                except ValueError as exc:
                    raise BigFormatError(f"entry escaped extraction root: {entry.name}") from exc
                target.parent.mkdir(parents=True, exist_ok=True)
                if target.exists() and not overwrite:
                    if target.stat().st_size != entry.size:
                        raise FileExistsError(
                            f"cached extraction size mismatch; use --force: {target}"
                        )
                    cached_digest = sha256_file(target)
                    source.seek(entry.offset)
                    source_digest = hashlib.sha256()
                    remaining = entry.size
                    while remaining:
                        chunk = source.read(min(COPY_CHUNK, remaining))
                        if not chunk:
                            raise BigFormatError(
                                f"unexpected EOF verifying cached {entry.name!r}"
                            )
                        source_digest.update(chunk)
                        remaining -= len(chunk)
                    current_digest = source_digest.hexdigest()
                    if cached_digest != current_digest:
                        raise FileExistsError(
                            f"cached extraction hash mismatch; use --force: {target}"
                        )
                    results.append(ExtractedEntry(entry, target, current_digest))
                    continue

                temp = target.with_name(target.name + ".openbfme-part")
                hasher = hashlib.sha256()
                remaining = entry.size
                source.seek(entry.offset)
                try:
                    with temp.open("wb") as destination:
                        while remaining:
                            chunk = source.read(min(COPY_CHUNK, remaining))
                            if not chunk:
                                raise BigFormatError(
                                    f"unexpected EOF extracting {entry.name!r}"
                                )
                            destination.write(chunk)
                            hasher.update(chunk)
                            remaining -= len(chunk)
                        destination.flush()
                        os.fsync(destination.fileno())
                    os.replace(temp, target)
                finally:
                    temp.unlink(missing_ok=True)
                results.append(ExtractedEntry(entry, target, hasher.hexdigest()))
        return results


def sha256_file(path: Path | str) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        while chunk := stream.read(COPY_CHUNK):
            digest.update(chunk)
    return digest.hexdigest()
