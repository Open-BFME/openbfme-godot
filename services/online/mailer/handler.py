"""OpenBFME mail function: sends one lobby e-mail through Amazon SES. GPL-3.0.

ONLINEMENU-1 (owner identity decisions 2026-10-20, docs/IDENTITY.md "Mail function"). An AWS Lambda (Python 3.12 standard runtime, boto3 from the
runtime, no other dependency) behind a function URL with auth type NONE: the HMAC below is the only authentication. The lobby
(services/online/lobby/src/email.mjs sendMail) POSTs JSON {v:1, to, subject, text, html} with two headers:

  x-openbfme-time  unix seconds; refused when more than 300 s from this function's clock
  x-openbfme-sig   hex HMAC-SHA256(MAILER_KEY, f"{time}\\n{raw body}"), MAILER_KEY being the hex secret shared with the lobby's Worker

Answers: 200 {"ok": true}, or {"ok": false, "error": code} with a 4xx status (5xx when this function is not configured or SES refuses).
Privacy: the address, the body and the key are never logged. A log line holds the Lambda request id and the outcome code only.
"""

import base64
import binascii
import hashlib
import hmac
import json
import os
import re
import time

MAX_BODY = 64 * 1024
MAX_PART = 32 * 1024
MAX_ADDRESS = 254
MAX_SUBJECT = 200
WINDOW_S = 300
MAX_SEEN = 4096
FIELDS = {"v", "to", "subject", "text", "html"}
TIME = re.compile(r"^[0-9]{1,12}$")
SIG = re.compile(r"^[0-9a-f]{64}$")
# One plain address: no spaces, quotes, brackets, commas or semicolons (so never a list), one '@', a dotted ASCII domain.
ADDRESS = re.compile(r"^[A-Za-z0-9!#$%&'*+/=?^_`{|}~.-]{1,64}@[A-Za-z0-9-]{1,63}(?:\.[A-Za-z0-9-]{1,63})+$")
CONTROL = re.compile(r"[\x00-\x1f\x7f]")

# Signatures seen in the last WINDOW_S seconds (signature -> expiry). Per container only: see README "Limits".
_seen = {}
_client = None


def _ses():
    """The SES client, made once per container. boto3 is imported here so the tests run without it."""
    global _client
    if _client is None:
        import boto3

        _client = boto3.client("ses", region_name=os.environ.get("SES_REGION") or "us-east-2")
    return _client


def _answer(status, error=None):
    body = {"ok": True} if error is None else {"ok": False, "error": error}
    return {"statusCode": status, "headers": {"content-type": "application/json", "cache-control": "no-store"}, "body": json.dumps(body)}


def _log(context, outcome):
    request = getattr(context, "aws_request_id", None)
    request = request if isinstance(request, str) and re.fullmatch(r"[0-9A-Za-z-]{1,64}", request) else "-"
    print(json.dumps({"request": request, "outcome": outcome}))


def _key():
    """MAILER_KEY: hex, at least 32 bytes. None when missing or malformed."""
    raw = os.environ.get("MAILER_KEY", "")
    try:
        key = bytes.fromhex(raw)
    except ValueError:
        return None
    return key if len(key) >= 32 else None


def _header(event, name):
    headers = event.get("headers") or {}
    if not isinstance(headers, dict):
        return None
    for key, value in headers.items():
        if isinstance(key, str) and key.lower() == name:
            return value if isinstance(value, str) else None
    return None


def _raw_body(event):
    body = event.get("body")
    if body is None:
        return b""
    if not isinstance(body, str):
        return None
    if event.get("isBase64Encoded"):
        try:
            return base64.b64decode(body, validate=True)
        except (binascii.Error, ValueError):
            return None
    return body.encode("utf-8")


def _one_line(value, maximum):
    return isinstance(value, str) and 1 <= len(value) <= maximum and not CONTROL.search(value)


def _part(value):
    return isinstance(value, str) and len(value) >= 1 and len(value.encode("utf-8")) <= MAX_PART


def _remember(signature, now):
    """True when the signature is new (and keeps it until it expires); False for a replay or a full cache."""
    for old in [s for s, expiry in _seen.items() if expiry <= now]:
        del _seen[old]
    if signature in _seen:
        return False
    if len(_seen) >= MAX_SEEN:
        return None
    _seen[signature] = now + WINDOW_S
    return True


def _check(event, now):
    """The request's fields, or an (status, code) refusal."""
    method = ((event.get("requestContext") or {}).get("http") or {}).get("method")
    if method != "POST":
        return None, (405, "method")
    key = _key()
    if key is None:
        return None, (500, "not_configured")
    stamp = _header(event, "x-openbfme-time")
    signature = _header(event, "x-openbfme-sig")
    if stamp is None or not TIME.match(stamp):
        return None, (401, "bad_time")
    if abs(now - int(stamp)) > WINDOW_S:
        return None, (401, "stale_time")
    if signature is None or not SIG.match(signature):
        return None, (401, "bad_signature")
    raw = _raw_body(event)
    if raw is None:
        return None, (400, "bad_body")
    if len(raw) > MAX_BODY:
        return None, (413, "too_large")
    want = hmac.new(key, stamp.encode("ascii") + b"\n" + raw, hashlib.sha256).hexdigest()
    if not hmac.compare_digest(want, signature):
        return None, (401, "bad_signature")
    fresh = _remember(signature, now)
    if fresh is None:
        return None, (429, "busy")
    if not fresh:
        return None, (401, "replay")
    try:
        message = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None, (400, "bad_json")
    if not isinstance(message, dict) or set(message) != FIELDS or message.get("v") != 1 or isinstance(message.get("v"), bool):
        return None, (400, "bad_fields")
    to = message["to"]
    if not _one_line(to, MAX_ADDRESS) or not ADDRESS.match(to):
        return None, (400, "bad_to")
    if not _one_line(message["subject"], MAX_SUBJECT):
        return None, (400, "bad_subject")
    if not _part(message["text"]) or not _part(message["html"]):
        return None, (400, "bad_content")
    return message, None


def lambda_handler(event, context):
    try:
        if not isinstance(event, dict):
            _log(context, "bad_event")
            return _answer(400, "bad_event")
        message, refusal = _check(event, int(time.time()))
        if refusal is not None:
            _log(context, refusal[1])
            return _answer(*refusal)
        try:
            _ses().send_email(
                Source=os.environ.get("MAIL_FROM") or "no-reply@openbfme.com",
                Destination={"ToAddresses": [message["to"]]},
                Message={
                    "Subject": {"Data": message["subject"], "Charset": "UTF-8"},
                    "Body": {
                        "Text": {"Data": message["text"], "Charset": "UTF-8"},
                        "Html": {"Data": message["html"], "Charset": "UTF-8"},
                    },
                },
            )
        except Exception as error:  # noqa: BLE001 - SES messages can name the address: log only AWS's error code
            code = ((getattr(error, "response", None) or {}).get("Error") or {}).get("Code")
            code = code if isinstance(code, str) and re.fullmatch(r"[A-Za-z0-9.]{1,64}", code) else type(error).__name__
            _log(context, f"send_failed:{code}")
            return _answer(502, "send_failed")
        _log(context, "sent")
        return _answer(200)
    except Exception as error:  # noqa: BLE001 - never let a traceback (which could hold the body) reach the log
        _log(context, f"internal:{type(error).__name__}")
        return _answer(500, "internal")
