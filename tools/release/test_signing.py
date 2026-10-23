"""Lane SIGNKMS-1: the trusted key list, the signature envelope and the verification rule (release_keys.py), the KMS signer (kms_sign.py
with the stand-in aws, aws_stand_in.py), sealing through KMS, sign_manifest.py --verify and publish_release.sh with a key list and
.sig2 files, and the Deck co-signer (cosign_release.py with the stand-in gh). Never the real AWS, key or GitHub."""
from __future__ import annotations

import hashlib
import json
import os
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
KMS_SECRET = bytes(range(33, 65))
OTHER_SECRET = bytes(range(65, 97))
DECK, KMS, OTHER = (ed25519.public_key(s) for s in (DECK_SECRET, KMS_SECRET, OTHER_SECRET))
TARGET = "Test/repo"
GIT_ENV = {"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@example.invalid", "GIT_COMMITTER_NAME": "t",
           "GIT_COMMITTER_EMAIL": "t@example.invalid", "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}


def run(*cmd, env=None, **kw) -> subprocess.CompletedProcess:
    return subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=600, stdin=subprocess.DEVNULL,
                          env={**os.environ, **GIT_ENV, **(env or {})}, **kw)


def key_list(path: Path, *entries: tuple[bytes, str]) -> Path:
    path.write_text("# test list\n" + "".join(f"{k.hex()}  {label}\n" for k, label in entries))
    return path


def kms_env_sign(kind: str, data: bytes, secret: bytes = KMS_SECRET) -> bytes:
    return ed25519.sign(secret, release_keys.envelope(kind, data))


class EnvelopeSigner:
    """the KMS key's scheme with a local key (what kms_sign.KmsSigner gets back from KMS)"""

    def __init__(self, secret: bytes = KMS_SECRET):
        self.secret = secret

    def sign(self, kind: str, data: bytes) -> bytes:
        return kms_env_sign(kind, data, self.secret)


# ---- the key list ---------------------------------------------------------------------------------------------------------------------------
def test_the_list_parses_with_comments_and_blank_lines(tmp_path):
    text = f"# the release keys\n\n{DECK.hex()}  deck-2026\n   \n# the KMS key\n{KMS.hex()}\tkms-2026\r\n"
    assert release_keys.parse(text) == [("deck-2026", DECK), ("kms-2026", KMS)]
    p = tmp_path / "keys.pub"
    p.write_text(text)
    assert release_keys.load(p) == [("deck-2026", DECK), ("kms-2026", KMS)]


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
    (f"{DECK.hex()}  deck-2026\n{KMS.hex()}  deck-2026\n", "used twice"),
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
    lst = key_list(d / "release_keys.pub", (DECK, "deck-2026"), (KMS, "kms-2026"))
    (d / "release_key.pub").write_text(DECK.hex() + "\n")
    assert [lb for lb, _ in release_keys.load(lst)] == ["deck-2026", "kms-2026"]
    (d / "release_key.pub").write_text(KMS.hex() + "\n")
    with pytest.raises(release_keys.KeyListError, match="first entry"):
        release_keys.load(lst)
    assert release_keys.load(lst, legacy=False)[0] == ("deck-2026", DECK)
    alone = key_list(tmp_path / "elsewhere.pub", (KMS, "kms-2026"), (DECK, "deck-2026"))
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
    policy = release_keys.load_policy()
    assert keys[0][0] == policy["legacy_label"]


def test_load_any_reads_a_list_a_one_key_file_a_pem_or_hex(tmp_path):
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (KMS, "kms-2026"))
    assert release_keys.load_any(lst) == [("deck-2026", DECK), ("kms-2026", KMS)]
    one = tmp_path / "release_key.pub"
    one.write_text(DECK.hex() + "\n")
    assert release_keys.load_any(one) == [("key", DECK)]
    pem = tmp_path / "k.pem"
    pem.write_text(ed25519.public_key_pem(KMS))
    assert release_keys.load_any(pem) == [("key", KMS)]
    assert release_keys.load_any(OTHER.hex()) == [("key", OTHER)]
    with pytest.raises(release_keys.KeyListError):
        release_keys.load_any("not a key")


def test_spki_to_hex(tmp_path):
    der = release_keys.SPKI_PREFIX + KMS
    (tmp_path / "kms.der").write_bytes(der)
    (tmp_path / "kms.pem").write_text(ed25519.public_key_pem(KMS))
    for f in ("kms.der", "kms.pem"):
        r = run(sys.executable, HERE / "release_keys.py", "--spki-to-hex", tmp_path / f)
        assert r.returncode == 0 and r.stdout.strip() == KMS.hex(), r.stderr
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
    env_kms = kms_env_sign("manifest.json", data)
    old, new, both = [("deck-2026", DECK)], [("kms-2026", KMS)], [("deck-2026", DECK), ("kms-2026", KMS)]
    assert release_keys.verify(old, "manifest.json", data, raw_deck) == "deck-2026"
    assert release_keys.verify(new, "manifest.json", data, env_kms) == "kms-2026"
    assert release_keys.verify(both, "manifest.json", data, raw_deck) == "deck-2026"
    assert release_keys.verify(both, "manifest.json", data, env_kms) == "kms-2026"
    assert release_keys.verify_detail(both, "manifest.json", data, env_kms) == ("kms-2026", "envelope")
    assert release_keys.verify_detail(both, "manifest.json", data, raw_deck) == ("deck-2026", "raw")
    # the envelope scheme works for any key (the Deck key could sign envelopes too), the raw scheme for the KMS key's raw signature
    assert release_keys.verify(both, "manifest.json", data, kms_env_sign("manifest.json", data, DECK_SECRET)) == "deck-2026"
    assert release_keys.verify(old, "manifest.json", data, env_kms) is None, "the KMS key is not in the old list"
    assert release_keys.verify(new, "manifest.json", data, raw_deck) is None
    assert release_keys.verify(both, "manifest.json", data, ed25519.sign(OTHER_SECRET, data)) is None, "an unknown key"
    assert release_keys.verify(both, "manifest.json", data, kms_env_sign("manifest.json", data, OTHER_SECRET)) is None
    assert release_keys.verify(both, "manifest.json", data + b" ", raw_deck) is None, "changed data (raw)"
    assert release_keys.verify(both, "manifest.json", data + b" ", env_kms) is None, "changed data (envelope)"
    assert release_keys.verify(both, "manifest-macos.json", data, env_kms) is None, "the wrong kind"
    assert release_keys.verify(both, "payload.json", data, env_kms) is None
    assert release_keys.verify(both, "manifest-macos.json", data, raw_deck) == "deck-2026", "a raw signature has no kind"
    assert release_keys.verify(both, "nonsense", data, raw_deck) is None, "an unknown kind fails closed"
    for bad in (env_kms[:63], env_kms + b"\0", b"", bytes(64)):
        assert release_keys.verify(both, "manifest.json", data, bad) is None, len(bad)
    assert release_keys.verify([], "manifest.json", data, raw_deck) is None, "an empty list fails"
    # the envelope itself, signed raw, is not a signature of the data, nor of another kind's envelope
    assert release_keys.verify(both, "manifest.json", release_keys.envelope("manifest.json", data), env_kms) == "kms-2026"
    assert release_keys.verify(both, "payload.json", release_keys.envelope("manifest.json", data), env_kms) == "kms-2026"
    assert release_keys.verify(both, "manifest.json", data, kms_env_sign("payload.json", data)) is None


def test_the_cli_verify(tmp_path):
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (KMS, "kms-2026"))
    m = tmp_path / "manifest.json"
    m.write_bytes(b"{}\n")
    (tmp_path / "s").write_bytes(kms_env_sign("manifest.json", m.read_bytes()))
    r = run(sys.executable, HERE / "release_keys.py", "--verify", lst, "manifest.json", m, tmp_path / "s")
    assert r.returncode == 0 and "kms-2026, envelope" in r.stdout, r.stdout + r.stderr
    r = run(sys.executable, HERE / "release_keys.py", "--verify", DECK.hex(), "manifest.json", m, tmp_path / "s")
    assert r.returncode == 1 and "INVALID" in r.stdout
    r = run(sys.executable, HERE / "release_keys.py", "--verify", lst, "manifest-macos.json", m, tmp_path / "s")
    assert r.returncode == 1
    r = run(sys.executable, HERE / "release_keys.py", "--hex", lst, "--format", "csv")
    assert r.stdout.strip() == f"{DECK.hex()},{KMS.hex()}"


# ---- the signing policy -------------------------------------------------------------------------------------------------------------------
def test_the_committed_policy():
    assert release_keys.load_policy() == {"phase": "transition", "kms_label": "kms-2026", "legacy_label": "deck-2026"}


@pytest.mark.parametrize("policy,why", [
    ({"phase": "later", "kms_label": "kms-2026", "legacy_label": "deck-2026"}, "phase"),
    ({"phase": "kms", "kms_label": "kms-2026"}, "exactly"),
    ({"phase": "kms", "kms_label": "kms-2026", "legacy_label": "deck-2026", "extra": 1}, "exactly"),
    ({"phase": "kms", "kms_label": "KMS", "legacy_label": "deck-2026"}, "not a key label"),
    ({"phase": "kms", "kms_label": 5, "legacy_label": "deck-2026"}, "not a key label"),
    ({"phase": "kms", "kms_label": "same", "legacy_label": "same"}, "the same"),
    ([], "exactly"),
])
def test_a_malformed_policy_is_refused(tmp_path, policy, why):
    p = tmp_path / "policy.json"
    p.write_text(json.dumps(policy))
    with pytest.raises(release_keys.KeyListError, match=why):
        release_keys.load_policy(p)
    p.write_text("{not json")
    with pytest.raises(release_keys.KeyListError):
        release_keys.load_policy(p)


def test_policy_keys():
    policy = release_keys.load_policy()
    keys = [("deck-2026", DECK), ("kms-2026", KMS)]
    assert release_keys.policy_key(keys, policy, "kms_label") == KMS
    assert release_keys.policy_key(keys, policy, "legacy_label") == DECK
    with pytest.raises(release_keys.KeyListError, match="no key labelled kms-2026"):
        release_keys.policy_key(keys[:1], policy, "kms_label")
    with pytest.raises(release_keys.KeyListError, match="first entry"):
        release_keys.policy_key(keys[::-1], policy, "legacy_label")


# ---- kms_sign.py with the stand-in aws ------------------------------------------------------------------------------------------------------
@pytest.fixture
def kms(tmp_path):
    """a stand-in aws on its own, the trusted list (Deck + KMS) and the committed policy"""
    aws = tmp_path / "bin" / "aws"
    aws.parent.mkdir()
    aws.write_text(f"#!/bin/bash\nexec {sys.executable} {HERE / 'aws_stand_in.py'} \"$@\"\n")
    aws.chmod(0o755)
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (KMS, "kms-2026"))
    log = tmp_path / "aws.log"

    class K:
        keys, aws_log = lst, log
        policy = release_keys.POLICY_FILE

        def env(self, fault: str = "", **extra) -> dict:
            return {"AWS": str(aws), "STANDIN_AWS_SECRET": KMS_SECRET.hex(), "STANDIN_AWS_FAULT": fault, "STANDIN_AWS_LOG": str(log),
                    "AWS_REGION": "us-east-1", **extra}

        def sign(self, *args, fault: str = "", keys: Path | None = None, policy: Path | None = None) -> subprocess.CompletedProcess:
            return run(sys.executable, HERE / "kms_sign.py", *args, "--keys", keys or lst, "--policy", policy or release_keys.POLICY_FILE,
                       env=self.env(fault))
    return K()


def manifests(tmp_path) -> tuple[Path, Path]:
    d = tmp_path / "pkg"
    d.mkdir(exist_ok=True)
    (d / "manifest.json").write_bytes(b'{"version": "v1.0.0", "x": "' + b"m" * 9000 + b'"}\n')   # larger than KMS's 4096-byte limit
    (d / "manifest-macos.json").write_bytes(b'{"version": "v1.0.0", "mac": true}\n')
    return d / "manifest.json", d / "manifest-macos.json"


def test_kms_sign_happy_path(kms, tmp_path):
    m, mac = manifests(tmp_path)
    r = kms.sign("--slot", "sig2", m, mac)   # the lead's form: no sub-command
    assert r.returncode == 0, r.stdout + r.stderr
    assert "SIGNED manifest.json.sig2 (kms-2026, envelope)" in r.stdout
    keys = release_keys.load(kms.keys)
    for f in (m, mac):
        sig = (f.parent / (f.name + ".sig2")).read_bytes()
        assert len(sig) == 64 and release_keys.verify_detail(keys, f.name, f.read_bytes(), sig) == ("kms-2026", "envelope")
        assert not (f.parent / (f.name + ".sig")).exists(), "the transition phase never writes X.sig"
    calls = kms.aws_log.read_text().splitlines()
    assert calls[0].startswith("kms get-public-key --key-id alias/openbfme-release")
    assert all("--message-type RAW --signing-algorithm ED25519_SHA_512 --message fileb://" in c for c in calls[1:]) and len(calls) == 3
    assert all("--region us-east-1" in c for c in calls)
    # the default slot follows the policy's phase: transition sig2, kms both (the same signature)
    for f in (m, mac):
        (f.parent / (f.name + ".sig2")).unlink()
    assert kms.sign("sign", m).returncode == 0 and (m.parent / "manifest.json.sig2").exists() and not (m.parent / "manifest.json.sig").exists()
    policy = tmp_path / "policy-kms.json"
    policy.write_text(json.dumps({"phase": "kms", "kms_label": "kms-2026", "legacy_label": "deck-2026"}))
    assert kms.sign("sign", mac, policy=policy).returncode == 0
    assert (mac.parent / "manifest-macos.json.sig").read_bytes() == (mac.parent / "manifest-macos.json.sig2").read_bytes()
    r = kms.sign("public-key")
    assert r.returncode == 0 and r.stdout.strip() == KMS.hex()


def no_signatures(*files: Path) -> bool:
    return not any((f.parent / (f.name + e)).exists() for f in files for e in (".sig", ".sig2", ".sig.tmp", ".sig2.tmp"))


@pytest.mark.parametrize("fault,why", [
    ("wrong-key", "is not the trusted list's kms-2026 key (it is not in the list)"),
    ("wrong-spec", "KeySpec is 'ECC_NIST_P256'"),
    ("wrong-usage", "KeyUsage is 'ENCRYPT_DECRYPT'"),
    ("no-ed25519", "does not offer ED25519_SHA_512"),
    ("bad-der", "not an Ed25519 SubjectPublicKeyInfo"),
    ("bad-signature", "does not verify under the trusted list's kms-2026 key"),
    ("sign-other-key", "does not verify under the trusted list's kms-2026 key"),
    ("short-signature", "63-byte signature"),
    ("sign-error", "aws kms sign failed (exit 254)"),
    ("error", "aws kms get-public-key failed (exit 254)"),
])
def test_kms_sign_refuses_and_writes_nothing(kms, tmp_path, fault, why):
    m, mac = manifests(tmp_path)
    r = kms.sign("--slot", "both", m, mac, fault=fault)
    assert r.returncode != 0 and why in r.stdout + r.stderr, r.stdout + r.stderr
    assert no_signatures(m, mac)
    # never a credential (the stand-in's error carries a fake access key id and session token)
    assert "IOSFODNN7EXAMPLE" not in r.stdout + r.stderr and "FwoGZXIvYXdz" not in r.stdout + r.stderr


def test_kms_sign_refuses_a_key_under_another_label_or_a_list_without_it(kms, tmp_path):
    m, _ = manifests(tmp_path)
    other_label = key_list(tmp_path / "other.pub", (DECK, "deck-2026"), (KMS, "spare-1"))
    r = kms.sign("sign", m, keys=other_label)
    assert r.returncode != 0 and "no key labelled kms-2026" in r.stdout + r.stderr
    swapped = key_list(tmp_path / "swapped.pub", (DECK, "deck-2026"), (OTHER, "kms-2026"), (KMS, "spare-1"))
    r = kms.sign("sign", m, keys=swapped)
    assert r.returncode != 0 and "is not the trusted list's kms-2026 key (it is listed as spare-1)" in r.stdout + r.stderr
    r = kms.sign("sign", m, keys=key_list(tmp_path / "deck.pub", (DECK, "deck-2026")))
    assert r.returncode != 0 and no_signatures(m)


def test_kms_sign_writes_nothing_when_one_file_fails(kms, tmp_path):
    m, _ = manifests(tmp_path)
    notes = tmp_path / "pkg" / "notes.md"
    notes.write_text("x\n")
    r = kms.sign("sign", m, notes)
    assert r.returncode != 0 and "the kind of notes.md is not known" in r.stderr and no_signatures(m, notes)
    r = kms.sign("sign", m, tmp_path / "pkg" / "missing.json")
    assert r.returncode != 0 and no_signatures(m)


def test_the_stand_in_answers_sts(kms):
    r = run(kms.env()["AWS"], "sts", "assume-role-with-web-identity", "--role-arn", "r", "--role-session-name", "s", "--web-identity-token",
            "t", "--duration-seconds", "900", "--output", "json", env=kms.env())
    c = json.loads(r.stdout)["Credentials"]
    assert r.returncode == 0 and set(c) == {"AccessKeyId", "SecretAccessKey", "SessionToken", "Expiration"}
    assert run(kms.env()["AWS"], "sts", "assume-role-with-web-identity", "--output", "json", env=kms.env("sts-error")).returncode == 254


# ---- sealing through KMS ------------------------------------------------------------------------------------------------------------------
def package_dir(d: Path, version: str) -> Path:
    from test_autorelease import package_dir as make
    return make(d, version)


def test_seal_through_kms_then_check(kms, tmp_path):
    d = package_dir(tmp_path / "pkg", "v1.0.0")
    before = {p.name: p.read_bytes() for p in d.iterdir()}
    r = run(sys.executable, HERE / "seal_package.py", "seal", d, "--kms", "--repo", TARGET, "--keys", kms.keys, env=kms.env("error"))
    assert r.returncode != 0 and "get-public-key failed" in r.stdout + r.stderr
    assert {p.name: p.read_bytes() for p in d.iterdir()} == before, "a failed KMS seal changes nothing"
    r = run(sys.executable, HERE / "seal_package.py", "seal", d, "--kms", "--repo", TARGET, "--keys", kms.keys, env=kms.env())
    assert r.returncode == 0 and "seal ok" in r.stdout, r.stdout + r.stderr
    assert run(sys.executable, HERE / "seal_package.py", "check", d, "--public", kms.keys, "--repo", TARGET).returncode == 0
    r = run(sys.executable, HERE / "seal_package.py", "check", d, "--public", DECK.hex(), "--repo", TARGET)
    assert r.returncode == 1 and "does not verify" in r.stdout, "the Deck key alone does not trust a KMS seal"
    assert seal_package.check(d, [("kms-2026", KMS)], TARGET) == []
    files, _, _ = seal_package.read_archive(next(iter(seal_package.combined_archives(d))))
    assert release_keys.verify_detail([("kms-2026", KMS)], "payload.json", files["payload.json"], files["payload.json.sig"]) == \
        ("kms-2026", "envelope")
    # kms_sign.py's own seal helper does the same
    d2 = package_dir(tmp_path / "pkg2", "v1.0.0")
    r = run(sys.executable, HERE / "kms_sign.py", "seal", d2, "--repo", TARGET, "--keys", kms.keys, env=kms.env())
    assert r.returncode == 0 and "SEALED" in r.stdout, r.stdout + r.stderr
    assert seal_package.check(d2, release_keys.load(kms.keys), TARGET) == []
    # the old single-key callers still work: a file key seals raw
    d3 = package_dir(tmp_path / "pkg3", "v1.0.0")
    seal_package.seal(d3, DECK_SECRET, TARGET)
    assert seal_package.check(d3, DECK, TARGET) == [] and seal_package.check(d3, release_keys.load(kms.keys), TARGET) == []


# ---- sign_manifest.py --verify with a list ------------------------------------------------------------------------------------------------
def test_sign_manifest_verify_with_a_list(tmp_path):
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (KMS, "kms-2026"))
    m = tmp_path / "manifest.json"
    m.write_bytes(b'{"a": 1}\n')
    (tmp_path / "manifest.json.sig").write_bytes(ed25519.sign(DECK_SECRET, m.read_bytes()))
    (tmp_path / "manifest.json.sig2").write_bytes(kms_env_sign("manifest.json", m.read_bytes()))
    tool = HERE / "sign_manifest.py"
    r = run(sys.executable, tool, "--verify", lst, m)
    assert r.returncode == 0 and "(deck-2026, raw)" in r.stdout, r.stdout + r.stderr
    r = run(sys.executable, tool, "--verify", lst, m, "--sig", tmp_path / "manifest.json.sig2")
    assert r.returncode == 0 and "(kms-2026, envelope)" in r.stdout
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


def test_publish_dry_run_with_sig_and_sig2(tmp_path, denylist):
    d = package_dir(tmp_path / "pkg", "v1.0.0")
    assert run(sys.executable, HERE / "make_manifest.py", d, "--repo", TARGET, "--commit", COMMIT, "--date", "2026-10-22",
               "--allow-untagged").returncode == 0
    data = (d / "manifest.json").read_bytes()
    lst = key_list(tmp_path / "keys.pub", (DECK, "deck-2026"), (KMS, "kms-2026"))
    env = {"OPENBFME_RELEASE_DENYLIST": str(denylist)}

    def publish():
        return run("bash", HERE / "publish_release.sh", d, "--repo", TARGET, "--key-file", lst, "--allow-untagged", env=env)

    (d / "manifest.json.sig").write_bytes(ed25519.sign(DECK_SECRET, data))
    (d / "manifest.json.sig2").write_bytes(kms_env_sign("manifest.json", data))
    r = publish()
    assert r.returncode == 0 and "DRY RUN" in r.stdout, r.stdout + r.stderr
    upload = next(ln for ln in r.stdout.splitlines() if " release upload " in ln)
    assert upload.rstrip().endswith("manifest.json.sig") is False and "manifest.json.sig2" in upload and "manifest.json.sig " in upload
    # the transition phase's Actions folder: .sig2 alone
    (d / "manifest.json.sig").unlink()
    r = publish()
    assert r.returncode == 0, r.stdout + r.stderr
    upload = next(ln for ln in r.stdout.splitlines() if " release upload " in ln)
    assert upload.rstrip().endswith("manifest.json.sig2") and "manifest.json.sig " not in upload
    # a .sig2 by an unknown key, or a .sig that does not verify beside a good .sig2, stops it
    (d / "manifest.json.sig2").write_bytes(kms_env_sign("manifest.json", data, OTHER_SECRET))
    r = publish()
    assert r.returncode != 0 and "does not verify" in r.stderr, r.stdout + r.stderr
    (d / "manifest.json.sig2").write_bytes(kms_env_sign("manifest.json", data))
    (d / "manifest.json.sig").write_bytes(ed25519.sign(OTHER_SECRET, data))
    r = publish()
    assert r.returncode != 0 and "manifest.json.sig)" in r.stderr, r.stdout + r.stderr
    # the wrong kind (a payload envelope) is refused
    (d / "manifest.json.sig").unlink()
    (d / "manifest.json.sig2").write_bytes(kms_env_sign("payload.json", data))
    assert publish().returncode != 0
    (d / "manifest.json.sig2").unlink()
    r = publish()
    assert r.returncode != 0 and "no manifest.json.sig or manifest.json.sig2" in r.stderr


def test_the_privacy_gate_takes_sig2_for_a_signature():
    import autorelease
    assert autorelease.is_detached_signature("manifest.json.sig2", bytes(64), 0)
    assert autorelease.is_detached_signature("manifest-macos.json.sig2", bytes(64), 0)
    assert not autorelease.is_detached_signature("manifest.json.sig3", bytes(64), 0)


# ---- cosign_release.py with the stand-in gh -------------------------------------------------------------------------------------------------
REQUEST = "0123456789abcdef0123456789abcdef"


def git(repo: Path, *args: str) -> str:
    r = run("git", "-C", repo, *args)
    assert r.returncode == 0, r.stderr
    return r.stdout.strip()


@pytest.fixture(scope="module")
def built(tmp_path_factory):
    """one release folder per channel, made like the release workflow makes it (actions_release.py cmd_sign): the packages, the Mac
    package, KMS-sealed, the manifests and their KMS .sig2; a public remote with main and the tags"""
    import make_macos_manifest
    from test_autorelease import mac_package_dir
    base = tmp_path_factory.mktemp("cosign")
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
    key_list(checkout / "launcher/release_keys.pub", (DECK, "deck-2026"), (KMS, "kms-2026"))
    folders = {}
    for version, commit in (("v0.4.13-nightly.20261022", c1), ("v0.4.13", c1), ("v0.4.14-nightly.20261023", side),
                            ("v0.4.15-nightly.20261024", c1)):
        git(seed, "tag", version, c2 if version.startswith("v0.4.15") else commit)   # v0.4.15: the tag is not the manifest's commit
        d = package_dir(base / f"pkg-{version}", version)
        mac = mac_package_dir(checkout, version, base / f"mac-{version}", commit=commit)
        shutil.copy2(mac, d / mac.name)
        with open(d / f"SHA256SUMS-{version}.txt", "a") as f:
            f.write(f"{hashlib.sha256(mac.read_bytes()).hexdigest()}  {mac.name}\n")
        seal_package.seal(d, EnvelopeSigner(), TARGET)
        assert run(sys.executable, HERE / "make_manifest.py", d, "--repo", TARGET, "--commit", commit, "--date", "2026-10-22",
                   "--allow-untagged").returncode == 0
        sealed = base / f"{mac.name}.sealed.sha256"
        sealed.write_text(f"{hashlib.sha256((d / mac.name).read_bytes()).hexdigest()}  {mac.name}\n")
        (d / "manifest-macos.json").write_bytes(make_macos_manifest.dumps(make_macos_manifest.build(
            version, TARGET, commit, "2026-10-22", archive=d / mac.name, checksum=sealed)))
        for m in ("manifest.json", "manifest-macos.json"):
            (d / f"{m}.sig2").write_bytes(kms_env_sign(m, (d / m).read_bytes()))
        (d / f"BUILDINFO-{version}.txt").unlink()
        folders[version] = d
    git(seed, "push", "-q", str(remote), "--tags")
    return {"remote": remote, "folders": folders, "checkout": checkout, "main": c2, "side": side}


class Cosign:
    def __init__(self, tmp: Path, built: dict):
        self.tmp, self.built = tmp, built
        self.state = tmp / "gh.json"
        self.log = tmp / "gh.log"
        self.gh = tmp / "bin" / "gh"
        self.gh.parent.mkdir()
        self.gh.write_text(f"#!/bin/bash\nexec {sys.executable} {HERE / 'gh_stand_in.py'} \"$@\"\n")
        self.gh.chmod(0o755)
        self.key = tmp / "keys" / "deck.pem"
        self.key.parent.mkdir()
        self.key.write_text(ed25519.private_key_pem(DECK_SECRET))
        self.key.chmod(0o600)
        self.keys = built["checkout"] / "launcher/release_keys.pub"
        self.runs = 0

    def draft(self, version: str, author: str = "github-actions[bot]", request: str = REQUEST, change=None, extra: dict | None = None,
              created: str = "2026-10-22T01:00:00Z") -> int:
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
        rel = {"id": rid, "tag_name": version, "name": f"OpenBFME {version}", "draft": True, "prerelease": "-" in version,
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

    def run(self, version: str, rid: int, *extra, request: str = REQUEST, fault: str = "", policy: Path | None = None):
        self.runs += 1
        out = self.tmp / f"out-{self.runs}"
        r = run(sys.executable, HERE / "cosign_release.py", "--repo", TARGET, "--tag", version, "--release-id", rid, "--request", request,
                "--out", out, "--key", self.key, "--keys", self.keys, "--policy", policy or release_keys.POLICY_FILE, *extra,
                env={"GH": str(self.gh), "FAKE_GH_STATE": str(self.state), "FAKE_GH_REMOTE": str(self.built["remote"]),
                     "FAKE_GH_LOG": str(self.log), "FAKE_GH_FAULT": fault})
        return r, out


@pytest.fixture
def cosign(tmp_path, built):
    return Cosign(tmp_path, built)


def sig_assets(rel: dict) -> list[str]:
    return sorted(a["name"] for a in rel["assets"] if a["name"].endswith(".sig"))


@pytest.mark.parametrize("version", ["v0.4.13-nightly.20261022", "v0.4.13"])
def test_cosign_adds_the_deck_signature_and_is_idempotent(cosign, version):
    rid = cosign.draft(version)
    before = cosign.release(rid)
    r, out = cosign.run(version, rid)
    assert r.returncode == 0 and f"COSIGN {version}: co-signed manifest.json.sig, manifest-macos.json.sig" in r.stdout, r.stdout + r.stderr
    rel = cosign.release(rid)
    assert sig_assets(rel) == ["manifest-macos.json.sig", "manifest.json.sig"]
    assert rel["draft"] is True and rel["body"] == before["body"], "never published, the body untouched (the caller publishes)"
    store = Path(str(cosign.state) + ".d")
    for m in ("manifest.json", "manifest-macos.json"):
        sig = (store / str(next(a["id"] for a in rel["assets"] if a["name"] == f"{m}.sig"))).read_bytes()
        data = (out / m).read_bytes()
        assert ed25519.verify(DECK, data, sig), "the Deck key's raw signature: what every launcher before SIGNKMS-1 reads"
        assert (out / f"{m}.sig").read_bytes() == sig
    assert sorted(p.name for p in out.iterdir()) == sorted(a["name"] for a in rel["assets"]), "the kept folder is exactly the release"
    assert "uploads.github.com" in cosign.log.read_text() and "--method PATCH" not in cosign.log.read_text()
    # the second run signs nothing, uploads nothing
    cosign.log.unlink()
    r, _ = cosign.run(version, rid)
    assert r.returncode == 0 and "already co-signed (nothing signed)" in r.stdout, r.stdout + r.stderr
    assert len(cosign.release(rid)["assets"]) == len(rel["assets"]) and "uploads.github.com" not in cosign.log.read_text()


def test_cosign_check_only_and_the_kms_phase(cosign, tmp_path):
    version = "v0.4.13-nightly.20261022"
    rid = cosign.draft(version)
    r, _ = cosign.run(version, rid, "--check-only")
    assert r.returncode == 0 and "checks passed (manifest.json, manifest-macos.json to co-sign)" in r.stdout, r.stdout + r.stderr
    assert sig_assets(cosign.release(rid)) == []
    policy = tmp_path / "kms-policy.json"
    policy.write_text(json.dumps({"phase": "kms", "kms_label": "kms-2026", "legacy_label": "deck-2026"}))
    r, _ = cosign.run(version, rid, policy=policy)
    assert r.returncode == 1 and "manifest.json has no manifest.json.sig (the KMS signature)" in r.stdout, r.stdout
    folder = cosign.built["folders"][version]
    kms_sigs = {f"{m}.sig": (folder / f"{m}.sig2").read_bytes() for m in ("manifest.json", "manifest-macos.json")}
    rid2 = cosign.draft(version, extra=kms_sigs, created="2026-10-22T02:00:00Z")
    r, _ = cosign.run(version, rid2, policy=policy)
    assert r.returncode == 0 and "phase kms: nothing added" in r.stdout, r.stdout + r.stderr
    assert len(cosign.release(rid2)["assets"]) == len(list(folder.iterdir())) + 2
    # in the kms phase a Deck-signed .sig is not the KMS signature
    deck = {f"{m}.sig": ed25519.sign(DECK_SECRET, (folder / m).read_bytes()) for m in ("manifest.json", "manifest-macos.json")}
    rid3 = cosign.draft(version, extra=deck, created="2026-10-22T03:00:00Z")
    r, _ = cosign.run(version, rid3, policy=policy)
    assert r.returncode == 1 and "manifest.json.sig is not the kms-2026 key's envelope signature" in r.stdout


def tamper_archive(files: dict) -> None:
    name = next(n for n in files if n.endswith("linux-x64.tar.gz") and n.startswith("openbfme-v"))
    files[name] = files[name] + b"\0"


def unknown_sig2(files: dict) -> None:
    files["manifest.json.sig2"] = kms_env_sign("manifest.json", files["manifest.json"], OTHER_SECRET)


def deck_sig2(files: dict) -> None:
    files["manifest.json.sig2"] = ed25519.sign(DECK_SECRET, files["manifest.json"])   # a trusted key, but not the KMS envelope


def bad_sig(files: dict) -> None:
    files["manifest.json.sig"] = ed25519.sign(OTHER_SECRET, files["manifest.json"])


def extra_asset(files: dict) -> None:
    files["notes.txt"] = b"not a release asset\n"


def changed_mac(files: dict) -> None:
    name = next(n for n in files if n.endswith("macos-arm64.zip"))
    files[name] = files[name][:-1] + bytes([files[name][-1] ^ 1])


@pytest.mark.parametrize("version,kw,why", [
    ("v0.4.13-nightly.20261022", {"change": tamper_archive}, "make_manifest.py --check"),
    ("v0.4.13-nightly.20261022", {"change": changed_mac}, "make_macos_manifest.py --check"),
    ("v0.4.13-nightly.20261022", {"change": unknown_sig2}, "manifest.json.sig2 is not the kms-2026 key's envelope signature"),
    ("v0.4.13-nightly.20261022", {"change": deck_sig2}, "manifest.json.sig2 is not the kms-2026 key's envelope signature"),
    ("v0.4.13-nightly.20261022", {"change": bad_sig}, "manifest.json.sig exists but does NOT verify"),
    ("v0.4.13-nightly.20261022", {"change": extra_asset}, "extra ['notes.txt']"),
    ("v0.4.13-nightly.20261022", {"author": "someone"}, "not created by the release workflow"),
    ("v0.4.13-nightly.20261022", {"request": "f" * 32}, "does not carry the line"),
    ("v0.4.14-nightly.20261023", {}, "is not on the public main"),
    ("v0.4.15-nightly.20261024", {}, "not the manifest's commit"),
])
def test_cosign_refuses(cosign, version, kw, why):
    rid = cosign.draft(version, **kw)
    before = cosign.release(rid)
    r, _ = cosign.run(version, rid)
    assert r.returncode == 1 and why in r.stdout and "(nothing signed)" in r.stdout, r.stdout + r.stderr
    assert cosign.release(rid) == before, "nothing uploaded, nothing changed"
    assert "uploads.github.com" not in cosign.log.read_text()


def test_cosign_refuses_a_stale_draft_another_tag_or_a_published_release(cosign):
    version = "v0.4.13"
    old = cosign.draft(version, created="2026-10-22T01:00:00Z")
    cosign.draft(version, created="2026-10-22T02:00:00Z")
    r, _ = cosign.run(version, old)
    assert r.returncode == 1 and "is not the newest draft of v0.4.13" in r.stdout, r.stdout
    r, _ = cosign.run("v0.4.13-nightly.20261022", old)
    assert r.returncode == 1 and "the release's tag is 'v0.4.13'" in r.stdout
    s = json.loads(cosign.state.read_text())
    for rel in s["releases"]:
        rel["draft"] = False
    cosign.state.write_text(json.dumps(s))
    r, _ = cosign.run(version, old)
    assert r.returncode == 1 and "is not a draft" in r.stdout
    r, _ = cosign.run(version, 99999)
    assert r.returncode == 1 and "Not Found" in r.stdout


def test_cosign_refuses_a_key_that_is_not_the_deck_key_or_lies_in_a_repository(cosign, tmp_path):
    version = "v0.4.13-nightly.20261022"
    rid = cosign.draft(version)
    cosign.key.write_text(ed25519.private_key_pem(OTHER_SECRET))
    r, _ = cosign.run(version, rid)
    assert r.returncode != 0 and "is not the trusted list's deck-2026 key" in r.stdout + r.stderr
    cosign.key.write_text(ed25519.private_key_pem(DECK_SECRET))
    cosign.key.chmod(0o644)
    r, _ = cosign.run(version, rid)
    assert r.returncode != 0 and "other users" in r.stdout + r.stderr
    assert sig_assets(cosign.release(rid)) == []


def test_cosign_reads_back_after_a_lost_upload_answer(cosign):
    version = "v0.4.13-nightly.20261022"
    rid = cosign.draft(version)
    r, _ = cosign.run(version, rid, fault="cosign-upload-lost")
    assert r.returncode == 0 and "its answer was lost: reading the release back" in r.stdout, r.stdout + r.stderr
    assert sig_assets(cosign.release(rid)) == ["manifest-macos.json.sig", "manifest.json.sig"]
    rid2 = cosign.draft(version, created="2026-10-22T05:00:00Z")
    r, _ = cosign.run(version, rid2, fault="upload-fail")
    assert r.returncode == 1 and "asset missing: manifest.json.sig" in r.stdout and "left as it is" in r.stdout, r.stdout
