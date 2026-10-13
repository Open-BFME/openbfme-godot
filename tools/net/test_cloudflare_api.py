# Lane CFDEPLOY-1: services/online/deploy/cloudflare_api.py offline: the Free plan check stops a deployment before any change, the secrets come from
# stdin only and are never printed, the multipart upload carries the metadata (Durable Object binding, new_sqlite_classes migration, vars, secrets) and
# the two ES modules byte for byte. The API is a fake transport: nothing here talks to Cloudflare.
import base64
import email.parser
import email.policy
import io
import json
import re
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "services" / "online" / "deploy"))
import cloudflare_api as cf  # noqa: E402

SRC = ROOT / "services" / "online" / "lobby" / "src"
TOKEN = "test-token-not-real-0123456789"
ACCOUNT = "a" * 32
ZONE = "b" * 32
ENV = {"CLOUDFLARE_API_TOKEN": TOKEN, "CLOUDFLARE_ACCOUNT_ID": ACCOUNT, "CLOUDFLARE_ZONE_ID": ZONE}
TICKET = "# OpenBFME relay ticket keys (keyId:hex)\n1:" + "11" * 32 + "\n"
SESSION = "22" * 32
UPLOAD = "33" * 32
ADMIN = "44" * 32


def secrets_dir(tmp_path):
    d = tmp_path / "secrets"
    d.mkdir(exist_ok=True)
    (d / "ticket.keys").write_text(TICKET)
    (d / "session.key").write_text(SESSION)
    (d / "upload.key").write_text(UPLOAD)
    (d / "admin.key").write_text(ADMIN)
    return d


class FakeCloudflare:
    def __init__(self, subscriptions=None, subs_status=200, scripts=(), page_size=20, upload_answer=None, fail_page=None):
        self.calls = []
        self.subscriptions = [] if subscriptions is None else subscriptions
        self.subs_status = subs_status
        self.scripts = list(scripts)
        self.page_size = page_size  # the API's page size, whatever per_page asks (Sol r1: 21 subscriptions, 20 on page 1)
        self.upload_answer = upload_answer
        self.fail_page = fail_page  # a subscriptions page that answers 503

    def __call__(self, method, url, headers, data):
        path = url[len(cf.API):]
        assert headers["authorization"] == f"Bearer {TOKEN}"
        self.calls.append((method, path, headers.get("content-type"), data))
        ok = lambda result: (200, json.dumps({"success": True, "errors": [], "result": result}).encode())  # noqa: E731
        if path.startswith(f"/accounts/{ACCOUNT}/subscriptions"):
            if self.subs_status != 200:
                return self.subs_status, json.dumps({"success": False, "errors": [{"code": 9109, "message": "Unauthorized to access requested resource"}]}).encode()
            page = int(re.search(r"page=(\d+)", path).group(1))
            if page == self.fail_page:
                return 503, json.dumps({"success": False, "errors": [{"code": 10000, "message": "upstream unavailable"}]}).encode()
            n = self.page_size
            pages = max(1, -(-len(self.subscriptions) // n))
            return 200, json.dumps({"success": True, "errors": [], "result": self.subscriptions[(page - 1) * n:page * n],
                                    "result_info": {"page": page, "per_page": n, "total_pages": pages, "total_count": len(self.subscriptions)}}).encode()
        if method == "GET" and path == f"/accounts/{ACCOUNT}/workers/scripts":
            return ok(self.scripts)
        if method == "PUT" and path.startswith(f"/accounts/{ACCOUNT}/workers/scripts/"):
            if self.upload_answer:
                return self.upload_answer
            return ok({"id": path.rsplit("/", 1)[1], "migration_tag": "v1"})
        if method == "PUT" and path == f"/accounts/{ACCOUNT}/workers/domains":
            body = json.loads(data)
            return ok({"hostname": body["hostname"], "service": body["service"], "zone_name": "openbfme.com"})
        if method == "GET" and path == f"/accounts/{ACCOUNT}/workers/domains":
            return ok([])
        return 404, json.dumps({"success": False, "errors": [{"code": 7003, "message": "no route"}]}).encode()

    def writes(self):
        return [(m, p) for m, p, _, _ in self.calls if m != "GET"]


def run_deploy(tmp_path, fake, capsys, extra=()):
    stdin = io.StringIO(cf.pack_secrets(secrets_dir(tmp_path)))
    argv = ["deploy", "--src", str(SRC), "--hostname", "play.openbfme.com", "--public-url", "https://play.openbfme.com", "--relays", "", "--no-health", *extra]
    code = cf.main(argv, stdin=stdin, environ=ENV, transport=fake)
    out = capsys.readouterr()
    return code, out.out + out.err


def parse_upload(content_type, body):
    msg = email.parser.BytesParser(policy=email.policy.HTTP).parsebytes(f"Content-Type: {content_type}\r\n\r\n".encode() + body)
    return {p.get_param("name", header="content-disposition"): (p.get_content_type(), p.get_payload(decode=True)) for p in msg.iter_parts()}


def test_deploy_uploads_the_worker_with_the_sqlite_durable_object_vars_and_stdin_secrets(tmp_path, capsys):
    fake = FakeCloudflare(subscriptions=[{"rate_plan": {"id": "free", "public_name": "Free Website"}, "price": 0}])
    code, printed = run_deploy(tmp_path, fake, capsys)
    assert code == 0, printed
    assert fake.writes() == [("PUT", f"/accounts/{ACCOUNT}/workers/scripts/openbfme-lobby"), ("PUT", f"/accounts/{ACCOUNT}/workers/domains")]
    _, _, content_type, body = next(c for c in fake.calls if c[0] == "PUT" and "/scripts/" in c[1])
    parts = parse_upload(content_type, body)
    assert list(parts) == ["metadata", "worker.mjs", "lobby.mjs"]
    for name in ("worker.mjs", "lobby.mjs"):
        assert parts[name] == ("application/javascript+module", (SRC / name).read_bytes())
    meta = json.loads(parts["metadata"][1])
    assert meta["main_module"] == "worker.mjs" and meta["compatibility_date"] == cf.COMPATIBILITY_DATE
    assert meta["migrations"] == {"new_tag": "v1", "new_sqlite_classes": ["LobbyObject"]}
    assert {b["name"]: (b["type"], b.get("text", b.get("class_name"))) for b in meta["bindings"]} == {
        "LOBBY": ("durable_object_namespace", "LobbyObject"),
        "RELAYS": ("plain_text", ""),
        "PUBLIC_URL": ("plain_text", "https://play.openbfme.com"),
        "LOG_STORE": ("plain_text", "durable-object"),
        "TICKET_KEYS": ("secret_text", TICKET.strip()),
        "SESSION_KEY": ("secret_text", SESSION),
        "UPLOAD_KEY": ("secret_text", UPLOAD),
        "ADMIN_KEY": ("secret_text", ADMIN),
    }
    domain = json.loads(fake.calls[-1][3])
    assert domain == {"hostname": "play.openbfme.com", "service": "openbfme-lobby", "zone_id": ZONE, "environment": "production"}
    # nothing secret on the screen: no key, no token
    for secret in (TOKEN, SESSION, UPLOAD, ADMIN, "11" * 32):
        assert secret not in printed


def test_a_redeploy_keeps_the_migration_and_refuses_an_unknown_tag(tmp_path, capsys):
    fake = FakeCloudflare(scripts=[{"id": "openbfme-lobby", "migration_tag": "v1"}])
    code, printed = run_deploy(tmp_path, fake, capsys)
    assert code == 0, printed
    _, _, content_type, body = next(c for c in fake.calls if c[0] == "PUT" and "/scripts/" in c[1])
    assert "migrations" not in json.loads(parse_upload(content_type, body)["metadata"][1])
    fake = FakeCloudflare(scripts=[{"id": "openbfme-lobby", "migration_tag": "v7"}])
    code, printed = run_deploy(tmp_path, fake, capsys)
    assert code == 1 and "migration tag is 'v7'" in printed
    assert fake.writes() == []


FREE_SITE = {"rate_plan": {"id": "free", "public_name": "Free Website"}, "price": 0}
WORKERS_PAID = {"rate_plan": {"id": "workers_paid", "public_name": "Workers Paid"}, "price": 5, "state": "Paid", "currency": "USD"}


@pytest.mark.parametrize("subs", [
    [WORKERS_PAID],
    [{"rate_plan": {"id": "PARTNERS_WORKERS_BASIC", "public_name": "Workers"}, "price": 0}],
    [FREE_SITE] * 20 + [WORKERS_PAID],  # Sol r1: the paid plan on page 2 was missed
])
def test_a_readable_paid_workers_plan_stops_before_any_change(tmp_path, capsys, subs):
    fake = FakeCloudflare(subscriptions=subs)
    code, printed = run_deploy(tmp_path, fake, capsys)
    assert code == 1
    assert "not Free" in printed
    assert fake.writes() == []
    assert all("/subscriptions" in p for _, p, _, _ in fake.calls), "it stopped before even reading the scripts"
    assert len(fake.calls) == -(-len(subs) // 20), "every page was read"


def test_unreadable_subscriptions_rely_on_the_owner_fact_and_are_never_called_free(tmp_path, capsys):
    # the owner's tokens have no Billing Read: the list is unreadable; the deploy goes on, saying what it relies on
    fake = FakeCloudflare(subs_status=403)
    code, printed = run_deploy(tmp_path, fake, capsys)
    assert code == 0, printed
    assert "not readable with this token" in printed and "owner's verified fact" in printed
    assert "no paid Workers subscription" not in printed
    # a readable list of free subscriptions says exactly that, and that every page was read
    code, printed = run_deploy(tmp_path, FakeCloudflare(subscriptions=[FREE_SITE] * 45), capsys)
    assert code == 0 and "among the account's 45 subscriptions (every page read)" in printed


@pytest.mark.parametrize("change,why", [
    (lambda m: m["bindings"].append({"type": "r2_bucket", "name": "LOGS", "bucket_name": "x"}), "r2_bucket"),
    (lambda m: m["bindings"].append({"type": "queue", "name": "Q", "queue_name": "x"}), "queue"),
    (lambda m: m["bindings"].append({"type": "kv_namespace", "name": "K", "namespace_id": "x"}), "kv_namespace"),
    (lambda m: m["bindings"][0].update(script_name="other"), "must be this Worker's"),
    (lambda m: m.update(migrations={"new_tag": "v1", "new_classes": ["LobbyObject"]}), "SQLite class migration"),
    (lambda m: m.update(keep_bindings=["r2_bucket"]), "keep_bindings"),
    (lambda m: m.update(usage_model="unbound"), "usage_model"),
])
def test_only_free_plan_bindings_are_uploaded(change, why):
    secrets = {"TICKET_KEYS": TICKET, "SESSION_KEY": SESSION, "UPLOAD_KEY": UPLOAD, "ADMIN_KEY": ADMIN}
    meta = cf.metadata("", "https://x", "durable-object", secrets, None)
    cf.check_free_metadata(meta)  # the default deployment passes
    change(meta)
    with pytest.raises(cf.DeployError, match=why):
        cf.check_free_metadata(meta)


def test_api_failures_print_no_secret_and_no_raw_body(tmp_path, capsys):
    # Sol r1: an error message reflecting what was sent reached the output
    reflected = json.dumps({"success": False, "errors": [{"code": 10021, "message": f"binding SESSION_KEY: bad value {SESSION}; token {TOKEN}; ring 1:{'11' * 32}"}]})
    code, printed = run_deploy(tmp_path, FakeCloudflare(upload_answer=(400, reflected.encode())), capsys)
    assert code == 1
    assert "10021" in printed and "messages are not shown" in printed
    for secret in (SESSION, UPLOAD, TOKEN, "11" * 32):
        assert secret not in printed
    # a body that is not Cloudflare's JSON is never shown at all
    raw = f"<html>upstream echoed {UPLOAD} and the word RAWBODY</html>".encode()
    code, printed = run_deploy(tmp_path, FakeCloudflare(upload_answer=(502, raw)), capsys)
    assert code == 1
    assert "RAWBODY" not in printed and UPLOAD not in printed and "the response body is not shown" in printed
    # the redaction keeps ordinary words
    r = cf.Redactor()
    r.add(TICKET)
    assert r("OpenBFME relay ticket keys") == "OpenBFME relay ticket keys"
    assert r("x " + "11" * 32) == "x <redacted>"


@pytest.mark.parametrize("answer,relays,store,problem", [
    ({"v": 1, "ok": True, "relays": [], "relay": "no relay configured", "logStore": "durable-object"}, "", "durable-object", None),
    ({"v": 1, "ok": True, "relays": [{"id": "us-east", "address": "203.0.113.10:7350"}], "logStore": "durable-object"}, "us-east=203.0.113.10:7350", "durable-object", None),
    ({"v": 1, "ok": False, "relays": [], "relay": "no relay configured", "logStore": "durable-object"}, "", "durable-object", "ok is False"),
    ({"v": 2, "ok": True, "relays": [], "relay": "no relay configured"}, "", None, "version 2"),
    ({"v": 1, "ok": True, "relays": []}, "", None, "without the"),
    ({"v": 1, "ok": True, "relays": [], "relay": "no relay configured"}, "us-east=203.0.113.10:7350", None, "no relay, but RELAYS"),
    ({"v": 1, "ok": True, "relays": [{"id": "x", "address": "198.51.100.1:7350"}]}, "us-east=203.0.113.10:7350", None, "expected"),
    ({"v": 1, "ok": True, "relays": [], "relay": "no relay configured", "logStore": "none"}, "", "durable-object", "log store"),
    (["ok"], "", None, "not a JSON object"),
])
def test_the_health_check_requires_a_healthy_answer(answer, relays, store, problem):
    got = cf.health_problem(answer, relays, store)
    assert (got is None) if problem is None else (problem in got), got


def test_an_unhealthy_answer_fails_the_deploy_check():
    class Answer(io.BytesIO):
        def __enter__(self):
            return self

        def __exit__(self, *a):
            return False
    calls = []

    def opener(url):
        calls.append(url)
        return Answer(json.dumps({"v": 1, "ok": False, "relays": [], "relay": "no relay configured"}).encode())
    with pytest.raises(cf.DeployError, match="not healthy after 3 tries: ok is False"):
        cf.health("https://play.example/v1/health", relays="", attempts=3, wait=0, opener=opener)
    assert len(calls) == 3


def test_secrets_come_only_from_stdin_in_make_keys_format(tmp_path):
    packed = cf.pack_secrets(secrets_dir(tmp_path))
    assert cf.read_secrets(packed) == {"TICKET_KEYS": TICKET.strip(), "SESSION_KEY": SESSION, "UPLOAD_KEY": UPLOAD, "ADMIN_KEY": ADMIN}
    # CRLF and a leading BOM (PowerShell's pipe) read the same
    assert cf.read_secrets(packed.replace("\n", "\r\n")) == cf.read_secrets(packed)
    assert cf.read_secrets("\ufeff" + packed) == cf.read_secrets(packed)
    with pytest.raises(cf.DeployError, match="missing ADMIN_KEY"):
        cf.read_secrets("".join(ln + "\n" for ln in packed.splitlines() if not ln.startswith("ADMIN_KEY")))
    with pytest.raises(cf.DeployError, match="missing UPLOAD_KEY"):
        cf.read_secrets("".join(ln + "\n" for ln in packed.splitlines() if not ln.startswith("UPLOAD_KEY")))
    with pytest.raises(cf.DeployError, match="unknown secret name"):
        cf.read_secrets(packed + "OTHER eA==\n")
    bad = packed.replace(base64.b64encode(SESSION.encode()).decode(), base64.b64encode(b"zz").decode())
    with pytest.raises(cf.DeployError, match="SESSION_KEY must be hex"):
        cf.read_secrets(bad)
    with pytest.raises(cf.DeployError, match="TICKET_KEYS"):
        cf.read_secrets(packed.replace(base64.b64encode(TICKET.encode()).decode(), base64.b64encode(b"1:00\n").decode()))


def test_metadata_refuses_a_bad_relay_and_the_r2_store():
    secrets = {"TICKET_KEYS": TICKET, "SESSION_KEY": SESSION, "UPLOAD_KEY": UPLOAD, "ADMIN_KEY": ADMIN}
    assert cf.metadata("us-east=203.0.113.10:7350", "https://x", "durable-object", secrets, None)["bindings"][1]["text"] == "us-east=203.0.113.10:7350"
    with pytest.raises(cf.DeployError, match="a.b.c.d:port"):
        cf.metadata("us-east=relay.example:7350", "https://x", "durable-object", secrets, None)
    with pytest.raises(cf.DeployError, match="R2"):
        cf.metadata("", "https://x", "r2", secrets, None)
    with pytest.raises(cf.DeployError, match="unknown log store"):
        cf.metadata("", "https://x", "kv", secrets, None)


def test_no_token_is_an_error_not_an_anonymous_call(tmp_path, capsys):
    fake = FakeCloudflare()
    code = cf.main(["check"], environ={}, transport=fake)
    assert code == 1
    assert "CLOUDFLARE_API_TOKEN" in capsys.readouterr().err
    assert fake.calls == []


def test_a_paid_plan_already_read_stops_the_deploy_whatever_a_later_page_does(tmp_path, capsys):
    # Sol r2: the paid plan on page 1, a 503 on page 2: the answer was thrown away and the deploy went on
    subs = [WORKERS_PAID] + [FREE_SITE] * 25
    fake = FakeCloudflare(subscriptions=subs, fail_page=2)
    code, printed = run_deploy(tmp_path, fake, capsys)
    assert code == 1 and "not Free" in printed
    assert fake.writes() == []
    # without a paid plan before the failure, an incomplete read says so and still never says "free"
    fake = FakeCloudflare(subscriptions=[FREE_SITE] * 25, fail_page=2)
    code, printed = run_deploy(tmp_path, fake, capsys)
    assert code == 0, printed
    assert "only 20 subscription(s) could be read (a later page failed)" in printed
    assert "no paid Workers subscription among" not in printed


@pytest.mark.parametrize("cut", [0, 1, 15, 16, 17, 31, 199, 200, 201])
def test_a_reflected_key_never_reaches_the_output_at_any_truncation_boundary(tmp_path, capsys, cut):
    # Sol r2: the message was cut to 200 characters before redacting, so a key straddling the cut leaked its first 16 hex characters: only codes print
    key = "ab12cd34ef56" * 5 + "abcd"  # 64 hex characters
    pad = ("word. " * 40)[:200 - cut]  # spaces and dots: not itself a long run the pattern redaction would catch
    message = pad + key  # the key starts `cut` characters before the old 200-character cut
    reflected = json.dumps({"success": False, "errors": [{"code": 10021, "message": message}, {"code": "9999 " + key, "message": key}]})
    code, printed = run_deploy(tmp_path, FakeCloudflare(upload_answer=(400, reflected.encode())), capsys)
    assert code == 1
    assert "10021" in printed
    assert (key[:8] not in printed) or cut < 8
    assert "word." not in printed
