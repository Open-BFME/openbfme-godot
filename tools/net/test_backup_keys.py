# Lane INFRA-2: services/online/deploy/backup_keys.sh with a stand-in ssh (each "host" is a directory used as HOME): push writes 0600 files in 0700
# directories, verify catches a changed, missing or loosened file by name without printing a hash, and restore gives back the same bytes.
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "services/online/deploy/backup_keys.sh"
KEYS = {"ticket.keys": "1:" + "11" * 32 + "\n", "session.key": "22" * 32, "upload.key": "33" * 32, "admin.key": "44" * 32}


def setup(tmp_path):
    sec = tmp_path / "deck" / "online-secrets"
    sec.mkdir(parents=True)
    for k, v in KEYS.items():
        (sec / k).write_text(v)
    ssh_key = tmp_path / "deck" / "relay_key"
    ssh_key.write_text("-----BEGIN OPENSSH PRIVATE KEY-----\nnot a real key\n")
    Path(f"{ssh_key}.pub").write_text("ssh-ed25519 AAAA test\n")
    fake = tmp_path / "fake-ssh"
    fake.write_text('#!/bin/bash\nhost="$1"; shift\nmkdir -p "$FAKE_HOSTS/$host"\nHOME="$FAKE_HOSTS/$host" exec bash -c "$*"\n')
    fake.chmod(0o755)
    env = {**os.environ, "SECRETS_DIR": str(sec), "RELAY_SSH_KEY": str(ssh_key), "BACKUP_SSH": str(fake), "FAKE_HOSTS": str(tmp_path / "hosts")}
    return env, sec


def run(env, *args):
    return subprocess.run(["bash", str(SCRIPT), *args], env=env, capture_output=True, text=True)


def test_push_verify_restore(tmp_path):
    env, sec = setup(tmp_path)
    r = run(env, "push", "nas", "pve")
    assert r.returncode == 0, r.stderr
    assert r.stdout.count("6 files match the Deck's") == 2
    for host in ("nas", "pve"):
        b = tmp_path / "hosts" / host / "openbfme-keys"
        assert oct(b.stat().st_mode & 0o777) == "0o700" and oct((b / "online-secrets").stat().st_mode & 0o777) == "0o700"
        assert all(oct(p.stat().st_mode & 0o777) == "0o600" for p in b.rglob("*") if p.is_file())
        assert (b / "online-secrets" / "admin.key").read_text() == KEYS["admin.key"]
    for v in KEYS.values():  # nothing secret is printed
        assert v.strip() not in r.stdout + r.stderr
    # a changed key, a missing one and a loosened mode are each named
    pve = tmp_path / "hosts" / "pve" / "openbfme-keys"
    (pve / "online-secrets" / "upload.key").write_text("55" * 32)
    r = run(env, "verify", "nas", "pve")
    assert r.returncode == 1 and "pve: online-secrets/upload.key MISSING or DIFFERENT" in r.stderr and "nas: 6 files match" in r.stdout
    (pve / "ssh" / "openbfme_relay_ed25519.pub").unlink()
    os.chmod(tmp_path / "hosts" / "nas" / "openbfme-keys" / "online-secrets" / "session.key", 0o644)
    r = run(env, "verify", "nas", "pve")
    assert "pve: ssh/openbfme_relay_ed25519.pub MISSING or DIFFERENT" in r.stderr and "nas: online-secrets/session.key is mode 644, not 600" in r.stderr
    # a push repairs it; a restore gives back the Deck's bytes
    assert run(env, "push", "nas", "pve").returncode == 0
    out = tmp_path / "restored"
    r = run(env, "restore", "pve", str(out))
    assert r.returncode == 0, r.stderr
    for k, v in KEYS.items():
        assert (out / "online-secrets" / k).read_text() == v
    assert (out / "ssh" / "openbfme_relay_ed25519.pub").read_text() == "ssh-ed25519 AAAA test\n"
    # never into a directory that holds something
    assert run(env, "restore", "pve", str(out)).returncode == 1
