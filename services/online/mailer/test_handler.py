"""Tests for the OpenBFME mail function (handler.py). ONLINEMENU-1 (owner identity decisions 2026-10-20).

Plain unittest (pytest runs them too); boto3 is never imported: a fake SES client stands in for it.
    python3 -m unittest services/online/mailer/test_handler.py
"""

import base64
import contextlib
import hashlib
import hmac
import io
import json
import os
import sys
import time
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import handler  # noqa: E402

KEY_HEX = "e5" * 32
ADDRESS = "Private.Person@example.org"
SECRET_WORDS = ("private.person", KEY_HEX, "magic-token-xyz")


class FakeSes:
    def __init__(self, error=None):
        self.calls = []
        self.error = error

    def send_email(self, **kwargs):
        self.calls.append(kwargs)
        if self.error is not None:
            raise self.error
        return {"MessageId": "fake"}


class FakeClientError(Exception):
    """Shaped like botocore's ClientError, whose message can name the address."""

    def __init__(self, code):
        super().__init__(f"An error occurred ({code}): Email address is not verified: {ADDRESS}")
        self.response = {"Error": {"Code": code, "Message": f"not verified: {ADDRESS}"}}


class Context:
    aws_request_id = "11111111-2222-3333-4444-555555555555"


def message(**overrides):
    value = {
        "v": 1,
        "to": ADDRESS,
        "subject": "Confirm your e-mail for OpenBFME",
        "text": "Click https://play.openbfme.com/email/verify/magic-token-xyz",
        "html": '<p><a href="https://play.openbfme.com/email/verify/magic-token-xyz">confirm</a></p>',
    }
    value.update(overrides)
    return value


def signed(body, stamp=None, key_hex=KEY_HEX, method="POST", base64_body=False, headers=None):
    raw = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode("utf-8")
    stamp = str(int(time.time())) if stamp is None else str(stamp)
    sig = hmac.new(bytes.fromhex(key_hex), stamp.encode() + b"\n" + raw, hashlib.sha256).hexdigest()
    event = {
        "requestContext": {"http": {"method": method}},
        "headers": {"x-openbfme-time": stamp, "x-openbfme-sig": sig, "content-type": "application/json"},
        "body": base64.b64encode(raw).decode() if base64_body else raw.decode("utf-8"),
        "isBase64Encoded": base64_body,
    }
    if headers is not None:
        event["headers"].update(headers)
    return event


class HandlerTest(unittest.TestCase):
    def setUp(self):
        handler._seen.clear()
        self.ses = FakeSes()
        self.patches = [
            mock.patch.object(handler, "_ses", lambda: self.ses),
            mock.patch.dict(os.environ, {"MAILER_KEY": KEY_HEX, "MAIL_FROM": "no-reply@openbfme.com"}),
        ]
        for patch in self.patches:
            patch.start()

    def tearDown(self):
        for patch in reversed(self.patches):
            patch.stop()
        handler._seen.clear()

    def call(self, event):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            answer = handler.lambda_handler(event, Context())
        printed = out.getvalue() + err.getvalue()
        for word in SECRET_WORDS:
            self.assertNotIn(word, printed.lower() if word == "private.person" else printed)
        for line in printed.splitlines():
            self.assertEqual(set(json.loads(line)), {"request", "outcome"})
        return answer["statusCode"], json.loads(answer["body"]), printed

    def assertRefused(self, event, status, code):
        got_status, body, _ = self.call(event)
        self.assertEqual((got_status, body), (status, {"ok": False, "error": code}))
        self.assertEqual(self.ses.calls, [])

    def test_sends_a_signed_message(self):
        status, body, printed = self.call(signed(message()))
        self.assertEqual((status, body), (200, {"ok": True}))
        self.assertEqual(len(self.ses.calls), 1)
        call = self.ses.calls[0]
        self.assertEqual(call["Source"], "no-reply@openbfme.com")
        self.assertEqual(call["Destination"], {"ToAddresses": [ADDRESS]})
        self.assertEqual(call["Message"]["Subject"]["Data"], "Confirm your e-mail for OpenBFME")
        self.assertIn("magic-token-xyz", call["Message"]["Body"]["Text"]["Data"])
        self.assertIn("magic-token-xyz", call["Message"]["Body"]["Html"]["Data"])
        self.assertEqual(json.loads(printed), {"request": Context.aws_request_id, "outcome": "sent"})

    def test_base64_bodies_and_header_case(self):
        event = signed(message(), base64_body=True)
        event["headers"] = {k.upper(): v for k, v in event["headers"].items()}
        status, body, _ = self.call(event)
        self.assertEqual((status, body), (200, {"ok": True}))

    def test_mail_from_default(self):
        with mock.patch.dict(os.environ, {"MAIL_FROM": ""}):
            self.call(signed(message()))
        self.assertEqual(self.ses.calls[0]["Source"], "no-reply@openbfme.com")

    def test_method_must_be_post(self):
        self.assertRefused(signed(message(), method="GET"), 405, "method")

    def test_missing_or_short_key(self):
        with mock.patch.dict(os.environ, {"MAILER_KEY": ""}):
            self.assertRefused(signed(message()), 500, "not_configured")
        with mock.patch.dict(os.environ, {"MAILER_KEY": "ab" * 16}):
            self.assertRefused(signed(message(), key_hex="ab" * 16), 500, "not_configured")
        with mock.patch.dict(os.environ, {"MAILER_KEY": "zz" * 32}):
            self.assertRefused(signed(message()), 500, "not_configured")

    def test_time_window(self):
        now = int(time.time())
        self.assertRefused(signed(message(), stamp=now - 301), 401, "stale_time")
        self.assertRefused(signed(message(), stamp=now + 301), 401, "stale_time")
        self.assertRefused(signed(message(), stamp="12.5"), 401, "bad_time")
        self.assertRefused(signed(message(), stamp="-5"), 401, "bad_time")
        event = signed(message())
        del event["headers"]["x-openbfme-time"]
        self.assertRefused(event, 401, "bad_time")
        status, _, _ = self.call(signed(message(), stamp=now - 290))
        self.assertEqual(status, 200)

    def test_signature(self):
        self.assertRefused(signed(message(), key_hex="f6" * 32), 401, "bad_signature")
        event = signed(message())
        event["body"] = json.dumps(message(subject="Changed"))
        self.assertRefused(event, 401, "bad_signature")
        event = signed(message())
        event["headers"]["x-openbfme-sig"] = event["headers"]["x-openbfme-sig"].upper()
        self.assertRefused(event, 401, "bad_signature")
        event = signed(message())
        del event["headers"]["x-openbfme-sig"]
        self.assertRefused(event, 401, "bad_signature")
        # The timestamp is covered too.
        event = signed(message())
        event["headers"]["x-openbfme-time"] = str(int(event["headers"]["x-openbfme-time"]) - 1)
        self.assertRefused(event, 401, "bad_signature")

    def test_replay_is_refused(self):
        event = signed(message())
        self.assertEqual(self.call(event)[0], 200)
        self.ses.calls.clear()
        self.assertRefused(event, 401, "replay")
        # An expired entry is forgotten (the time window refuses the request itself by then).
        handler._seen.update({k: 0 for k in handler._seen})
        handler._remember("x" * 64, int(time.time()))
        self.assertEqual(set(handler._seen), {"x" * 64})

    def test_replay_cache_is_bounded(self):
        now = int(time.time())
        handler._seen.update({f"{i:064x}": now + 300 for i in range(handler.MAX_SEEN)})
        self.assertRefused(signed(message()), 429, "busy")

    def test_body_size(self):
        big = message(text="t" * (handler.MAX_BODY))
        self.assertRefused(signed(big), 413, "too_large")
        self.assertRefused(signed(message(text="t" * (32 * 1024 + 1))), 400, "bad_content")
        self.assertRefused(signed(message(html="h" * (16 * 1024 + 1) + "é" * (8 * 1024))), 400, "bad_content")

    def test_fields(self):
        self.assertRefused(signed(b"not json"), 400, "bad_json")
        self.assertRefused(signed(b"\xff\xfe", base64_body=True), 400, "bad_json")
        broken = signed(message(), base64_body=True)
        broken["body"] = "not base64!"
        self.assertRefused(broken, 400, "bad_body")
        self.assertRefused(signed([message()]), 400, "bad_fields")
        self.assertRefused(signed(message(v=2)), 400, "bad_fields")
        self.assertRefused(signed(message(v=True)), 400, "bad_fields")
        self.assertRefused(signed({**message(), "cc": "x@example.com"}), 400, "bad_fields")
        partial = message()
        del partial["html"]
        self.assertRefused(signed(partial), 400, "bad_fields")
        for to in [
            "a@b.example\r\nBcc: evil@example.com",
            "a@b.example\n",
            "a@b.example, c@d.example",
            "a@b.example;c@d.example",
            "Name <a@b.example>",
            "no-at.example",
            "a@@b.example",
            "a@localhost",
            "a b@c.example",
            "x" * 250 + "@b.example",
            "",
            42,
        ]:
            self.assertRefused(signed(message(to=to)), 400, "bad_to")
        self.assertRefused(signed(message(subject="two\r\nlines")), 400, "bad_subject")
        self.assertRefused(signed(message(subject="s" * 201)), 400, "bad_subject")
        self.assertRefused(signed(message(subject="")), 400, "bad_subject")
        self.assertRefused(signed(message(text="")), 400, "bad_content")
        self.assertRefused(signed(message(html=None)), 400, "bad_content")

    def test_ses_failure_logs_only_the_code(self):
        self.ses.error = FakeClientError("MessageRejected")
        status, body, printed = self.call(signed(message()))
        self.assertEqual((status, body), (502, {"ok": False, "error": "send_failed"}))
        self.assertEqual(json.loads(printed)["outcome"], "send_failed:MessageRejected")
        self.ses.error = RuntimeError(f"boom {ADDRESS}")
        status, _, printed = self.call(signed(message(subject="Another")))
        self.assertEqual(status, 502)
        self.assertEqual(json.loads(printed)["outcome"], "send_failed:RuntimeError")

    def test_unexpected_errors_are_contained(self):
        self.assertEqual(self.call(None)[0:2], (400, {"ok": False, "error": "bad_event"}))
        with mock.patch.object(handler, "_check", side_effect=ValueError(f"leak {ADDRESS} {KEY_HEX}")):
            status, body, printed = self.call(signed(message()))
        self.assertEqual((status, body), (500, {"ok": False, "error": "internal"}))
        self.assertEqual(json.loads(printed)["outcome"], "internal:ValueError")

    def test_request_id_is_sanitized(self):
        class Odd:
            aws_request_id = f"id {ADDRESS}\n"

        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            handler.lambda_handler(signed(message()), Odd())
        self.assertEqual(json.loads(out.getvalue()), {"request": "-", "outcome": "sent"})

    def test_matches_the_lobby_signature(self):
        # The lobby signs `${time}\n${JSON.stringify(body)}` with the hex-decoded MAILER_KEY (email.test.mjs checks the same formula).
        raw = b'{"v":1,"to":"a@b.example","subject":"s","text":"t","html":"h"}'
        want = hmac.new(bytes.fromhex(KEY_HEX), b"1760000123\n" + raw, hashlib.sha256).hexdigest()
        event = signed(raw, stamp=1760000123)
        self.assertEqual(event["headers"]["x-openbfme-sig"], want)
        with mock.patch.object(handler.time, "time", return_value=1760000123.4):
            self.assertEqual(self.call(event)[0], 200)


if __name__ == "__main__":
    unittest.main()
