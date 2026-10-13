#!/usr/bin/env python3
# Deploys the OpenBFME lobby (services/online/lobby) to Cloudflare Workers through the REST API, on the Workers FREE plan (lane CFDEPLOY-1).
# Python 3 standard library only: no wrangler, no npm. Nothing secret is in this file or on its command line.
#
#   check     looks for a paid Workers plan (refuses if it finds one) and prints the Worker, its custom domains and the health check
#   deploy    --src DIR --name openbfme-lobby --hostname play.openbfme.com --public-url https://play.openbfme.com --relays "" [--log-store durable-object]
#             looks for a paid plan (refuses on one), reads the four Worker secrets from STDIN (pack-secrets' format), uploads worker.mjs + lobby.mjs as an ES module
#             Worker with the LOBBY Durable Object binding (a SQLite class: new_sqlite_classes migration "v1"), the vars and the secrets, attaches the
#             custom domain (Cloudflare makes its proxied DNS record) and polls https://<hostname>/v1/health
#   pack-secrets DIR
#             (on the machine that holds make_keys.sh's files) writes ticket.keys, session.key, upload.key and admin.key to stdout in the format deploy reads:
#             pipe it straight into the deploy command (ssh stdin), never into a file
#
# The API token, account id and zone id come from the environment (CLOUDFLARE_API_TOKEN, CLOUDFLARE_ACCOUNT_ID, CLOUDFLARE_ZONE_ID): the owner's
# launcher (infra-run.py cloudflare-workers -- ...) injects them into this process only. They are never printed. services/online/DEPLOY.md has the steps.
#
# The Free plan (CFDEPLOY-1 r2, Sol r1): the account's plan is the OWNER'S VERIFIED FACT (their infrastructure handoff, 2026-10-12: "All domain plans
# and Workers are Free; no paid product subscriptions were listed"). The tokens deliberately lack Billing Read, so the script cannot prove "free".
# What it does instead:
#  - it uploads only Free-plan bindings: the Durable Object namespace (a SQLite class: new_sqlite_classes, the only kind the Free plan has), plain
#    text vars and secrets. Any other binding (R2, Queues, KV, D1, AI, browser, ...), a non-SQLite Durable Object migration, keep_bindings or a usage
#    model is refused before anything is sent;
#  - it tries to read the account's subscriptions, every page. If the read works and shows a Workers subscription that is not Free, it stops before
#    any change. If the read fails (the expected case: no Billing Read), it says so and relies on the owner's fact: an unreadable answer is never
#    reported as "free".
# Output never carries a secret: Cloudflare's error messages are printed with the token, the three secrets and any long hex / base64 run redacted, and
# a response body is never printed raw.
import argparse
import base64
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import uuid
from pathlib import Path

API = "https://api.cloudflare.com/client/v4"
MODULES = ("worker.mjs", "lobby.mjs")  # worker.mjs imports './lobby.mjs': the part names are the module names
MAIN_MODULE = "worker.mjs"
DO_BINDING = "LOBBY"
DO_CLASS = "LobbyObject"
MIGRATION_TAG = "v1"
COMPATIBILITY_DATE = "2026-10-01"  # the same date as lobby/wrangler.toml.example
SECRET_NAMES = ("TICKET_KEYS", "SESSION_KEY", "UPLOAD_KEY", "ADMIN_KEY")  # ADMIN_KEY: INFRA-2, the backup export (lobby.mjs "the admin backup")
SECRET_FILES = {"TICKET_KEYS": "ticket.keys", "SESSION_KEY": "session.key", "UPLOAD_KEY": "upload.key", "ADMIN_KEY": "admin.key"}
LOG_STORES = ("durable-object", "r2", "")
USER_AGENT = "openbfme-deploy/1"


class DeployError(Exception):
    pass


# ---- the REST API ------------------------------------------------------------------------------------------------------------------------------------
class Api:
    """Calls api.cloudflare.com with the token from the environment. `transport(method, url, headers, body) -> (status, bytes)` is replaceable (tests)."""

    def __init__(self, token, account, zone, transport=None):
        if not token or not account:
            raise DeployError("CLOUDFLARE_API_TOKEN and CLOUDFLARE_ACCOUNT_ID must be set (run under the owner's launcher: infra-run.py cloudflare-workers --)")
        self.token = token
        self.account = account
        self.zone = zone
        self.transport = transport or http_transport

    def call(self, method, path, body=None, content_type="application/json", ok_missing=False, want_info=False):
        headers = {"authorization": f"Bearer {self.token}", "user-agent": USER_AGENT}
        data = None
        if body is not None:
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            headers["content-type"] = content_type
        status, raw = self.transport(method, API + path, headers, data)
        try:
            doc = json.loads(raw) if raw else {}
        except ValueError:
            doc = {}
        if ok_missing and status == 404:
            return None
        if status >= 300 or not isinstance(doc, dict) or not doc.get("success", False):
            raise DeployError(f"{method} {redact_path(path)}: HTTP {status}: {api_errors(doc)}")
        return doc.get("result") if not want_info else (doc.get("result"), doc.get("result_info") or {})


def http_transport(method, url, headers, data):
    req = urllib.request.Request(url, data=data, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def api_errors(doc):
    """Cloudflare's numeric error codes only (CFDEPLOY-1 r3, Sol r2: a message can reflect what was sent, and cutting it before redacting leaked the
    first 16 hex characters of a key). No message, no raw body."""
    errors = doc.get("errors") if isinstance(doc, dict) else None
    codes = []
    if isinstance(errors, list):
        for e in errors[:5]:
            code = e.get("code") if isinstance(e, dict) else None
            if isinstance(code, int) and not isinstance(code, bool):
                codes.append(str(code))
    if not codes:
        return "no error code (the response body is not shown)"
    return "Cloudflare error code " + ", ".join(codes) + " (messages are not shown)"


class Redactor:
    """Removes known secrets (the API token, the Worker secrets, each key of the ring) and any long hex or base64 run from text that is printed."""

    LONG_RUN = re.compile(r"[0-9a-fA-F]{32,}|[A-Za-z0-9+/_-]{40,}={0,2}")

    def __init__(self):
        self.secrets = set()

    def add(self, *values):
        for v in values:
            lines = [ln.strip() for ln in str(v).splitlines() if ln.strip() and not ln.strip().startswith("#")]  # a key ring's comment is no secret
            for piece in [str(v).strip(), *lines, *(ln.split(":", 1)[-1] for ln in lines)]:
                if len(piece) >= 8:
                    self.secrets.add(piece)

    def __call__(self, text):
        text = str(text)
        for secret in sorted(self.secrets, key=len, reverse=True):
            text = text.replace(secret, "<redacted>")
        return self.LONG_RUN.sub("<redacted>", text)


REDACT = Redactor()


def redact_path(path):
    # ids are not secret, but keep error lines short
    return re.sub(r"[0-9a-f]{32}", "<id>", path)


# ---- the Free plan ------------------------------------------------------------------------------------------------------------------------------------
SUBSCRIPTION_PAGE = 50
SUBSCRIPTION_MAX_PAGES = 40


def read_subscriptions(api):
    """(subscriptions read so far, complete). Every page of GET /accounts/<id>/subscriptions; a page that fails ends the read with what was already
    read and complete False (CFDEPLOY-1 r3, Sol r2: a paid plan on page 1 and a 503 on page 2 must not discard page 1)."""
    subs = []
    for page in range(1, SUBSCRIPTION_MAX_PAGES + 1):
        try:
            result, info = api.call("GET", f"/accounts/{api.account}/subscriptions?page={page}&per_page={SUBSCRIPTION_PAGE}", want_info=True)
        except DeployError:
            return subs, False
        if not isinstance(result, list):
            return subs, False
        subs += result
        total_pages = info.get("total_pages")
        if isinstance(total_pages, int) and total_pages >= 1:
            if page >= total_pages:
                return subs, True
        elif len(result) < SUBSCRIPTION_PAGE:
            return subs, True
    return subs, False  # more pages than any account of this owner has: not a complete answer


def is_free_workers_plan(sub):
    plan = sub.get("rate_plan") or {}
    return str(plan.get("id", "")).lower() in ("workers_free", "free") and not (sub.get("price") or 0)


def check_plan(api):
    """Refuses (DeployError) when ANY page read shows a Workers subscription that is not Free, whatever happens to the other pages. Returns the line to
    print otherwise: an incomplete or unreadable list is reported as such, never as free."""
    subs, complete = read_subscriptions(api)
    workers = [s for s in subs if isinstance(s, dict) and "worker" in f"{(s.get('rate_plan') or {}).get('id', '')} {(s.get('rate_plan') or {}).get('public_name', '')}".lower()]
    paid = [s for s in workers if not is_free_workers_plan(s)]
    if paid:
        names = ", ".join(f"{(s.get('rate_plan') or {}).get('id')} ({s.get('state')}, {s.get('price')} {s.get('currency', '')})" for s in paid)
        raise DeployError(f"the account has a Workers subscription that is not Free: {names}; this deployment runs on the Free plan only")
    if not complete:
        what = f"only {len(subs)} subscription(s) could be read (a later page failed), none a paid Workers plan" if subs else "the subscriptions are not readable with this token"
        return (f"Workers plan: {what} (no Billing Read, by the owner's design); relying on the owner's verified fact (handoff 2026-10-12: Workers "
                "Free) and on uploading Free-plan bindings only")
    return f"Workers plan: no paid Workers subscription among the account's {len(subs)} subscriptions (every page read)"


# ---- the secrets (stdin) --------------------------------------------------------------------------------------------------------------------------------
def pack_secrets(directory):
    """The stdin format deploy reads: one line per secret, "NAME <base64 of the file>"."""
    out = []
    for name in SECRET_NAMES:
        p = Path(directory) / SECRET_FILES[name]
        if not p.is_file():
            raise DeployError(f"{p} is missing (make_keys.sh writes it)")
        out.append(f"{name} {base64.b64encode(p.read_bytes()).decode()}\n")
    return "".join(out)


def read_secrets(text):
    secrets = {}
    for line in text.lstrip("\ufeff").splitlines():  # a PowerShell pipe starts stdin with a BOM (CFDEPLOY-1's live deploy, 2026-10-12)
        line = line.strip()
        if not line:
            continue
        name, _, b64 = line.partition(" ")
        if name not in SECRET_NAMES:
            raise DeployError(f"stdin: unknown secret name {name[:20]!r} (expected {', '.join(SECRET_NAMES)})")
        if name in secrets:
            raise DeployError(f"stdin: {name} given twice")
        try:
            secrets[name] = base64.b64decode(b64, validate=True).decode("ascii").strip()
        except ValueError:
            raise DeployError(f"stdin: {name} is not base64 of ASCII text") from None
    missing = [n for n in SECRET_NAMES if n not in secrets]
    if missing:
        raise DeployError(f"stdin: missing {', '.join(missing)} (pipe pack-secrets' output in)")
    # the formats lobby.mjs's configFromEnv reads (parseKeyRing, hexToBytes); a bad key fails here, not in the Worker
    rings = [ln for ln in secrets["TICKET_KEYS"].splitlines() if ln.strip() and not ln.startswith("#")]
    if not rings or not all(re.fullmatch(r"\d{1,3}:[0-9a-fA-F]{64}", ln.strip()) for ln in rings):
        raise DeployError("TICKET_KEYS must hold keyId:<64 hex> lines (make_keys.sh's ticket.keys)")
    for n in ("SESSION_KEY", "UPLOAD_KEY", "ADMIN_KEY"):
        if not re.fullmatch(r"[0-9a-fA-F]{32,}", secrets[n]) or len(secrets[n]) % 2:
            raise DeployError(f"{n} must be hex of 16+ bytes (make_keys.sh's {SECRET_FILES[n]})")
    return secrets


# ---- the upload ------------------------------------------------------------------------------------------------------------------------------------------
def check_relays(relays):
    for r in filter(None, (x.strip() for x in relays.split(","))):
        address = r.split("=", 1)[1] if "=" in r else r
        if not re.fullmatch(r"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}:\d{1,5}", address):
            raise DeployError(f"relay address {address!r} must be a.b.c.d:port (lobby.mjs refuses anything else)")


def metadata(relays, public_url, log_store, secrets, migration_tag):
    """The Worker's metadata part. `migration_tag` is the tag the deployed script has now (None: a new script, or one without migrations)."""
    if log_store not in LOG_STORES:
        raise DeployError(f"unknown log store {log_store!r} (durable-object, r2, or empty)")
    if log_store == "r2":
        raise DeployError("the R2 log store needs an R2 bucket binding, which this script does not create (R2 is not on the Free plan's token)")
    check_relays(relays)
    bindings = [
        {"type": "durable_object_namespace", "name": DO_BINDING, "class_name": DO_CLASS},
        {"type": "plain_text", "name": "RELAYS", "text": relays},
        {"type": "plain_text", "name": "PUBLIC_URL", "text": public_url},
        {"type": "plain_text", "name": "LOG_STORE", "text": log_store},
    ]
    bindings += [{"type": "secret_text", "name": n, "text": secrets[n]} for n in SECRET_NAMES]
    meta = {"main_module": MAIN_MODULE, "compatibility_date": COMPATIBILITY_DATE, "bindings": bindings}
    if migration_tag is None:
        # the Free plan has SQLite-backed Durable Objects only: the class is created with new_sqlite_classes
        meta["migrations"] = {"new_tag": MIGRATION_TAG, "new_sqlite_classes": [DO_CLASS]}
    elif migration_tag != MIGRATION_TAG:
        raise DeployError(f"the deployed Worker's migration tag is {migration_tag!r}, this script knows only {MIGRATION_TAG!r}: migrate it by hand")
    check_free_metadata(meta)
    return meta


FREE_BINDING_TYPES = ("durable_object_namespace", "plain_text", "secret_text")
FREE_METADATA_KEYS = ("main_module", "compatibility_date", "bindings", "migrations")


def check_free_metadata(meta):
    """Only what the Workers Free plan has: refuses any other binding type, key or migration before the upload (CFDEPLOY-1 r2, Sol r1)."""
    extra = [k for k in meta if k not in FREE_METADATA_KEYS]
    if extra:
        raise DeployError(f"metadata {', '.join(sorted(extra))} is not part of the Free-plan deployment")
    for b in meta["bindings"]:
        if b.get("type") not in FREE_BINDING_TYPES:
            raise DeployError(f"binding {b.get('name')} of type {b.get('type')} is not allowed: only {', '.join(FREE_BINDING_TYPES)} (no R2, Queues or paid add-ons)")
        if b.get("type") == "durable_object_namespace" and (b.get("script_name") or b.get("class_name") != DO_CLASS):
            raise DeployError(f"Durable Object binding {b.get('name')} must be this Worker's {DO_CLASS}")
    m = meta.get("migrations")
    if m is not None and (set(m) - {"new_tag", "old_tag", "new_sqlite_classes"} or m.get("new_sqlite_classes") != [DO_CLASS]):
        raise DeployError(f"migration {json.dumps(m, sort_keys=True)} is not the Free plan's SQLite class migration")


def multipart(meta, modules, boundary=None):
    """multipart/form-data: the metadata part, then one ES module part per file. Returns (content type, body)."""
    boundary = boundary or f"openbfme-{uuid.uuid4().hex}"
    out = bytearray()

    def part(name, filename, content_type, data):
        out.extend(f"--{boundary}\r\n".encode())
        disposition = f'form-data; name="{name}"' + (f'; filename="{filename}"' if filename else "")
        out.extend(f"Content-Disposition: {disposition}\r\nContent-Type: {content_type}\r\n\r\n".encode())
        out.extend(data)
        out.extend(b"\r\n")

    part("metadata", None, "application/json", json.dumps(meta).encode())
    for name, data in modules:
        if f"--{boundary}".encode() in data:
            raise DeployError(f"{name} contains the multipart boundary")
        part(name, name, "application/javascript+module", data)
    out.extend(f"--{boundary}--\r\n".encode())
    return f"multipart/form-data; boundary={boundary}", bytes(out)


def read_modules(src):
    modules = []
    for name in MODULES:
        p = Path(src) / name
        if not p.is_file():
            raise DeployError(f"{p} is missing (--src is services/online/lobby/src or a copy of it)")
        modules.append((name, p.read_bytes()))
    return modules


def deployed_script(api, name):
    scripts = api.call("GET", f"/accounts/{api.account}/workers/scripts") or []
    return next((s for s in scripts if s.get("id") == name), None)


def health_problem(h, relays=None, log_store=None):
    """None when /v1/health is healthy, else why not. The one degraded state allowed for now: no relay configured (RELAYS empty).
    relays: the configured RELAYS text (None: any relay list); log_store: the configured LOG_STORE (None: any)."""
    if not isinstance(h, dict):
        return "the answer is not a JSON object"
    if h.get("v") != 1:
        return f"version {h.get('v')!r}, expected 1"
    if h.get("ok") is not True:
        return f"ok is {h.get('ok')!r}"
    got = h.get("relays")
    if not isinstance(got, list):
        return "no relay list"
    if not got:
        if h.get("relay") != "no relay configured":
            return "an empty relay list without the \"no relay configured\" state"
        if relays:
            return f"no relay, but RELAYS is {relays!r}"
    else:
        if "relay" in h:
            return f"relays listed and a relay state {h.get('relay')!r}"
        if relays is not None:
            want = [r.split("=", 1)[1] if "=" in r else r for r in filter(None, (x.strip() for x in relays.split(",")))]
            if [r.get("address") for r in got if isinstance(r, dict)] != want:
                return f"relays {got!r}, expected {want!r}"
    if log_store is not None and h.get("logStore") != (log_store or "none"):
        return f"log store {h.get('logStore')!r}, expected {log_store or 'none'!r}"
    return None


def health(url, relays=None, log_store=None, attempts=30, wait=10, opener=None):
    """Polls url until it answers healthy (health_problem); a network error or an unhealthy answer is retried, then reported."""
    opener = opener or (lambda u: urllib.request.urlopen(urllib.request.Request(u, headers={"user-agent": USER_AGENT}), timeout=30))
    last = None
    for i in range(attempts):
        try:
            with opener(url) as r:
                h = json.loads(r.read())
            last = health_problem(h, relays, log_store)
            if last is None:
                return h
        except (urllib.error.URLError, OSError, ValueError) as e:
            last = f"{type(e).__name__}: {e}"
        if i + 1 < attempts:
            time.sleep(wait)
    raise DeployError(f"{url}: not healthy after {attempts} tries: {last}")


def deploy(api, args, stdin_text, log=print):
    log(check_plan(api))
    secrets = read_secrets(stdin_text)
    REDACT.add(*secrets.values())
    modules = read_modules(args.src)
    current = deployed_script(api, args.name)
    tag = (current or {}).get("migration_tag") or None
    meta = metadata(args.relays, args.public_url, args.log_store, secrets, tag)
    content_type, body = multipart(meta, modules)
    result = api.call("PUT", f"/accounts/{api.account}/workers/scripts/{args.name}", body, content_type=content_type)
    log(f"worker {args.name}: uploaded ({'new' if current is None else 'updated'}; migration tag {result.get('migration_tag') if result else None}; "
        f"{len(body)} bytes; bindings {', '.join(b['name'] for b in meta['bindings'])})")
    if args.hostname:
        if not api.zone:
            raise DeployError("CLOUDFLARE_ZONE_ID must be set for a custom domain")
        d = api.call("PUT", f"/accounts/{api.account}/workers/domains",
                     {"hostname": args.hostname, "service": args.name, "zone_id": api.zone, "environment": "production"})
        log(f"custom domain {args.hostname} -> {d.get('service') if d else args.name} (zone {d.get('zone_name') if d else '?'})")
    if args.public_url and not args.no_health:
        h = health(args.public_url.rstrip("/") + "/v1/health", relays=args.relays, log_store=args.log_store)
        log("health: " + json.dumps(h, sort_keys=True))
    return 0


def check(api, args, log=print):
    log(check_plan(api))
    current = deployed_script(api, args.name)
    log(f"worker {args.name}: " + ("not deployed" if current is None else f"deployed, modified {current.get('modified_on')}, migration tag {current.get('migration_tag')}"))
    domains = api.call("GET", f"/accounts/{api.account}/workers/domains") or []
    for d in domains:
        if d.get("service") == args.name:
            log(f"custom domain {d.get('hostname')} -> {d.get('service')}")
    if args.public_url and current is not None:
        log("health: " + json.dumps(health(args.public_url.rstrip("/") + "/v1/health", attempts=1), sort_keys=True))
    return 0


def main(argv=None, stdin=None, environ=None, transport=None):
    environ = os.environ if environ is None else environ
    REDACT.secrets.clear()
    REDACT.add(environ.get("CLOUDFLARE_API_TOKEN", ""))
    ap = argparse.ArgumentParser(description="Deploys the OpenBFME lobby to Cloudflare Workers (Free plan) through the REST API")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("check", "deploy"):
        p = sub.add_parser(name)
        p.add_argument("--name", default="openbfme-lobby")
        p.add_argument("--public-url", default="")
        if name == "deploy":
            p.add_argument("--src", required=True)
            p.add_argument("--hostname", default="")
            p.add_argument("--relays", required=True, help='"id=a.b.c.d:port,..." or "" (no relay yet: START is refused)')
            p.add_argument("--log-store", default="durable-object")
            p.add_argument("--no-health", action="store_true")
    p = sub.add_parser("pack-secrets")
    p.add_argument("dir")
    args = ap.parse_args(argv)
    try:
        if args.cmd == "pack-secrets":
            sys.stdout.write(pack_secrets(args.dir))
            return 0
        api = Api(environ.get("CLOUDFLARE_API_TOKEN", ""), environ.get("CLOUDFLARE_ACCOUNT_ID", ""), environ.get("CLOUDFLARE_ZONE_ID", ""), transport)
        log = lambda line: print(REDACT(line), flush=True)  # noqa: E731
        if args.cmd == "check":
            return check(api, args, log=log)
        return deploy(api, args, (stdin or sys.stdin).read(), log=log)
    except DeployError as e:
        print(f"cloudflare_api: {REDACT(e)}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
