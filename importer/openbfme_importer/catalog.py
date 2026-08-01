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
from typing import Any, Iterable, Mapping

from .big import BigArchive, BigEntry
from .big import sha256_file
from .game import RETAIL_GAMES, retail_game
from .paths import safe_relative_parts
from .util import read_json, write_json_atomic


PATCH_ARCHIVES = (
    *RETAIL_GAMES["rotwk"].patch_archives,
    *RETAIL_GAMES["bfme2"].patch_archives,
)
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
    # BFME2 1.06 patch103 (fortress hierarchical models, HUD fonts). Hashed from
    # a clean 1.06 tree at <install>/_patch103.big (12_865_477 bytes).
    "_patch103.big": "4b9057b8c49053802797e22a171b378678f3b8f45edc9563c6480b254b968a1a",
    # Script libraries BIG used by men-fords AI / map script import.
    "libraries.big": "d6bf62be5d8b690226a3d0e0d1a6f7dcce0000482ade6b3d970620202023fd67",
}

class CatalogProvenanceError(RuntimeError):
    """A catalog does not demonstrably come from the requested retail game.

    Raised instead of silently rebuilding or silently accepting a catalog whose
    contents belong to a different edition. Carries a structured ``diagnostic``
    so ``--json`` callers get a machine-readable failure rather than content
    that quietly came from the wrong game.
    """

    def __init__(self, diagnostic: Mapping[str, Any]) -> None:
        self.diagnostic = dict(diagnostic)
        super().__init__(str(self.diagnostic.get("message", "catalog provenance check failed")))


def catalog_provenance_reason(
    archive_paths: Iterable[str], game: str
) -> str | None:
    """Return why ``archive_paths`` contradict ``game``, else ``None``.

    Rejects only on *affirmative contradiction*: the tree carries another
    edition's patch archives and none of the requested edition's own. Matching
    is by basename because a layered RotWK install keeps its patch archives in
    per-layer subdirectories (``layer-0-rotwk/_patch201.big``).

    Deliberately conservative, in both directions:

    * Absence of evidence is not rejected. A freshly installed, never-patched
      retail tree carries no patch archive at all, and refusing it would be a
      false positive on legitimate retail input.
    * A RotWK tree legitimately layers BFME2 and therefore *does* contain BFME2
      patch archives, so BFME2 evidence alone never rejects.

    What it does catch is the dangerous direction this project keeps hitting: a
    BFME2 catalog served under the RotWK name. That tree carries 1.x patch
    archives and no 2.x archive, so it is refused rather than silently
    supplying BFME2 content to a RotWK request.
    """

    definition = retail_game(game)
    present = {Path(item).name.casefold() for item in archive_paths}
    own = {name.casefold() for name in definition.patch_archives}
    if own & present:
        return None
    foreign: dict[str, set[str]] = {}
    for other_id, other in RETAIL_GAMES.items():
        if other_id == definition.id:
            continue
        matched = {name.casefold() for name in other.patch_archives} & present
        if matched:
            foreign[other.display_name] = matched
    if not foreign:
        return None
    detail = "; ".join(
        f"{name}: {sorted(matched)}" for name, matched in sorted(foreign.items())
    )
    return (
        f"catalog carries no {definition.display_name} patch archive but does "
        f"carry another edition's ({detail})"
    )


ARCHIVE_POLICY_SCHEMA = "openbfme.retail-archive-policy"
ARCHIVE_POLICY_VERSION = 1
DEFAULT_BFME2_ARCHIVE_POLICY = (
    Path(__file__).resolve().parents[2]
    / "contracts"
    / "bfme2-106-english-archives.json"
)


def _md5_file(path: Path) -> str:
    digest = hashlib.md5(usedforsecurity=False)
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


@dataclass(frozen=True, slots=True)
class ArchivePolicyMember:
    path: str
    md5: str
    size: int
    language: str


@dataclass(frozen=True, slots=True)
class ArchivePolicy:
    game: str
    patch: str
    package_guid: str
    package_name: str
    package_version: str
    receipt_sha256: str
    language: str
    policy_sha256: str
    archives: tuple[ArchivePolicyMember, ...]

    @classmethod
    def _from_value(
        cls, value: Mapping[str, Any], *, label: str
    ) -> "ArchivePolicy":
        if not isinstance(value, dict) or set(value) != {
            "schema",
            "schemaVersion",
            "game",
            "patch",
            "package",
            "language",
            "policySha256",
            "archives",
        }:
            raise ValueError(f"{label} root is invalid")
        if (
            value.get("schema") != ARCHIVE_POLICY_SCHEMA
            or value.get("schemaVersion") != ARCHIVE_POLICY_VERSION
        ):
            raise ValueError(f"{label} schema is unsupported")
        package = value.get("package")
        if not isinstance(package, dict) or set(package) != {
            "guid",
            "name",
            "version",
            "receiptSha256",
        }:
            raise ValueError(f"{label} package identity is invalid")
        raw_archives = value.get("archives")
        if not isinstance(raw_archives, list) or not raw_archives:
            raise ValueError(f"{label} must contain archive members")
        members: list[ArchivePolicyMember] = []
        keys: set[str] = set()
        for index, raw in enumerate(raw_archives):
            if not isinstance(raw, dict) or set(raw) != {
                "path",
                "md5",
                "size",
                "language",
            }:
                raise ValueError(f"{label} member {index} is invalid")
            member_path = raw.get("path")
            member_md5 = raw.get("md5")
            member_size = raw.get("size")
            member_language = raw.get("language")
            if not isinstance(member_path, str):
                raise ValueError(f"{label} member {index} path is invalid")
            canonical = "/".join(safe_relative_parts(member_path))
            if canonical != member_path or not canonical.casefold().endswith(".big"):
                raise ValueError(f"{label} member path is not canonical: {member_path!r}")
            key = canonical.casefold()
            if key in keys:
                raise ValueError(f"duplicate {label} member: {member_path}")
            keys.add(key)
            if (
                not isinstance(member_md5, str)
                or not re.fullmatch(r"[0-9a-f]{32}", member_md5)
                or isinstance(member_size, bool)
                or not isinstance(member_size, int)
                or member_size < 16
                or not isinstance(member_language, str)
                or not member_language
            ):
                raise ValueError(f"{label} member metadata is invalid: {member_path}")
            members.append(
                ArchivePolicyMember(canonical, member_md5, member_size, member_language)
            )
        members.sort(key=lambda item: item.path.casefold())
        policy_sha256 = value.get("policySha256")
        canonical_rows = "".join(
            f"{item.path}|{item.md5}|{item.size}|{item.language}\n" for item in members
        )
        actual_policy_sha256 = hashlib.sha256(canonical_rows.encode("utf-8")).hexdigest()
        if policy_sha256 != actual_policy_sha256:
            raise ValueError(f"{label} digest does not match its members")
        receipt_sha256 = package.get("receiptSha256")
        strings = {
            "game": value.get("game"),
            "patch": value.get("patch"),
            "package_guid": package.get("guid"),
            "package_name": package.get("name"),
            "package_version": package.get("version"),
            "receipt_sha256": receipt_sha256,
            "language": value.get("language"),
        }
        if any(not isinstance(item, str) or not item for item in strings.values()):
            raise ValueError(f"{label} identity fields are invalid")
        if not re.fullmatch(r"[0-9a-f]{64}", str(receipt_sha256)):
            raise ValueError(f"{label} receipt digest is invalid")
        return cls(
            **strings,
            policy_sha256=actual_policy_sha256,
            archives=tuple(members),
        )

    @classmethod
    def load(cls, path: Path | str) -> "ArchivePolicy":
        value = read_json(Path(path))
        if not isinstance(value, dict):
            raise ValueError("archive policy root is invalid")
        return cls._from_value(value, label="archive policy")

    def identity_record(self) -> dict[str, Any]:
        return {
            "schema": ARCHIVE_POLICY_SCHEMA,
            "schema_version": ARCHIVE_POLICY_VERSION,
            "game": self.game,
            "patch": self.patch,
            "package_guid": self.package_guid,
            "package_name": self.package_name,
            "package_version": self.package_version,
            "receipt_sha256": self.receipt_sha256,
            "language": self.language,
            "policy_sha256": self.policy_sha256,
        }

    def serialized(self) -> dict[str, Any]:
        return {
            "schema": ARCHIVE_POLICY_SCHEMA,
            "schemaVersion": ARCHIVE_POLICY_VERSION,
            "game": self.game,
            "patch": self.patch,
            "package": {
                "guid": self.package_guid,
                "name": self.package_name,
                "version": self.package_version,
                "receiptSha256": self.receipt_sha256,
            },
            "language": self.language,
            "policySha256": self.policy_sha256,
            "archives": [asdict(item) for item in self.archives],
        }

    @classmethod
    def from_serialized(cls, value: Mapping[str, Any]) -> "ArchivePolicy":
        return cls._from_value(value, label="serialized archive policy")


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


_LAYER_DIRECTORY = re.compile(r"layer-(\d{1,4})(?:-[a-z0-9]+)?", re.IGNORECASE)


def archive_precedence(relative_path: str) -> tuple[int, int, str]:
    """Lower tuples win when duplicate virtual paths exist.

    The leading component is the install layer: an expansion install mounts
    after (and therefore shadows) its base game, so a layered install root
    whose top-level directories are named ``layer-<n>[-label]`` (junctions to
    the real installs; the same naming the edition overlay uses) ranks every
    layer-0 archive above every layer-1 archive regardless of archive family.
    Plain single-install roots have no such directory and stay in layer 0 —
    their ordering is unchanged.
    """

    layer = 0
    first = relative_path.split("/", 1)[0]
    matched = _LAYER_DIRECTORY.fullmatch(first)
    if matched is not None:
        layer = int(matched.group(1))
    name = Path(relative_path).name.casefold()
    patch_names = [value.casefold() for value in PATCH_ARCHIVES]
    if name in patch_names:
        return layer, patch_names.index(name), relative_path.casefold()
    if "patch" in name and name.endswith(".big"):
        # Language patch archives use names such as EnglishPatch105.big.
        return layer, 50, relative_path.casefold()
    # EA loads underscore override archives before normal data archives.
    if name.startswith("_"):
        return layer, 100, relative_path.casefold()
    return layer, 1000, relative_path.casefold()


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


def _payload_sample_sha256(path: Path) -> str:
    """Cheap content canary: size + head/mid/tail samples (not full archive MD5)."""

    stat = path.stat()
    size = int(stat.st_size)
    digest = hashlib.sha256()
    digest.update(str(size).encode("ascii"))
    sample = 256 * 1024
    with path.open("rb") as stream:
        digest.update(stream.read(sample))
        if size > sample * 2:
            stream.seek(max(0, size // 2 - sample // 2))
            digest.update(stream.read(sample))
        if size > sample:
            stream.seek(max(0, size - sample))
            digest.update(stream.read(sample))
    return digest.hexdigest()


class InstallCatalog:
    FORMAT = 4

    def __init__(
        self,
        install_root: Path,
        archives: tuple[ArchiveInfo, ...],
        entries: tuple[CatalogEntry, ...],
        source_policy: ArchivePolicy | None = None,
        payload_samples: Mapping[str, str] | None = None,
    ) -> None:
        self.install_root = install_root.resolve()
        self.archives = archives
        self.entries = entries
        self.source_policy = source_policy
        self.payload_samples = {
            key.casefold(): value.casefold()
            for key, value in (payload_samples or {}).items()
            if isinstance(key, str) and isinstance(value, str)
        }
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
        source_policy: ArchivePolicy | None = None,
    ) -> "InstallCatalog":
        root = Path(install_root).expanduser().resolve()
        if not root.is_dir():
            raise FileNotFoundError(f"BFME II install directory not found: {root}")
        if archive_names is not None and source_policy is not None:
            raise ValueError("archive_names and source_policy are mutually exclusive")
        if source_policy is not None:
            discovered: dict[str, list[Path]] = {}
            for path in root.rglob("*.big"):
                relative = path.relative_to(root).as_posix()
                key = relative.casefold()
                discovered.setdefault(key, []).append(path)
            archive_paths = []
            for member in source_policy.archives:
                matches = discovered.get(member.path.casefold(), [])
                if not matches:
                    raise FileNotFoundError(
                        f"archive policy member is missing: {member.path}"
                    )
                if len(matches) != 1:
                    raise ValueError(
                        f"case-colliding archive policy member: {member.path}"
                    )
                path = matches[0]
                if path.stat().st_size != member.size:
                    raise ValueError(f"archive policy member size changed: {member.path}")
                if _md5_file(path) != member.md5:
                    raise ValueError(f"archive policy member digest changed: {member.path}")
                archive_paths.append(path)
        elif archive_names is None:
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
        samples = {
            archive.relative_path: _payload_sample_sha256(
                root / Path(archive.relative_path)
            )
            for archive in archives
        }
        return cls(
            root,
            tuple(archives),
            tuple(entries),
            source_policy,
            payload_samples=samples,
        )

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
        raw_policy = value.get("source_policy")
        source_policy = None
        if raw_policy is not None:
            if not isinstance(raw_policy, dict):
                raise ValueError("catalog source policy is invalid")
            source_policy = ArchivePolicy.from_serialized(raw_policy)
        raw_samples = value.get("payload_samples")
        samples: dict[str, str] = {}
        if isinstance(raw_samples, dict):
            for key, sample in raw_samples.items():
                if (
                    isinstance(key, str)
                    and isinstance(sample, str)
                    and re.fullmatch(r"[0-9a-f]{64}", sample.casefold())
                ):
                    samples[key] = sample.casefold()
        catalog = cls(
            Path(value["install_root"]),
            archives,
            entries,
            source_policy,
            payload_samples=samples,
        )
        if source_policy is not None:
            expected = {item.path.casefold() for item in source_policy.archives}
            actual = {item.relative_path.casefold() for item in archives}
            if actual != expected:
                raise ValueError("catalog archive membership does not match source policy")
        return catalog

    def save(self, path: Path | str) -> None:
        # Refresh samples at save so re-index always pins current canaries.
        samples = {
            archive.relative_path: _payload_sample_sha256(
                self.install_root / Path(archive.relative_path)
            )
            for archive in self.archives
            if (self.install_root / Path(archive.relative_path)).is_file()
        }
        self.payload_samples = {
            key.casefold(): value.casefold() for key, value in samples.items()
        }
        value = {
            "format": self.FORMAT,
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "install_root": str(self.install_root),
            "archives": [asdict(item) for item in self.archives],
            "entries": [asdict(item) for item in self.entries],
            "payload_samples": samples,
        }
        if self.source_policy is not None:
            value["source_policy"] = self.source_policy.serialized()
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
        if self.source_policy is None:
            current = {
                path.relative_to(self.install_root).as_posix().casefold()
                for path in self.install_root.rglob("*.big")
            }
            for relative in sorted(current - indexed):
                reasons.append(f"new archive: {relative}")
        else:
            current = indexed
        for relative in sorted(indexed - current):
            reasons.append(f"missing archive: {relative}")
        deep = os.environ.get("OPENBFME_CATALOG_DEEP", "").strip().casefold() in {
            "1",
            "true",
            "yes",
        }
        for archive in self.archives:
            path = self.install_root / Path(archive.relative_path)
            if not path.is_file():
                reasons.append(f"missing archive: {archive.relative_path}")
                continue
            stat = path.stat()
            if stat.st_size != archive.size or stat.st_mtime_ns != archive.mtime_ns:
                reasons.append(f"changed archive: {archive.relative_path}")
                continue
            # Fast path: size+mtime already matched. Re-parse/MD5 the full 4GB
            # policy set only when OPENBFME_CATALOG_DEEP=1 (or --deep tooling).
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
                continue
            if self.source_policy is not None:
                members = {
                    item.path.casefold(): item for item in self.source_policy.archives
                }
                member = members.get(archive.relative_path.casefold())
                if member is None:
                    reasons.append(
                        f"archive is outside source policy: {archive.relative_path}"
                    )
                    continue
                if deep and _md5_file(path) != member.md5:
                    reasons.append(
                        f"changed archive payload: {archive.relative_path}"
                    )
                    continue
            # Fast content canary: head/mid/tail sample. Catches many bit-flips
            # without reading the full multi-GB archive set every CLI invoke.
            expected_sample = self.payload_samples.get(archive.relative_path.casefold())
            if expected_sample:
                try:
                    actual_sample = _payload_sample_sha256(path)
                except OSError as exc:
                    reasons.append(
                        f"unreadable archive sample: {archive.relative_path}: {exc}"
                    )
                    continue
                if actual_sample != expected_sample:
                    reasons.append(
                        f"changed archive payload sample: {archive.relative_path}"
                    )
                    continue
            if deep:
                try:
                    current_directory = _archive_directory_sha256(
                        BigArchive.open(path),
                        archive.relative_path,
                        canonical_precedence[archive.relative_path.casefold()],
                    )
                except (OSError, ValueError) as exc:
                    reasons.append(
                        f"unreadable archive: {archive.relative_path}: {exc}"
                    )
                    continue
                if current_directory != archive.directory_sha256:
                    reasons.append(
                        f"changed archive directory: {archive.relative_path}"
                    )
                    continue
        return reasons

    @property
    def source_policy_sha256(self) -> str | None:
        return self.source_policy.policy_sha256 if self.source_policy is not None else None

    def identity_sha256(self) -> str:
        # Catalogs are immutable after build/load; memoize the expensive
        # full-entry serialization so convert loops do not rehash ~50k rows.
        cached = getattr(self, "_identity_sha256_memo", None)
        if isinstance(cached, str) and len(cached) == 64:
            return cached
        archives = [
            {
                "relative_path": item.relative_path,
                "size": item.size,
                "magic": item.magic,
                "header_size": item.header_size,
                "entry_count": item.entry_count,
                "directory_sha256": item.directory_sha256,
            }
            for item in sorted(
                self.archives,
                key=lambda item: (item.relative_path.casefold(), item.relative_path),
            )
        ]
        entries = [
            asdict(item)
            for item in sorted(
                self.entries,
                key=lambda item: (
                    item.precedence,
                    item.archive.casefold(),
                    item.archive,
                    item.name.casefold(),
                    item.name,
                    item.offset,
                    item.size,
                ),
            )
        ]
        payload = {
            "format": self.FORMAT,
            "archives": archives,
            "entries": entries,
        }
        if self.source_policy is not None:
            payload["source_policy"] = self.source_policy.identity_record()
        encoded = json.dumps(
            payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8")
        digest = hashlib.sha256(encoded).hexdigest()
        object.__setattr__(self, "_identity_sha256_memo", digest)
        return digest

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
        # Reuse one BigArchive directory parse per archive path for the life of
        # this catalog instance. Faction census used to re-open ini.big per doc.
        cache = getattr(self, "_archive_handle_cache", None)
        if cache is None:
            self._archive_handle_cache: dict[str, BigArchive] = {}
            cache = self._archive_handle_cache
        key = entry.archive.casefold()
        archive = cache.get(key)
        if archive is None:
            archive = BigArchive.open(self.install_root / Path(entry.archive))
            cache[key] = archive
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


def doctor_install(
    install_root: Path | str,
    *,
    deep: bool = False,
    game: str = "bfme2",
) -> dict[str, Any]:
    definition = retail_game(game)
    root = Path(install_root).expanduser().resolve()
    archive_status = []
    for name in CORE_ARCHIVES:
        path = root / name
        archive_status.append(
            {"name": name, "present": path.is_file(), "size": path.stat().st_size if path.is_file() else 0}
        )
    patches = [name for name in definition.patch_archives if (root / name).is_file()]
    languages = sorted(path.name for path in (root / "lang").glob("*Audio.big")) if (root / "lang").is_dir() else []
    missing_required = [item["name"] for item in archive_status[:3] if not item["present"]]
    patch_document = root / "patch.doc"
    patch_text = patch_document.read_bytes().decode("latin-1", errors="ignore") if patch_document.is_file() else ""
    game_binary = root / "game.dat"
    version_info = _windows_version_info(game_binary)
    company_name = str(version_info.get("CompanyName", ""))
    modified_marker = "DEV!ANCE" in company_name.upper()
    executable = next(
        (root / name for name in definition.executable_names if (root / name).is_file()),
        root / definition.executable_names[0],
    )
    declared_patch = next(
        (marker for marker in definition.patch_markers if marker in patch_text),
        "unknown",
    )
    if declared_patch == "unknown":
        declared_patch = next(
            (
                marker
                for archive, marker in zip(
                    definition.patch_archives, definition.patch_markers
                )
                if archive in patches
            ),
            "unknown",
        )
    result = {
        "game": definition.id,
        "game_name": definition.display_name,
        "install_root": str(root),
        "install_present": root.is_dir(),
        "executable": executable.name,
        "executable_present": executable.is_file(),
        "patch_archives": patches,
        "declared_patch": declared_patch,
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
        "ready": root.is_dir() and executable.is_file() and not missing_required,
        "missing_required": missing_required,
    }
    if deep and definition.id == "bfme2":
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
    elif deep:
        # ROTWK reference archive hashes are not pinned yet. Catalog and pack
        # manifests still bind every selected archive directory and payload.
        result["archive_attestation"] = []
        result["modified_or_missing_slice_archives"] = []
        result["deep_attestation_note"] = (
            "no fixed ROTWK archive reference hashes are configured"
        )
    return result
