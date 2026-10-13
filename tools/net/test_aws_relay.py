# Lane INFRA-2: services/online/deploy/aws_relay.py against an in-memory Lightsail (no AWS call): the account and bundle guards, an idempotent create
# (a second run changes nothing), refusals on mismatched resources, an exact firewall check, and a destroy that flushes the relay's logs first, never
# touches the live relay without --allow-live and leaves the shared key pair.
import argparse
import subprocess
import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / "services/online/deploy"))
import aws_relay  # noqa: E402
import monitor  # noqa: E402

MUTATING = ("import-key-pair", "create-instances", "allocate-static-ip", "attach-static-ip", "detach-static-ip", "release-static-ip",
            "put-instance-public-ports", "delete-instance")


class FakeAws:
    def __init__(self, account=aws_relay.ACCOUNT):
        self.account = account
        self.instances, self.ips, self.keys, self.ports, self.calls = {}, {}, {}, {}, []
        self.pinned_state = None

    def __call__(self, args):
        self.calls.append(args)
        if args[:2] == ["sts", "get-caller-identity"]:
            return {"Account": self.account}
        op, rest = args[1], args[2:]
        opt = {rest[i]: rest[i + 1] for i in range(0, len(rest) - 1, 2) if rest[i].startswith("--")}
        if op == "get-instance":
            if opt["--instance-name"] not in self.instances:
                raise aws_relay.NotFound("x")
            i = self.instances[opt["--instance-name"]]
            i["state"]["name"] = self.pinned_state or "running"  # pending on create, running at the next look (unless a test pins a state)
            return {"instance": i}
        if op == "get-static-ip":
            if opt["--static-ip-name"] not in self.ips:
                raise aws_relay.NotFound("x")
            return {"staticIp": self.ips[opt["--static-ip-name"]]}
        if op == "get-key-pair":
            if opt["--key-pair-name"] not in self.keys:
                raise aws_relay.NotFound("x")
            return {"keyPair": {"name": opt["--key-pair-name"]}}
        if op == "import-key-pair":
            # as Lightsail answered live (2026-10-13, aws_relay.py's comment): the OpenSSH line is taken, Ed25519 included; base64 of it is refused
            line = opt["--public-key-base64"]
            if line.split()[0] not in ("ssh-ed25519", "ssh-rsa"):
                raise aws_relay.RelayError("InvalidInputException: The format of this public key is not valid.")
            self.keys[opt["--key-pair-name"]] = line
        elif op == "create-instances":
            n = opt["--instance-names"]
            self.instances[n] = {"name": n, "state": {"name": "pending"}, "bundleId": opt["--bundle-id"], "blueprintId": opt["--blueprint-id"],
                                 "sshKeyName": opt["--key-pair-name"], "location": {"availabilityZone": opt["--availability-zone"]}}
            self.ports[n] = [{"protocol": "tcp", "fromPort": 22, "toPort": 22, "cidrs": ["0.0.0.0/0"], "ipv6Cidrs": ["::/0"]},
                             {"protocol": "tcp", "fromPort": 80, "toPort": 80, "cidrs": ["0.0.0.0/0"], "ipv6Cidrs": ["::/0"]}]
        elif op == "allocate-static-ip":
            self.ips[opt["--static-ip-name"]] = {"name": opt["--static-ip-name"], "ipAddress": "203.0.113.77", "isAttached": False}
        elif op == "attach-static-ip":
            self.ips[opt["--static-ip-name"]].update(isAttached=True, attachedTo=opt["--instance-name"])
        elif op == "detach-static-ip":
            self.ips[opt["--static-ip-name"]].update(isAttached=False, attachedTo=None)
        elif op == "release-static-ip":
            del self.ips[opt["--static-ip-name"]]
        elif op == "put-instance-public-ports":
            infos = rest[rest.index("--port-infos") + 1:rest.index("--port-infos") + 3]
            self.ports[opt["--instance-name"]] = [dict(kv.split("=") for kv in p.split(",")) for p in infos]
            for p in self.ports[opt["--instance-name"]]:
                p.update(fromPort=int(p["fromPort"]), toPort=int(p["toPort"]), cidrs=[p["cidrs"]], ipv6Cidrs=[])
        elif op == "get-instance-port-states":
            return {"portStates": self.ports[opt["--instance-name"]]}
        elif op == "delete-instance":
            del self.instances[opt["--instance-name"]]
        else:
            raise AssertionError(f"unexpected {op}")
        return {}

    def mutations(self):
        return [c[1] for c in self.calls if len(c) > 1 and c[1] in MUTATING]


class FakeRun:
    def __init__(self, logs_left="0"):
        self.cmds, self.logs_left = [], logs_left

    def __call__(self, cmd, **kw):
        self.cmds.append(cmd)
        out = self.logs_left + "\n" if cmd[0] == "ssh" and "relay-logs" in cmd[-1] else ""
        return subprocess.CompletedProcess(cmd, 0, stdout=out, stderr="")


@pytest.fixture
def env(tmp_path, monkeypatch):
    keys = tmp_path / "keys"
    keys.mkdir()
    (keys / "ticket.keys").write_text("1:" + "11" * 32 + "\n")
    (keys / "upload.key").write_text("33" * 32)
    (tmp_path / "id.pub").write_text("ssh-ed25519 AAAAtest deck\n")
    monkeypatch.setattr(aws_relay, "my_cidr", lambda: "198.51.100.9/32")
    monkeypatch.setattr(monitor, "probe_relay", lambda addr, *a: (True, f"{addr} NOT_BOUND (1 ms)"))
    fake = FakeAws()
    lines = []
    ls = aws_relay.Lightsail(aws=fake, sleep=lambda s: None, log=lines.append)

    def args(**kw):
        base = dict(name="openbfme-relay-drill", static_ip="openbfme-relay-drill-ip", keys=str(keys), lobby="http://192.0.2.1:8787", bundle="micro_3_0",
                    allow_bigger=False, ssh_cidr="auto", ssh_key=str(tmp_path / "id"), zone="us-east-2a", key_pair="openbfme-relay-deck",
                    discard_logs=False, allow_live=False)
        return argparse.Namespace(**{**base, **kw})
    return fake, ls, args, lines


def test_the_account_and_the_bundle_are_checked_before_anything_changes(env):
    fake, ls, args, _ = env
    fake.account = "111111111111"
    with pytest.raises(aws_relay.RelayError, match="not the project's 425820477456"):
        aws_relay.create(ls, args(), run=FakeRun())
    fake.account = aws_relay.ACCOUNT
    with pytest.raises(aws_relay.RelayError, match="above micro"):
        aws_relay.create(ls, args(bundle="small_3_0"), run=FakeRun())
    assert fake.mutations() == [] and not any(c[0] == "lightsail" for c in fake.calls)


def test_create_from_nothing_then_again_changes_nothing(env):
    fake, ls, args, lines = env
    run = FakeRun()
    address, times = aws_relay.create(ls, args(), run=run)
    assert address == "203.0.113.77"
    assert fake.mutations() == ["import-key-pair", "create-instances", "allocate-static-ip", "attach-static-ip", "put-instance-public-ports"]
    assert fake.keys["openbfme-relay-deck"] == "ssh-ed25519 AAAAtest deck"
    assert [c for c in run.cmds if c[0] == "ssh-keygen"] == [["ssh-keygen", "-R", "203.0.113.77", "-f", str(aws_relay.KNOWN_HOSTS)]]
    assert fake.instances["openbfme-relay-drill"]["bundleId"] == "micro_3_0"
    # the default ports of a new instance (22 and 80 from anywhere) are replaced by exactly UDP 7350 + SSH from the Deck's address
    assert aws_relay.got_ports(fake.ports["openbfme-relay-drill"]) == aws_relay.want_ports("198.51.100.9/32")
    vps = [c for c in run.cmds if c[0].endswith("vps_relay.sh")]
    assert vps == [[vps[0][0], "ubuntu@203.0.113.77", args().keys, "http://192.0.2.1:8787", "7350"]]
    assert "verified: openbfme-relay-drill running (micro_3_0), 203.0.113.77 attached, firewall exact" in lines[-1]
    fake.calls.clear()
    again = FakeRun()
    aws_relay.create(ls, args(), run=again)
    assert fake.mutations() == ["put-instance-public-ports"]
    # an ordinary reinstall keeps the trusted host key (a changed key is then refused by ssh, not re-learnt)
    assert not [c for c in again.cmds if c[0] == "ssh-keygen"]  # the firewall is re-asserted (put is idempotent); nothing is created twice


def test_create_refuses_resources_that_do_not_match(env):
    fake, ls, args, _ = env
    aws_relay.create(ls, args(), run=FakeRun())
    with pytest.raises(aws_relay.RelayError, match="exists as micro_3_0"):
        aws_relay.create(ls, args(bundle="nano_3_0"), run=FakeRun())
    fake.ips["openbfme-relay-drill-ip"]["attachedTo"] = "someone-else"
    with pytest.raises(aws_relay.RelayError, match="attached to someone-else"):
        aws_relay.create(ls, args(), run=FakeRun())


def test_verify_catches_an_extra_open_port(env):
    fake, ls, args, _ = env
    aws_relay.create(ls, args(), run=FakeRun())
    fake.ports["openbfme-relay-drill"].append({"protocol": "tcp", "fromPort": 80, "toPort": 80, "cidrs": ["0.0.0.0/0"], "ipv6Cidrs": []})
    with pytest.raises(aws_relay.RelayError, match="firewall is"):
        aws_relay.verify(ls, args())


def test_destroy_flushes_logs_refuses_the_live_relay_and_keeps_the_key_pair(env):
    fake, ls, args, lines = env
    aws_relay.create(ls, args(), run=FakeRun())
    with pytest.raises(aws_relay.RelayError, match="the live relay"):
        aws_relay.destroy(ls, args(name="openbfme-relay-1", static_ip="openbfme-relay-ip"), run=FakeRun())
    with pytest.raises(aws_relay.RelayError, match="holds 2 relay log"):
        aws_relay.destroy(ls, args(), run=FakeRun(logs_left="2"))
    assert "openbfme-relay-drill" in fake.instances and "openbfme-relay-drill-ip" in fake.ips
    fake.calls.clear()
    run = FakeRun(logs_left="2")
    aws_relay.destroy(ls, args(discard_logs=True), run=run)
    assert fake.mutations() == ["detach-static-ip", "release-static-ip", "delete-instance"]
    assert fake.instances == {} and fake.ips == {} and "openbfme-relay-deck" in fake.keys
    assert "systemctl stop openbfme-relay.service" in run.cmds[0][-1] and "2 log(s) not uploaded, discarded" in "\n".join(lines)
    assert lines[-1].endswith("verified: neither the instance nor the static IP exists")


def test_powershell_quoting():
    assert aws_relay.ps_quote("it's") == "'it''s'"
    assert aws_relay.ps_quote("fromPort=7350,toPort=7350,protocol=udp,cidrs=0.0.0.0/0") == "'fromPort=7350,toPort=7350,protocol=udp,cidrs=0.0.0.0/0'"
    with pytest.raises(aws_relay.RelayError):
        aws_relay.ps_quote('a"b')


def test_destroy_does_not_take_a_missing_log_directory_for_an_empty_one(env):
    fake, ls, args, _ = env
    aws_relay.create(ls, args(), run=FakeRun())
    with pytest.raises(aws_relay.RelayError, match="no relay log directory"):
        aws_relay.destroy(ls, args(), run=FakeRun(logs_left="no-log-dir"))
    assert "openbfme-relay-drill" in fake.instances


def test_the_relay_unit_creates_its_log_directory():
    # the drill of 2026-10-13: --log-dir /var/lib/openbfme/relay-logs did not exist (StateDirectory made only /var/lib/openbfme), so the relay wrote
    # no log and the uploader failed every minute; systemd must create the log directory, as the relay does not
    unit = (HERE.parents[1] / "services/online/deploy/openbfme-relay.service").read_text()
    log_dir = unit.split("--log-dir ")[1].split()[0]
    state = next(ln.split("=", 1)[1].split() for ln in unit.splitlines() if ln.startswith("StateDirectory="))
    assert log_dir.startswith("/var/lib/") and log_dir[len("/var/lib/"):] in state


def test_destroy_needs_discard_logs_when_the_logs_cannot_be_checked(env):
    fake, ls, args, lines = env
    aws_relay.create(ls, args(), run=FakeRun())
    # a stopped instance: no ssh, so its un-uploaded logs are unknown
    fake.pinned_state = "stopped"
    run = FakeRun()
    with pytest.raises(aws_relay.RelayError, match="is stopped: its un-uploaded logs cannot be checked"):
        aws_relay.destroy(ls, args(), run=run)
    assert not [c for c in run.cmds if c[0] == "ssh"] and "openbfme-relay-drill" in fake.instances
    fake.pinned_state = None
    # a missing static IP: the same
    del fake.ips["openbfme-relay-drill-ip"]
    with pytest.raises(aws_relay.RelayError, match="has no static IP"):
        aws_relay.destroy(ls, args(), run=FakeRun())
    aws_relay.destroy(ls, args(discard_logs=True), run=FakeRun())
    assert fake.instances == {} and "has no static IP: logs not checked; --discard-logs" in "\n".join(lines)
