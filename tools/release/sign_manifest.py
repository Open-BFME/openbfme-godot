#!/usr/bin/env python3
"""Lane LAUNCH-1: the Ed25519 signature of a release manifest (docs/RELEASE.md, "The launcher": signing).

    python3 tools/release/sign_manifest.py --key KEY manifest.json          writes manifest.json.sig (64 raw bytes) and checks it
    python3 tools/release/sign_manifest.py --generate-key KEY               a new release key (PKCS#8 PEM, mode 0600); prints the public key
    python3 tools/release/sign_manifest.py --public-key KEY                 prints the public key (64 hex digits) of KEY
    python3 tools/release/sign_manifest.py --verify PUBLIC manifest.json    checks manifest.json.sig; PUBLIC: 64 hex digits or a file holding them

The private key never enters the repository, CI or GitHub: it lives on the coordinator's offline machine. This tool refuses a key path
inside this repository or inside any git work tree, and (POSIX) a key file other users can read. The key is OpenSSL's format
(`openssl genpkey -algorithm ed25519`), so `openssl pkeyutl -sign -rawin` makes the same signature (Ed25519 is deterministic) and
`openssl pkeyutl -verify -rawin -pubin` checks it. The public key goes into launcher/release_key.pub (committed), which package.sh compiles
into the launcher.
"""
from __future__ import annotations

import argparse
import os
import stat
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402


def key_location_problem(path: Path) -> str:
    """'' when `path` may hold a private key: outside this repository and outside any git work tree."""
    p = path.resolve()
    try:
        p.relative_to(REPO)
        return f"{path} is inside the repository {REPO}: a release key never enters the repository"
    except ValueError:
        pass
    d = p.parent
    while not d.exists():
        d = d.parent
    r = subprocess.run(["git", "-C", str(d), "rev-parse", "--is-inside-work-tree"], capture_output=True, text=True)
    if r.returncode == 0 and r.stdout.strip() == "true":
        return f"{path} is inside a git work tree ({d}): keep the release key outside every repository"
    return ""


def load_secret(path: Path) -> bytes:
    why = key_location_problem(path)
    if why:
        raise SystemExit(f"sign_manifest: {why}")
    if os.name == "posix":
        mode = path.stat().st_mode
        if mode & (stat.S_IRWXG | stat.S_IRWXO):
            raise SystemExit(f"sign_manifest: {path} can be read by other users (mode {stat.S_IMODE(mode):o}): chmod 600 it")
    return ed25519.secret_from_pem(path.read_text(encoding="ascii"))


def read_public(arg: str) -> bytes:
    text = Path(arg).read_text(encoding="ascii").strip() if Path(arg).is_file() else arg.strip()
    if "BEGIN PUBLIC KEY" in text:
        return ed25519.public_from_pem(text)
    try:
        key = bytes.fromhex(text)
    except ValueError:
        key = b""
    if len(key) != 32:
        raise SystemExit("sign_manifest: the public key must be 64 hex digits (or a PEM public key)")
    return key


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--key", type=Path, help="sign with this private key")
    g.add_argument("--generate-key", type=Path)
    g.add_argument("--public-key", type=Path)
    g.add_argument("--verify", metavar="PUBLIC")
    ap.add_argument("manifest", type=Path, nargs="?")
    ap.add_argument("--sig", type=Path, help="the signature file (default: <manifest>.sig)")
    a = ap.parse_args(argv)
    if a.generate_key:
        why = key_location_problem(a.generate_key)
        if why:
            raise SystemExit(f"sign_manifest: {why}")
        if a.generate_key.exists():
            raise SystemExit(f"sign_manifest: {a.generate_key} exists (never overwrite a key)")
        secret = os.urandom(32)
        fd = os.open(a.generate_key, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w", encoding="ascii") as f:
            f.write(ed25519.private_key_pem(secret))
        print(ed25519.public_key(secret).hex())
        return 0
    if a.public_key:
        print(ed25519.public_key(load_secret(a.public_key)).hex())
        return 0
    if a.manifest is None:
        ap.error("the manifest is required")
    sig_path = a.sig or a.manifest.with_name(a.manifest.name + ".sig")
    data = a.manifest.read_bytes()
    if a.verify:
        ok = ed25519.verify(read_public(a.verify), data, sig_path.read_bytes())
        print(f"signature {'ok' if ok else 'INVALID'}: {sig_path.name} for {a.manifest.name}")
        return 0 if ok else 1
    secret = load_secret(a.key)
    sig = ed25519.sign(secret, data)
    if not ed25519.verify(ed25519.public_key(secret), data, sig):
        raise SystemExit("sign_manifest: the new signature does not verify (a broken implementation?)")
    sig_path.write_bytes(sig)
    print(f"SIGNED {a.manifest.name} -> {sig_path.name} (public key {ed25519.public_key(secret).hex()})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
