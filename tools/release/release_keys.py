#!/usr/bin/env python3
"""Lane SIGNKMS-1: the trusted release keys, the signature envelope and the verification rule (docs/RELEASE.md, "Nightly signing on GitHub Actions").

    python3 tools/release/release_keys.py --verify LIST|HEX KIND FILE SIGFILE     exit 0 and the key's label when SIGFILE is valid
    python3 tools/release/release_keys.py --check LIST                            the list parses (and its first entry is release_key.pub)
    python3 tools/release/release_keys.py --hex LIST [--format lines|csv|json]     the keys, in file order (package.sh, build_launcher.sh)
    python3 tools/release/release_keys.py --verify LIST|HEX KIND FILE SIGFILE --nightly NLIST
                                                                                   the same with NLIST's nightly-only keys (below)
    python3 tools/release/release_keys.py --check-nightly NLIST [--stable LIST]    the nightly list parses (none of its keys in LIST)
    python3 tools/release/release_keys.py --hex-nightly NLIST [--format ...]       its keys (possibly none: json [], csv/lines empty)
    python3 tools/release/release_keys.py --spki-to-hex FILE                       the 64 hex of a DER SubjectPublicKeyInfo or PEM public key

The list (launcher/release_keys.pub, committed, public): one key per line, `<64 lower-case hex>  <label>`; '#' comments and blank lines
are allowed. 1..8 keys, no key twice, no label twice, labels [a-z0-9-]{1,32}. The first entry is the legacy key, the one every launcher
before SIGNKMS-1 trusts: it must equal launcher/release_key.pub (checked whenever a release_key.pub lies beside the list).

A 64-byte signature `sig` over the bytes `data` of the file kind `kind` is valid iff for SOME key K of the list
    Ed25519.verify(K, data, sig)                       scheme "raw" (v1: what the Deck key has always made)
 or Ed25519.verify(K, envelope(kind, data), sig)       scheme "envelope" (v2: what the KMS key makes; AWS KMS signs at most 4096 bytes)
    envelope(kind, data) = b"OpenBFME signature v2\\n" + kind + b"\\n" + sha512(data).hexdigest() + b"\\n"
kind is one of manifest.json, manifest-macos.json, payload.json (launcher-manifest.json, a copy of a release's manifest.json, is
"manifest.json"). Everything else fails closed: an unknown key, a wrong size, changed data, the wrong kind, an empty list.

Lane RELGH-2: the NIGHTLY list (launcher/nightly_keys.pub, the same line format, 0..4 keys, none of them in the stable list) holds keys
trusted for nightly releases ONLY. A signature under a nightly key is valid iff it verifies (raw or envelope) AND the signed file is a
JSON object whose "version" is a nightly tag (NIGHTLY_VERSION) and, for the manifest kinds, whose "channel" is "preview"
(is_nightly_document). So a nightly key can never make a stable manifest, a stable launcher proof or a stable package's payload.json
valid; the launcher (launcher/scripts/crypto/release_signature.gd valid_scoped) and the game apply the same rule.
Pure standard library plus tools/release/ed25519.py.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402

ENVELOPE_MAGIC = b"OpenBFME signature v2\n"
KINDS = ("manifest.json", "manifest-macos.json", "payload.json")
# the file names a kind is taken from (sign_manifest.py --verify): a release's file, or the launcher's proof copy
KIND_OF_NAME = {"manifest.json": "manifest.json", "manifest-macos.json": "manifest-macos.json", "payload.json": "payload.json",
                "launcher-manifest.json": "manifest.json"}
MAX_KEYS = 8
LABEL = re.compile(r"[a-z0-9-]{1,32}")
HEX_KEY = re.compile(r"[0-9a-f]{64}")
DEFAULT_LIST = HERE.parents[1] / "launcher" / "release_keys.pub"
NIGHTLY_LIST = HERE.parents[1] / "launcher" / "nightly_keys.pub"
MAX_NIGHTLY_KEYS = 4
# a nightly's tag (autorelease.py nightly-version: <YYMMDD><1|2>00 since NIGHTLY-2, <YYYYMMDD> before)
NIGHTLY_VERSION = re.compile(r"v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})-nightly\.[0-9]{8,9}")


SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")   # SubjectPublicKeyInfo { id-Ed25519, BIT STRING (32) }


class KeyListError(ValueError):
    pass


def spki_to_key(blob: bytes) -> bytes:
    """the 32-byte Ed25519 key of a DER SubjectPublicKeyInfo or its PEM form ("-----BEGIN PUBLIC KEY-----")"""
    if blob.lstrip().startswith(b"-----BEGIN PUBLIC KEY-----"):
        try:
            der = ed25519._unpem("PUBLIC KEY", blob.decode("ascii"))
        except (ValueError, UnicodeDecodeError) as e:
            raise KeyListError(f"not a PEM public key ({e})")
    else:
        der = blob
    if len(der) != len(SPKI_PREFIX) + 32 or not der.startswith(SPKI_PREFIX):
        raise KeyListError("not an Ed25519 SubjectPublicKeyInfo (302a300506032b6570032100 || 32 bytes): is the KMS key ECC_NIST_EDWARDS25519?")
    key = der[len(SPKI_PREFIX):]
    point = ed25519._decompress(key)
    if point is None or ed25519.is_small_order(point):
        raise KeyListError("the key is not a usable Ed25519 public key")
    return key


def kind_of(name: str) -> str | None:
    """the kind of a file by its name (None: not a signed file kind)"""
    return KIND_OF_NAME.get(Path(name).name)


def envelope(kind: str, data: bytes) -> bytes:
    if kind not in KINDS:
        raise ValueError(f"release_keys: {kind!r} is not a signed file kind ({', '.join(KINDS)})")
    return ENVELOPE_MAGIC + kind.encode("ascii") + b"\n" + hashlib.sha512(data).hexdigest().encode("ascii") + b"\n"


def parse(text: str, where: str = "the key list", allow_empty: bool = False, max_keys: int = MAX_KEYS) -> list[tuple[str, bytes]]:
    """[(label, 32-byte key)] of a key list's text, strictly (KeyListError on any malformed line)"""
    keys: list[tuple[str, bytes]] = []
    for n, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 2:
            raise KeyListError(f"{where} line {n}: not '<64 hex>  <label>'")
        hexkey, label = parts
        if not HEX_KEY.fullmatch(hexkey):
            raise KeyListError(f"{where} line {n}: the key is not 64 lower-case hex digits")
        if not LABEL.fullmatch(label):
            raise KeyListError(f"{where} line {n}: the label {label!r} is not [a-z0-9-]{{1,32}}")
        key = bytes.fromhex(hexkey)
        if any(k == key for _, k in keys):
            raise KeyListError(f"{where} line {n}: the key {hexkey[:16]}... is listed twice")
        if any(lb == label for lb, _ in keys):
            raise KeyListError(f"{where} line {n}: the label {label} is used twice")
        point = ed25519._decompress(key)
        if point is None or ed25519.is_small_order(point):
            raise KeyListError(f"{where} line {n}: {label} is not a usable Ed25519 public key (not a curve point, or of small order)")
        keys.append((label, key))
        if len(keys) > max_keys:
            raise KeyListError(f"{where}: more than {max_keys} keys")
    if not keys and not allow_empty:
        raise KeyListError(f"{where}: no key")
    return keys


def load(path: Path | str, legacy: Path | str | None | bool = True) -> list[tuple[str, bytes]]:
    """the list file `path`. legacy: True (default) checks the first entry against a release_key.pub beside the list when there is one;
    a path checks against that file (which must exist); False/None checks nothing."""
    path = Path(path)
    try:
        text = path.read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as e:
        raise KeyListError(f"{path}: cannot be read ({e})")
    keys = parse(text, path.name)
    if legacy is True:
        side = path.with_name("release_key.pub")
        legacy = side if side.is_file() else None
    if legacy:
        try:
            want = Path(legacy).read_text(encoding="ascii").strip()
        except (OSError, UnicodeDecodeError) as e:
            raise KeyListError(f"{legacy}: cannot be read ({e})")
        if want != keys[0][1].hex():
            raise KeyListError(f"{path.name}: the first entry ({keys[0][0]}) is not {Path(legacy).name}'s key")
    return keys


def load_any(arg: str | Path) -> list[tuple[str, bytes]]:
    """the trusted keys named by a tool's PUBLIC argument: a key list file, a one-key file (launcher/release_key.pub's format: 64 hex
    digits alone, or a PEM public key) or 64 hex digits"""
    p = Path(arg)
    if p.is_file():
        text = p.read_text(encoding="ascii").strip()
        if "BEGIN PUBLIC KEY" in text:
            return [("key", ed25519.public_from_pem(text))]
        if HEX_KEY.fullmatch(text.lower()) and len(text.split()) == 1:
            return [("key", bytes.fromhex(text))]
        return load(p)
    text = str(arg).strip()
    if HEX_KEY.fullmatch(text.lower()):
        return [("key", bytes.fromhex(text))]
    raise KeyListError(f"{arg!r} is neither a key file nor 64 hex digits")


def verify_detail(keys, kind: str, data: bytes, sig: bytes) -> tuple[str, str] | None:
    """(label, "raw" | "envelope") of the first key and scheme under which sig is valid, None when none is"""
    if kind not in KINDS or not keys or not isinstance(sig, (bytes, bytearray)) or len(sig) != 64:
        return None
    env = envelope(kind, data)
    for label, key in keys:
        if len(key) != 32:
            continue
        if ed25519.verify(key, data, bytes(sig)):
            return label, "raw"
        if ed25519.verify(key, env, bytes(sig)):
            return label, "envelope"
    return None


def verify(keys, kind: str, data: bytes, sig: bytes) -> str | None:
    """the label of the trusted key the signature is valid under (raw or envelope), None when it is not valid"""
    got = verify_detail(keys, kind, data, sig)
    return got[0] if got else None


def load_nightly(path: Path | str = NIGHTLY_LIST, stable: Path | str | None = DEFAULT_LIST) -> list[tuple[str, bytes]]:
    """lane RELGH-2: the nightly-only keys of `path` (possibly none); none of them may be in the stable list `stable` (None: not checked)"""
    path = Path(path)
    try:
        text = path.read_text(encoding="ascii")
    except (OSError, UnicodeDecodeError) as e:
        raise KeyListError(f"{path}: cannot be read ({e})")
    keys = parse(text, path.name, allow_empty=True, max_keys=MAX_NIGHTLY_KEYS)
    if stable is not None:
        both = {k for _, k in keys} & {k for _, k in load(stable)}
        if both:
            raise KeyListError(f"{path.name}: {len(both)} key(s) also in {Path(stable).name}: a nightly key is never a stable key")
    return keys


def is_nightly_document(kind: str, data: bytes) -> bool:
    """lane RELGH-2: the signed file is a nightly's: a JSON object whose version is a nightly tag (manifest kinds: and channel preview)"""
    try:
        d = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return False
    if not isinstance(d, dict) or not isinstance(d.get("version"), str) or not NIGHTLY_VERSION.fullmatch(d["version"]):
        return False
    return kind == "payload.json" or d.get("channel") == "preview"


def verify_scoped(keys, nightly, kind: str, data: bytes, sig: bytes) -> tuple[str, str] | None:
    """(label, scheme) under the stable list `keys`, else under a nightly-only key of `nightly` when the file is a nightly document;
    None when neither"""
    got = verify_detail(keys, kind, data, sig)
    if got:
        return got
    if nightly and is_nightly_document(kind, data):
        return verify_detail(nightly, kind, data, sig)
    return None


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--verify", nargs=4, metavar=("LIST", "KIND", "FILE", "SIGFILE"))
    g.add_argument("--check", metavar="LIST")
    g.add_argument("--hex", metavar="LIST")
    g.add_argument("--check-nightly", metavar="NLIST")
    g.add_argument("--hex-nightly", metavar="NLIST")
    g.add_argument("--spki-to-hex", metavar="FILE")
    ap.add_argument("--format", choices=("lines", "csv", "json"), default="lines")
    ap.add_argument("--nightly", metavar="NLIST", help="--verify: also the nightly-only keys of NLIST (nightly documents only)")
    ap.add_argument("--stable", metavar="LIST", default=str(DEFAULT_LIST), help="--check-nightly / --hex-nightly: the stable list")
    a = ap.parse_args(argv)
    try:
        if a.verify:
            lst, kind, f, s = a.verify
            if kind not in KINDS:
                raise KeyListError(f"{kind!r} is not a signed file kind ({', '.join(KINDS)})")
            nightly = load_nightly(a.nightly, None) if a.nightly else []
            got = verify_scoped(load_any(lst), nightly, kind, Path(f).read_bytes(), Path(s).read_bytes())
            if got:
                print(f"signature ok: {Path(s).name} for {Path(f).name} ({got[0]}, {got[1]})")
                return 0
            print(f"signature INVALID: {Path(s).name} for {Path(f).name}")
            return 1
        if a.check:
            keys = load(a.check)
            print(f"key list ok: {', '.join(lb for lb, _ in keys)}")
            return 0
        if a.check_nightly or a.hex_nightly:
            nkeys = load_nightly(a.check_nightly or a.hex_nightly, a.stable)
            if a.check_nightly:
                print(f"nightly key list ok: {', '.join(lb for lb, _ in nkeys) or 'no key'}")
                return 0
            hexes = [k.hex() for _, k in nkeys]
            print({"lines": "\n".join(hexes), "csv": ",".join(hexes), "json": json.dumps(hexes)}[a.format])
            return 0
        if a.spki_to_hex:
            print(spki_to_key(Path(a.spki_to_hex).read_bytes()).hex())
            return 0
        if a.hex:
            hexes = [k.hex() for _, k in load(a.hex)]
            print({"lines": "\n".join(hexes), "csv": ",".join(hexes), "json": json.dumps(hexes)}[a.format])
            return 0
        raise KeyListError("no command")
    except (KeyListError, OSError, ValueError) as e:
        print(f"release_keys: {e}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
