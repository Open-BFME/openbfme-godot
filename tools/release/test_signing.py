"""Lane RELGH-2 (was SIGNNIGHTLY-1's): the trusted key list, the signature envelope and the verification rule (release_keys.py), the nightly-only
key list and its scope rule (a nightly key signs a nightly's files only: release_keys.verify_scoped, sign_manifest.py --verify --nightly,
seal_package.py check --nightly, publish_release.sh), and the Deck's check of the release workflow's draft (check_draft.py with the stand-in
gh). Never the real key or GitHub."""
from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import release_assets  # noqa: E402
import release_keys  # noqa: E402
import seal_package  # noqa: E402

DECK_SECRET = bytes(range(1, 33))
NIGHTLY_SECRET = bytes(range(33, 65))
OTHER_SECRET = bytes(range(65, 97))
STABLE_SECRET = bytes(range(97, 129))     # RELGH-3: the stable Actions key (stable-2026) of the tests
DECK, NIGHTLY, OTHER, STABLE = (ed25519.public_key(s) for s in (DECK_SECRET, NIGHTLY_SECRET, OTHER_SECRET, STABLE_SECRET))
TARGET = "Test/repo"
GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@example.invalid", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}


def run(*cmd, env=None, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=600, stdin=subprocess.DEVNULL,
                          env={**os.environ, **GIT_ENV, **(env or {})}, **kw)


def key_list(path: Path, *entries: tuple[bytes, str]) -> Path:
    path.write_text("# test list\n" + "".join(f"{k.hex()}  {label}\n" for k, label in entries))
    return path


def env_sign(kind: str, data: bytes, secret: bytes = NIGHTLY_SECRET) -> bytes:
    """the envelope scheme (release_keys.envelope) with a local key"""
    return ed25519.sign(secret, release_keys.envelope(kind, data))


# ---- the key list ---------------------------------------------------------------------------------------------------------------------------
def test_the_list_parses_with_comments_and_blank_lines(tmp_path):
    text = f"# the release keys\n\n{DECK.hex()}  deck-2026\n   \n# the NIGHTLY key\n{NIGHTLY.hex()}\tnightly-2026\r\n"
    assert release_keys.parse(text) == [("deck-2026", DECK), ("nightly-2026", NIGHTLY)]
    p = tmp_path / "keys.pub"
    p.write_text(text)
    assert release_keys.load(p) == [("deck-2026", DECK), ("nightly-2026", NIGHTLY)]


@pytest.mark.parametrize("text,why", [
    ("", "no key"),
    ("# only a comment\n", "no key"),
    (f"{DECK.hex().upper()}  deck-2026\n", "64 lower-case hex"),
    (f"{DECK.hex()[:62]}  deck-2026\n", "64 lower-case hex"),
    (f"{DECK.hex()}zz  deck-2026\n", "64 lower-case hex"),
    (f"{DECK.hex()}\n", "not '<64 hex>  <label>'"),
    (f"{DECK.hex()}  deck  2026\n", "not '<64 hex>  <label>'"),
    (f"{DECK.hex()}  Deck-2026\n", "is not [a-z0-9-]"),
    (f"{DECK.hex()}  deck_2026\n", "is not [a-z0-9-]"),
    (f"{DECK.hex()}  {'a' * 33}\n", "is not [a-z0-9-]"),
    (f"{DECK.hex()}  deck-2026\n{DECK.hex()}  deck-copy\n", "listed twice"),
    (f"{DECK.hex()}  deck-2026\n{NIGHTLY.hex()}  deck-2026\n", "used twice"),
    ("".join(f"{ed25519.public_key(bytes([i]) * 32).hex()}  k{i}\n" for i in range(1, 10)), "more than 8 keys"),
    (f"{'00' * 31}80  zero\n", "not a usable Ed25519 public key"),            # small order (the identity's sign flip)
    (f"{'01' + '00' * 31}  identity\n", "not a usable Ed25519 public key"),     # the neutral element
])
def test_a_malformed_list_is_refused(text, why):
    with pytest.raises(release_keys.KeyListError, match=why.replace("(", r"\(").replace("[", r"\[")):
        release_keys.parse(text)


def test_eight_keys_and_a_32_character_label_are_allowed():
    text = "".join(f"{ed25519.public_key(bytes([i]) * 32).hex()}  {'k' * 31}{i}\n" for i in range(1, 9))
    assert len(release_keys.parse(text)) == 8


def test_the_first_entry_is_release_key_pub(tmp_path):
    d = tmp_path / "launcher"
    d.mkdir()
    lst = key_list(d / "release_keys.pub", (DECK, "deck-2026"), (NIGHTLY, "nightly-2026"))
    (d / "release_key.pub").write_text(DECK.hex() + "\n")
    assert [lb for lb, _ in release_keys.load(lst)] == ["deck-2026", "nightly-2026"]
    (d / "release_key.pub").write_text(NIGHTLY.hex() + "\n")
    with pytest.raises(release_keys.KeyListError, match="first entry"):
        release_keys.load(lst)
    assert release_keys.load(lst, legacy=False)[0] == ("deck-2026", DECK)
    alone = key_list(tmp_path / "elsewhere.pub", (NIGHTLY, "nightly-2026"), (DECK, "deck-2026"))
    with pytest.raises(release_keys.KeyListError, match="cannot be read"):
        release_keys.load(alone, legacy=tmp_path / "legacy.pub")   # a legacy file named but missing fails
    (tmp_path / "legacy.pub").write_text(DECK.hex() + "\n")
    with pytest.raises(release_keys.KeyListError, match="first entry"):
        release_keys.load(alone, legacy=tmp_path / "legacy.pub")


def test_the_committed_list_if_present_starts_with_release_key_pub():
    lst = HERE.parents[1] / "launcher" / "release_keys.pub"
    if not lst.exists():
        pytest.skip("launcher/release_keys.pub is not committed yet")
    keys = release_keys.load(lst)
    assert keys[0][1].hex() == (lst.parent / "release_key.pub").read_text().strip()
    assert keys[0][0] == "deck-2026"


def test_load_any_reads_a_list_a_one_key_file_a_pem_or_hex(tmp_path):
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (NIGHTLY, "nightly-2026"))
    assert release_keys.load_any(lst) == [("deck-2026", DECK), ("nightly-2026", NIGHTLY)]
    one = tmp_path / "release_key.pub"
    one.write_text(DECK.hex() + "\n")
    assert release_keys.load_any(one) == [("key", DECK)]
    pem = tmp_path / "k.pem"
    pem.write_text(ed25519.public_key_pem(NIGHTLY))
    assert release_keys.load_any(pem) == [("key", NIGHTLY)]
    assert release_keys.load_any(OTHER.hex()) == [("key", OTHER)]
    with pytest.raises(release_keys.KeyListError):
        release_keys.load_any("not a key")


def test_spki_to_hex(tmp_path):
    der = release_keys.SPKI_PREFIX + NIGHTLY
    (tmp_path / "second.der").write_bytes(der)
    (tmp_path / "second.pem").write_text(ed25519.public_key_pem(NIGHTLY))
    for f in ("second.der", "second.pem"):
        r = run(sys.executable, HERE / "release_keys.py", "--spki-to-hex", tmp_path / f)
        assert r.returncode == 0 and r.stdout.strip() == NIGHTLY.hex(), r.stderr
    # a P-256 key (another KeySpec) and a truncated file are refused
    (tmp_path / "p256.der").write_bytes(bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200") + b"\x04" + bytes(64))
    (tmp_path / "short.der").write_bytes(der[:-1])
    for f in ("p256.der", "short.der"):
        r = run(sys.executable, HERE / "release_keys.py", "--spki-to-hex", tmp_path / f)
        assert r.returncode == 2 and "Ed25519 SubjectPublicKeyInfo" in r.stderr and not r.stdout


# ---- the envelope and the rule ------------------------------------------------------------------------------------------------------------
def test_envelope_known_answer():
    """the exact bytes (the launcher's and the game's GDScript make the same; a change here breaks every signed release)"""
    env = release_keys.envelope("manifest.json", b'{"x": 1}\n')
    assert env == (b"OpenBFME signature v2\nmanifest.json\n"
                   b"a6ffa83d44d5481bb3eac7707cb0d5867c2dcc97293dfb85caed0443f6f000e6"
                   b"eaba543533660649f1c751b253bc25f4c9fad75717541e4e258bf9c23aa92935\n")
    assert release_keys.envelope("payload.json", b"").hex() == (
        "4f70656e42464d45207369676e61747572652076320a7061796c6f61642e6a736f6e0a"
        "63663833653133353765656662386264663135343238353064363664383030376436323065343035306235373135646338336634613932316433366365"
        "396365343764306431336335643835663262306666383331386432383737656563326636336239333162643437343137613831613533383332376166"
        "393237646133650a")
    # the envelope signature under the fixed key bytes(1..32) (public 79b5...9664): deterministic Ed25519
    assert DECK.hex() == "79b5562e8fe654f94078b112e8a98ba7901f853ae695bed7e0e3910bad049664"
    assert ed25519.sign(DECK_SECRET, env).hex() == (
        "3beaca2512b0e83200d4aec0958a1a270411b75f5cb4df5db796c736ee0c26bdb74117a031bb39cf67ca7e55fda0e247c6fa9c5c8c94bca8089527896b7b4b05")
    with pytest.raises(ValueError):
        release_keys.envelope("launcher-manifest.json", b"")
    assert release_keys.kind_of("dir/launcher-manifest.json") == "manifest.json" and release_keys.kind_of("notes.md") is None


def test_verify_the_rule():
    data = b'{"version": "v1.0.0"}\n'
    raw_deck = ed25519.sign(DECK_SECRET, data)
    env_sig = env_sign("manifest.json", data)
    old, new, both = [("deck-2026", DECK)], [("nightly-2026", NIGHTLY)], [("deck-2026", DECK), ("nightly-2026", NIGHTLY)]
    assert release_keys.verify(old, "manifest.json", data, raw_deck) == "deck-2026"
    assert release_keys.verify(new, "manifest.json", data, env_sig) == "nightly-2026"
    assert release_keys.verify(both, "manifest.json", data, raw_deck) == "deck-2026"
    assert release_keys.verify(both, "manifest.json", data, env_sig) == "nightly-2026"
    assert release_keys.verify_detail(both, "manifest.json", data, env_sig) == ("nightly-2026", "envelope")
    assert release_keys.verify_detail(both, "manifest.json", data, raw_deck) == ("deck-2026", "raw")
    # the envelope scheme works for any key (the Deck key could sign envelopes too), the raw scheme for the NIGHTLY key's raw signature
    assert release_keys.verify(both, "manifest.json", data, env_sign("manifest.json", data, DECK_SECRET)) == "deck-2026"
    assert release_keys.verify(old, "manifest.json", data, env_sig) is None, "the NIGHTLY key is not in the old list"
    assert release_keys.verify(new, "manifest.json", data, raw_deck) is None
    assert release_keys.verify(both, "manifest.json", data, ed25519.sign(OTHER_SECRET, data)) is None, "an unknown key"
    assert release_keys.verify(both, "manifest.json", data, env_sign("manifest.json", data, OTHER_SECRET)) is None
    assert release_keys.verify(both, "manifest.json", data + b" ", raw_deck) is None, "changed data (raw)"
    assert release_keys.verify(both, "manifest.json", data + b" ", env_sig) is None, "changed data (envelope)"
    assert release_keys.verify(both, "manifest-macos.json", data, env_sig) is None, "the wrong kind"
    assert release_keys.verify(both, "payload.json", data, env_sig) is None
    assert release_keys.verify(both, "manifest-macos.json", data, raw_deck) == "deck-2026", "a raw signature has no kind"
    assert release_keys.verify(both, "nonsense", data, raw_deck) is None, "an unknown kind fails closed"
    for bad in (env_sig[:63], env_sig + b"\0", b"", bytes(64)):
        assert release_keys.verify(both, "manifest.json", data, bad) is None, len(bad)
    assert release_keys.verify([], "manifest.json", data, raw_deck) is None, "an empty list fails"
    # the envelope itself, signed raw, is not a signature of the data, nor of another kind's envelope
    assert release_keys.verify(both, "manifest.json", release_keys.envelope("manifest.json", data), env_sig) == "nightly-2026"
    assert release_keys.verify(both, "payload.json", release_keys.envelope("manifest.json", data), env_sig) == "nightly-2026"
    assert release_keys.verify(both, "manifest.json", data, env_sign("payload.json", data)) is None


def test_the_cli_verify(tmp_path):
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (NIGHTLY, "nightly-2026"))
    m = tmp_path / "manifest.json"
    m.write_bytes(b"{}\n")
    (tmp_path / "s").write_bytes(env_sign("manifest.json", m.read_bytes()))
    r = run(sys.executable, HERE / "release_keys.py", "--verify", lst, "manifest.json", m, tmp_path / "s")
    assert r.returncode == 0 and "nightly-2026, envelope" in r.stdout, r.stdout + r.stderr
    r = run(sys.executable, HERE / "release_keys.py", "--verify", DECK.hex(), "manifest.json", m, tmp_path / "s")
    assert r.returncode == 1 and "INVALID" in r.stdout
    r = run(sys.executable, HERE / "release_keys.py", "--verify", lst, "manifest-macos.json", m, tmp_path / "s")
    assert r.returncode == 1
    r = run(sys.executable, HERE / "release_keys.py", "--hex", lst, "--format", "csv")
    assert r.stdout.strip() == f"{DECK.hex()},{NIGHTLY.hex()}"


# ---- RELGH-2: the nightly-only key list ------------------------------------------------------------------------------------------------
STABLE_LIST = [("deck-2026", DECK)]
NIGHTLY_LIST = [("nightly-2026", NIGHTLY)]
NV = "v0.4.13-nightly.261023100"   # a nightly's tag (NIGHTLY-2); the NIGHTLY-1 form is v0.4.13-nightly.20261023


def doc(version: str, channel: str | None = "preview", **extra) -> bytes:
    d = {"version": version, **({"channel": channel} if channel else {}), **extra}
    return json.dumps(d, indent=2).encode() + b"\n"


def test_load_nightly_rules(tmp_path):
    stable = key_list(tmp_path / "release_keys.pub", (DECK, "deck-2026"))
    assert release_keys.load_nightly(key_list(tmp_path / "n1.pub", (NIGHTLY, "nightly-2026")), stable) == NIGHTLY_LIST
    assert release_keys.load_nightly(tmp_path / "n1.pub", None) == NIGHTLY_LIST
    # an empty list (only comments) is allowed: no nightly can then be signed on GitHub, but every tool still works
    (tmp_path / "empty.pub").write_text("# the nightly-only keys: none\n")
    assert release_keys.load_nightly(tmp_path / "empty.pub", stable) == []
    # up to 4 keys
    four = [(ed25519.public_key(bytes([i]) * 32), f"n{i}") for i in range(1, 5)]
    assert len(release_keys.load_nightly(key_list(tmp_path / "four.pub", *four), stable)) == 4
    five = four + [(ed25519.public_key(bytes([5]) * 32), "n5")]
    with pytest.raises(release_keys.KeyListError, match="more than 4 keys"):
        release_keys.load_nightly(key_list(tmp_path / "five.pub", *five), stable)
    # a stable key in the nightly list: refused (a nightly key is never a stable key), with the stable list given only
    with pytest.raises(release_keys.KeyListError, match="also in release_keys.pub"):
        release_keys.load_nightly(key_list(tmp_path / "overlap.pub", (NIGHTLY, "nightly-2026"), (DECK, "deck-copy")), stable)
    assert len(release_keys.load_nightly(tmp_path / "overlap.pub", None)) == 2, "(None: the overlap is not looked for)"
    # malformed lines are refused as in the stable list
    for text, why in ((f"{NIGHTLY.hex().upper()}  nightly-2026\n", "64 lower-case hex"), (f"{NIGHTLY.hex()}\n", "not '<64 hex>"),
                      (f"{NIGHTLY.hex()}  Nightly\n", "is not [a-z0-9-]"), (f"{NIGHTLY.hex()}  a\n{NIGHTLY.hex()}  b\n", "listed twice"),
                      (f"{'00' * 31}80  zero\n", "not a usable Ed25519 public key")):
        (tmp_path / "bad.pub").write_text(text)
        with pytest.raises(release_keys.KeyListError, match=re.escape(why)):
            release_keys.load_nightly(tmp_path / "bad.pub", stable)
    with pytest.raises(release_keys.KeyListError, match="cannot be read"):
        release_keys.load_nightly(tmp_path / "missing.pub", stable)


def test_the_committed_nightly_list():
    lst = HERE.parents[1] / "launcher" / "nightly_keys.pub"
    keys = release_keys.load_nightly(lst, release_keys.DEFAULT_LIST)     # also: none of them in release_keys.pub
    assert [lb for lb, _ in keys] == ["nightly-2026"]
    assert release_keys.NIGHTLY_LIST == lst


def test_the_nightly_cli(tmp_path):
    stable = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"))
    one = key_list(tmp_path / "n.pub", (NIGHTLY, "nightly-2026"))
    (tmp_path / "none.pub").write_text("# none\n")
    tool = HERE / "release_keys.py"
    r = run(sys.executable, tool, "--check-nightly", one, "--stable", stable)
    assert r.returncode == 0 and "nightly key list ok: nightly-2026" in r.stdout, r.stderr
    r = run(sys.executable, tool, "--check-nightly", tmp_path / "none.pub", "--stable", stable)
    assert r.returncode == 0 and "no key" in r.stdout
    r = run(sys.executable, tool, "--hex-nightly", one, "--stable", stable, "--format", "csv")
    assert r.returncode == 0 and r.stdout.strip() == NIGHTLY.hex()
    assert run(sys.executable, tool, "--hex-nightly", one, "--stable", stable, "--format", "json").stdout.strip() == json.dumps([NIGHTLY.hex()])
    r = run(sys.executable, tool, "--hex-nightly", tmp_path / "none.pub", "--stable", stable, "--format", "json")
    assert r.stdout.strip() == "[]"
    assert run(sys.executable, tool, "--hex-nightly", tmp_path / "none.pub", "--stable", stable, "--format", "csv").stdout.strip() == ""
    overlap = key_list(tmp_path / "o.pub", (DECK, "deck-copy"))
    r = run(sys.executable, tool, "--check-nightly", overlap, "--stable", stable)
    assert r.returncode == 2 and "also in" in r.stderr and not r.stdout


# ---- the scope rule ---------------------------------------------------------------------------------------------------------------------
@pytest.mark.parametrize("scheme", ["raw", "envelope"])
@pytest.mark.parametrize("kind", ["manifest.json", "manifest-macos.json", "payload.json"])
def test_a_nightly_key_signs_a_nightly_document(kind, scheme):
    data = doc(NV)
    sig = ed25519.sign(NIGHTLY_SECRET, data) if scheme == "raw" else env_sign(kind, data)
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, kind, data, sig) == ("nightly-2026", scheme)
    assert release_keys.is_nightly_document(kind, data)
    # the NIGHTLY-1 form of the number (<YYYYMMDD>) too
    old = doc("v0.4.13-nightly.20261023")
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, kind, old, ed25519.sign(NIGHTLY_SECRET, old)) is not None
    # without the nightly list the same signature is nothing; under the stable list alone it never was valid
    assert release_keys.verify_scoped(STABLE_LIST, [], kind, data, sig) is None
    assert release_keys.verify_detail(STABLE_LIST, kind, data, sig) is None


@pytest.mark.parametrize("kind,data", [
    ("manifest.json", doc("v0.4.13", "stable")),                         # a stable manifest
    ("manifest-macos.json", doc("v0.4.13", "stable")),
    ("manifest.json", doc(NV, "stable")),                                 # a nightly's version on the stable channel
    ("manifest-macos.json", doc(NV, "stable")),
    ("manifest.json", doc("v0.4.13", "preview")),                         # the preview channel of a stable version
    ("manifest.json", doc(NV, None)),                                     # no channel
    ("manifest.json", doc("v0.4.13-alpha.1", "preview")),
    ("payload.json", doc("v0.4.13", None)),                               # the payload of a stable package
    ("payload.json", doc("v0.4.13-alpha.1", None)),
    ("payload.json", doc("v0.4.13-nightly.123", None)),                   # not a nightly number (8 or 9 digits)
    ("payload.json", doc("v0.4.13-nightly.2610231000", None)),
    ("payload.json", doc("xv0.4.13-nightly.261023100", None)),
    ("payload.json", doc("v0.4.13-nightly.261023100\n", None)),
    ("payload.json", doc("v01.4.13-nightly.261023100", None)),
    ("payload.json", b"[1]\n"), ("payload.json", b"not json"), ("payload.json", b"\xff\xfe"), ("payload.json", b'{"version": 7}\n'),
    ("manifest.json", b'{"channel": "preview"}\n'),
])
def test_a_nightly_key_never_signs_anything_else(kind, data):
    for sig in (ed25519.sign(NIGHTLY_SECRET, data), env_sign(kind, data)):
        assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, kind, data, sig) is None, data
        assert not release_keys.is_nightly_document(kind, data) or kind == "payload.json" and doc(NV, None) == data
    # the stable key still signs all of it
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, kind, data, ed25519.sign(DECK_SECRET, data)) == ("deck-2026", "raw")


def test_the_stable_list_wins_and_an_unknown_key_is_nothing():
    data = doc(NV)
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, "manifest.json", data, ed25519.sign(DECK_SECRET, data)) == ("deck-2026", "raw")
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, "manifest.json", data, ed25519.sign(OTHER_SECRET, data)) is None
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, "manifest.json", data + b" ", ed25519.sign(NIGHTLY_SECRET, data)) is None
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, "manifest.json", data, bytes(64)) is None
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, "nonsense", data, ed25519.sign(NIGHTLY_SECRET, data)) is None
    assert release_keys.verify_scoped(STABLE_LIST, NIGHTLY_LIST, "manifest-macos.json", data, env_sign("manifest.json", data)) is None


def test_the_cli_verify_with_a_nightly_list(tmp_path):
    stable = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"))
    nl = key_list(tmp_path / "n.pub", (NIGHTLY, "nightly-2026"))
    tool = HERE / "release_keys.py"
    for name, data, ok in (("manifest.json", doc(NV), True), ("manifest.json", doc("v0.4.13", "stable"), False)):
        f = tmp_path / name
        f.write_bytes(data)
        (tmp_path / "s").write_bytes(ed25519.sign(NIGHTLY_SECRET, data))
        r = run(sys.executable, tool, "--verify", stable, name if False else "manifest.json", f, tmp_path / "s", "--nightly", nl)
        assert (r.returncode == 0) is ok, r.stdout + r.stderr
        if ok:
            assert "(nightly-2026, raw)" in r.stdout
        assert run(sys.executable, tool, "--verify", stable, "manifest.json", f, tmp_path / "s").returncode == 1, "no --nightly: invalid"


# ---- sign_manifest.py --verify with a list ------------------------------------------------------------------------------------------------
def test_sign_manifest_verify_with_a_list(tmp_path):
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (OTHER, "second-2026"))
    m = tmp_path / "manifest.json"
    m.write_bytes(b'{"a": 1}\n')
    (tmp_path / "manifest.json.sig").write_bytes(ed25519.sign(DECK_SECRET, m.read_bytes()))
    (tmp_path / "manifest.json.sig2").write_bytes(env_sign("manifest.json", m.read_bytes(), OTHER_SECRET))
    tool = HERE / "sign_manifest.py"
    r = run(sys.executable, tool, "--verify", lst, m)
    assert r.returncode == 0 and "(deck-2026, raw)" in r.stdout, r.stdout + r.stderr
    r = run(sys.executable, tool, "--verify", lst, m, "--sig", tmp_path / "manifest.json.sig2")
    assert r.returncode == 0 and "(second-2026, envelope)" in r.stdout
    assert run(sys.executable, tool, "--verify", DECK.hex(), m).returncode == 0, "the old single-key form"
    assert run(sys.executable, tool, "--verify", DECK.hex(), m, "--sig", tmp_path / "manifest.json.sig2").returncode == 1
    assert run(sys.executable, tool, "--verify", lst, m, "--sig", tmp_path / "manifest.json.sig2", "--kind", "payload.json").returncode == 1
    proof = tmp_path / "launcher-manifest.json"
    shutil.copy(m, proof)
    shutil.copy(tmp_path / "manifest.json.sig2", tmp_path / "launcher-manifest.json.sig")
    assert run(sys.executable, tool, "--verify", lst, proof).returncode == 0, "the launcher's proof copy is kind manifest.json"
    other = tmp_path / "other.json"
    shutil.copy(m, other)
    shutil.copy(tmp_path / "manifest.json.sig", tmp_path / "other.json.sig")
    r = run(sys.executable, tool, "--verify", lst, other)
    assert r.returncode != 0 and "pass --kind" in r.stderr
    assert run(sys.executable, tool, "--verify", lst, other, "--kind", "manifest.json").returncode == 0
    (tmp_path / "manifest.json.sig").unlink()
    r = run(sys.executable, tool, "--verify", lst, m)
    assert r.returncode == 1 and "does not exist" in r.stdout


def signed_manifest(d: Path, name: str, data: bytes, secret: bytes = NIGHTLY_SECRET) -> Path:
    d.mkdir(parents=True, exist_ok=True)
    (d / name).write_bytes(data)
    (d / (name + ".sig")).write_bytes(ed25519.sign(secret, data))
    return d / name


def test_sign_manifest_verify_scopes_a_nightly_key(tmp_path):
    stable = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"))
    nl = key_list(tmp_path / "n.pub", (NIGHTLY, "nightly-2026"))
    tool = HERE / "sign_manifest.py"
    for name in ("manifest.json", "manifest-macos.json"):
        good = signed_manifest(tmp_path / "nightly", name, doc(NV))
        r = run(sys.executable, tool, "--verify", stable, good, "--nightly", nl)
        assert r.returncode == 0 and "(nightly-2026, raw)" in r.stdout, r.stdout + r.stderr
        assert run(sys.executable, tool, "--verify", stable, good).returncode == 1, "the nightly list is not given: invalid"
        for bad in (doc("v0.4.13", "stable"), doc(NV, "stable"), doc("v0.4.13", "preview")):
            f = signed_manifest(tmp_path / f"bad-{hash(bad) & 0xffff}", name, bad)
            r = run(sys.executable, tool, "--verify", stable, f, "--nightly", nl)
            assert r.returncode == 1 and "INVALID" in r.stdout, bad
    # the stable key under both lists: still valid for a stable manifest
    f = signed_manifest(tmp_path / "stable", "manifest.json", doc("v0.4.13", "stable"), DECK_SECRET)
    assert run(sys.executable, tool, "--verify", stable, f, "--nightly", nl).returncode == 0
    # the nightly list must itself be a valid one
    (tmp_path / "bad.pub").write_text("zz\n")
    r = run(sys.executable, tool, "--verify", stable, f, "--nightly", tmp_path / "bad.pub")
    assert r.returncode != 0 and "sign_manifest" in r.stderr


# ---- seal_package.py with the nightly list -------------------------------------------------------------------------------------------------
def package_dir(d: Path, version: str) -> Path:
    from test_autorelease import package_dir as make
    return make(d, version)


def test_seal_check_scopes_a_nightly_key(tmp_path):
    nl = key_list(tmp_path / "n.pub", (NIGHTLY, "nightly-2026"))
    tool = HERE / "seal_package.py"
    d = package_dir(tmp_path / "nightly", NV)
    seal_package.seal(d, NIGHTLY_SECRET, TARGET)
    assert seal_package.check(d, STABLE_LIST, TARGET) != [], "a nightly key is no stable key"
    assert seal_package.check(d, STABLE_LIST, TARGET, NIGHTLY_LIST) == []
    r = run(sys.executable, tool, "check", d, "--public", DECK.hex(), "--repo", TARGET)
    assert r.returncode == 1 and "does not verify" in r.stdout
    r = run(sys.executable, tool, "check", d, "--public", DECK.hex(), "--nightly", nl, "--repo", TARGET)
    assert r.returncode == 0 and "seal ok" in r.stdout, r.stdout + r.stderr
    # the same key on a STABLE version's package: refused whatever the lists say
    s = package_dir(tmp_path / "stable", "v0.4.13")
    seal_package.seal(s, NIGHTLY_SECRET, TARGET)
    problems = seal_package.check(s, STABLE_LIST, TARGET, NIGHTLY_LIST)
    assert problems and all("does not verify" in p and "nightly key signs nightly packages only" in p for p in problems)
    r = run(sys.executable, tool, "check", s, "--public", DECK.hex(), "--nightly", nl, "--repo", TARGET)
    assert r.returncode == 1 and "nightly packages only" in r.stdout
    # the Deck's key still seals a stable package
    t = package_dir(tmp_path / "deck", "v0.4.13")
    seal_package.seal(t, DECK_SECRET, TARGET)
    assert seal_package.check(t, STABLE_LIST, TARGET, NIGHTLY_LIST) == []
    # a malformed nightly list stops the check
    (tmp_path / "bad.pub").write_text("zz\n")
    r = run(sys.executable, tool, "check", d, "--public", DECK.hex(), "--nightly", tmp_path / "bad.pub", "--repo", TARGET)
    assert r.returncode != 0 and "seal_package" in r.stderr


def test_seal_with_a_key_file_and_no_kms(tmp_path):
    d = package_dir(tmp_path / "pkg", NV)
    key = tmp_path / "keys" / "nightly.pem"
    key.parent.mkdir()
    key.write_text(ed25519.private_key_pem(NIGHTLY_SECRET))
    key.chmod(0o600)
    r = run(sys.executable, HERE / "seal_package.py", "seal", d, "--key", key, "--repo", TARGET)
    assert r.returncode == 0 and "seal ok" in r.stdout, r.stdout + r.stderr
    assert seal_package.check(d, STABLE_LIST, TARGET, NIGHTLY_LIST) == []
    files, _, _ = seal_package.read_archive(next(iter(seal_package.combined_archives(d))))
    assert release_keys.verify_detail(NIGHTLY_LIST, "payload.json", files["payload.json"], files["payload.json.sig"]) == ("nightly-2026", "raw")
    r = run(sys.executable, HERE / "seal_package.py", "seal", package_dir(tmp_path / "p2", NV), "--kms", "--repo", TARGET)
    assert r.returncode == 2 and "required: --key" in r.stderr, "the KMS signer is gone"
    # the old single-key callers still work
    d3 = package_dir(tmp_path / "pkg3", "v1.0.0")
    seal_package.seal(d3, DECK_SECRET, TARGET)
    assert seal_package.check(d3, DECK, TARGET) == [] and seal_package.check(d3, STABLE_LIST, TARGET) == []


# ---- publish_release.sh with .sig and .sig2 ----------------------------------------------------------------------------------------------
@pytest.fixture
def denylist(tmp_path):
    f = tmp_path / "denylist.json"
    f.write_text(json.dumps({"rules": [{"id": "hosts", "kind": "word", "pattern": ["pubhost-q"]}]}))
    return f


COMMIT = "0123456789abcdef0123456789abcdef01234567"


def test_release_assets_name_the_sig2_slots(tmp_path):
    d = package_dir(tmp_path / "pkg", "v1.0.0")
    assert run(sys.executable, HERE / "make_manifest.py", d, "--repo", TARGET, "--commit", COMMIT, "--date", "2026-10-22",
               "--allow-untagged").returncode == 0
    names = lambda: list(release_assets.expected(d, "v1.0.0"))  # noqa: E731
    with pytest.raises(FileNotFoundError, match="manifest.json.sig"):
        names()   # no signature at all: the classic slot is named, and fails
    (d / "manifest.json.sig2").write_bytes(b"s" * 64)
    assert names()[-2:] == ["manifest.json", "manifest.json.sig2"]
    (d / "manifest.json.sig").write_bytes(b"s" * 64)
    assert names()[-3:] == ["manifest.json", "manifest.json.sig", "manifest.json.sig2"]


@pytest.fixture
def publisher(tmp_path, denylist):
    """a copy of the release scripts with its own launcher/ lists (publish_release.sh reads launcher/nightly_keys.pub beside its tools/)"""
    repo = tmp_path / "repo"
    shutil.copytree(HERE, repo / "tools/release", ignore=shutil.ignore_patterns("__pycache__", ".pytest_cache"))
    shutil.copy2(HERE.parents[1] / "tools/precommit.py", repo / "tools/precommit.py")
    (repo / "launcher").mkdir()
    key_list(repo / "launcher/release_keys.pub", (DECK, "deck-2026"))
    (repo / "launcher/release_key.pub").write_text(DECK.hex() + "\n")
    key_list(repo / "launcher/nightly_keys.pub", (NIGHTLY, "nightly-2026"))

    class P:
        root = repo

        def package(self, version: str) -> Path:
            d = package_dir(tmp_path / f"pkg-{version}", version)
            assert run(sys.executable, HERE / "make_manifest.py", d, "--repo", TARGET, "--commit", COMMIT, "--date", "2026-10-22",
                       "--allow-untagged").returncode == 0
            return d

        def publish(self, d: Path):
            return run("bash", repo / "tools/release/publish_release.sh", d, "--repo", TARGET, "--key-file", repo / "launcher/release_keys.pub",
                       "--allow-untagged", env={"OPENBFME_RELEASE_DENYLIST": str(denylist)})
    return P()


def test_publish_dry_run_with_sig_and_sig2(publisher):
    d = publisher.package("v1.0.0")
    data = (d / "manifest.json").read_bytes()
    publish = lambda: publisher.publish(d)  # noqa: E731
    (d / "manifest.json.sig").write_bytes(ed25519.sign(DECK_SECRET, data))
    (d / "manifest.json.sig2").write_bytes(env_sign("manifest.json", data, DECK_SECRET))
    r = publish()
    assert r.returncode == 0 and "DRY RUN" in r.stdout, r.stdout + r.stderr
    upload = next(ln for ln in r.stdout.splitlines() if " release upload " in ln)
    assert upload.rstrip().endswith("manifest.json.sig") is False and "manifest.json.sig2" in upload and "manifest.json.sig " in upload
    (d / "manifest.json.sig").unlink()
    r = publish()
    assert r.returncode == 0, r.stdout + r.stderr
    upload = next(ln for ln in r.stdout.splitlines() if " release upload " in ln)
    assert upload.rstrip().endswith("manifest.json.sig2") and "manifest.json.sig " not in upload
    # a .sig2 by an unknown key, or a .sig that does not verify beside a good .sig2, stops it
    (d / "manifest.json.sig2").write_bytes(env_sign("manifest.json", data, OTHER_SECRET))
    r = publish()
    assert r.returncode != 0 and "does not verify" in r.stderr, r.stdout + r.stderr
    (d / "manifest.json.sig2").write_bytes(env_sign("manifest.json", data, DECK_SECRET))
    (d / "manifest.json.sig").write_bytes(ed25519.sign(OTHER_SECRET, data))
    r = publish()
    assert r.returncode != 0 and "manifest.json.sig)" in r.stderr, r.stdout + r.stderr
    # the wrong kind (a payload envelope) is refused
    (d / "manifest.json.sig").unlink()
    (d / "manifest.json.sig2").write_bytes(env_sign("payload.json", data, DECK_SECRET))
    assert publish().returncode != 0
    (d / "manifest.json.sig2").unlink()
    r = publish()
    assert r.returncode != 0 and "no manifest.json.sig or manifest.json.sig2" in r.stderr


def test_publish_accepts_a_nightly_key_for_a_nightly_alone(publisher):
    nightly = publisher.package(NV)
    (nightly / "manifest.json.sig").write_bytes(ed25519.sign(NIGHTLY_SECRET, (nightly / "manifest.json").read_bytes()))
    r = publisher.publish(nightly)
    assert r.returncode == 0 and "DRY RUN" in r.stdout, r.stdout + r.stderr
    # the same key on a STABLE manifest: refused (the nightly list does not make a stable release valid)
    stable = publisher.package("v0.4.13")
    (stable / "manifest.json.sig").write_bytes(ed25519.sign(NIGHTLY_SECRET, (stable / "manifest.json").read_bytes()))
    r = publisher.publish(stable)
    assert r.returncode != 0 and "does not verify" in r.stderr, r.stdout + r.stderr
    assert "DRY RUN" not in r.stdout
    (stable / "manifest.json.sig").write_bytes(ed25519.sign(DECK_SECRET, (stable / "manifest.json").read_bytes()))
    assert publisher.publish(stable).returncode == 0, "the stable key still publishes it"
    # and the nightly list has to be there
    (publisher.root / "launcher/nightly_keys.pub").unlink()
    r = publisher.publish(nightly)
    assert r.returncode != 0 and "launcher/nightly_keys.pub is not committed" in r.stderr


def test_the_privacy_gate_takes_sig2_for_a_signature():
    import autorelease
    assert autorelease.is_detached_signature("manifest.json.sig2", bytes(64), 0)
    assert autorelease.is_detached_signature("manifest-macos.json.sig2", bytes(64), 0)
    assert not autorelease.is_detached_signature("manifest.json.sig3", bytes(64), 0)


# ---- check_draft.py with the stand-in gh -----------------------------------------------------------------------------------------------------
REQUEST = "0123456789abcdef0123456789abcdef"


def git(repo: Path, *args: str) -> str:
    r = run("git", "-C", repo, *args)
    assert r.returncode == 0, r.stderr
    return r.stdout.strip()


@pytest.fixture(scope="module")
def built(tmp_path_factory):
    """one release folder per tag, made like the release workflow makes it (actions_release.py sign): the packages and the Mac package sealed
    with the NIGHTLY key, the manifests and their raw X.sig by it; a public remote with main and the tags. v0.4.13 is a STABLE version signed
    the same way (what a stolen nightly key would make). RELGH-3: the stable list holds the stable Actions key stable-2026 after the Deck
    key; v0.4.16 is a stable release made by the workflow with it, v0.4.17 one signed by the Deck key (which never signs on GitHub)"""
    import make_macos_manifest
    from test_autorelease import mac_package_dir
    base = tmp_path_factory.mktemp("draft")
    remote = base / "public.git"
    seed = base / "seed"
    run("git", "init", "-q", "--bare", "-b", "main", remote)
    run("git", "init", "-q", "-b", "main", seed)
    (seed / "README.md").write_text("public\n")
    git(seed, "add", "-A")
    git(seed, "commit", "-q", "-m", "one")
    c1 = git(seed, "rev-parse", "HEAD")
    (seed / "README.md").write_text("public 2\n")
    git(seed, "commit", "-q", "-am", "two")
    c2 = git(seed, "rev-parse", "HEAD")
    git(seed, "push", "-q", str(remote), "main")
    git(seed, "checkout", "-q", "-b", "side", c1)
    (seed / "side.txt").write_text("not on main\n")
    git(seed, "add", "-A")
    git(seed, "commit", "-q", "-m", "side")
    side = git(seed, "rev-parse", "HEAD")
    git(seed, "push", "-q", str(remote), "side")
    checkout = base / "checkout"
    (checkout / "launcher").mkdir(parents=True)
    (checkout / "launcher/release_key.pub").write_text(DECK.hex() + "\n")
    key_list(checkout / "launcher/release_keys.pub", (DECK, "deck-2026"), (STABLE, "stable-2026"))
    key_list(checkout / "launcher/nightly_keys.pub", (NIGHTLY, "nightly-2026"))
    folders = {}
    signer = {"v0.4.16": STABLE_SECRET, "v0.4.17": DECK_SECRET}
    for version, commit in (("v0.4.13-nightly.20261022", c1), ("v0.4.13", c1), ("v0.4.14-nightly.20261023", side),
                            ("v0.4.15-nightly.20261024", c1), ("v0.4.16", c1), ("v0.4.17", c1)):
        secret = signer.get(version, NIGHTLY_SECRET)
        git(seed, "tag", version, c2 if version.startswith("v0.4.15") else commit)   # v0.4.15: the tag is not the manifest's commit
        d = package_dir(base / f"pkg-{version}", version)
        mac = mac_package_dir(checkout, version, base / f"mac-{version}", commit=commit)
        shutil.copy2(mac, d / mac.name)
        with open(d / f"SHA256SUMS-{version}.txt", "a") as f:
            f.write(f"{hashlib.sha256(mac.read_bytes()).hexdigest()}  {mac.name}\n")
        seal_package.seal(d, secret, TARGET)
        assert run(sys.executable, HERE / "make_manifest.py", d, "--repo", TARGET, "--commit", commit, "--date", "2026-10-22",
                   "--allow-untagged").returncode == 0
        sealed = base / f"{mac.name}.sealed.sha256"
        sealed.write_text(f"{hashlib.sha256((d / mac.name).read_bytes()).hexdigest()}  {mac.name}\n")
        (d / "manifest-macos.json").write_bytes(make_macos_manifest.dumps(make_macos_manifest.build(
            version, TARGET, commit, "2026-10-22", archive=d / mac.name, checksum=sealed)))
        for m in ("manifest.json", "manifest-macos.json"):
            (d / f"{m}.sig").write_bytes(ed25519.sign(secret, (d / m).read_bytes()))
        (d / f"BUILDINFO-{version}.txt").unlink()
        folders[version] = d
    git(seed, "push", "-q", str(remote), "--tags")
    return {"remote": remote, "folders": folders, "checkout": checkout, "main": c2, "side": side}


class Drafter:
    def __init__(self, tmp: Path, built: dict):
        self.tmp, self.built = tmp, built
        self.state = tmp / "gh.json"
        self.log = tmp / "gh.log"
        self.gh = tmp / "bin" / "gh"
        self.gh.parent.mkdir()
        self.gh.write_text(f"#!/bin/bash\nexec {sys.executable} {HERE / 'gh_stand_in.py'} \"$@\"\n")
        self.gh.chmod(0o755)
        self.keys = built["checkout"] / "launcher/release_keys.pub"
        self.nightly = built["checkout"] / "launcher/nightly_keys.pub"
        self.runs = 0

    def draft(self, version: str, author: str = "github-actions[bot]", request: str = REQUEST, change=None, extra: dict | None = None,
              created: str = "2026-10-22T01:00:00Z", prerelease: bool | None = None) -> int:
        """a draft of `version` with the folder's files as its assets (change(dict name -> bytes) may tamper with them first)"""
        s = json.loads(self.state.read_text()) if self.state.exists() else {"prs": {}, "releases": [], "next": 100}
        files = {p.name: p.read_bytes() for p in sorted(self.built["folders"][version].iterdir())}
        if change:
            change(files)
        files.update(extra or {})
        store = Path(str(self.state) + ".d")
        store.mkdir(exist_ok=True)
        rid = s["next"]
        s["next"] += 1
        rel = {"id": rid, "tag_name": version, "name": f"OpenBFME {version}", "draft": True,
               "prerelease": "-" in version if prerelease is None else prerelease,
               "body": f"OpenBFME {version}: signed on GitHub Actions (run 7).\n\nopenbfme-release-request: {request}\n",
               "assets": [], "author": {"login": author}, "created_at": created}
        for name, data in files.items():
            aid = s["next"]
            s["next"] += 1
            (store / str(aid)).write_bytes(data)
            rel["assets"].append({"id": aid, "name": name, "size": len(data), "digest": "sha256:" + hashlib.sha256(data).hexdigest(),
                                  "state": "uploaded"})
        s["releases"].append(rel)
        self.state.write_text(json.dumps(s, indent=1))
        return rid

    def release(self, rid: int) -> dict:
        return next(r for r in json.loads(self.state.read_text())["releases"] if r["id"] == rid)

    def run(self, version: str, rid: int, *extra, request: str = REQUEST, fault: str = "", keys: Path | None = None,
            nightly: Path | None = None):
        self.runs += 1
        out = self.tmp / f"out-{self.runs}"
        r = run(sys.executable, HERE / "check_draft.py", "--repo", TARGET, "--tag", version, "--release-id", rid, "--request", request,
                "--out", out, "--keys", keys or self.keys, "--nightly-keys", nightly or self.nightly, *extra,
                env={"GH": str(self.gh), "FAKE_GH_STATE": str(self.state), "FAKE_GH_REMOTE": str(self.built["remote"]),
                     "FAKE_GH_LOG": str(self.log), "FAKE_GH_FAULT": fault})
        return r, out


@pytest.fixture
def drafts(tmp_path, built):
    return Drafter(tmp_path, built)


NIGHTLY_TAG = "v0.4.13-nightly.20261022"


def test_check_draft_passes_a_nightly_draft_and_changes_nothing(drafts):
    rid = drafts.draft(NIGHTLY_TAG)
    before = drafts.release(rid)
    r, out = drafts.run(NIGHTLY_TAG, rid)
    assert r.returncode == 0 and f"DRAFT CHECK {NIGHTLY_TAG}: checks passed (signed by the nightly key; nothing added)" in r.stdout, \
        r.stdout + r.stderr
    rel = drafts.release(rid)
    assert rel == before and rel["draft"] is True, "never published, nothing added or changed"
    assert sorted(p.name for p in out.iterdir()) == sorted(a["name"] for a in rel["assets"]), "the kept folder is exactly the release"
    assert not any(p.name.endswith(".sig2") for p in out.iterdir())
    log = drafts.log.read_text()
    assert "uploads.github.com" not in log and "--method PATCH" not in log and "--method POST" not in log and "--method DELETE" not in log
    for m in ("manifest.json", "manifest-macos.json"):
        got = release_keys.verify_scoped(release_keys.load(drafts.keys), release_keys.load_nightly(drafts.nightly, None), m,
                                         (out / m).read_bytes(), (out / f"{m}.sig").read_bytes())
        assert got == ("nightly-2026", "raw")


def tamper_archive(files: dict) -> None:
    name = next(n for n in files if n.endswith("linux-x64.tar.gz") and n.startswith("openbfme-v"))
    files[name] = files[name] + b"\0"


def sig2_present(files: dict) -> None:
    files["manifest.json.sig2"] = env_sign("manifest.json", files["manifest.json"])


def deck_sig(files: dict) -> None:
    files["manifest.json.sig"] = ed25519.sign(DECK_SECRET, files["manifest.json"])    # a trusted key, but not a nightly key's


def unknown_sig(files: dict) -> None:
    files["manifest.json.sig"] = ed25519.sign(OTHER_SECRET, files["manifest.json"])


def envelope_sig(files: dict) -> None:
    files["manifest.json.sig"] = env_sign("manifest.json", files["manifest.json"])    # the nightly key, the wrong scheme


def no_sig(files: dict) -> None:
    del files["manifest-macos.json.sig"]


def extra_asset(files: dict) -> None:
    files["notes.txt"] = b"not a release asset\n"


def changed_mac(files: dict) -> None:
    name = next(n for n in files if n.endswith("macos-arm64.zip"))
    files[name] = files[name][:-1] + bytes([files[name][-1] ^ 1])


@pytest.mark.parametrize("version,kw,why", [
    (NIGHTLY_TAG, {"change": tamper_archive}, "make_manifest.py --check"),
    (NIGHTLY_TAG, {"change": changed_mac}, "make_macos_manifest.py --check"),
    (NIGHTLY_TAG, {"change": sig2_present}, "manifest.json.sig2 exists"),
    (NIGHTLY_TAG, {"change": deck_sig}, "manifest.json.sig is not a nightly key's signature"),
    (NIGHTLY_TAG, {"change": unknown_sig}, "manifest.json.sig is not a nightly key's signature"),
    (NIGHTLY_TAG, {"change": envelope_sig}, "manifest.json.sig is not a nightly key's signature"),
    (NIGHTLY_TAG, {"change": no_sig}, "manifest-macos.json has no manifest-macos.json.sig"),
    (NIGHTLY_TAG, {"change": extra_asset}, "extra ['notes.txt']"),
    (NIGHTLY_TAG, {"author": "someone"}, "not created by the release workflow"),
    (NIGHTLY_TAG, {"request": "f" * 32}, "does not carry the line"),
    (NIGHTLY_TAG, {"prerelease": False}, "pre-release flag"),
    ("v0.4.14-nightly.20261023", {}, "is not on the public main"),
    ("v0.4.15-nightly.20261024", {}, "not the manifest's commit"),
    # a STABLE version signed by the nightly key (a stolen key): the scope rule refuses its signatures
    ("v0.4.13", {}, "manifest.json.sig is not the stable Actions key stable-2026's signature"),
    ("v0.4.13", {"prerelease": True}, "pre-release flag"),
    # RELGH-3: a stable draft signed by the Deck key (it never signs on GitHub) or by an unknown key; flagged as a pre-release
    ("v0.4.17", {}, "manifest.json.sig is not the stable Actions key stable-2026's signature"),
    ("v0.4.16", {"change": unknown_sig}, "manifest.json.sig is not the stable Actions key stable-2026's signature"),
    ("v0.4.16", {"change": envelope_sig}, "manifest.json.sig is not the stable Actions key stable-2026's signature"),
    ("v0.4.16", {"prerelease": True}, "pre-release flag"),
    ("v0.4.16", {"author": "someone"}, "not created by the release workflow"),
    ("v0.4.16", {"change": tamper_archive}, "make_manifest.py --check"),
    ("v0.4.16-alpha.1", {}, "neither a nightly nor a stable release"),
])
def test_check_draft_refuses(drafts, version, kw, why):
    rid = drafts.draft(version if version in drafts.built["folders"] else "v0.4.16", **kw)
    if version not in drafts.built["folders"]:
        s = json.loads(drafts.state.read_text())
        s["releases"][-1]["tag_name"] = version
        drafts.state.write_text(json.dumps(s))
    before = drafts.release(rid)
    r, _ = drafts.run(version, rid)
    assert r.returncode == 1 and why in r.stdout and "DRAFT CHECK FAIL" in r.stdout, r.stdout + r.stderr
    assert drafts.release(rid) == before, "nothing uploaded, nothing changed"
    assert "uploads.github.com" not in drafts.log.read_text()


def test_check_draft_refuses_a_stable_manifest_under_the_nightly_key_whatever_the_tag(drafts, built):
    """the nightly key signs a manifest of a stable version: the scope rule alone (not the tag check) refuses that signature"""
    keys, nightly = release_keys.load(drafts.keys), release_keys.load_nightly(drafts.nightly, None)
    folder = built["folders"]["v0.4.13"]
    for m in ("manifest.json", "manifest-macos.json"):
        assert release_keys.verify_scoped(keys, nightly, m, (folder / m).read_bytes(), (folder / f"{m}.sig").read_bytes()) is None
    # and its sealed payloads
    assert seal_package.check(folder, keys, TARGET, nightly) != []
    # RELGH-3: a stable tag needs the stable Actions key's label among the stable keys after the Deck key
    import check_draft
    for label in (None, "deck-2026", "nightly-2026"):
        c = check_draft.DraftChecker(TARGET, keys, nightly, "main", say=lambda *_: None, stable_label=label)
        rel = {"tag_name": "v0.4.13", "draft": True}
        with pytest.raises(check_draft.Refused, match="is not a key of launcher/release_keys.pub after its first entry"):
            c.check_release(rel, "v0.4.13", REQUEST, drafts.tmp / "x", drafts.keys, drafts.nightly)


def test_check_draft_passes_a_stable_draft_signed_by_the_stable_actions_key(drafts):
    """RELGH-3: the workflow's stable draft (the owner approved the run): signed by stable-2026, a full release, nothing added"""
    rid = drafts.draft("v0.4.16")
    before = drafts.release(rid)
    r, out = drafts.run("v0.4.16", rid)
    assert r.returncode == 0 and "DRAFT CHECK v0.4.16: checks passed (signed by the stable Actions key stable-2026; nothing added)" in r.stdout, \
        r.stdout + r.stderr
    assert drafts.release(rid) == before and before["prerelease"] is False
    for m in ("manifest.json", "manifest-macos.json"):
        assert release_keys.verify_detail(release_keys.load(drafts.keys), m, (out / m).read_bytes(), (out / f"{m}.sig").read_bytes()) \
            == ("stable-2026", "raw")
    # another configuration's key label (the label check follows stable_environment.json)
    cfg = json.loads((HERE / "stable_environment.json").read_text())
    cfg["key_label"] = "stable-2027"
    other = drafts.tmp / "env.json"
    other.write_text(json.dumps(cfg))
    r, _ = drafts.run("v0.4.16", rid, "--environment-config", other)
    assert r.returncode == 1 and "'stable-2027' is not a key of launcher/release_keys.pub" in r.stdout, r.stdout + r.stderr


def test_check_draft_refuses_a_stale_draft_another_tag_or_a_published_release(drafts):
    old = drafts.draft(NIGHTLY_TAG, created="2026-10-22T01:00:00Z")
    drafts.draft(NIGHTLY_TAG, created="2026-10-22T02:00:00Z")
    r, _ = drafts.run(NIGHTLY_TAG, old)
    assert r.returncode == 1 and f"is not the newest draft of {NIGHTLY_TAG}" in r.stdout, r.stdout
    r, _ = drafts.run("v0.4.15-nightly.20261024", old)
    assert r.returncode == 1 and f"the release's tag is '{NIGHTLY_TAG}'" in r.stdout
    s = json.loads(drafts.state.read_text())
    for rel in s["releases"]:
        rel["draft"] = False
    drafts.state.write_text(json.dumps(s))
    r, _ = drafts.run(NIGHTLY_TAG, old)
    assert r.returncode == 1 and "is not a draft" in r.stdout
    r, _ = drafts.run(NIGHTLY_TAG, 99999)
    assert r.returncode == 1 and "Not Found" in r.stdout


def test_check_draft_needs_a_usable_nightly_list(drafts, tmp_path):
    rid = drafts.draft(NIGHTLY_TAG)
    empty = tmp_path / "empty.pub"
    empty.write_text("# none\n")
    r, _ = drafts.run(NIGHTLY_TAG, rid, nightly=empty)
    assert r.returncode != 0 and "lists no key" in r.stdout + r.stderr
    overlap = key_list(tmp_path / "overlap.pub", (DECK, "deck-copy"))
    r, _ = drafts.run(NIGHTLY_TAG, rid, nightly=overlap)
    assert r.returncode != 0 and "also in" in r.stdout + r.stderr
    # another nightly key: the draft's signature is no longer a listed key's
    other = key_list(tmp_path / "other.pub", (OTHER, "other-2026"))
    r, _ = drafts.run(NIGHTLY_TAG, rid, nightly=other)
    assert r.returncode == 1 and "not a nightly key's signature" in r.stdout
    assert drafts.release(rid)["draft"] is True


def test_check_draft_argument_checks(drafts):
    rid = drafts.draft(NIGHTLY_TAG)
    r, out = drafts.run(NIGHTLY_TAG, rid, request="short")
    assert r.returncode != 0 and "32 lower-case hex" in r.stderr
    out.mkdir()
    r = run(sys.executable, HERE / "check_draft.py", "--repo", TARGET, "--tag", NIGHTLY_TAG, "--release-id", rid, "--request", REQUEST,
            "--out", out, env={"GH": str(drafts.gh), "FAKE_GH_STATE": str(drafts.state), "FAKE_GH_REMOTE": str(drafts.built["remote"])})
    assert r.returncode != 0 and "exists" in r.stderr
