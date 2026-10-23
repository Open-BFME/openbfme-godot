#!/usr/bin/env python3
"""Lane SIGNKMS-1: the Deck's check (and, in the transition phase, co-signature) of a draft release the release workflow made.

    python3 tools/release/cosign_release.py --repo OWNER/NAME --tag TAG --release-id ID --request HEX [--out DIR]
        [--key KEY] [--keys LIST] [--policy FILE] [--main REF] [--check-only]

The release workflow (.github/workflows/release.yml on main, tools/release/actions_release.py) signs with the AWS KMS key and always
leaves a DRAFT, whose body carries the line `openbfme-release-request: <32 hex>` of the request that started it. Publishing is the
caller's job (autorelease.sh / publish_release.sh after their own checks); this tool never publishes and never changes the body.

It downloads every asset of the draft into DIR (--out, created here, kept for the caller; default a new temporary folder, printed) and
checks, before signing anything:
  * the release is a draft of TAG with id ID, created by the workflow (author github-actions[bot]), the newest draft of its tag, its body
    names the request HEX, its pre-release flag is its channel's; it lists only the release's assets (release_assets.py), each one
    downloaded with its listed size and digest;
  * the KMS signatures: X.sig2 (and in phase "kms" also X.sig) is the envelope signature of the list's kms_label key of X, for
    manifest.json and manifest-macos.json (tools/release/release_keys.py, launcher/release_keys.pub, tools/release/signing_policy.json);
  * make_manifest.py --check of the folder; make_macos_manifest.py --check (the Mac sidecar; the Mac launcher's build_info.json carrying
    the list; the list as the seal key); seal_package.py check (every package's payload.json under the list);
  * the manifest names this repository, TAG as its version and its channel, and its commit is the tag's commit on GitHub and an ancestor
    of (or equal to) the public main (GitHub's compare API).
Phase "transition": then it signs manifest.json and manifest-macos.json raw with the Deck key (sign_manifest.load_secret's rules: outside
every repository, mode 0600; its public half must be the list's legacy_label key, the list's first entry), checks each signature, uploads
X.sig to exactly this release (the upload API, by id) and reads the release back (every asset with its size and SHA-256). An X.sig that
is already there and verifies under the list is kept (idempotent: never signed again); one that does NOT verify refuses the release (it
is reported, never overwritten). Phase "kms": nothing is added. Fail closed: any problem exits 1 and nothing is signed.
GH (default gh) is the gh binary.
"""
from __future__ import annotations

import argparse
import hashlib
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
import make_manifest  # noqa: E402
import release_assets  # noqa: E402
import release_keys  # noqa: E402

WORKFLOW_AUTHOR = "github-actions[bot]"
REQUEST_LINE = "openbfme-release-request: "
MANIFESTS = ("manifest.json", "manifest-macos.json")
ASSET_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,200}")
REPO_NAME = re.compile(r"[A-Za-z0-9-]{1,39}/[A-Za-z0-9._-]{1,100}")


class Refused(Exception):
    pass


def gh_bin() -> str:
    return os.environ.get("GH") or "gh"


def gh_api(*args: str) -> bytes:
    r = subprocess.run([gh_bin(), "api", *args], capture_output=True, timeout=600)
    if r.returncode:
        raise Refused(f"gh api {args[-1] if args else ''} failed: {r.stderr.decode('utf-8', 'replace').strip()[:300]}")
    return r.stdout


def gh_json(*args: str):
    try:
        return json.loads(gh_api(*args))
    except ValueError:
        raise Refused(f"gh api {args[-1]} answered no JSON")


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run_tool(*cmd) -> tuple[int, str]:
    r = subprocess.run([sys.executable, *[str(c) for c in cmd]], capture_output=True, text=True, timeout=3600)
    return r.returncode, (r.stdout + r.stderr).strip()


def created_key(rel: dict):
    return (str(rel.get("created_at") or ""), int(rel.get("id") or 0))


class Cosigner:
    def __init__(self, repo: str, keys, policy: dict, secret: bytes | None, main_ref: str, check_only: bool, say=print):
        self.repo, self.keys, self.policy, self.secret = repo, keys, policy, secret
        self.main_ref, self.check_only, self.say = main_ref, check_only, say
        self.kms_label, self.legacy_label = policy["kms_label"], policy["legacy_label"]
        try:
            release_keys.policy_key(keys, policy, "kms_label")
            legacy = release_keys.policy_key(keys, policy, "legacy_label")
        except release_keys.KeyListError as e:
            raise SystemExit(f"cosign_release: {e}")
        if secret is not None and ed25519.public_key(secret) != legacy:
            raise SystemExit(f"cosign_release: the key is not the trusted list's {self.legacy_label} key")

    def read(self, rid: int) -> dict:
        rel = gh_json(f"repos/{self.repo}/releases/{rid}")
        if not isinstance(rel, dict) or rel.get("id") != rid:
            raise Refused(f"the release {rid} reads back as another one")
        return rel

    # ---- the checks ---------------------------------------------------------------------------------------------------------------------
    def download(self, rel: dict, d: Path) -> set[str]:
        assets = {}
        for a in rel.get("assets") or []:
            n = a.get("name")
            if not isinstance(n, str) or not ASSET_NAME.fullmatch(n) or n in assets:
                raise Refused(f"an asset with an unsafe or repeated name: {n!r}")
            if a.get("state") != "uploaded" or not isinstance(a.get("size"), int) or not isinstance(a.get("id"), int):
                raise Refused(f"{n}: not an uploaded asset")
            assets[n] = a
        for n, a in assets.items():
            path = d / n
            with path.open("wb") as f:
                r = subprocess.run([gh_bin(), "api", "-H", "Accept: application/octet-stream", f"repos/{self.repo}/releases/assets/{a['id']}"],
                                   stdout=f, stderr=subprocess.PIPE, timeout=3600)
            if r.returncode:
                raise Refused(f"{n}: the download failed")
            if path.stat().st_size != a["size"]:
                raise Refused(f"{n}: {path.stat().st_size} bytes downloaded, the release lists {a['size']}")
            digest = a.get("digest") or ""
            if digest and digest != f"sha256:{sha256_file(path)}":
                raise Refused(f"{n}: the downloaded bytes are not the release's digest")
        return set(assets)

    def check_release(self, rel: dict, tag: str, request: str, d: Path, list_file: Path) -> list[str]:
        """every check before anything is signed; returns the manifests whose X.sig is still to be made (transition phase)"""
        if rel.get("tag_name") != tag:
            raise Refused(f"the release's tag is {rel.get('tag_name')!r}, not {tag}")
        if rel.get("draft") is not True:
            raise Refused("the release is not a draft (the workflow leaves a draft; publishing is the caller's)")
        try:
            channel = make_manifest.channel_of(tag)
        except SystemExit:
            raise Refused(f"{tag!r} is not a release tag")
        if (rel.get("author") or {}).get("login") != WORKFLOW_AUTHOR:
            raise Refused(f"the release was not created by the release workflow ({WORKFLOW_AUTHOR})")
        if f"{REQUEST_LINE}{request}" not in str(rel.get("body") or "").replace("\r\n", "\n").split("\n"):
            raise Refused(f"the draft's body does not carry the line '{REQUEST_LINE}{request}' (another run's draft?)")
        if rel.get("prerelease") is not (channel == "preview"):
            raise Refused(f"the release's pre-release flag {rel.get('prerelease')!r} is not its channel's ({channel})")
        releases = gh_json(f"repos/{self.repo}/releases?per_page=100")
        if not isinstance(releases, list):
            raise Refused("the release list is not a list")
        drafts = sorted((r for r in releases if r.get("tag_name") == tag and r.get("draft")), key=created_key)
        if not drafts or drafts[-1].get("id") != rel.get("id"):
            raise Refused(f"the draft {rel.get('id')} is not the newest draft of {tag}")
        if any(r.get("tag_name") == tag and not r.get("draft") for r in releases):
            raise Refused(f"{tag} has a published release already")
        names = self.download(rel, d)
        kms_phase = self.policy["phase"] == "kms"
        todo = []
        for m in MANIFESTS:
            if not (d / m).exists():
                if (d / f"{m}.sig").exists() or (d / f"{m}.sig2").exists():
                    raise Refused(f"a signature of {m} without {m}")
                if m == "manifest.json":
                    raise Refused("the release has no manifest.json")
                continue
            data = (d / m).read_bytes()
            for slot in ("sig2", "sig") if kms_phase else ("sig2",):
                if not (d / f"{m}.{slot}").exists():
                    raise Refused(f"{m} has no {m}.{slot} (the KMS signature)")
                if release_keys.verify_detail(self.keys, m, data, (d / f"{m}.{slot}").read_bytes()) != (self.kms_label, "envelope"):
                    raise Refused(f"{m}.{slot} is not the {self.kms_label} key's envelope signature of {m}")
            if not kms_phase:
                if (d / f"{m}.sig").exists():
                    if release_keys.verify(self.keys, m, data, (d / f"{m}.sig").read_bytes()) is None:
                        raise Refused(f"{m}.sig exists but does NOT verify under the trusted list: not overwritten (repair it by hand)")
                else:
                    todo.append(m)
        try:
            manifest = json.loads((d / "manifest.json").read_bytes())
        except ValueError:
            raise Refused("manifest.json is not JSON")
        if not isinstance(manifest, dict):
            raise Refused("manifest.json is not an object")
        for k, v in {"repo": self.repo, "version": tag, "channel": channel}.items():
            if manifest.get(k) != v:
                raise Refused(f"manifest.json has {k} {manifest.get(k)!r}, not {v!r}")
        commit = manifest.get("commit")
        if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise Refused("manifest.json names no 40-hex commit")
        rc, out = run_tool(HERE / "make_manifest.py", "--check", d, "--version", tag)
        if rc:
            raise Refused(f"make_manifest.py --check: {out.splitlines()[-1] if out else rc}")
        if (d / "manifest-macos.json").exists():
            rc, out = run_tool(HERE / "make_macos_manifest.py", "--check", d, "--release-key-file", list_file, "--seal-key-file", list_file)
            if rc:
                raise Refused(f"make_macos_manifest.py --check: {'; '.join(out.splitlines()[-3:])}")
        elif any(n.startswith("openbfme-") and "-macos-" in n for n in names):
            raise Refused("a Mac package without manifest-macos.json")
        import seal_package
        try:
            problems = seal_package.check(d, self.keys, self.repo)
        except SystemExit as e:
            problems = [str(e)]
        if problems:
            raise Refused(f"seal_package check: {'; '.join(problems)}")
        try:
            expected = release_assets.expected(d, tag)
        except (SystemExit, OSError, ValueError, KeyError) as e:
            raise Refused(f"the release's asset list: {e}")
        extra, missing = sorted(names - set(expected)), sorted(set(expected) - names)
        if extra or missing:
            raise Refused(f"the release's assets are not the release's: extra {extra}, missing {missing}")
        problems = release_assets.check(rel, expected, tag, True, bool(rel.get("prerelease")), self.repo)
        if problems:
            raise Refused("; ".join(problems))
        tagged = gh_api(f"repos/{self.repo}/commits/{tag}", "--jq", ".sha").decode().strip()
        if tagged != commit:
            raise Refused(f"the tag {tag} on GitHub is {tagged[:12]}, not the manifest's commit {commit[:12]}")
        cmp = gh_json(f"repos/{self.repo}/compare/{self.main_ref}...{commit}")
        if not isinstance(cmp, dict) or cmp.get("status") not in ("behind", "identical") or \
                (cmp.get("merge_base_commit") or {}).get("sha") != commit:
            raise Refused(f"the manifest's commit {commit[:12]} is not on the public {self.main_ref} "
                          f"(compare: {cmp.get('status') if isinstance(cmp, dict) else '?'})")
        return todo

    # ---- signing, upload, read-back -----------------------------------------------------------------------------------------------------
    def sign(self, d: Path, todo: list[str]) -> list[Path]:
        planned = []
        for m in todo:
            data = (d / m).read_bytes()
            sig = ed25519.sign(self.secret, data)
            if release_keys.verify_detail(self.keys, m, data, sig) != (self.legacy_label, "raw"):
                raise Refused(f"the new signature of {m} does not verify under the {self.legacy_label} key")
            planned.append((d / f"{m}.sig", sig))
        for p, sig in planned:   # written only once every signature checks
            p.write_bytes(sig)
        return [p for p, _ in planned]

    def upload(self, rid: int, path: Path) -> None:
        url = f"https://uploads.github.com/repos/{self.repo}/releases/{rid}/assets?name={path.name}"
        r = subprocess.run([gh_bin(), "api", "--method", "POST", "-H", "Content-Type: application/octet-stream", url, "--input", str(path)],
                           capture_output=True, timeout=600)
        if r.returncode:
            self.say(f"COSIGN: uploading {path.name} failed or its answer was lost: reading the release back")

    def run(self, tag: str, rid: int, request: str, d: Path) -> str:
        rel = self.read(rid)
        with tempfile.TemporaryDirectory(prefix="cosign-keys-") as t:
            list_file = Path(t) / "release_keys.pub"   # beside the downloads, never inside them (the caller reuses that folder)
            list_file.write_text("".join(f"{k.hex()}  {lb}\n" for lb, k in self.keys), encoding="ascii")
            try:
                todo = self.check_release(rel, tag, request, d, list_file)
            except Refused as e:
                raise Refused(f"{e} (nothing signed)")
        if self.check_only or self.policy["phase"] == "kms":
            return "checks passed" + (f" ({', '.join(todo)} to co-sign)" if todo and self.check_only else "") + \
                (" (phase kms: nothing added)" if self.policy["phase"] == "kms" else "")
        signed = self.sign(d, todo)
        for p in signed:
            self.upload(rid, p)
        try:
            back = self.read(rid)
            problems = release_assets.check(back, release_assets.expected(d, tag), tag, True, bool(rel.get("prerelease")), self.repo)
            if (back.get("body") or "") != (rel.get("body") or ""):
                problems.append("the release's body changed meanwhile")
        except Refused as e:
            problems = [str(e)]
        if problems:
            raise Refused(f"after uploading {', '.join(p.name for p in signed) or 'nothing'}: the release read back: {'; '.join(problems)}; "
                          "it is left as it is")
        return f"co-signed {', '.join(p.name for p in signed)}" if signed else "already co-signed (nothing signed)"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--release-id", type=int, required=True)
    ap.add_argument("--request", required=True, help="the 32-hex request id the draft's body names")
    ap.add_argument("--out", type=Path, help="the folder the release is downloaded into (created here, kept; default a new temporary one)")
    ap.add_argument("--key", type=Path, default=os.environ.get("OPENBFME_RELEASE_KEY") or None,
                    help="the Deck release key (default OPENBFME_RELEASE_KEY)")
    ap.add_argument("--keys", type=Path, default=release_keys.DEFAULT_LIST, help="the trusted list (default launcher/release_keys.pub)")
    ap.add_argument("--policy", type=Path, default=release_keys.POLICY_FILE)
    ap.add_argument("--main", default="main", help="the public main branch the commit must be on")
    ap.add_argument("--check-only", action="store_true", help="every check, nothing signed or uploaded")
    a = ap.parse_args(argv)
    if not REPO_NAME.fullmatch(a.repo):
        raise SystemExit(f"cosign_release: '{a.repo}' is not <owner>/<name>")
    if not re.fullmatch(r"[0-9a-f]{32}", a.request):
        raise SystemExit("cosign_release: --request is not 32 lower-case hex digits")
    if not re.fullmatch(r"[A-Za-z0-9._/-]{1,100}", a.main) or ".." in a.main:
        raise SystemExit("cosign_release: --main is not a branch name")
    if a.release_id <= 0:
        raise SystemExit("cosign_release: --release-id is not a release id")
    try:
        keys = release_keys.load(a.keys)
        policy = release_keys.load_policy(a.policy)
    except release_keys.KeyListError as e:
        raise SystemExit(f"cosign_release: {e}")
    secret = None
    if not a.check_only and policy["phase"] == "transition":
        if a.key is None:
            raise SystemExit("cosign_release: no key (--key or OPENBFME_RELEASE_KEY)")
        from sign_manifest import load_secret   # outside every repository, mode 0600
        secret = load_secret(a.key)
    c = Cosigner(a.repo, keys, policy, secret, a.main, a.check_only)
    if a.out:
        if a.out.exists():
            raise SystemExit(f"cosign_release: {a.out} exists (the download folder is made here, never reused)")
        a.out.mkdir(parents=True)
        d = a.out
    else:
        d = Path(tempfile.mkdtemp(prefix="cosign-"))
    print(f"COSIGN {a.tag}: downloading release {a.release_id} into {d}")
    try:
        print(f"COSIGN {a.tag}: {c.run(a.tag, a.release_id, a.request, d)}")
        return 0
    except Refused as e:
        print(f"COSIGN FAIL {a.tag}: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
