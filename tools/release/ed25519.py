"""Lane LAUNCH-1: Ed25519 (RFC 8032, PureEdDSA) in pure Python, for signing and checking release manifests.

This is the reference implementation of RFC 8032 section 6 (Python big integers, hashlib's SHA-512), kept close to the RFC's text so it
can be read against it. No third-party module is needed (this machine has no `cryptography` / PyNaCl). It is checked against the RFC 8032
section 7.1 test vectors and against OpenSSL (`openssl pkeyutl -rawin`) in tools/release/test_launcher_tools.py and test_launcher.py.

It is not constant-time: signing runs on the coordinator's offline machine, where timing side channels are not part of the threat model
(docs/RELEASE.md, "The updater").

Keys on disk are OpenSSL's PKCS#8 PEM ("-----BEGIN PRIVATE KEY-----", `openssl genpkey -algorithm ed25519`), so the same key works with both.
"""
from __future__ import annotations

import base64
import hashlib

p = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493  # the group order
d = -121665 * pow(121666, p - 2, p) % p
SQRT_M1 = pow(2, (p - 1) // 4, p)


def _sha512(b: bytes) -> bytes:
    return hashlib.sha512(b).digest()


# points in extended coordinates (X, Y, Z, T), x = X/Z, y = Y/Z, x*y = T/Z
def _add(P, Q):
    A = (P[1] - P[0]) * (Q[1] - Q[0]) % p
    B = (P[1] + P[0]) * (Q[1] + Q[0]) % p
    C = 2 * P[3] * Q[3] * d % p
    D = 2 * P[2] * Q[2] % p
    E, F, G, H = B - A, D - C, D + C, B + A
    return (E * F % p, G * H % p, F * G % p, E * H % p)


def _mul(s: int, P):
    Q = (0, 1, 1, 0)  # the neutral element
    while s > 0:
        if s & 1:
            Q = _add(Q, P)
        P = _add(P, P)
        s >>= 1
    return Q


def _equal(P, Q) -> bool:
    return (P[0] * Q[2] - Q[0] * P[2]) % p == 0 and (P[1] * Q[2] - Q[1] * P[2]) % p == 0


def _recover_x(y: int, sign: int) -> int | None:
    if y >= p:
        return None
    x2 = (y * y - 1) * pow(d * y * y + 1, p - 2, p)
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (p + 3) // 8, p)
    if (x * x - x2) % p != 0:
        x = x * SQRT_M1 % p
    if (x * x - x2) % p != 0:
        return None
    if (x & 1) != sign:
        x = p - x
    return x


_GY = 4 * pow(5, p - 2, p) % p
_GX = _recover_x(_GY, 0)
G = (_GX, _GY, 1, _GX * _GY % p)


def _compress(P) -> bytes:
    zinv = pow(P[2], p - 2, p)
    x, y = P[0] * zinv % p, P[1] * zinv % p
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(s: bytes):
    if len(s) != 32:
        return None
    y = int.from_bytes(s, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % p)


def _expand(secret: bytes) -> tuple[int, bytes]:
    if len(secret) != 32:
        raise ValueError("an Ed25519 secret key is 32 bytes")
    h = _sha512(secret)
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(secret: bytes) -> bytes:
    a, _ = _expand(secret)
    return _compress(_mul(a, G))


def sign(secret: bytes, msg: bytes) -> bytes:
    a, prefix = _expand(secret)
    A = _compress(_mul(a, G))
    r = int.from_bytes(_sha512(prefix + msg), "little") % L
    R = _compress(_mul(r, G))
    h = int.from_bytes(_sha512(R + A + msg), "little") % L
    s = (r + h * a) % L
    return R + int.to_bytes(s, 32, "little")


def is_small_order(P) -> bool:
    """[8]P is the neutral element: P is one of the 8 torsion points"""
    return _equal(_mul(8, P), (0, 1, 1, 0))


def verify(public: bytes, msg: bytes, signature: bytes) -> bool:
    """RFC 8032 5.1.7, strict: a 64-byte signature, a canonical public key (y < p, on the curve), S < L; [S]B == R + [h]A.
    Also refused (defence in depth, as the launcher does): a public key A or a commitment R of small order."""
    if len(public) != 32 or len(signature) != 64:
        return False
    A = _decompress(public)
    R = _decompress(signature[:32])
    if A is None or R is None or is_small_order(A) or is_small_order(R):
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    h = int.from_bytes(_sha512(signature[:32] + public + msg), "little") % L
    return _equal(_mul(s, G), _add(R, _mul(h, A)))


# ---- OpenSSL's key file formats (RFC 8410) -------------------------------------------------------------------------------------------------
_PKCS8_PREFIX = bytes.fromhex("302e020100300506032b657004220420")  # PrivateKeyInfo { v1, id-Ed25519, OCTET STRING { OCTET STRING (32) } }
_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")            # SubjectPublicKeyInfo { id-Ed25519, BIT STRING (32) }


def _pem(label: str, der: bytes) -> str:
    b64 = base64.b64encode(der).decode()
    return f"-----BEGIN {label}-----\n" + "\n".join(b64[i:i + 64] for i in range(0, len(b64), 64)) + f"\n-----END {label}-----\n"


def _unpem(label: str, text: str) -> bytes:
    lines = [ln.strip() for ln in text.strip().splitlines()]
    if len(lines) < 3 or lines[0] != f"-----BEGIN {label}-----" or lines[-1] != f"-----END {label}-----":
        raise ValueError(f"not a PEM {label.lower()}")
    return base64.b64decode("".join(lines[1:-1]), validate=True)


def private_key_pem(secret: bytes) -> str:
    return _pem("PRIVATE KEY", _PKCS8_PREFIX + secret)


def secret_from_pem(text: str) -> bytes:
    der = _unpem("PRIVATE KEY", text)
    if len(der) != len(_PKCS8_PREFIX) + 32 or not der.startswith(_PKCS8_PREFIX):
        raise ValueError("not an Ed25519 PKCS#8 private key (openssl genpkey -algorithm ed25519)")
    return der[len(_PKCS8_PREFIX):]


def public_key_pem(public: bytes) -> str:
    return _pem("PUBLIC KEY", _SPKI_PREFIX + public)


def public_from_pem(text: str) -> bytes:
    der = _unpem("PUBLIC KEY", text)
    if len(der) != len(_SPKI_PREFIX) + 32 or not der.startswith(_SPKI_PREFIX):
        raise ValueError("not an Ed25519 public key")
    return der[len(_SPKI_PREFIX):]
