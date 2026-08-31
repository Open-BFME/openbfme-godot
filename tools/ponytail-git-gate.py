"""Fail-closed Ponytail review gate for OpenBFME Git hooks.

The private hooks installed by Install-PonytailHooks.ps1 call this file with
the repository-pinned Python.  Review inputs and receipts remain in the
per-worktree Git directory, never in tracked files.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from typing import Any, Sequence


MANIFEST_NAME = "openbfme-ponytail.json"
PASS_LINE = "Lean already. Ship."
ZERO_RE = re.compile(r"^0+$")
APPROVED_PONYTAIL = {
    "ponytailOrigin": "https://github.com/DietrichGebert/ponytail",
    "ponytailCommit": "2ed6c52c9d7e5e56942508591085fd45dea277d3",
    "ponytailVersion": "4.9.0",
    "ponytailSkillSha256": "6c8b7e5c897a406b66da6aabda7fa6509e8d1d447e73e602265d7986497445d1",
    "ponytailCommandSha256": "f858d6d19ce21768b2076f098c4002901c5d797e79488c19f43876f301dec7af",
}


class GateError(RuntimeError):
    pass


def _run(
    argv: Sequence[str], *, cwd: Path, input_bytes: bytes | None = None,
    timeout: int | None = 120, check: bool = True,
    clear_env: Sequence[str] = (),
) -> subprocess.CompletedProcess[bytes]:
    env = os.environ.copy()
    for name in clear_env:
        env.pop(name, None)
    env.update({"GIT_PAGER": "cat", "PAGER": "cat", "NO_COLOR": "1"})
    try:
        result = subprocess.run(
            list(argv), cwd=cwd, env=env, input=input_bytes,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise GateError(f"command failed to run: {argv[0]}: {exc}") from exc
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).decode("utf-8", "replace").strip()
        raise GateError(f"command failed ({result.returncode}): {' '.join(argv)}: {detail}")
    return result


def _git(root: Path, *args: str, input_bytes: bytes | None = None) -> bytes:
    return _run(
        ["git", "-c", "color.ui=false", "-c", "core.pager=cat", *args],
        cwd=root, input_bytes=input_bytes,
    ).stdout


def _foreign_git(plugin: Path, root: Path, *args: str) -> bytes:
    local_names = _text(_git(root, "rev-parse", "--local-env-vars")).splitlines()
    return _run(
        ["git", "-C", str(plugin), *args], cwd=root, clear_env=local_names,
    ).stdout


def _sha(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _file_sha(path: Path) -> str:
    return _sha(path.read_bytes())


def _text(payload: bytes) -> str:
    return payload.decode("utf-8", "strict").strip()


def _repo() -> tuple[Path, Path, Path]:
    root = Path(_text(_git(Path.cwd(), "rev-parse", "--show-toplevel"))).resolve()
    common = Path(_text(_git(root, "rev-parse", "--path-format=absolute", "--git-common-dir"))).resolve()
    git_dir = Path(_text(_git(root, "rev-parse", "--path-format=absolute", "--git-dir"))).resolve()
    return root, common, git_dir


def _load_manifest(common: Path) -> dict[str, Any]:
    path = common / "hooks" / MANIFEST_NAME
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"Ponytail hook manifest is missing or invalid: {path}: {exc}") from exc
    if (
        not isinstance(value, dict)
        or value.get("schema") != "openbfme.ponytail-hooks"
        or value.get("schemaVersion") != 1
    ):
        raise GateError("Ponytail hook manifest schema is invalid")
    return value


def _plugin_details(grok: Path, root: Path) -> tuple[Path, str, str]:
    details = _run([str(grok), "plugin", "details", "ponytail"], cwd=root).stdout.decode(
        "utf-8", "strict"
    )
    match = re.search(r"(?m)^\s*path:\s*(.+?)\s*$", details)
    if match is None:
        raise GateError("Grok Ponytail plugin details have no installed path")
    plugin = Path(match.group(1)).resolve()
    skill = plugin / "skills" / "ponytail-review" / "SKILL.md"
    command = plugin / "commands" / "ponytail-review.toml"
    if not skill.is_file() or not command.is_file():
        raise GateError("official Ponytail review skill or command is missing")
    return skill, _file_sha(skill), _file_sha(command)


def _require_approved_plugin(observed: dict[str, str]) -> None:
    if observed != APPROVED_PONYTAIL:
        raise GateError("installed Ponytail identity is not the tracked approved release")


def verify_approved_plugin(grok: Path, root: Path) -> dict[str, str]:
    skill, skill_sha, command_sha = _plugin_details(grok, root)
    plugin = skill.parents[2]
    if _foreign_git(plugin, root, "status", "--porcelain=v1", "--untracked-files=all"):
        raise GateError("installed Ponytail plugin checkout is dirty")
    package = json.loads((plugin / "package.json").read_text(encoding="utf-8"))
    observed = {
        "ponytailOrigin": _text(_foreign_git(plugin, root, "remote", "get-url", "origin")),
        "ponytailCommit": _text(_foreign_git(plugin, root, "rev-parse", "HEAD")),
        "ponytailVersion": str(package.get("version", "")),
        "ponytailSkillSha256": skill_sha,
        "ponytailCommandSha256": command_sha,
    }
    _require_approved_plugin(observed)
    return observed


def verify_installation(root: Path, common: Path) -> dict[str, Any]:
    manifest = _load_manifest(common)
    configured = _run(
        ["git", "config", "--get-all", "core.hooksPath"], cwd=root, check=False,
    )
    if configured.returncode != 1:
        raise GateError("effective core.hooksPath must remain unset")
    required = {"pre-commit", "pre-merge-commit", "pre-applypatch", "pre-push"}
    hooks = manifest.get("hooks")
    if not isinstance(hooks, dict) or set(hooks) != required:
        raise GateError("Ponytail hook manifest does not bind pre-commit and pre-push exactly")
    for name, expected in hooks.items():
        path = common / "hooks" / name
        if not path.is_file() or _file_sha(path) != expected:
            raise GateError(f"installed {name} hook differs from its private manifest")
    for key in ("gatePath", "pythonPath", "grokPath"):
        path = Path(str(manifest.get(key, ""))).resolve()
        expected = manifest.get(key.replace("Path", "Sha256"))
        if not path.is_file() or _file_sha(path) != expected:
            raise GateError(f"{key} differs from the private hook manifest")
    gate_path = Path(str(manifest["gatePath"])).resolve()
    if gate_path != Path(__file__).resolve():
        raise GateError("installed hooks do not bind this gate implementation")
    if Path(sys.executable).resolve() != Path(str(manifest["pythonPath"])).resolve():
        raise GateError("gate is not running under the manifest-bound Python interpreter")
    grok = Path(str(manifest["grokPath"])).resolve()
    observed = verify_approved_plugin(grok, root)
    if any(manifest.get(key) != value for key, value in observed.items()):
        raise GateError("private manifest differs from the tracked approved Ponytail identity")
    inspection = _run([str(grok), "inspect"], cwd=root).stdout.decode("utf-8", "replace")
    if "ponytail (user, enabled)" not in inspection or "/ponytail:ponytail-review" not in inspection:
        raise GateError("official Ponytail plugin or qualified review skill is not enabled in Grok")
    _run(["git", "lfs", "version"], cwd=root)
    return manifest


def _head(root: Path) -> str:
    result = _run(["git", "rev-parse", "--verify", "HEAD^{commit}"], cwd=root, check=False)
    return result.stdout.decode("ascii", "strict").strip() if result.returncode == 0 else "0" * 40


def _commit_input(root: Path, event: str) -> tuple[dict[str, Any], bytes]:
    unmerged = _git(root, "ls-files", "-u", "-z")
    if unmerged:
        raise GateError("Ponytail review refuses an unmerged index")
    tree = _text(_git(root, "write-tree"))
    index_rows = _git(root, "ls-files", "-s", "-z")
    diff = _git(
        root, "diff", "--cached", "--binary", "--full-index", "--no-ext-diff",
        "--no-color", "--no-textconv", "--src-prefix=a/", "--dst-prefix=b/",
    )
    if not diff:
        raise GateError("Ponytail pre-commit review has no staged diff")
    identity = {
        "event": event,
        "head": _head(root),
        "tree": tree,
        "indexSha256": _sha(index_rows),
        "patchSha256": _sha(diff),
    }
    header = (json.dumps(identity, sort_keys=True) + "\n\n").encode("utf-8")
    return identity, header + diff


def _object_format(root: Path) -> tuple[str, int]:
    value = _text(_git(root, "rev-parse", "--show-object-format"))
    if value == "sha1":
        return value, 40
    if value == "sha256":
        return value, 64
    raise GateError(f"unsupported Git object format: {value}")


def _push_input(
    root: Path, raw: bytes, remote_name: str, remote_location: str,
) -> tuple[dict[str, Any], bytes]:
    object_format, hex_length = _object_format(root)
    try:
        text = raw.decode("ascii", "strict")
    except UnicodeDecodeError as exc:
        raise GateError("pre-push ref updates are not ASCII") from exc
    if not text or "\r" in text or not text.endswith("\n"):
        raise GateError("pre-push ref input is empty or not canonical LF text")
    updates: list[dict[str, str]] = []
    commits: list[str] = []
    seen: set[str] = set()
    for line in text.splitlines():
        fields = line.split(" ")
        if len(fields) != 4 or any(not value for value in fields):
            raise GateError(f"malformed pre-push ref update: {line!r}")
        local_ref, local_sha, remote_ref, remote_sha = fields
        for value in (local_sha, remote_sha):
            if len(value) != hex_length or re.fullmatch(r"[0-9a-f]+", value) is None:
                raise GateError(f"invalid {object_format} object id in pre-push input")
        deleting = ZERO_RE.fullmatch(local_sha) is not None
        if not deleting:
            _git(root, "cat-file", "-e", f"{local_sha}^{{commit}}")
            exclusions = [] if ZERO_RE.fullmatch(remote_sha) else [f"^{remote_sha}^{{commit}}"]
            if not exclusions:
                exclusions = [f"--not", f"--remotes={remote_name}"]
            rows = _text(_git(root, "rev-list", "--reverse", local_sha, *exclusions)).splitlines()
            for commit in rows:
                if commit and commit not in seen:
                    seen.add(commit)
                    commits.append(commit)
        updates.append({
            "localRef": local_ref,
            "localSha": local_sha,
            "remoteRef": remote_ref,
            "remoteSha": remote_sha,
        })
    sections: list[bytes] = []
    trees: list[str] = []
    for commit in commits:
        trees.append(_text(_git(root, "show", "-s", "--format=%T", commit)))
        sections.append(_git(
            root, "show", "-m", "--first-parent", "--format=fuller", "--binary", "--full-index",
            "--no-ext-diff", "--no-color", "--no-textconv", commit,
        ))
    patch = b"\n".join(sections)
    identity = {
        "event": "pre-push",
        "objectFormat": object_format,
        "remoteName": remote_name,
        "remoteLocationSha256": _sha(remote_location.encode("utf-8")),
        "refInputSha256": _sha(raw),
        "updates": updates,
        "commits": commits,
        "trees": trees,
        "patchSha256": _sha(patch),
    }
    header = (json.dumps(identity, sort_keys=True) + "\n\n").encode("utf-8")
    return identity, header + patch


def _write_atomic(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".tmp-{os.getpid()}")
    with temporary.open("xb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def _assistant_text(record: dict[str, Any]) -> tuple[str, str] | None:
    message = record.get("message")
    if not isinstance(message, dict) or message.get("role") != "assistant":
        return None
    content = message.get("content")
    if not isinstance(content, list):
        return None
    text_parts: list[str] = []
    for block in content:
        if not isinstance(block, dict):
            return None
        if block.get("type") == "text":
            text = block.get("text")
            if not isinstance(text, str):
                return None
            text_parts.append(text)
    stop_reason = message.get("stop_reason")
    if not isinstance(stop_reason, str):
        return None
    return "".join(text_parts).strip(), stop_reason


def _approved_review(payload: bytes) -> tuple[bool, str]:
    try:
        lines = payload.decode("utf-8", "strict").splitlines()
        records = [json.loads(line) for line in lines if line.strip()]
    except (UnicodeDecodeError, json.JSONDecodeError):
        return False, ""
    if not records or any(not isinstance(record, dict) for record in records):
        return False, ""

    result_records = [record for record in records if record.get("type") == "result"]
    if len(result_records) != 1 or records[-1] is not result_records[0]:
        return False, ""
    terminal = result_records[0]
    terminal_text = terminal.get("result")
    if not isinstance(terminal_text, str):
        return False, ""

    assistant_messages: list[tuple[str, str]] = []
    for record in records:
        if record.get("type") != "assistant":
            continue
        parsed = _assistant_text(record)
        if parsed is None:
            return False, ""
        assistant_messages.append(parsed)
    final_messages = [message for message in assistant_messages if message[1] == "end_turn"]
    if len(final_messages) != 1 or final_messages[0][0] != PASS_LINE:
        return False, terminal_text.strip()

    finding = re.compile(
        r"(?:^|:\s*)(?:delete|stdlib|native|yagni|shrink):|^net:\s*-\d+\s+lines\s+possible\.$",
        re.IGNORECASE | re.MULTILINE,
    )
    earlier_text = "\n".join(text for text, stop in assistant_messages if stop != "end_turn")
    approved = (
        terminal.get("subtype") == "success"
        and terminal.get("is_error") is False
        and terminal.get("stop_reason") == "end_turn"
        and terminal_text == PASS_LINE
        and not finding.search(earlier_text)
    )
    return approved, terminal_text.strip()


def _review(
    root: Path, git_dir: Path, manifest: dict[str, Any], identity: dict[str, Any], packet: bytes,
) -> None:
    identity_sha = _sha(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode("utf-8"))
    review_dir = git_dir / "ponytail-review"
    packet_path = review_dir / f"{identity_sha}.patch"
    prompt_path = review_dir / f"{identity_sha}.prompt.txt"
    stdout_path = review_dir / f"{identity_sha}.stdout.txt"
    stderr_path = review_dir / f"{identity_sha}.stderr.txt"
    receipt_path = review_dir / f"{identity_sha}.receipt.json"
    _write_atomic(packet_path, packet)
    prompt_text = (
        "/ponytail:ponytail-review\n"
        "Review only the frozen Git change packet at the exact path below. Do not review "
        "unstaged, untracked, ignored, or unrelated repository changes. You may inspect the "
        "existing source needed to understand this patch, but do not edit anything. The packet "
        f"identity is SHA-256 {identity_sha}. Packet: {packet_path}\n"
        "The gate already verified the packet bytes. Use only read/search tools; do not run "
        "shell commands, rehash the packet, or mutate repository state. "
        "Use the official Ponytail format. If there is nothing to cut, your complete terminal "
        "response must be exactly `Lean already. Ship.` with no preface, explanation, or trailing "
        "text. Otherwise return only the official removable-line finding and do not include the "
        "approval sentence.\n"
    )
    prompt = prompt_text.encode("utf-8")
    _write_atomic(prompt_path, prompt)
    grok = str(Path(str(manifest["grokPath"])).resolve())
    result = _run(
        [grok, "--single", prompt_text, "--output-format", "streaming-messages-json",
         "--max-turns", "30", "--no-subagents", "--disable-web-search",
         "--permission-mode", "dontAsk", "--allow", "Read", "--allow", "Grep",
         "--deny", "Bash", "--deny", "Edit", "--deny", "WebFetch",
         "--deny", "WebSearch", "--cwd", str(root)],
        cwd=root, timeout=600, check=False,
    )
    _write_atomic(stdout_path, result.stdout)
    _write_atomic(stderr_path, result.stderr)
    approved, review_text = _approved_review(result.stdout)
    passed = result.returncode == 0 and approved
    receipt = {
        "schema": "openbfme.ponytail-review-receipt",
        "timestampUtc": datetime.now(timezone.utc).isoformat(),
        "identity": identity,
        "identitySha256": identity_sha,
        "packetSha256": _sha(packet),
        "grokExitCode": result.returncode,
        "stdoutSha256": _sha(result.stdout),
        "stderrSha256": _sha(result.stderr),
        "reviewText": review_text,
        "result": "PASS" if passed else "FAIL",
    }
    _write_atomic(receipt_path, (json.dumps(receipt, indent=2) + "\n").encode("utf-8"))
    if not passed:
        sys.stderr.write((review_text or result.stdout.decode("utf-8", "replace")) + "\n")
        sys.stderr.write(result.stderr.decode("utf-8", "replace"))
        raise GateError("Ponytail review did not end with exact approval: Lean already. Ship.")


def _verify_head_receipt(root: Path, git_dir: Path) -> None:
    head = _text(_git(root, "rev-parse", "--verify", "HEAD^{commit}"))
    tree = _text(_git(root, "show", "-s", "--format=%T", head))
    parents = _text(_git(root, "rev-list", "--parents", "-n", "1", head)).split()
    prior_head = parents[1] if len(parents) > 1 else "0" * len(head)
    receipt_dir = git_dir / "ponytail-review"
    for path in receipt_dir.glob("*.receipt.json") if receipt_dir.is_dir() else ():
        try:
            receipt = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        identity = receipt.get("identity", {})
        if (
            receipt.get("result") == "PASS"
            and receipt.get("reviewText") == PASS_LINE
            and identity.get("event") in {"pre-commit", "pre-merge-commit", "pre-applypatch"}
            and identity.get("head") == prior_head
            and identity.get("tree") == tree
        ):
            identity_sha = _sha(json.dumps(identity, sort_keys=True, separators=(",", ":")).encode("utf-8"))
            stdout_path = receipt_dir / f"{identity_sha}.stdout.txt"
            packet_path = receipt_dir / f"{identity_sha}.patch"
            if (
                identity_sha == receipt.get("identitySha256")
                and stdout_path.is_file()
                and packet_path.is_file()
                and _file_sha(stdout_path) == receipt.get("stdoutSha256")
                and _file_sha(packet_path) == receipt.get("packetSha256")
            ):
                print(f"PONYTAIL_HEAD_RECEIPT PASS commit={head}")
                return
    raise GateError("HEAD has no exact Ponytail PASS receipt")


def _self_test() -> None:
    def stream(*records: dict[str, Any]) -> bytes:
        return ("\n".join(json.dumps(record) for record in records) + "\n").encode()

    progress = {
        "type": "assistant",
        "message": {"role": "assistant", "content": [{"type": "text", "text": "Reading packet."}],
                    "stop_reason": "tool_use"},
    }
    final = {
        "type": "assistant",
        "message": {"role": "assistant", "content": [{"type": "text", "text": PASS_LINE}],
                    "stop_reason": "end_turn"},
    }
    result = {"type": "result", "subtype": "success", "is_error": False,
              "stop_reason": "end_turn", "result": PASS_LINE}
    finding = {
        "type": "assistant",
        "message": {"role": "assistant", "content": [{"type": "text", "text": "L1: delete: cut it."}],
                    "stop_reason": "tool_use"},
    }
    net_finding = {
        "type": "assistant",
        "message": {"role": "assistant",
                    "content": [{"type": "text", "text": "net: -8 lines possible."}],
                    "stop_reason": "tool_use"},
    }
    assert _approved_review(stream(progress, final, result)) == (True, PASS_LINE)
    assert _approved_review(stream(finding, final, result)) == (False, PASS_LINE)
    assert _approved_review(stream(progress, net_finding, final, result)) == (False, PASS_LINE)
    bad_result = dict(result, result=f"L1: delete: cut it.\n{PASS_LINE}")
    assert _approved_review(stream(progress, final, bad_result)) == (False, bad_result["result"])
    assert _approved_review(stream(final, result, result)) == (False, "")
    assert _approved_review(b"not-json") == (False, "")
    print("PONYTAIL_GATE_SELF_TEST PASS streaming_terminal=true findings_plus_pass=false")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--event", choices=("pre-commit", "pre-merge-commit", "pre-applypatch", "pre-push"))
    parser.add_argument("--verify-installation", action="store_true")
    parser.add_argument("--verify-approved-plugin", action="store_true")
    parser.add_argument("--grok-path", type=Path)
    parser.add_argument("--verify-head-receipt", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("hook_args", nargs="*")
    args = parser.parse_args()
    try:
        if args.self_test:
            _self_test()
            return 0
        if args.verify_approved_plugin:
            if args.grok_path is None:
                raise GateError("--verify-approved-plugin requires --grok-path")
            verify_approved_plugin(args.grok_path.resolve(strict=True), Path.cwd().resolve())
            print("PONYTAIL_APPROVED_PLUGIN PASS")
            return 0
        root, common, git_dir = _repo()
        manifest = verify_installation(root, common)
        if args.verify_installation:
            print("PONYTAIL_HOOKS PASS")
            return 0
        if args.verify_head_receipt:
            _verify_head_receipt(root, git_dir)
            return 0
        if args.event in {"pre-commit", "pre-merge-commit", "pre-applypatch"}:
            before, packet = _commit_input(root, args.event)
            _review(root, git_dir, manifest, before, packet)
            after, _ = _commit_input(root, args.event)
        elif args.event == "pre-push":
            if len(args.hook_args) != 2:
                raise GateError("pre-push requires remote name and location")
            raw = sys.stdin.buffer.read()
            before, packet = _push_input(root, raw, *args.hook_args)
            _review(root, git_dir, manifest, before, packet)
            after, _ = _push_input(root, raw, *args.hook_args)
            if after != before:
                raise GateError("push refs or objects drifted during Ponytail review")
            lfs = _run(
                ["git", "lfs", "pre-push", *args.hook_args], cwd=root,
                input_bytes=raw, timeout=None, check=False,
            )
            if lfs.returncode != 0:
                sys.stderr.write((lfs.stderr or lfs.stdout).decode("utf-8", "replace"))
                raise GateError("git lfs pre-push failed after Ponytail approval")
        else:
            raise GateError("one hook event or --verify-installation is required")
        if after != before:
            raise GateError("staged Git identity drifted during Ponytail review")
        print(f"PONYTAIL_GIT_GATE PASS event={args.event}")
        return 0
    except GateError as exc:
        print(f"PONYTAIL_GIT_GATE FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
