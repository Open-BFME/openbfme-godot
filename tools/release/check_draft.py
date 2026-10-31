#!/usr/bin/env python3
"""Lane RELGH-2 (was SIGNKMS-1's cosign_release.py), RELGH-3: the Deck's check of a draft release the release workflow signed (a nightly, or
a stable release after the owner's approval).

    python3 tools/release/check_draft.py --repo OWNER/NAME --tag TAG --release-id ID --request HEX [--out DIR]
        [--keys LIST] [--nightly-keys NLIST] [--main REF]

The release workflow (.github/workflows/release.yml on main, tools/release/actions_release.py) builds a NIGHTLY, seals and signs it with
the nightly key of its protected environment `nightly-signing` (RELGH-3: or a STABLE release, with the stable Actions key of the
environment `stable-signing`, tools/release/stable_environment.json) and always leaves a DRAFT, whose body carries the line
`openbfme-release-request: <32 hex>` of the request that started it. Publishing is the caller's job (autorelease.sh / publish_release.sh
after their own checks); this tool never publishes, never signs and never changes the release (the Deck adds no signature: owner,
2026-10-24, the Deck stops being the nightly signer).

It downloads every asset of the draft into DIR (--out, created here, kept for the caller; default a new temporary folder, printed) and
checks:
  * the release is a draft of TAG (a nightly tag or a stable v<x.y.z>) with id ID, created by the workflow (author github-actions[bot]),
    the newest draft of its tag, its body names the request HEX, it is a pre-release exactly when it is a nightly; it lists only the
    release's assets (release_assets.py), each one downloaded with its listed size and digest;
  * the signatures: X.sig is a raw signature of X under a key of NLIST (launcher/nightly_keys.pub), valid by the nightly scope rule
    (release_keys.verify_scoped: X is a nightly's), for manifest.json and manifest-macos.json; RELGH-3, a stable TAG: under the key of
    LIST labelled by stable_environment.json's key_label (the stable Actions key; never LIST's first entry, the Deck key, which never
    signs on GitHub); there is no X.sig2;
  * make_manifest.py --check of the folder; make_macos_manifest.py --check (the Mac sidecar; the Mac launcher's build_info.json carrying
    both lists); seal_package.py check (every package's payload.json under the lists, scoped);
  * the manifest names this repository, TAG as its version and the preview channel, and its commit is the tag's commit on GitHub and an
    ancestor of (or equal to) the public main (GitHub's compare API).
Fail closed: any problem exits 1. GH (default gh) is the gh binary.
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


class DraftChecker:
    def __init__(self, repo: str, keys, nightly, main_ref: str, say=print, stable_label: str | None = None):
        self.repo, self.keys, self.nightly, self.main_ref, self.say = repo, keys, nightly, main_ref, say
        self.stable_label = stable_label   # RELGH-3: the label of the stable Actions key (stable_environment.json key_label)

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

    def check_release(self, rel: dict, tag: str, request: str, d: Path, list_file: Path, nightly_file: Path) -> None:
        """every check of the draft"""
        if rel.get("tag_name") != tag:
            raise Refused(f"the release's tag is {rel.get('tag_name')!r}, not {tag}")
        if rel.get("draft") is not True:
            raise Refused("the release is not a draft (the workflow leaves a draft; publishing is the caller's)")
        try:
            channel = make_manifest.channel_of(tag)
        except SystemExit:
            raise Refused(f"{tag!r} is not a release tag")
        nightly = bool(release_keys.NIGHTLY_VERSION.fullmatch(tag))
        if not nightly and not re.fullmatch(r"v(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})\.(0|[1-9][0-9]{0,8})", tag):
            raise Refused(f"{tag} is neither a nightly nor a stable release: nothing else is signed on GitHub Actions")
        if nightly and not self.nightly:
            raise Refused("launcher/nightly_keys.pub lists no key: no draft can be a signed nightly")
        if not nightly and (self.stable_label is None or self.stable_label not in {lb for lb, _ in self.keys[1:]}):
            raise Refused(f"the stable Actions key {self.stable_label!r} is not a key of launcher/release_keys.pub after its first entry")
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
        signers = {lb for lb, _ in self.nightly} if nightly else {self.stable_label}
        who = "a nightly key's signature of a nightly's" if nightly else f"the stable Actions key {self.stable_label}'s signature of"
        for m in MANIFESTS:
            if (d / f"{m}.sig2").exists():
                raise Refused(f"{m}.sig2 exists: the workflow signs {m}.sig only")
            if not (d / m).exists():
                if (d / f"{m}.sig").exists():
                    raise Refused(f"a signature of {m} without {m}")
                if m == "manifest.json":
                    raise Refused("the release has no manifest.json")
                continue
            if not (d / f"{m}.sig").exists():
                raise Refused(f"{m} has no {m}.sig")
            got = release_keys.verify_scoped(self.keys, self.nightly, m, (d / m).read_bytes(), (d / f"{m}.sig").read_bytes())
            if got is None or got[0] not in signers or got[1] != "raw":
                raise Refused(f"{m}.sig is not {who} {m}")
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
            rc, out = run_tool(HERE / "make_macos_manifest.py", "--check", d, "--release-key-file", list_file, "--seal-key-file", list_file,
                               "--nightly-key-file", nightly_file)
            if rc:
                raise Refused(f"make_macos_manifest.py --check: {'; '.join(out.splitlines()[-3:])}")
        elif any(n.startswith("openbfme-") and "-macos-" in n for n in names):
            raise Refused("a Mac package without manifest-macos.json")
        import seal_package
        try:
            problems = seal_package.check(d, self.keys, self.repo, self.nightly)
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

    def run(self, tag: str, rid: int, request: str, d: Path) -> str:
        rel = self.read(rid)
        with tempfile.TemporaryDirectory(prefix="draft-keys-") as t:
            # the lists beside the downloads, never inside them (the caller publishes that folder)
            list_file, nightly_file = Path(t) / "release_keys.pub", Path(t) / "nightly_keys.pub"
            list_file.write_text("".join(f"{k.hex()}  {lb}\n" for lb, k in self.keys), encoding="ascii")
            nightly_file.write_text("".join(f"{k.hex()}  {lb}\n" for lb, k in self.nightly), encoding="ascii")
            self.check_release(rel, tag, request, d, list_file, nightly_file)
        if release_keys.NIGHTLY_VERSION.fullmatch(tag):
            return "checks passed (signed by the nightly key; nothing added)"
        return f"checks passed (signed by the stable Actions key {self.stable_label}; nothing added)"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter, allow_abbrev=False)
    ap.add_argument("--repo", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--release-id", type=int, required=True)
    ap.add_argument("--request", required=True, help="the 32-hex request id the draft's body names")
    ap.add_argument("--out", type=Path, help="the folder the release is downloaded into (created here, kept; default a new temporary one)")
    ap.add_argument("--keys", type=Path, default=release_keys.DEFAULT_LIST, help="the trusted list (default launcher/release_keys.pub)")
    ap.add_argument("--nightly-keys", type=Path, default=release_keys.NIGHTLY_LIST,
                    help="the nightly-only list (default launcher/nightly_keys.pub)")
    ap.add_argument("--main", default="main", help="the public main branch the commit must be on")
    ap.add_argument("--environment-config", type=Path, default=HERE / "stable_environment.json",
                    help="RELGH-3: the stable environment's configuration (its key_label names the stable Actions key)")
    a = ap.parse_args(argv)
    if not REPO_NAME.fullmatch(a.repo):
        raise SystemExit(f"check_draft: '{a.repo}' is not <owner>/<name>")
    if not re.fullmatch(r"[0-9a-f]{32}", a.request):
        raise SystemExit("check_draft: --request is not 32 lower-case hex digits")
    if not re.fullmatch(r"[A-Za-z0-9._/-]{1,100}", a.main) or ".." in a.main:
        raise SystemExit("check_draft: --main is not a branch name")
    if a.release_id <= 0:
        raise SystemExit("check_draft: --release-id is not a release id")
    try:
        keys = release_keys.load(a.keys)
        nightly = release_keys.load_nightly(a.nightly_keys, a.keys)
    except release_keys.KeyListError as e:
        raise SystemExit(f"check_draft: {e}")
    import stable_environment
    try:
        label = stable_environment.load_config(path=a.environment_config)["key_label"]
    except stable_environment.Fail as e:
        raise SystemExit(f"check_draft: {e}")
    c = DraftChecker(a.repo, keys, nightly, a.main, stable_label=label)
    if a.out:
        if a.out.exists():
            raise SystemExit(f"check_draft: {a.out} exists (the download folder is made here, never reused)")
        a.out.mkdir(parents=True)
        d = a.out
    else:
        d = Path(tempfile.mkdtemp(prefix="draft-"))
    print(f"DRAFT CHECK {a.tag}: downloading release {a.release_id} into {d}")
    try:
        print(f"DRAFT CHECK {a.tag}: {c.run(a.tag, a.release_id, a.request, d)}")
        return 0
    except Refused as e:
        print(f"DRAFT CHECK FAIL {a.tag}: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
