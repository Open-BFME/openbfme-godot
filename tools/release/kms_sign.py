#!/usr/bin/env python3
"""Lane SIGNKMS-1: sign release files with the AWS KMS release key (envelope scheme, tools/release/release_keys.py).

    python3 tools/release/kms_sign.py [sign] FILE... [--slot sig|sig2|both] [--kind KIND] [--key-id ID] [--keys LIST] [--policy FILE]
    python3 tools/release/kms_sign.py seal <package dir> --repo OWNER/NAME [--key-id ID] [--keys LIST] [--policy FILE]
    python3 tools/release/kms_sign.py public-key [--key-id ID]          the KMS key's public half (64 hex), its spec checked

AWS KMS signs at most 4096 bytes with ED25519_SHA_512 / MessageType RAW (pure Ed25519), so the key signs a release file's ENVELOPE
(release_keys.envelope: the kind and the file's SHA-512), never the file. The signature is still 64 raw bytes.

Uses the `aws` CLI (preinstalled on GitHub's ubuntu runners; AWS overrides the binary, tests use tools/release/aws_stand_in.py);
no boto3. The region is the CLI's own (AWS_REGION, or --region). Credentials are the CLI's (the workflow's OIDC role): this tool never
reads, prints or passes them.

Before anything is written:
  * `aws kms get-public-key`: KeySpec ECC_NIST_EDWARDS25519, KeyUsage SIGN_VERIFY, SigningAlgorithms holding ED25519_SHA_512, the
    SubjectPublicKeyInfo 302a300506032b6570032100 || 32 bytes; that key must be in the trusted list (launcher/release_keys.pub) under
    the signing policy's kms_label (tools/release/signing_policy.json);
  * for each file `aws kms sign --message-type RAW --signing-algorithm ED25519_SHA_512 --message fileb://<envelope>`: the answer's
    signature must be 64 bytes and valid under the list as the envelope signature of THAT key (release_keys.verify_detail).
Every file is signed and checked first; only then are the signatures written (atomically), so a failure writes nothing.
--slot: sig2 writes <file>.sig2, sig <file>.sig, both both (the same signature). Default from the policy's phase: "transition" sig2
(the Deck co-signer adds <file>.sig, tools/release/cosign_release.py), "kms" both.
"""
from __future__ import annotations

import argparse
import base64
import binascii
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import ed25519  # noqa: E402
import release_keys  # noqa: E402

DEFAULT_KEY_ID = "alias/openbfme-release"
SPKI_PREFIX = release_keys.SPKI_PREFIX
KEY_SPEC, KEY_USAGE, ALGORITHM = "ECC_NIST_EDWARDS25519", "SIGN_VERIFY", "ED25519_SHA_512"
KEY_ID = re.compile(r"[A-Za-z0-9:/_.+=,@-]{1,256}")


class KmsError(Exception):
    pass


def _redact(text: str) -> str:
    """the CLI's error text, one line, with anything credential-like masked (access key ids, secrets, session tokens, signatures, ARNs and
    account ids: the workflow's log is public)"""
    line = (text.strip().splitlines() or [""])[-1][:400]
    line = re.sub(r"\b(?:AKIA|ASIA|AROA|AIDA)[A-Z0-9]{12,}\b", "<key id>", line)
    line = re.sub(r"[A-Za-z0-9+/=]{32,}", "<redacted>", line)
    line = re.sub(r"\barn:aws\S*", "<arn>", line)          # key and role names carry the account id
    line = re.sub(r"\b\d{12}\b", "<account>", line)
    return line


def aws_cli() -> str:
    return os.environ.get("AWS") or "aws"


def run_aws(args: list[str], region: str | None) -> dict:
    cmd = [aws_cli(), "kms", *args, "--output", "json"] + (["--region", region] if region else [])
    try:
        r = subprocess.run(cmd, capture_output=True, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise KmsError(f"aws kms {args[0]} could not run ({type(e).__name__})")
    if r.returncode != 0:
        raise KmsError(f"aws kms {args[0]} failed (exit {r.returncode}): {_redact(r.stderr.decode('utf-8', 'replace'))}")
    try:
        answer = json.loads(r.stdout)
    except ValueError:
        raise KmsError(f"aws kms {args[0]} answered no JSON")
    if not isinstance(answer, dict):
        raise KmsError(f"aws kms {args[0]} answered no JSON object")
    return answer


def _b64(value, what: str) -> bytes:
    if not isinstance(value, str):
        raise KmsError(f"the answer has no {what}")
    try:
        return base64.b64decode(value, validate=True)
    except (binascii.Error, ValueError):
        raise KmsError(f"the answer's {what} is not base64")


def get_public_key(key_id: str, region: str | None) -> bytes:
    """the KMS key's 32-byte Ed25519 public key, its spec, usage and algorithms checked"""
    a = run_aws(["get-public-key", "--key-id", key_id], region)
    if a.get("KeySpec") != KEY_SPEC:
        raise KmsError(f"the KMS key's KeySpec is {a.get('KeySpec')!r}, not {KEY_SPEC}")
    if a.get("KeyUsage") != KEY_USAGE:
        raise KmsError(f"the KMS key's KeyUsage is {a.get('KeyUsage')!r}, not {KEY_USAGE}")
    algs = a.get("SigningAlgorithms")
    if not isinstance(algs, list) or ALGORITHM not in algs:
        raise KmsError(f"the KMS key does not offer {ALGORITHM} (SigningAlgorithms {algs!r})")
    der = _b64(a.get("PublicKey"), "PublicKey")
    if len(der) != len(SPKI_PREFIX) + 32 or not der.startswith(SPKI_PREFIX):
        raise KmsError("the KMS key's PublicKey is not an Ed25519 SubjectPublicKeyInfo (302a300506032b6570032100 || 32 bytes)")
    pub = der[len(SPKI_PREFIX):]
    point = ed25519._decompress(pub)
    if point is None or ed25519.is_small_order(point):
        raise KmsError("the KMS key's public key is not a usable Ed25519 point")
    return pub


class KmsSigner:
    """sign(kind, data) -> the KMS key's envelope signature, valid under the trusted list as the policy's kms_label (else KmsError)"""

    def __init__(self, key_id: str, region: str | None, keys: list[tuple[str, bytes]], policy: dict):
        if not KEY_ID.fullmatch(key_id or ""):
            raise KmsError("the KMS key id is not a key id, ARN or alias")
        self.key_id, self.region, self.keys, self.policy = key_id, region, keys, policy
        self.label = policy["kms_label"]
        try:
            want = release_keys.policy_key(keys, policy, "kms_label")
        except release_keys.KeyListError as e:
            raise KmsError(str(e))
        self.public = get_public_key(key_id, region)
        if self.public != want:
            others = [lb for lb, k in keys if k == self.public]
            raise KmsError(f"the KMS key is not the trusted list's {self.label} key"
                           + (f" (it is listed as {others[0]})" if others else " (it is not in the list)"))

    @classmethod
    def open(cls, key_id: str | None = None, region: str | None = None, keys_path: Path | None = None,
             policy_path: Path | None = None) -> "KmsSigner":
        """the signer of the committed list and policy (SystemExit with the reason on any problem: the tools' CLI behaviour)"""
        try:
            keys = release_keys.load(keys_path or release_keys.DEFAULT_LIST)
            policy = release_keys.load_policy(policy_path or release_keys.POLICY_FILE)
            return cls(key_id or DEFAULT_KEY_ID, region or os.environ.get("AWS_REGION") or None, keys, policy)
        except (KmsError, release_keys.KeyListError) as e:
            raise SystemExit(f"kms_sign: {e}")

    def trusted(self) -> list[tuple[str, bytes]]:
        return [(self.label, self.public)]

    def sign(self, kind: str, data: bytes) -> bytes:
        env = release_keys.envelope(kind, data)
        if len(env) > 4096:
            raise KmsError("the envelope is longer than KMS's 4096-byte RAW limit")
        with tempfile.TemporaryDirectory(prefix="kms-sign-") as t:
            f = Path(t) / "envelope"
            f.write_bytes(env)
            a = run_aws(["sign", "--key-id", self.key_id, "--message-type", "RAW", "--signing-algorithm", ALGORITHM,
                         "--message", f"fileb://{f}"], self.region)
        if a.get("SigningAlgorithm", ALGORITHM) != ALGORITHM:
            raise KmsError(f"KMS signed with {a.get('SigningAlgorithm')!r}, not {ALGORITHM}")
        sig = _b64(a.get("Signature"), "Signature")
        if len(sig) != 64:
            raise KmsError(f"KMS returned a {len(sig)}-byte signature (an Ed25519 signature is 64 bytes)")
        got = release_keys.verify_detail(self.keys, kind, data, sig)
        if got != (self.label, "envelope") or not ed25519.verify(self.public, env, sig):
            raise KmsError(f"KMS's signature of the {kind} envelope does not verify under the trusted list's {self.label} key")
        return sig


def default_slot(policy: dict) -> str:
    return "sig2" if policy["phase"] == "transition" else "both"


def sign_files(signer: KmsSigner, files: list[Path], slot: str, kind: str | None = None) -> list[Path]:
    """signs every file, checks every signature, then writes them all; returns the written paths (nothing written on any failure)"""
    planned: list[tuple[Path, bytes]] = []
    for f in files:
        k = kind or release_keys.kind_of(f.name)
        if k is None:
            raise KmsError(f"the kind of {f.name} is not known from its name: pass --kind")
        sig = signer.sign(k, f.read_bytes())
        exts = {"sig": [".sig"], "sig2": [".sig2"], "both": [".sig", ".sig2"]}[slot]
        planned += [(f.with_name(f.name + e), sig) for e in exts]
    written = []
    for path, sig in planned:
        tmp = path.with_name(path.name + ".tmp")
        tmp.write_bytes(sig)
        tmp.replace(path)
        written.append(path)
    return written


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--key-id", default=DEFAULT_KEY_ID)
    common.add_argument("--region", default=None, help="default: the aws CLI's (AWS_REGION)")
    common.add_argument("--keys", type=Path, default=release_keys.DEFAULT_LIST, help="the trusted list")
    common.add_argument("--policy", type=Path, default=release_keys.POLICY_FILE)
    s = sub.add_parser("sign", parents=[common])
    s.add_argument("files", type=Path, nargs="+")
    s.add_argument("--slot", choices=("sig", "sig2", "both"), help="default: the policy's phase (transition: sig2, kms: both)")
    s.add_argument("--kind", choices=release_keys.KINDS, help="default: from each file's name")
    z = sub.add_parser("seal", parents=[common])
    z.add_argument("dir", type=Path)
    z.add_argument("--repo", required=True)
    sub.add_parser("public-key", parents=[common])
    if argv[:1] not in (["sign"], ["seal"], ["public-key"], ["-h"], ["--help"]):
        argv = ["sign", *argv]   # `kms_sign.py --slot sig2 manifest.json`: sign is the default command
    a = ap.parse_args(argv)
    try:
        if a.cmd == "public-key":
            print(get_public_key(a.key_id, a.region).hex())
            return 0
        signer = KmsSigner.open(a.key_id, a.region, a.keys, a.policy)
        if a.cmd == "seal":
            import seal_package
            if not seal_package.REPO_NAME.match(a.repo):
                raise SystemExit(f"kms_sign: '{a.repo}' is not <owner>/<name>")
            for n in seal_package.seal(a.dir, signer, a.repo):
                print(f"SEALED {n} ({signer.label})")
            problems = seal_package.check(a.dir, signer.trusted(), a.repo)
            for pr in problems:
                print(f"SEAL CHECK: {pr}")
            return 1 if problems else 0
        missing = [f.name for f in a.files if not f.is_file()]
        if missing:
            raise KmsError(f"no such file: {', '.join(missing)}")
        for path in sign_files(signer, a.files, a.slot or default_slot(signer.policy), a.kind):
            print(f"SIGNED {path.name} ({signer.label}, envelope)")
        return 0
    except KmsError as e:
        print(f"kms_sign: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
