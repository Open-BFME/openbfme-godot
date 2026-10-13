#!/usr/bin/env python3
# The online relay on AWS Lightsail as code (lane INFRA-2): create, verify, destroy and list relays through the owner's AWS launcher on JonathanPC.
# Python 3 standard library only; run on the Deck.
#
#   create   --name N --static-ip IPNAME --keys DIR --lobby URL [--bundle micro_3_0] [--allow-bigger] [--ssh-cidr auto|a.b.c.d/32] [--ssh-key PATH]
#            idempotent, each step only when missing: the Lightsail key pair (imported from --ssh-key's .pub), the instance (Ubuntu 24.04), the static
#            IP (allocated, attached), the firewall (exactly: UDP 7350 from anywhere, TCP 22 from --ssh-cidr), then the relay itself with
#            vps_relay.sh (ticket.keys + upload.key from --keys, the lobby URL for the log uploader), then verify. Prints each step's time.
#   verify   --name N --static-ip IPNAME [--ssh-cidr ...]: running, the static IP attached, the firewall exactly as above, and the relay answers a UDP
#            probe (monitor.py's NOT_BOUND probe)
#   destroy  --name N --static-ip IPNAME [--discard-logs]: stops the relay (its open games' logs are closed), runs the log uploader once, and refuses
#            when logs are left that the lobby did not take (unless --discard-logs); then releases the static IP and deletes the instance. The key
#            pair stays (the live relay uses it). The live relay's names are refused without --allow-live.
#   status   every instance and static IP in the region, with the bundles' monthly prices
#
# The owner's rules (services/online/DEPLOY.md, the owner's handoff of 2026-10-12): AWS only in the project account 425820477456 (every run checks
# sts get-caller-identity first; the launcher refuses other accounts too), us-east-2, short-lived login credentials (no static keys), nothing but
# Lightsail instances, static IPs and key pairs; bundles above micro need --allow-bigger. The launcher: python infra-run.py aws -- aws ... over ssh.
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ACCOUNT = "425820477456"
REGION = "us-east-2"
LAUNCHER = r"(Join-Path $env:USERPROFILE '.openbfme\infra\infra-run.py')"  # the owner's launcher, resolved on JonathanPC
JPC = os.environ.get("JPC_SSH_HOST", "jonathan-windows")
SMALL_BUNDLES = ("nano_3_0", "micro_3_0")
BLUEPRINT = "ubuntu_24_04"
PORT = 7350
LIVE = ("openbfme-relay-1", "openbfme-relay-ip")
KNOWN_HOSTS = Path.home() / ".ssh" / "openbfme_relays_known_hosts"


class RelayError(Exception):
    pass


class NotFound(RelayError):
    pass


def ps_quote(arg):
    """One argument for the PowerShell on the far side of ssh: single-quoted ('' inside). A double quote is refused: Windows PowerShell 5.1 mangles
    double quotes in arguments to native programs."""
    if '"' in arg:
        raise RelayError(f"an argument with a double quote cannot cross PowerShell safely: {arg[:40]!r}")
    return "'" + arg.replace("'", "''") + "'"


def launcher_aws(args):
    """Runs `aws <args> --region us-east-2 --output json` under the owner's launcher on JonathanPC. Returns parsed JSON (or {})."""
    remote = "& python " + LAUNCHER + " " + " ".join(ps_quote(a) for a in ["aws", "--", "aws", *args, "--region", REGION, "--output", "json"])
    r = subprocess.run(["ssh", JPC, remote], capture_output=True, text=True, stdin=subprocess.DEVNULL, timeout=300)
    if r.returncode:
        err = r.stderr.strip().splitlines()
        tail = next((ln for ln in reversed(err) if ln.strip()), "") if err else ""
        if "NotFoundException" in r.stderr or "does not exist" in r.stderr:
            raise NotFound(f"aws {args[0]} {args[1]}: not found")
        raise RelayError(f"aws {' '.join(args[:2])}: exit {r.returncode}: {tail[:300]}")
    return json.loads(r.stdout) if r.stdout.strip() else {}


class Lightsail:
    def __init__(self, aws=launcher_aws, sleep=time.sleep, log=print):
        self.aws = aws
        self.sleep = sleep
        self.log = log

    def ls(self, *args):
        return self.aws(["lightsail", *args])

    def check_account(self):
        who = self.aws(["sts", "get-caller-identity"])
        if who.get("Account") != ACCOUNT:
            raise RelayError(f"the AWS account is {who.get('Account')!r}, not the project's {ACCOUNT}: nothing done")

    def get(self, what, name):
        try:
            return self.ls(f"get-{what}", f"--{what}-name", name)[{"instance": "instance", "static-ip": "staticIp", "key-pair": "keyPair"}[what]]
        except NotFound:
            return None

    def instance(self, name):
        return self.get("instance", name)

    def static_ip(self, name):
        return self.get("static-ip", name)

    def key_pair(self, name):
        return self.get("key-pair", name)

    def wait_state(self, name, want, timeout=600):
        for _ in range(timeout // 5):
            i = self.instance(name)
            state = (i or {}).get("state", {}).get("name") if i else "gone"
            if state == want:
                return i
            self.sleep(5)
        raise RelayError(f"{name} did not reach {want} in {timeout} s")

    def ports(self, name):
        return self.ls("get-instance-port-states", "--instance-name", name)["portStates"]


def want_ports(ssh_cidr):
    return {("udp", PORT, PORT, ("0.0.0.0/0",)), ("tcp", 22, 22, (ssh_cidr,))}


def got_ports(states):
    return {(p["protocol"], p["fromPort"], p["toPort"], tuple(sorted(p.get("cidrs", [])))) for p in states if p.get("state", "open") == "open"} | (
        {("ipv6", 0, 0, tuple(p["ipv6Cidrs"])) for p in states if p.get("ipv6Cidrs")})


def my_cidr():
    with urllib.request.urlopen("https://checkip.amazonaws.com", timeout=15) as r:
        ip = r.read().decode().strip()
    if not re.fullmatch(r"\d{1,3}(\.\d{1,3}){3}", ip):
        raise RelayError(f"checkip.amazonaws.com answered {ip[:40]!r}")
    return f"{ip}/32"


class Steps:
    """Times each step; the summary is the drill's timing."""

    def __init__(self, log):
        self.log = log
        self.t0 = time.monotonic()
        self.times = []

    def __call__(self, what):
        t = time.monotonic() - self.t0
        self.times.append((what, t))
        self.log(f"[{t:6.0f} s] {what}")


def ssh_opts(ssh_key):
    return ["-i", str(ssh_key), "-o", "IdentitiesOnly=yes", "-o", f"UserKnownHostsFile={KNOWN_HOSTS}", "-o", "StrictHostKeyChecking=accept-new",
            "-o", "ConnectTimeout=10", "-o", "BatchMode=yes"]


def create(ls, a, run=subprocess.run):
    step = Steps(ls.log)
    ls.check_account()
    if a.bundle not in SMALL_BUNDLES and not a.allow_bigger:
        raise RelayError(f"bundle {a.bundle} is above micro: pass --allow-bigger if the owner agreed to its price")
    keys = Path(a.keys)
    for f in ("ticket.keys", "upload.key"):
        if not (keys / f).is_file():
            raise RelayError(f"{keys / f} is missing")
    pub = Path(f"{a.ssh_key}.pub").read_text().strip()
    cidr = my_cidr() if a.ssh_cidr == "auto" else a.ssh_cidr
    if not re.fullmatch(r"\d{1,3}(\.\d{1,3}){3}/\d{1,2}", cidr):
        raise RelayError(f"--ssh-cidr {cidr!r} is not a.b.c.d/n")
    step(f"account {ACCOUNT} checked; ssh from {cidr}")
    if ls.key_pair(a.key_pair) is None:
        # INFRA-2 r2 (Sol r1 asked whether publicKeyBase64 needs base64 and whether Ed25519 is accepted): checked live on 2026-10-13 with a throwaway
        # pair through the owner's AWS CLI: the OpenSSH line itself is accepted, Ed25519 included (fingerprint = the key's SHA256, as the live pair
        # openbfme-relay-deck), and the base64 OF the line is refused ("InvalidInputException: The format of this public key is not valid")
        if pub.split()[0] not in ("ssh-ed25519", "ssh-rsa"):
            raise RelayError(f"{a.ssh_key}.pub is not an OpenSSH ed25519 / rsa public key")
        ls.ls("import-key-pair", "--key-pair-name", a.key_pair, "--public-key-base64", pub)
        step(f"key pair {a.key_pair} imported")
    i = ls.instance(a.name)
    created = i is None
    if i is None:
        ls.ls("create-instances", "--instance-names", a.name, "--availability-zone", a.zone, "--blueprint-id", BLUEPRINT, "--bundle-id", a.bundle,
              "--key-pair-name", a.key_pair, "--tags", "key=project,value=openbfme")
        step(f"instance {a.name} ({a.bundle}, {BLUEPRINT}, {a.zone}) requested")
    elif i.get("bundleId") != a.bundle or i.get("blueprintId") != BLUEPRINT or i.get("sshKeyName") != a.key_pair:
        raise RelayError(f"{a.name} exists as {i.get('bundleId')} / {i.get('blueprintId')} / key {i.get('sshKeyName')}: not what was asked; nothing changed")
    ls.wait_state(a.name, "running")
    step(f"instance {a.name} running")
    ip = ls.static_ip(a.static_ip)
    if ip is None:
        ls.ls("allocate-static-ip", "--static-ip-name", a.static_ip)
        ip = ls.static_ip(a.static_ip)
        step(f"static IP {a.static_ip} allocated: {ip['ipAddress']}")
    if ip.get("isAttached") and ip.get("attachedTo") != a.name:
        raise RelayError(f"static IP {a.static_ip} is attached to {ip.get('attachedTo')}, not {a.name}")
    if not ip.get("isAttached"):
        ls.ls("attach-static-ip", "--static-ip-name", a.static_ip, "--instance-name", a.name)
        step(f"static IP {ip['ipAddress']} attached to {a.name}")
    address = ip["ipAddress"]
    ls.ls("put-instance-public-ports", "--instance-name", a.name, "--port-infos",
          f"fromPort={PORT},toPort={PORT},protocol=udp,cidrs=0.0.0.0/0", f"fromPort=22,toPort=22,protocol=tcp,cidrs={cidr}")
    step(f"firewall: UDP {PORT} from anywhere, TCP 22 from {cidr}")
    # a NEW instance behind a reused static IP has a new host key: forget the old one for this address only. A reinstall of an existing instance
    # keeps the trusted key (INFRA-2 r2, Sol r1): a changed key there is refused by ssh, never re-learnt
    if created:
        run(["ssh-keygen", "-R", address, "-f", str(KNOWN_HOSTS)], capture_output=True)
    for _ in range(60):
        if run(["ssh", *ssh_opts(a.ssh_key), f"ubuntu@{address}", "true"], capture_output=True, stdin=subprocess.DEVNULL).returncode == 0:
            break
        ls.sleep(5)
    else:
        raise RelayError(f"ssh to {address} did not answer in 5 minutes")
    step(f"ssh to {address} answers")
    env = {**os.environ, "RELAY_SSH_OPTS": " ".join(ssh_opts(a.ssh_key))}
    r = run([str(HERE / "vps_relay.sh"), f"ubuntu@{address}", str(keys), a.lobby, str(PORT)], env=env, stdin=subprocess.DEVNULL)
    if r.returncode:
        raise RelayError(f"vps_relay.sh failed (exit {r.returncode})")
    step(f"relay installed by vps_relay.sh (lobby {a.lobby})")
    verify(ls, a, cidr=cidr, steps=step)
    return address, step.times


def verify(ls, a, cidr=None, steps=None):
    step = steps or Steps(ls.log)
    if steps is None:
        ls.check_account()
    cidr = cidr or (my_cidr() if a.ssh_cidr == "auto" else a.ssh_cidr)
    i = ls.instance(a.name)
    if i is None or i.get("state", {}).get("name") != "running":
        raise RelayError(f"{a.name} is {'missing' if i is None else i['state']['name']}")
    ip = ls.static_ip(a.static_ip)
    if ip is None or ip.get("attachedTo") != a.name or not ip.get("isAttached"):
        raise RelayError(f"static IP {a.static_ip} is {'missing' if ip is None else 'attached to ' + str(ip.get('attachedTo'))}, not on {a.name}")
    got = got_ports(ls.ports(a.name))
    if got != want_ports(cidr):
        raise RelayError(f"{a.name}'s firewall is {sorted(got)}, expected {sorted(want_ports(cidr))}")
    sys.path.insert(0, str(HERE))
    import monitor
    ok, what = monitor.probe_relay(f"{ip['ipAddress']}:{PORT}")
    if not ok:
        raise RelayError(f"relay probe: {what}")
    step(f"verified: {a.name} running ({i.get('bundleId')}), {ip['ipAddress']} attached, firewall exact, relay probe {what}")
    return ip["ipAddress"]


def destroy(ls, a, run=subprocess.run):
    step = Steps(ls.log)
    ls.check_account()
    if (a.name in LIVE or a.static_ip in LIVE) and not a.allow_live:
        raise RelayError(f"{a.name} / {a.static_ip} is the live relay: pass --allow-live to destroy it")
    ip = ls.static_ip(a.static_ip)
    i = ls.instance(a.name)
    if ip and ip.get("isAttached") and ip.get("attachedTo") != a.name:
        raise RelayError(f"static IP {a.static_ip} is attached to {ip.get('attachedTo')}, not {a.name}: nothing done")
    if i is not None and not (i.get("state", {}).get("name") == "running" and ip):
        # INFRA-2 r2 (Sol r1): a stopped instance or one without its static IP cannot be asked for its un-uploaded logs: never assume there are none
        why = "has no static IP" if not ip else f"is {i.get('state', {}).get('name')}"
        if not a.discard_logs:
            raise RelayError(f"{a.name} {why}: its un-uploaded logs cannot be checked; pass --discard-logs to destroy it anyway")
        step(f"{a.name} {why}: logs not checked; --discard-logs")
    if i is not None and i.get("state", {}).get("name") == "running" and ip:
        # the relay's logs: close the open games (SIGTERM writes their END), upload once, count what the lobby did not take
        # (a missing log directory is not "nothing left": that relay logged nothing, which the drill of 2026-10-13 found on the live relay)
        cmd = ("sudo systemctl stop openbfme-relay.service; sudo systemctl start openbfme-relay-upload.service || true; "
               "if sudo test -d /var/lib/openbfme/relay-logs; then sudo find /var/lib/openbfme/relay-logs -maxdepth 1 \\( -name '*.obfmerlog' -o -name '*.part' \\) | wc -l; "
               "else echo no-log-dir; fi")
        r = run(["ssh", *ssh_opts(a.ssh_key), f"ubuntu@{ip['ipAddress']}", cmd], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        left = r.stdout.strip().splitlines()[-1] if r.returncode == 0 and r.stdout.strip() else None
        if left is None:
            if not a.discard_logs:
                raise RelayError(f"cannot reach {a.name} over ssh to flush its logs: pass --discard-logs to destroy it anyway")
            step(f"{a.name}: logs not checked (ssh failed); --discard-logs")
        elif left == "no-log-dir":
            if not a.discard_logs:
                raise RelayError(f"{a.name} has no relay log directory (its games were not logged): pass --discard-logs to destroy it")
            step(f"{a.name}: no relay log directory (nothing was logged); --discard-logs")
        elif left != "0":
            if not a.discard_logs:
                raise RelayError(f"{a.name} holds {left} relay log(s) the lobby has not taken: fix the upload, or pass --discard-logs")
            step(f"{a.name}: {left} log(s) not uploaded, discarded (--discard-logs)")
        else:
            step(f"{a.name}: relay stopped, every log uploaded")
    if ip:
        if ip.get("isAttached"):
            ls.ls("detach-static-ip", "--static-ip-name", a.static_ip)
        ls.ls("release-static-ip", "--static-ip-name", a.static_ip)
        step(f"static IP {a.static_ip} ({ip['ipAddress']}) released")
    if i is not None:
        ls.ls("delete-instance", "--instance-name", a.name)
        ls.wait_state(a.name, "gone")
        step(f"instance {a.name} deleted")
    if ls.static_ip(a.static_ip) is not None or ls.instance(a.name) is not None:
        raise RelayError(f"{a.name} / {a.static_ip} still listed after the destroy")
    step("verified: neither the instance nor the static IP exists")
    return step.times


def status(ls):
    ls.check_account()
    prices = {b["bundleId"]: b["price"] for b in ls.ls("get-bundles")["bundles"]}
    total = 0.0
    for i in ls.ls("get-instances")["instances"]:
        p = prices.get(i["bundleId"], 0.0)
        total += p
        ls.log(f"instance {i['name']}: {i['state']['name']}, {i['bundleId']} (${p:.2f}/month), {i['blueprintId']}, {i['location']['availabilityZone']}, "
               f"IP {i.get('publicIpAddress')}, key {i.get('sshKeyName')}")
    for s in ls.ls("get-static-ips")["staticIps"]:
        ls.log(f"static IP {s['name']}: {s['ipAddress']} -> {s.get('attachedTo') if s.get('isAttached') else 'NOT ATTACHED (billed while idle)'}")
    ls.log(f"Lightsail instances: ${total:.2f}/month at list price (an attached static IP is free)")
    return total


def main(argv=None, ls=None):
    ap = argparse.ArgumentParser(description="The OpenBFME relay on AWS Lightsail (project account only)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("create", "verify", "destroy", "status"):
        p = sub.add_parser(name)
        if name == "status":
            continue
        p.add_argument("--name", required=True)
        p.add_argument("--static-ip", required=True)
        p.add_argument("--ssh-key", default=str(Path.home() / ".ssh" / "openbfme_relay_ed25519"))
        if name in ("create", "verify"):
            p.add_argument("--ssh-cidr", default="auto")
        if name == "create":
            p.add_argument("--keys", required=True)
            p.add_argument("--lobby", required=True)
            p.add_argument("--bundle", default="micro_3_0")
            p.add_argument("--allow-bigger", action="store_true")
            p.add_argument("--zone", default="us-east-2a")
            p.add_argument("--key-pair", default="openbfme-relay-deck")
        if name == "destroy":
            p.add_argument("--discard-logs", action="store_true")
            p.add_argument("--allow-live", action="store_true")
    a = ap.parse_args(argv)
    ls = ls or Lightsail(log=lambda s: print(s, flush=True))
    try:
        if a.cmd == "create":
            create(ls, a)
        elif a.cmd == "verify":
            verify(ls, a)
        elif a.cmd == "destroy":
            destroy(ls, a)
        else:
            status(ls)
        return 0
    except RelayError as e:
        print(f"aws_relay: {a.cmd} FAILED: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
