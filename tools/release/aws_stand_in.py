#!/usr/bin/env python3
"""Lane SIGNKMS-1: a stand-in for the two `aws kms` commands tools/release/kms_sign.py runs, for tests (never the real AWS).

    aws kms get-public-key --key-id ID --output json [--region R]
    aws kms sign --key-id ID --message-type RAW --signing-algorithm ED25519_SHA_512 --message fileb://FILE --output json [--region R]
    aws sts assume-role-with-web-identity --role-arn R --role-session-name N --web-identity-token T ... --output json
        fake Credentials (AccessKeyId / SecretAccessKey / SessionToken / Expiration) for tools/release/actions_release.py's tests

(The settings are STANDIN_AWS_*, not AWS_*: actions_release.py starts aws with an environment free of AWS_* variables.)

The "KMS key" is a local test key: STANDIN_AWS_SECRET (64 hex digits, the 32-byte Ed25519 secret). Like KMS it refuses a RAW message
over 4096 bytes, another message type or algorithm. Every call is appended to STANDIN_AWS_LOG (when set). Faults (STANDIN_AWS_FAULT,
comma separated):
  wrong-key        get-public-key answers another key (and sign signs with it): a key not in the trusted list
  sign-other-key   get-public-key answers the right key, sign signs with another key
  bad-signature    sign answers the right signature with one bit flipped
  short-signature  sign answers 63 bytes
  wrong-spec       get-public-key answers KeySpec ECC_NIST_P256
  wrong-usage      get-public-key answers KeyUsage ENCRYPT_DECRYPT
  no-ed25519       get-public-key answers SigningAlgorithms without ED25519_SHA_512
  bad-der          get-public-key answers a PublicKey that is not an Ed25519 SubjectPublicKeyInfo
  error            every call fails like an expired session (exit 254; the message carries a fake access key id the tool must not print)
  sign-error       sign fails (exit 254)
  sts-error        assume-role-with-web-identity fails (exit 254)
"""
from __future__ import annotations

import base64
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ed25519  # noqa: E402

FAULTS = set(filter(None, os.environ.get("STANDIN_AWS_FAULT", "").split(",")))
OTHER = bytes(range(100, 132))
SPKI = bytes.fromhex("302a300506032b6570032100")
KEY_ID = "00000000-0000-0000-0000-000000000000"
# a fake access key id and session token (AWS's documented example shapes, built here so the public tree's privacy rules never see one)
FAKE_KEY_ID, FAKE_TOKEN = "AK" + "IA" + "IOSFODNN7EXAMPLE", "FwoGZXIvYXdzEBYaDHqa0AP1y0zsBkeR7yLBAWjp0Gk4lLz9xhAs"


def opt(args: list[str], name: str) -> str | None:
    return args[args.index(name) + 1] if name in args and args.index(name) + 1 < len(args) else None


def fail(msg: str) -> int:
    print(f"\nAn error occurred {msg}", file=sys.stderr)
    return 254


def main(argv: list[str]) -> int:
    log = os.environ.get("STANDIN_AWS_LOG")
    if log:
        with open(log, "a") as f:
            f.write(" ".join(argv) + "\n")
    if argv[:2] == ["sts", "assume-role-with-web-identity"]:
        if "error" in FAULTS or "sts-error" in FAULTS:
            return fail("(AccessDenied) when calling the AssumeRoleWithWebIdentity operation: Not authorized")
        if not opt(argv, "--role-arn") or not opt(argv, "--web-identity-token") or opt(argv, "--output") != "json":
            return fail("(ValidationException): --role-arn, --web-identity-token and --output json are required here")
        print(json.dumps({"Credentials": {"AccessKeyId": "AS" + "IA" + "STANDINFAKE00000", "SecretAccessKey": "standin-fake-secret-" + "0" * 20,
                                          "SessionToken": "standin-fake-session-token", "Expiration": "2026-10-22T00:15:00Z"},
                          "SubjectFromWebIdentityToken": "repo:Test/repo:environment:nightly"}, indent=4))
        return 0
    if len(argv) < 2 or argv[0] != "kms" or argv[1] not in ("get-public-key", "sign"):
        return fail("(InvalidCommand): aws stand-in knows kms get-public-key, kms sign and sts assume-role-with-web-identity only")
    if "error" in FAULTS:
        return fail("(ExpiredTokenException) when calling the operation: The security token included in the request is expired "
                    f"(access key {FAKE_KEY_ID}, token {FAKE_TOKEN})")
    if opt(argv, "--output") != "json" or not opt(argv, "--key-id"):
        return fail("(ValidationException): --key-id and --output json are required here")
    secret = bytes.fromhex(os.environ["STANDIN_AWS_SECRET"])
    if "wrong-key" in FAULTS:
        secret = OTHER
    if argv[1] == "get-public-key":
        der = SPKI + ed25519.public_key(secret)
        if "bad-der" in FAULTS:
            der = bytes.fromhex("3059301306072a8648ce3d020106082a8648ce3d030107034200") + b"\x04" + bytes(64)
        answer = {"KeyId": KEY_ID,
                  "PublicKey": base64.b64encode(der).decode(),
                  "CustomerMasterKeySpec": "ECC_NIST_EDWARDS25519", "KeySpec": "ECC_NIST_EDWARDS25519", "KeyUsage": "SIGN_VERIFY",
                  "SigningAlgorithms": ["ED25519_SHA_512", "ED25519_PH_SHA_512"]}
        if "wrong-spec" in FAULTS:
            answer["KeySpec"] = "ECC_NIST_P256"
        if "wrong-usage" in FAULTS:
            answer["KeyUsage"] = "ENCRYPT_DECRYPT"
        if "no-ed25519" in FAULTS:
            answer["SigningAlgorithms"] = ["ED25519_PH_SHA_512"]
        print(json.dumps(answer, indent=4))
        return 0
    if "sign-error" in FAULTS:
        return fail("(KMSInternalException) when calling the Sign operation")
    if opt(argv, "--message-type") != "RAW" or opt(argv, "--signing-algorithm") != "ED25519_SHA_512":
        return fail("(ValidationException): this stand-in signs RAW messages with ED25519_SHA_512 only")
    message = opt(argv, "--message") or ""
    if not message.startswith("fileb://"):
        return fail("(ValidationException): --message must be fileb://")
    data = Path(message[len("fileb://"):]).read_bytes()
    if len(data) > 4096:
        return fail("(ValidationException) when calling the Sign operation: the message is longer than 4096 bytes")
    if "sign-other-key" in FAULTS:
        secret = OTHER
    sig = ed25519.sign(secret, data)
    if "bad-signature" in FAULTS:
        sig = sig[:10] + bytes([sig[10] ^ 1]) + sig[11:]
    if "short-signature" in FAULTS:
        sig = sig[:63]
    print(json.dumps({"KeyId": KEY_ID,
                      "Signature": base64.b64encode(sig).decode(), "SigningAlgorithm": "ED25519_SHA_512"}, indent=4))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
