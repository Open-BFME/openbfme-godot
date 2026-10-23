#!/usr/bin/env python3
"""Lane ONEZIP-1 r2: the signed payload inventory of a combined package (the game and the launcher in one archive).

    python3 tools/release/seal_package.py seal <package dir> --key KEY --repo OWNER/NAME
    python3 tools/release/seal_package.py seal <package dir> --kms [--key-id ID] [--keys LIST] --repo OWNER/NAME
    python3 tools/release/seal_package.py check <package dir> --public PUBLIC --repo OWNER/NAME
    python3 tools/release/seal_package.py unseal <archive> <out archive>

A player starts the unpacked package before any update check has answered, so its files must be checkable against signed data that
travels inside the archive. The archive cannot hold a signature of itself (the release manifest names the archive's SHA-256), so the
inventory signs the package's FILES instead: payload.json lists every file of the package folder with its size and SHA-256 (all but
payload.json and payload.json.sig), payload.json.sig is its Ed25519 signature under the release key. The launcher
(launcher/scripts/core/manifest.gd verify_payload) and the game (godot/scripts/release/launcher_check.gd) check it.

seal: on the machine with the release key, after package.sh --verify and before make_manifest.py (tools/release/autorelease.sh step 6):
each combined archive of the folder (openbfme-<version>-<platform>, not the launcher-only ones) is read, the two files are added, and
it is written again with make_archive.py at the archive's own time stamp; SHA256SUMS-<version>.txt is rewritten. Deterministic: Ed25519
is, and the payload's bytes are sorted JSON. Refuses an archive that is sealed already, holds anything but files under its one folder,
or whose folder has no VERSION naming the version.
check: every combined archive is sealed, its signature verifies under PUBLIC, the payload names exactly the archive's other files with
their sizes and digests, its version / platform / repository are the archive's. Exit 1 with one line per problem.
unseal: the archive without the two files, written the way package.sh writes it (package.sh --verify compares that with its rebuild).

SIGNKMS-1: sealing goes through a signer: the release key file (FileKeySigner: the raw Ed25519 signature, as before) or the AWS KMS key
(--kms: tools/release/kms_sign.py KmsSigner, the envelope signature of release_keys.py, checked against the trusted list before it is
written). check's PUBLIC may be a key list (launcher/release_keys.pub): payload.json.sig is valid under any of its keys, raw or envelope.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import re
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import make_archive  # noqa: E402
import release_keys  # noqa: E402

PAYLOAD, PAYLOAD_SIG = "payload.json", "payload.json.sig"
# MACPLAY-1: the Apple Silicon package (openbfme-<v>-macos-arm64.zip, built on the Mac build host) carries the same signed inventory, so its
# launcher starts the game beside it only when every file is the signed one (launcher/scripts/core/updater.gd bundled)
NAME = re.compile(r"^openbfme-(v[0-9A-Za-z.+-]+)-(linux-x64|windows-x64|macos-universal|macos-arm64)\.(tar\.gz|zip)$")
REPO_NAME = re.compile(r"^[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}$")


MAX_MEMBERS = 10000          # the launcher's own limits (launcher/scripts/core/archive.gd)
MAX_TOTAL = 2 * 1024 ** 3
_FORBIDDEN = set(':*?"<>|\\')
_RESERVED = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(10)), *(f"LPT{i}" for i in range(10))}


def member_problem(rel: str) -> str:
    """"" when `rel` (a path below the package folder) is a plain relative path, else what is wrong with it (ONEZIP-1 r3, Sol r2: an
    absolute member path was written outside the staging folder)"""
    if not rel or len(rel) > 400:
        return "an empty or overlong name"
    if rel.startswith("/") or (len(rel) > 1 and rel[1] == ":"):
        return "an absolute path"
    if any(ord(c) < 0x20 or ord(c) == 0x7F for c in rel):
        return "a control character"
    if any(c in _FORBIDDEN for c in rel):
        return "a backslash, a drive letter or a character Windows refuses"
    for part in rel.split("/"):
        if part in ("", ".", ".."):
            return f"a '{part}' path component" if part else "an empty path component"
        if part.endswith((".", " ")) or part.split(".")[0].upper() in _RESERVED:
            return "a component Windows cannot hold"
    return ""


MAX_DIRECTORY = 2 * 1024 * 1024   # bytes of a zip's central directory: 10,000 entries of a 400-byte name take about 4.5 MB; real packages ~1 KB


def zip_directory_problem(path: Path, where: str) -> None:
    """Reads the zip's end-of-central-directory record (and the zip64 one when it says so) and refuses, before any entry is allocated,
    an archive with more than MAX_MEMBERS entries, a central directory larger than MAX_DIRECTORY, or a record that is malformed or
    inconsistent (ONEZIP-1 r5, Sol r4: a 14 MB zip of 100,000 empty entries allocated 63 MB before it was refused)."""
    import struct
    size = path.stat().st_size
    with open(path, "rb") as f:
        tail = min(size, 22 + 65535)
        f.seek(size - tail)
        data = f.read(tail)
        at = data.rfind(b"PK\x05\x06")
        if at < 0 or len(data) - at < 22:
            raise SystemExit(f"seal_package: {where}: no zip end-of-central-directory record (not a zip, or truncated)")
        _, _, _, entries_disk, entries, cd_size, cd_offset, comment = struct.unpack("<4sHHHHIIH", data[at:at + 22])
        if at + 22 + comment != len(data):
            raise SystemExit(f"seal_package: {where}: the end-of-central-directory record's comment length disagrees with the file")
        eocd_at = size - tail + at
        if entries == 0xFFFF or cd_size == 0xFFFFFFFF or cd_offset == 0xFFFFFFFF:   # zip64: the real values are in its own record
            loc = eocd_at - 20
            f.seek(max(loc, 0))
            locator = f.read(20)
            if loc < 0 or len(locator) != 20 or locator[:4] != b"PK\x06\x07":
                raise SystemExit(f"seal_package: {where}: the record says zip64 but has no zip64 locator")
            z64_at = struct.unpack("<Q", locator[8:16])[0]
            f.seek(z64_at)
            rec = f.read(56)
            if z64_at + 56 > size or len(rec) != 56 or rec[:4] != b"PK\x06\x06":
                raise SystemExit(f"seal_package: {where}: a malformed zip64 end-of-central-directory record")
            _, _, _, _, _, _, entries_disk, entries, cd_size, cd_offset = struct.unpack("<4sQHHIIQQQQ", rec)
        if entries != entries_disk:
            raise SystemExit(f"seal_package: {where}: the central directory's entry counts disagree ({entries_disk} on its disk, {entries} in all)")
    if entries > MAX_MEMBERS:
        raise SystemExit(f"seal_package: {where}: more than {MAX_MEMBERS} members ({entries} entries in the central directory)")
    if cd_size > MAX_DIRECTORY:
        raise SystemExit(f"seal_package: {where}: the central directory is {cd_size} bytes (at most {MAX_DIRECTORY})")
    if cd_offset + cd_size > size:
        raise SystemExit(f"seal_package: {where}: the central directory lies beyond the end of the file")


def read_archive(path: Path) -> tuple[dict[str, bytes], dict[str, int], int]:
    """({path below the top folder: bytes}, {path: unix mode}, mtime) of a package archive with one top folder named like it.
    Every member is checked BEFORE anything is read or written (ONEZIP-1 r3, Sol r2): only regular files below the one folder (and that
    folder itself in a tar), plain relative names (no absolute path, '..', '.', empty component, backslash or drive), no link, device or
    other special member, no name twice (ignoring case), at most MAX_MEMBERS members and MAX_TOTAL bytes; any other member refuses the
    whole archive by name."""
    import calendar
    import stat
    top = path.name.removesuffix(".tar.gz").removesuffix(".zip")
    where = path.name

    def refuse(name: str, why: str):
        raise SystemExit(f"seal_package: {where}: member {name!r}: {why}")

    plan, seen, total, mtime = [], set(), 0, None
    if path.name.endswith(".zip"):
        zip_directory_problem(path, where)   # before ZipFile reads (and allocates for) the whole central directory
        z = zipfile.ZipFile(path)
        infos = z.infolist()
        if len(infos) > MAX_MEMBERS:
            raise SystemExit(f"seal_package: {where}: more than {MAX_MEMBERS} members")
        for i in infos:
            mode = (i.external_attr >> 16) & 0xFFFF
            if i.is_dir():
                refuse(i.filename, "a folder (the package's zip has only files)")
            if i.create_system == 3 and stat.S_IFMT(mode) not in (0, stat.S_IFREG):
                refuse(i.filename, "a link or another special file")
            if i.flag_bits & 1:
                refuse(i.filename, "encrypted")
            plan.append((i.orig_filename, i, i.file_size, mode & 0o777, calendar.timegm(i.date_time + (0, 0, 0))))
    else:
        t = tarfile.open(path, "r:gz")
        # streamed (ONEZIP-1 r3, Sol r3): one header at a time, the count checked BEFORE each member is kept, so an archive of a million
        # headers allocates nothing past the limit; the package folder's own entry counts as a member and may appear once
        roots = 0
        counted = 0
        while True:
            m = t.next()
            if m is None:
                break
            counted += 1
            if counted > MAX_MEMBERS + 1:
                raise SystemExit(f"seal_package: {where}: more than {MAX_MEMBERS} members")
            if m.isdir() and m.name == top:
                roots += 1
                if roots > 1:
                    refuse(m.name, "the package folder appears twice")
                mtime = int(m.mtime)
                continue
            if not m.isreg():
                refuse(m.name, "a link, folder, device or other special member (only files)")
            if len(plan) >= MAX_MEMBERS:
                raise SystemExit(f"seal_package: {where}: more than {MAX_MEMBERS} files")
            plan.append((m.name, m, m.size, m.mode, int(m.mtime)))
            t.members = []   # TarFile keeps every header it has read: only this plan holds them
    for name, _, size, _, when in plan:
        if not name.startswith(top + "/"):
            refuse(name, f"not inside the package folder {top}")
        rel = name[len(top) + 1:]
        why = member_problem(rel)
        if why:
            refuse(name, why)
        if rel.lower() in seen:
            refuse(name, "appears twice (names are compared ignoring case)")
        seen.add(rel.lower())
        total += size
        if total > MAX_TOTAL:
            raise SystemExit(f"seal_package: {where}: the members add up to more than {MAX_TOTAL} bytes")
        mtime = when if mtime is None else mtime
    for rel in sorted(seen):   # a file that is also the folder of another member
        parts = rel.split("/")
        for k in range(1, len(parts)):
            if "/".join(parts[:k]) in seen:
                refuse(top + "/" + rel, "lies below a member that is a file")
        if "/" in rel and not top.endswith(("-macos-universal", "-macos-arm64")):
            refuse(top + "/" + rel, "nested package files are supported only for macOS universal apps")
    if not plan or mtime is None:
        raise SystemExit(f"seal_package: {where} holds no files")
    files, modes = {}, {}
    for name, ref, size, mode, _ in plan:
        rel = name[len(top) + 1:]
        data = z.read(ref) if path.name.endswith(".zip") else t.extractfile(ref).read()
        if len(data) != size:
            refuse(name, f"{len(data)} bytes, its header says {size}")
        files[rel], modes[rel] = data, mode
    (z if path.name.endswith(".zip") else t).close()
    return files, modes, mtime


def payload_bytes(files: dict[str, bytes], version: str, platform: str, repo: str) -> bytes:
    inventory = {rel: {"size": len(data), "sha256": hashlib.sha256(data).hexdigest()} for rel, data in sorted(files.items())
                 if rel not in (PAYLOAD, PAYLOAD_SIG)}
    p = {"format": 1, "product": "OpenBFME", "repo": repo, "version": version, "platform": platform, "files": inventory}
    return (json.dumps(p, indent=2, sort_keys=True) + "\n").encode("utf-8")


def write_archive(out: Path, top: str, files: dict[str, bytes], modes: dict[str, int], mtime: int) -> None:
    with tempfile.TemporaryDirectory() as t:
        d = Path(t) / top
        d.mkdir()
        root = d.resolve()
        for rel, data in files.items():
            why = member_problem(rel)
            if why or root not in (d / rel).resolve().parents:   # defence in depth: never a path outside the staging folder
                raise SystemExit(f"seal_package: refused to write {rel!r} ({why or 'outside the staging folder'})")
            (d / rel).parent.mkdir(parents=True, exist_ok=True)
            (d / rel).write_bytes(data)
            (d / rel).chmod(0o755 if modes.get(rel, 0o644) & 0o100 else 0o644)
        d.chmod(0o755)
        tmp = out.with_name(out.name + ".tmp")
        (make_archive.zip_file if out.name.endswith(".zip") else make_archive.tar_gz)(d, tmp, mtime)
        tmp.replace(out)


class FileKeySigner:
    """the release key file's raw Ed25519 signature (the scheme every launcher reads)"""

    def __init__(self, secret: bytes):
        self.secret = secret
        self.public = ed25519.public_key(secret)

    def sign(self, kind: str, data: bytes) -> bytes:
        return ed25519.sign(self.secret, data)

    def trusted(self) -> list[tuple[str, bytes]]:
        return [("key", self.public)]


def as_signer(signer):
    """a 32-byte secret (the callers before SIGNKMS-1) or an object with sign(kind, data) -> 64 bytes"""
    return FileKeySigner(signer) if isinstance(signer, (bytes, bytearray)) else signer


def as_keys(public) -> list[tuple[str, bytes]]:
    """a 32-byte public key (the callers before SIGNKMS-1) or a key list [(label, key)]"""
    return [("key", bytes(public))] if isinstance(public, (bytes, bytearray)) else list(public)


def combined_archives(d: Path) -> list[Path]:
    return sorted(p for p in d.iterdir() if NAME.match(p.name))


def seal_archive(path: Path, secret, repo: str) -> None:
    """adds the signed payload inventory to one combined archive (rewritten in place)"""
    m = NAME.match(path.name)
    if not m:
        raise SystemExit(f"seal_package: {path.name} is not a combined package archive")
    version, platform = m.group(1, 2)
    files, modes, mtime = read_archive(path)
    if PAYLOAD in files or PAYLOAD_SIG in files:
        raise SystemExit(f"seal_package: {path.name} is sealed already")
    first = files.get("VERSION", b"").split(b"\n")[0].replace(b"\r", b"")
    if platform == "macos-arm64":   # MACPLAY-1: package_macos.py's line carries a label after the version ("... Mac Unstable Preview ...")
        first = b" ".join(first.split(b" ")[:2])
    if first != f"OpenBFME {version}".encode():
        raise SystemExit(f"seal_package: {path.name}: its VERSION does not name {version}")
    body = payload_bytes(files, version, platform, repo)
    files[PAYLOAD] = body
    files[PAYLOAD_SIG] = as_signer(secret).sign(PAYLOAD, body)
    modes[PAYLOAD] = modes[PAYLOAD_SIG] = 0o644
    write_archive(path, path.name.removesuffix(".tar.gz").removesuffix(".zip"), files, modes, mtime)


def seal_folder(folder: Path, secret, repo: str, version: str, platform: str) -> None:
    """the inventory of an unpacked package folder, written into it (tests that change a package's files and seal it again)"""
    files = {p.relative_to(folder).as_posix(): p.read_bytes() for p in sorted(folder.rglob("*"))
             if p.is_file() and p.relative_to(folder).as_posix() not in (PAYLOAD, PAYLOAD_SIG)}
    body = payload_bytes(files, version, platform, repo)
    (folder / PAYLOAD).write_bytes(body)
    (folder / PAYLOAD_SIG).write_bytes(as_signer(secret).sign(PAYLOAD, body))


def seal(d: Path, secret, repo: str) -> list[str]:
    sealed = []
    for path in combined_archives(d):
        seal_archive(path, secret, repo)
        sealed.append(path.name)
    if not sealed:
        raise SystemExit(f"seal_package: {d} holds no combined package archive")
    rewrite_sums(d)
    return sealed


def rewrite_sums(d: Path) -> None:
    sums = sorted(d.glob("SHA256SUMS-*.txt"))
    if len(sums) != 1:
        raise SystemExit(f"seal_package: {d} holds {len(sums)} checksum files (exactly one)")
    lines = []
    for line in sums[0].read_text(encoding="utf-8").splitlines():
        digest, _, name = line.partition("  ")
        lines.append(f"{hashlib.sha256((d / name).read_bytes()).hexdigest()}  {name}")
    sums[0].write_text("".join(ln + "\n" for ln in lines), encoding="utf-8")


def check(d: Path, public, repo: str) -> list[str]:
    """public: one key (32 bytes) or a key list: payload.json.sig must be valid under one of them (release_keys.verify)"""
    keys = as_keys(public)
    problems = []
    archives = combined_archives(d)
    if not archives:
        problems.append(f"{d.name}: no combined package archive")
    for path in archives:
        version, platform = NAME.match(path.name).group(1, 2)
        files, _, _ = read_archive(path)
        body, sig = files.get(PAYLOAD), files.get(PAYLOAD_SIG)
        if body is None or sig is None:
            problems.append(f"{path.name}: not sealed (no {PAYLOAD} / {PAYLOAD_SIG})")
            continue
        if release_keys.verify(keys, PAYLOAD, body, sig) is None:
            problems.append(f"{path.name}: the signature of {PAYLOAD} does not verify under the release key")
            continue
        if body != payload_bytes(files, version, platform, repo):
            problems.append(f"{path.name}: {PAYLOAD} is not the inventory of the archive's files ({version}, {platform}, {repo})")
    return problems


def unseal(src: Path, out: Path) -> None:
    files, modes, mtime = read_archive(src)
    for n in (PAYLOAD, PAYLOAD_SIG):
        files.pop(n, None)
    write_archive(out, src.name.removesuffix(".tar.gz").removesuffix(".zip"), files, modes, mtime)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("seal")
    s.add_argument("dir", type=Path)
    k = s.add_mutually_exclusive_group(required=True)
    k.add_argument("--key", type=Path, help="the release key file (raw signatures)")
    k.add_argument("--kms", action="store_true", help="the AWS KMS key (envelope signatures, tools/release/kms_sign.py)")
    s.add_argument("--key-id", default=None, help="--kms: the KMS key (default kms_sign.py's alias/openbfme-release)")
    s.add_argument("--keys", type=Path, default=None, help="--kms: the trusted list (default launcher/release_keys.pub)")
    s.add_argument("--policy", type=Path, default=None, help="--kms: the signing policy (default tools/release/signing_policy.json)")
    s.add_argument("--repo", required=True)
    c = sub.add_parser("check")
    c.add_argument("dir", type=Path)
    c.add_argument("--public", required=True)
    c.add_argument("--repo", required=True)
    u = sub.add_parser("unseal")
    u.add_argument("archive", type=Path)
    u.add_argument("out", type=Path)
    a = ap.parse_args(argv)
    if a.cmd == "unseal":
        unseal(a.archive, a.out)
        return 0
    if not REPO_NAME.match(a.repo):
        raise SystemExit(f"seal_package: '{a.repo}' is not <owner>/<name>")
    if a.cmd == "seal":
        if a.kms:
            import kms_sign
            signer = kms_sign.KmsSigner.open(key_id=a.key_id, keys_path=a.keys, policy_path=a.policy)
        else:
            from sign_manifest import load_secret  # the release key's own rules (outside every repository, mode 0600)
            signer = FileKeySigner(load_secret(a.key))
        try:
            sealed = seal(a.dir, signer, a.repo)
        except Exception as e:   # kms_sign.KmsError: the archive being sealed is left unchanged (each is rewritten only once signed)
            if type(e).__name__ != "KmsError":
                raise
            raise SystemExit(f"seal_package: {e}")
        for n in sealed:
            print(f"SEALED {n}")
        problems = check(a.dir, signer.trusted(), a.repo)
    else:
        try:
            keys = release_keys.load_any(a.public)
        except release_keys.KeyListError as e:
            raise SystemExit(f"seal_package: {e}")
        problems = check(a.dir, keys, a.repo)
    for p in problems:
        print(f"SEAL CHECK: {p}")
    if not problems:
        print(f"seal ok: {', '.join(p.name for p in combined_archives(a.dir))}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
