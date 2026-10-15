# Deploying the OpenBFME online service

The online service has two parts (docs/ONLINE.md, phase O1):

- **The lobby** (`lobby/`): sign-in, the game list, each game's slots, ready
  flags and chat. It builds the game's START and signs one relay ticket per
  player. It also stores the relay's command logs, from which replays are
  made. The lobby is a JSON-over-HTTP service, version 1. The same code
  (`lobby/src/lobby.mjs`) runs in two places:
  - on **Cloudflare**, as a Worker with one Durable Object
    (`lobby/src/worker.mjs`). The relay logs go in the Durable Object's own
    SQLite storage (Workers Free plan) or in R2 (`LOG_STORE`, below);
  - **self-hosted**, as a plain Node 18+ server (`lobby/src/server.mjs`).
    It needs no packages.
- **The relay** (`relay/`): one C++ process, one thread and one UDP port. It
  forwards each player's turns to the others and checks every packet against
  the session its ticket bound. A bound slot moves to another address only
  with the session token the relay gave its first address. Packets the
  receiving game would refuse are dropped. It writes a command log per game.
  `relay/upload_logs.py` sends the finished logs to the lobby.

Nothing secret is in this repository. The four secrets are files that
`deploy/make_keys.sh` writes. Keep them off git; `services/online/.gitignore`
refuses them here.

| File | Holds | Who needs it |
|---|---|---|
| `ticket.keys` | the relay ticket key ring (`1:<64 hex>` lines) | the lobby and **every** relay (the same file) |
| `session.key` | the key of the lobby's sign-in tokens | the lobby |
| `upload.key` | the key the relay signs its log uploads with | the lobby and every relay |
| `admin.key` | the key of the lobby's backup export / import (INFRA-2) | the lobby and the backup host |

Sign-in is guest-only for now. Discord sign-in was taken out in round 2 of
the review: it had no way back into the game yet.

Reconnect (lane ONLINE-3, docs/ONLINE.md "Phase O3"): the relay and the lobby
of this version speak relay envelope version 3 and write relay log version 2,
so redeploy both together and update the players' game with them (an older
game is refused with `relay protocol version`). The relay must keep its log
directory (`--log-dir`): a rejoining player reads the running game's log from
it. Without a log the relay refuses the rejoin. New endpoints of the lobby:
`GET /v1/mine` (the player's running games) and `POST /v1/games/<id>/rejoin`
(a fresh START for the player's own slot, with a rejoin ticket). The relay's
`--silent-ms N` (default 3000) is how long a player must be silent before the
relay tells the others.

Spectators (lane ONLINE-3, round 2): the lobby's `POST /v1/games/<id>/spectate`
gives an account that is not a player a spectator START with a version 3
ticket (a spectator index and the host's `setup.spectatorDelay`, 0..600 s).
The relay serves a spectator the game's log (held back by the delay), refuses
anything else it sends, and counts them in its status JSON
(`spectatorsBound`, `spectatorDataRefused`, `spectatorsDropped`,
`spectatorLogBytes`). It needs the log directory, like a rejoin. Redeploy the
relay and the lobby together.

## The ports

| Service | Port | Exposed to players |
|---|---|---|
| relay | UDP 7350 (any) | yes: the address in the lobby's `RELAYS` must be reachable from the internet |
| lobby (self-hosted) | TCP 8787 on 127.0.0.1 | through a TLS reverse proxy (Caddy, nginx) at `PUBLIC_URL` |
| lobby (Cloudflare) | HTTPS on Cloudflare's edge | yes |

Players' game clients talk to the lobby over HTTP(S) and to the relay over
UDP. Nothing else is needed: no port forwarding on the players' side.

## The live setup: Cloudflare Workers Free plan (lane CFDEPLOY-1, 2026-10-12)

The owner's Cloudflare account (openbfme.com) runs **Free plans only**. Never
upgrade a plan, enable R2 or another paid product, or change billing. The
budget alerts only send email; they do not cap spending. On the Free plan,
going past a quota makes requests fail (error 1027) instead of costing money.

| What | Where |
|---|---|
| Worker `openbfme-lobby` (`worker.mjs` + `lobby.mjs`, ES modules) | account `f612ec3ed7251edb9a355f9b1b39e922` |
| Durable Object class `LobbyObject`, binding `LOBBY`, SQLite-backed (migration `v1`, `new_sqlite_classes`) | in the Worker |
| Custom domain `play.openbfme.com` (Cloudflare creates its proxied DNS record) | zone openbfme.com |
| vars: `RELAYS` = `us-east=3.13.126.208:7350`, `PUBLIC_URL` = `https://play.openbfme.com`, `LOG_STORE` = `durable-object` | Worker bindings |
| secrets: `TICKET_KEYS`, `SESSION_KEY`, `UPLOAD_KEY`, `ADMIN_KEY` | Worker secret bindings; the readable copies are `~/.openbfme/online-secrets` on the Deck (mode 0700) and the home-lab backups (below) |
| relay `openbfme-relay-1`: AWS Lightsail `micro_3_0` (Ubuntu 24.04, us-east-2a), static IP `openbfme-relay-ip` = 3.13.126.208, UDP 7350 from anywhere, TCP 22 from the owner's home address only | AWS project "Sky is the Limit" (account 425820477456, us-east-2), Lightsail key pair `openbfme-relay-deck` (`~/.ssh/openbfme_relay_ed25519` on the Deck, ssh alias `openbfme-relay`) |
| the daily lobby backup and the 10-minute monitor | bfme-pve, systemd `--user` timers (below) |

**Status (2026-10-13): live.** The lobby went live on 2026-10-12 (coordinator)
and was redeployed by INFRA-2 with `ADMIN_KEY`; the relay was reinstalled with
the log directory and uploader fixes found by the recovery drill. The
owner's launcher reads the Cloudflare tokens from Windows Credential Manager,
which is not available in an ssh logon session (`CredRead` error 1312, "A
specified logon session does not exist"), so a deploy runs in the owner's own
Windows session: `deploy/jpc_session_run.sh` (below) does that from the Deck.

DEPLOY-2 (2026-10-13) put ONLINE-3 / ONLINE-4 live: the lobby's `/v1/mine`,
`/rejoin`, `/spectate`, `/v1/motd` and `?running=1`, and the relay's envelope
version 3 (rejoin and spectator tickets) and log version 2. The relay was
reinstalled in place with `vps_relay.sh` over ssh (the `aws_relay.py`
options, the kept host key), because the AWS login had expired; nothing in
Lightsail changed. Clients built before ONLINE-3 speak envelope 2 and are
refused by this relay (`REFUSED reason 5`), and the monitor probes with
version 3.

### Free plan limits the lobby is built for

From developers.cloudflare.com, read on 2026-10-12:

- Workers: 100,000 requests a day, 10 ms CPU per request.
- Durable Objects: 100,000 requests and 13,000 GB-s a day, 5 M rows read and
  100,000 rows written a day, and 5 GB of SQL storage per account.
- Durable Object SQLite: a string, BLOB or row is at most 2 MB.

The lobby polls once a second per player, so 100,000 requests a day is about
28 player-hours. That is enough for testing; it is not enough for public play.

How the relay logs fit in SQLite (`worker.mjs`, `SqliteLogStore`):

- A log is stored as 1 MiB parts, one row each.
- A log is at most **8 MiB**. A bigger upload is refused with `413 too_big`.
  ONLINE-2 measured about 90 KB per game.
- All logs together are at most **1 GiB**. Past that, uploads are refused with
  `507 log_store_full`. Nothing is evicted: clean the store up or move to R2
  before it fills.
- With `LOG_STORE` unset there is no store. Uploads and replay downloads then
  fail with `501 no_log_store`.
- With `RELAYS` empty, `/v1/health` says `"relay": "no relay configured"` and
  START is refused with `503 no_relay`.

How uploads and downloads are protected:

- **Uploads.** A log is stored only under the game named in its own header
  (`409 log_game_mismatch` otherwise), and only for a game this lobby started
  (`404 unknown_game`). A stored log is never replaced: different bytes get
  `409 log_exists`, and the same bytes (the uploader retrying) get
  `200 unchanged`.
- **Downloads.** Replays are not public. `/v1/replays/<id>/log` and `/start`
  need the session token of one of that game's players (`401`, or
  `403 not_a_player`).
- **Request bodies.** A body is read before it joins the queue, with a
  per-chunk deadline (`408`), at most 4,096 chunks (`413 too_many_chunks`) and
  a byte limit (`413 too_big`). At most 2 log uploads and 64 bodies of any
  kind are held at once, from the first byte until the response is built
  (queue wait included); more get `503 overloaded` with `Retry-After: 5`.

### Deploying and redeploying

`deploy/cloudflare_api.py` talks to the REST API using only Python's standard
library. Nothing is installed: no wrangler, no npm. It does the following, in
order:

1. Checks the plan. The Free plan is the **owner's verified fact**, from their
   handoff of 2026-10-12. The tokens deliberately can't read billing, so the
   script cannot prove the plan is Free. It does two things instead:
   - It tries to read every page of the account's subscriptions. A Workers
     subscription that isn't Free, on any page it managed to read, stops it
     before anything changes, even if a later page then fails. If the read
     fails or is incomplete (expected without Billing Read), it says so and
     relies on the owner's fact. It never reports an unreadable or partial
     answer as "free".
   - It refuses to upload anything the Free plan doesn't have. The only
     bindings allowed are the SQLite Durable Object (`new_sqlite_classes`),
     plain-text vars and secrets: no R2, Queues, KV, D1, other Durable Object
     kinds, `keep_bindings` or usage model.
2. Reads the three secrets from **stdin**: never from the command line or a
   file.
3. Uploads the Worker. The DO migration is sent only on the first deploy.
4. Attaches the custom domain.
5. Polls `/v1/health` until it is healthy: version 1, `"ok": true`, the
   configured relays and log store. For now, "no relay configured" is the only
   degraded answer accepted. Any other answer fails the deploy after 30 tries.

Nothing secret is printed. A failed API call shows only Cloudflare's numeric
error codes: messages can reflect what was sent, so they are not printed at
all, and a response body never is. Everything printed is also passed through a
redaction of the token, the secrets and long hex or base64 runs.

`check` runs only step 1 and then prints the Worker, its domain and the
health check.

**From the Deck, one command** (INFRA-2). Run the lobby's tests first
(`node --test services/online/lobby/test/lobby.test.mjs`, `python3 -m pytest -q
tools/net`), then:

```
services/online/deploy/jpc_session_run.sh deploy --relays=us-east=3.13.126.208:7350
services/online/deploy/jpc_session_run.sh check
```

It needs the owner logged in on JonathanPC (it reports after 10 minutes when
the task never ran). What it does:

1. Makes `%USERPROFILE%\.openbfme\deploy-tmp` with an ACL for the owner only.
   It refuses if the folder exists already.
2. Copies in the bundle: `worker.mjs`, `lobby.mjs`, `cloudflare_api.py`,
   `jpc_session_task.py` and a `job.json` (scp).
3. Pipes `pack-secrets` over ssh stdin into `deploy-tmp\secrets.pack`. It
   compares the SHA-256 on both sides and prints neither.
4. Registers a one-off scheduled task, `OpenBFME-Deploy-<time>`: Interactive
   logon, the owner, limited rights (no UAC), a hidden PowerShell window. The
   task runs `jpc_session_task.py`, which runs `cloudflare_api.py` through
   `infra-run.py cloudflare-workers`. It writes `infra\deploy-out.txt`,
   deletes `deploy-tmp` and writes `=== done exit N`.
5. Prints that output. Then, on every path (an EXIT trap set before anything
   is touched), it stops the task if it still runs, along with the bundle's
   own processes, found by their command line. It unregisters the task and
   deletes `deploy-tmp` if this run made it, never another run's. Then it
   **verifies** that both are gone. A cleanup it cannot verify is exit 4,
   with what is left printed (INFRA-2 r2).

A redeploy takes about 15 s. Cloudflare can serve the old Worker version for
up to a minute after it: `/v1/health` already answers, so check a new route
before relying on it.

By hand, the same thing: the secrets go through a pipe straight from the Deck
into the launcher's child process. Run this from a cmd.exe (not PowerShell:
PowerShell's pipe adds a BOM, which `cloudflare_api.py` strips since INFRA-2)
**in the owner's session** on JonathanPC. It needs ssh from JonathanPC to the Deck, and `<deck>` is that
ssh host:

```
$T = "$env:USERPROFILE\.openbfme\deploy-tmp"
# copy lobby/src/worker.mjs, lobby/src/lobby.mjs and deploy/cloudflare_api.py of the commit to deploy into $T first (scp from the Deck)
ssh <deck> "python3 <worktree>/services/online/deploy/cloudflare_api.py pack-secrets ~/.openbfme/online-secrets" |
  python "$env:USERPROFILE\.openbfme\infra\infra-run.py" cloudflare-workers -- python "$T\cloudflare_api.py" deploy `
    --src $T --hostname play.openbfme.com --public-url https://play.openbfme.com --relays=
Remove-Item -Recurse $T
```

Use `--relays=`, not `--relays ""`: Windows PowerShell drops empty
arguments to programs. Once a relay is running, redeploy with
`--relays=us-east=<relay IPv4>:7350` (the secrets are sent again with every
upload).

Then check from anywhere:

```
curl https://play.openbfme.com/v1/health
python3 tools/net/online_client.py smoke --lobby https://play.openbfme.com --profile <md5> --map "maps/map mp evendim/map mp evendim.map" --expect-no-relay
```

`smoke` uses two guests to sign in, create a game, list it, join it, chat,
try to START (refused with no_relay) and leave. It prints one ok / FAIL line
per check.

Cost: $0 on the Free plan. Neither token can read the account's usage or
billing pages, so check them in the dashboard (Workers & Pages > Plans,
Billing).

### The relay on AWS Lightsail (`deploy/aws_relay.py`, INFRA-2)

`aws_relay.py` runs on the Deck and calls the AWS CLI through the owner's
launcher on JonathanPC (`infra-run.py aws --` over ssh; `aws login --profile
openbfme` sessions last 12 hours and renew for 90 days). Every run first checks
`sts get-caller-identity` and refuses any account but 425820477456. It only
touches Lightsail instances, static IPs and key pairs in us-east-2, and it
refuses bundles above `micro_3_0` without `--allow-bigger`.

```
# (re)install the live relay: each step only when missing; the relay itself is rebuilt from this tree by vps_relay.sh
python3 services/online/deploy/aws_relay.py create --name openbfme-relay-1 --static-ip openbfme-relay-ip \
    --keys ~/.openbfme/online-secrets --lobby https://play.openbfme.com
python3 services/online/deploy/aws_relay.py verify --name openbfme-relay-1 --static-ip openbfme-relay-ip
python3 services/online/deploy/aws_relay.py status          # every instance / static IP and the monthly list price
python3 services/online/deploy/aws_relay.py destroy --name N --static-ip IPNAME [--discard-logs]   # never the live one without --allow-live
```

`create` makes each of these only when it is missing: the key pair (imported
from `~/.ssh/openbfme_relay_ed25519.pub`), the instance (Ubuntu 24.04), the
static IP, then the firewall. The firewall ends up exactly UDP 7350 from
anywhere and TCP 22 from the Deck's public address (`--ssh-cidr auto`, from
checkip.amazonaws.com). Then it runs `vps_relay.sh`, which builds the relay on
the instance and installs `ticket.keys` + `upload.key`. Last, it verifies:
running, the IP attached, the firewall exact, and a UDP probe answered
NOT_BOUND. On the live relay it takes about 55 s; from nothing, about 110 s.

`verify` is read-only. `destroy` first stops the relay, which closes its open
games' logs. Then it runs the uploader once and refuses while logs are left
that the lobby has not taken. Then it releases the static IP and deletes the
instance. The key pair stays: the live relay uses it. When the logs cannot be
checked (the instance is not running, or its static IP is gone), `destroy`
refuses without `--discard-logs` (INFRA-2 r2). The relay's ssh host key is
kept in `~/.ssh/openbfme_relays_known_hosts`. It is forgotten only for an
instance that `create` made in the same run; on a reinstall, a changed key is
refused by ssh. The key pair is imported as the OpenSSH line itself. Checked
live: Lightsail accepts Ed25519 this way, and refuses the line base64-encoded.

The relay is **stateless** apart from its keys (`/etc/openbfme`, copies of
the Deck's) and the command logs the uploader has not sent yet
(`/var/lib/openbfme/relay-logs`). The uploader runs every minute and moves a
stored log to `uploaded/` (kept 14 days). A refused upload stays and is
retried, so nothing is lost while the lobby is down. A reinstall in place
(`create` on the same names) keeps that directory, and the timer starts again
on boot. A replaced instance loses only what `destroy` would have refused to
discard. So there are no Lightsail snapshots: a snapshot would cost about
$0.05/GB-month and hold nothing the rebuild doesn't make.

Two faults found and fixed by the drill on 2026-10-13:

- `/var/lib/openbfme/relay-logs` did not exist: the unit's `StateDirectory`
  made only its parent, and the relay does not create its `--log-dir`. Every
  game ran without a command log.
- Cloudflare's browser integrity check answers Python's default User-Agent
  with 403 (error 1010). Every log upload was refused.

## Backups and disaster recovery (lane INFRA-2, 2026-10-13)

### What is backed up where

| What | Backup | How it is checked |
|---|---|---|
| The four keys (`~/.openbfme/online-secrets`) and the relay's ssh key pair (`~/.ssh/openbfme_relay_ed25519{,.pub}`) | `~/openbfme-keys/{online-secrets,ssh}` on **bfme-nas** and **bfme-pve**: directories 0700, files 0600 (`deploy/backup_keys.sh push`) | `backup_keys.sh verify` compares each file's SHA-256 and mode with the Deck's. Hashes are compared, never printed. |
| The lobby's data: the relay command logs, replay START records and player lists in the Durable Object | `~/openbfme-backups/lobby/<UTC date>/` on bfme-pve, daily at 04:30 UTC, the 14 newest dates kept (`openbfme-lobby-backup.timer`, `deploy/lobby_backup.py pull` through the signed admin API) | each snapshot has a `manifest.json` with every blob's size and SHA-256; `lobby_backup.py verify --snapshot DIR`; the monitor fails when the newest snapshot is over 26 h old |
| The relay | nothing to back up: it is rebuilt from this repository and the keys (`aws_relay.py create`) | the monitor's UDP probe |
| The lobby's open games and sign-in sessions | not backed up, by design: open games expire after 10 idle minutes, started ones after 3 h; sessions are signed tokens, nothing is stored | |

**Why file modes, not encryption.** An encrypted copy is only as safe as its
key, and that key would have to survive the loss of the Deck, which is the
very case the backup is for. A key kept on the same host as the copy adds
nothing. A passphrase only the owner knows would work, but it is the owner's
decision: nobody else could run a restore then. So each host keeps its copy
readable by its single ssh user (`claude`) only. Both are LAN-only VMs on the
owner's own hardware, nothing on them is exposed to the internet, and the
copies stay out of every repository. If the owner wants encryption at rest
later, `backup_keys.sh` is the one place to add an `age` / `gpg` step with
their key.

The admin API (`lobby.mjs`, "the admin backup"): `GET /v1/admin/blobs?after=&limit=`
lists the stored blobs, `GET /v1/admin/blobs/<key>` exports one, and
`POST /v1/admin/blobs/<key>` imports one. Every request carries
`X-OpenBFME-Admin: <unix s>.<hex HMAC-SHA256(admin.key, "<METHOD> <path>?<query>\n<s>\n" + body)>`.
The signature must be within 300 s of the lobby's clock. A replay inside that
window can only repeat a read or an identical import. An import never replaces
a stored blob: the same bytes answer `unchanged`, different ones
`409 blob_exists`. A log must name its own game in its header. The admin key
is its own secret, not the relays' upload key, so a relay host can store logs
but never read them.

### The monitor

`openbfme-monitor.timer` on bfme-pve runs `deploy/monitor.py` every 10 minutes.
Each run appends one line to `~/openbfme-monitor/status.log`:

```
2026-10-13T04:44:28Z OK lobby ok games=5 relays=3.13.126.208:7350 store=durable-object (65 ms) | relay 3.13.126.208:7350 NOT_BOUND (42 ms) | uploads ok pending 0 running 0 (reported 6 s ago) | backup 2026-10-13 15 blobs (0.0 h old, 1 kept)
```

Any failed check turns the line into `FAIL` and names the check. It catches:

- the lobby down or unreachable: DNS, TLS, a Worker error, the Free plan's
  daily quota (error 1027);
- an unhealthy answer;
- a lobby redeployed without the relay, or with another relay address;
- a lost log store;
- the relay down, its instance stopped, the firewall closed, or the static IP
  moved: the probe gets no answer;
- a relay on another envelope version: `REFUSED` (update `--envelope` with a
  protocol change);
- the relay's logs not reaching the lobby (INFRA-2 r2). After every run (once
  a minute) the relay's uploader posts its status to `POST /v1/relay/status`,
  signed with the upload key and time-bound. It reports the logs waiting and
  the oldest one's age, this run's uploads and failures, and the relay's own
  `logErrors` counter. The monitor reads it through the admin API
  (`GET /v1/admin/relay-status`) and fails when:
  - the uploader never reported, or has been silent for 10 minutes;
  - an upload failed;
  - a log has waited more than 15 minutes;
  - the relay's status file is unreadable (the relay is down);
  - `logErrors` is not 0: a game the relay could not log, the drill's fault.
    This stays until the relay restarts;
- a daily backup that stopped working, or a malformed manifest.

A check that crashes is a `FAIL` naming the exception; the line is never
missing.

**Who watches the monitor.** On the Deck, `openbfme-monitor-watch.timer`
(`deploy/deck_watch.sh install`) runs `monitor.py stale` on bfme-pve every 10
minutes over ssh. It appends one line to
`~/.cache/openbfme-recover/logs/infra-monitor-watch.log`, next to the
coordinator's other logs:

- `MONITOR OK`, with pve's newest status line;
- `MONITOR STALE` when the newest OK line is over 30 minutes old: the monitor
  or its timer stopped, or every run has failed since;
- `MONITOR UNREACHABLE` when ssh to pve fails.

**Not covered** (S-4102):

- gameplay itself: a desync or a game that cannot start shows only in the
  players' reports;
- the AWS bill and the Cloudflare quotas and bill. Check them in the
  dashboards; AWS pauses the project at its $90 limit.

Install or update the backup and the monitor with
`deploy/backup_keys.sh push` (the admin key), then `deploy/pve_ops.sh`.
After a relay address change, edit `--relay` in `deploy/openbfme-monitor.service`
and run `pve_ops.sh` again.

### Recovery

**The lobby is lost** (the Worker or its Durable Object deleted, the data
damaged):

1. `services/online/deploy/jpc_session_run.sh deploy --relays=us-east=3.13.126.208:7350`.
   A deleted Worker is created again with its Durable Object (migration
   `v1`, empty) and the custom domain.
2. On bfme-pve, restore the newest snapshot:
   `python3 ~/openbfme-ops/lobby_backup.py push --lobby https://play.openbfme.com --admin-key-file ~/openbfme-keys/online-secrets/admin.key --snapshot ~/openbfme-backups/lobby/$(ls ~/openbfme-backups/lobby | tail -1)`
   It verifies the snapshot first, then imports. Run it again if it is
   interrupted: blobs already there answer `unchanged`.
3. `jpc_session_run.sh check`, and the next monitor line.

You lose at most the replays of the games played since the last backup (up to
24 h), and the games open at that moment. A relay log of such a game cannot be
uploaded again: its START record is gone, so the lobby answers `unknown_game`.
The Cloudflare tokens expire on **2027-01-10**. The owner renews them in
Credential Manager; until then a deploy fails at the first API call.

**The relay is lost** (the instance deleted or broken):

1. `python3 services/online/deploy/aws_relay.py create --name openbfme-relay-1 --static-ip openbfme-relay-ip --keys ~/.openbfme/online-secrets --lobby https://play.openbfme.com`
   (about 2 minutes from nothing).
2. If the static IP survived, the address is the same and nothing else
   changes. If it was released, `create` allocates a new one. Then redeploy
   the lobby with `--relays=us-east=<new IPv4>:7350` and point the monitor at
   it (above).
3. Games that were running are lost (no reconnect yet, S-3711). Logs it had
   not uploaded are lost with the instance. `destroy` refuses to throw them
   away on purpose.

**The keys are lost or leaked:**

- *The Deck's copy is lost:* `backup_keys.sh restore bfme-pve <new dir>`.
  Move `online-secrets/` to `~/.openbfme/online-secrets` (0700) and `ssh/*`
  to `~/.ssh/`. Then `backup_keys.sh verify`.
- *Leaked, or every copy lost:*
  1. Make new ones in a new directory with `make_keys.sh`.
  2. Install them on the relay first: `aws_relay.py create` (same names: a
     reinstall in place).
  3. Then redeploy the lobby with `jpc_session_run.sh deploy`.
  4. Then `backup_keys.sh push`.

  Every player signs in again: the new session key invalidates the old
  tokens. Stored replays stay. For a leaked ticket key alone, "Rotating a
  ticket key" below avoids the sign-out.
- *The relay ssh key is lost:* the relay is stateless. Make a new pair,
  `destroy` the relay (`--discard-logs` when it cannot be reached) and
  `create` it again with `--ssh-key <new> --key-pair <new name>`. Then delete
  the old key pair in Lightsail.

**The Deck is lost.** The services keep running without it: the lobby is on
Cloudflare, the relay on AWS, and the backup and monitor on bfme-pve. The
Deck is only where deploys start from. On a new machine:

1. Clone the repository.
2. Get ssh access to bfme-pve / bfme-nas (the owner adds the new machine's
   key through the Proxmox / TrueNAS console) and to JonathanPC (the owner
   adds it to `authorized_keys`).
3. `backup_keys.sh restore bfme-pve <dir>` and put the files in place (above).
4. Add the ssh alias `openbfme-relay` (`HostName 3.13.126.208`, `User ubuntu`,
   `IdentityFile ~/.ssh/openbfme_relay_ed25519`).
5. `aws_relay.py status`, `aws_relay.py verify ...` and
   `jpc_session_run.sh check` prove the control path.

The Deck's own ssh keys to the home lab and JonathanPC are not part of this
backup. They are access, not data: the owner grants a new machine its own.

### The recovery drill (2026-10-13)

From nothing, timed on the Deck:

| Step | Time |
|---|---|
| keys restored from bfme-pve's backup (`backup_keys.sh restore`) | 0 s |
| `aws_relay.py create --name openbfme-relay-drill --static-ip openbfme-relay-drill-ip` with the restored keys and ssh key: account check 4 s, instance requested 20 s, running 25 s, static IP 39-44 s, firewall 49 s, ssh 50 s, relay built and installed 100 s, verified (probe NOT_BOUND) 110 s | 123 s |
| a local lobby on the Deck (`server.mjs`, the restored keys, `RELAYS=drill=<its IP>:7350`) hosts a game; `online_client.py` host + join get both STARTs | 3 s |
| two headless `openbfme_peer --online-start ... --script --frames 600` on bfme-nas play through the drill relay: both 600 frames, `desyncs 0`, the same `final_hash 0xC56B128A`, `relay ... bound 1 refusal 0` | 142 s (with the wait for the CI slot) |
| `aws_relay.py destroy --discard-logs`: instance deleted, static IP released, both verified gone | 45 s |
| **total** | **312 s** |

The drill relay's uploader pointed at the Deck's local lobby, which AWS cannot
reach, so its log could never be uploaded and `--discard-logs` was passed.
Even so, `destroy` reported "every log uploaded". The log directory did not
exist, so `find` counted nothing. That is how the missing `relay-logs` fault
above was found; `destroy` now refuses a missing log directory. The drill is
the sequence above. Rerun it after any change to
`aws_relay.py`, `vps_relay.sh` or the relay units.

### The live check (2026-10-13)

The Godot client on the Deck hosted game `0bdb332681add3e2` on
https://play.openbfme.com (`--online-host`). A second player joined it through
the lobby (`online_client.py join`, then `openbfme_peer --online-start`) and
played through the AWS relay. At frame 420 both had state hash `C7726C8B`, with
4 CRC checks passed on each side, 0 desyncs and a relay round trip of about
100 ms.

The relay's log of the game was uploaded to the lobby within a minute and
pulled by `lobby_backup.py`. The pulled log made a replay
(`openbfme_peer --relay-log ... --record`): complete, 945 commands, 0 CRC
disagreements. `--replay` compared 4 hashes with 0 mismatches, and frame 400's
hash `69E765FB` matched the client's own. The video of that game is the lane's
proof (`workspace/rebuild/proof/INFRA-2/infra-live.mp4`).

## Option 1: Cloudflare (Workers Paid, wrangler) lobby + a VPS relay

1. Make the secrets once: `services/online/deploy/make_keys.sh ~/openbfme-secrets`.
2. Relay on a VPS (Debian or Ubuntu, any small plan; docs/ONLINE.md section 2
   has the sizing):
   `services/online/deploy/vps_relay.sh <ssh host> ~/openbfme-secrets https://<lobby URL> 7350`
   This builds the relay on the VPS, installs it as the `openbfme-relay`
   systemd service (user `openbfme`, `/opt/openbfme`, keys in
   `/etc/openbfme`), opens UDP 7350 in ufw when ufw is on, and starts the log
   uploader timer. Open UDP 7350 in the provider's firewall as well.
3. Lobby on Cloudflare:
   ```
   CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=... RELAYS="us-east=<VPS IPv4>:7350" \
   PUBLIC_URL=https://lobby.<domain> SECRETS=~/openbfme-secrets services/online/deploy/cloudflare.sh
   ```
   The token needs "Workers Scripts: Edit" and "Workers R2 Storage: Edit"
   (and "Workers Routes: Edit" for a custom domain). Without a domain, use
   the `*.workers.dev` URL that wrangler prints as `PUBLIC_URL`, and run the
   script again with it.
4. Check: `curl https://lobby.<domain>/v1/health` lists the relay.
   `tools/net/online_client.py` can then host and join a game (below).

## Option 2: everything on one Linux host (home lab, or one VPS)

`services/online/deploy/selfhost.sh <secrets dir> <state dir> <relay public a.b.c.d:port> <lobby public URL> [lobby bind]`

This needs no root. It builds the relay and installs both services and the log
uploader as systemd `--user` units. Run `loginctl enable-linger <user>` once so
they keep running after logout. The lobby listens on the bind address (default
`0.0.0.0:8787`); on the internet, put a TLS proxy in front of it. Node comes
from `PATH`, or from `NODE=/path/to/node`.

Reaching a home lab from the internet needs a router change: forward the
relay's UDP port and the proxy's TCP 443 to the host. A LAN or VPN test needs
neither.

## Rotating a ticket key

Add a line `2:<64 hex>` to `ticket.keys`. Install it on every relay first
(restart them), then on the lobby. The lobby signs with the highest key id;
the relays accept every id in the file. Remove the old line once the tickets
signed with it have expired (6 hours).

## Testing a deployment

```
P=$(openbfme_peer --print-profile | head -1)
python3 tools/net/online_client.py host --lobby $URL --name Host --profile $P --map "maps/map mp fords of isen ii/map mp fords of isen ii.map" \
    --slot player,0,0,0,0 --slot open --players 2 --id-file id.txt --out host.json &
python3 tools/net/online_client.py join --lobby $URL --name Guest --profile $P --game $(cat id.txt) --faction 3 --color 1 --start-pos 1 --team 1 --out guest.json
openbfme_peer --online-start host.json --script --frames 600 --report host.txt &
openbfme_peer --online-start guest.json --script --frames 600 --report guest.txt
```

Both reports must show `relay ... bound 1` and `desyncs 0`, with the same
`final_hash`. The relay's log of the game (`<game id>.obfmerlog`, or
`/v1/replays/<id>/log` once uploaded) becomes a replay with
`openbfme_peer --relay-log LOG --online-start host.json --record server.replay`.
`openbfme_peer --replay server.replay` then checks it against the players'
CRCs. `tools/net/online2_soak.sh` does all of this with a local relay and
simulated loss.

Reconnect, disconnects and pause (lane ONLINE-3): `tools/net/online3_e2e.sh`
plays the scenarios with a local relay (a player killed and rejoined, a Kick
vote, the rejoin window running out, a pause), and
`tools/net/online3_homelab.sh <lobby URL> <out> <scenario>` plays them against
a deployed lobby and relay, with the peers on two hosts.

## What it costs

The live setup costs **$7 a month**: the Lightsail `micro_3_0` relay at list
price (its attached static IP is free; AWS's $100 credit pays it for now). The
Workers Free plan and the home-lab backups cost nothing.
`aws_relay.py status` prints the current total.

See docs/ONLINE.md section 2: about $20-30 a month for the recommended setup
at 1,000 players at peak. This is Workers Paid at $5, plus one or two small
bandwidth-included VPSs. R2 stays inside its free tier at the measured
replay volume.

## Player count: `GET /v1/stats` (lane ONLINECOUNT-1)

New endpoint of the lobby, version 1, no sign-in needed:
`{"v":1,"players":N,"games_open":M,"games_running":K,"as_of":<unix s>}`. Counts only (no names, no addresses). The game's main menu and
online menu and the launcher's Play card show "N players online" from it. Older lobbies answer 404 and the clients show nothing.

**Deploying (the coordinator, Cloudflare access is on the owner's PC):** deploy `lobby/src/lobby.mjs` and `lobby/src/worker.mjs` as usual
(`wrangler deploy`); no new binding, secret, variable or migration. The self-hosted `server.mjs` needs only a restart. Check:
`curl -s https://<lobby>/v1/stats` returns the JSON above with `cache-control: public, max-age=30`.

**What "online" means.** A player counts while the lobby has seen a request with their valid session token in the last 150 s (sign-in, the
online menu's stats poll every 90 s, a staging room's polls, any signed request), or while they hold a human slot of a started game whose relay
log has not been uploaded (the lobby learns a game is over only from that upload; a game whose log never comes counts until the lobby forgets it,
3 h). Players in the main menu or in single player are not counted: the main menu's and the launcher's requests carry no token. Presence is in
memory only (no storage write per request); a restarted lobby counts from zero and recovers within 150 s. The answer is computed at most every 30 s.
Limits: the presence table holds 50,000 sessions; `/v1/stats` is rate limited to 60 a minute (burst 20) per address.

**Free plan budget (100,000 Worker requests a day, all endpoints together).** Cloudflare's docs (developers.cloudflare.com/workers/platform/pricing,
read 2026-10-14): the Worker runs before the cache, so a cached answer (Cache API, or Cache Rules) is still a Worker request; only the Durable Object
request and the CPU are saved. So `worker.mjs` admits every `/v1/stats` request first (a token bucket per address in the Worker isolate: 20 burst,
60 a minute, then 429 with Retry-After; kept per Worker isolate), then answers a token-less one from the data centre's Cache
API for 30 s (no Durable Object request), and the lobby's own limiter stays behind it for requests that reach the object. This is not an aggregate cap and gives no protection for the Worker request budget: separate or recycled isolates and the eviction of old buckets (5,000 addresses) reset it, so
many isolates or addresses pass. Every stats request still counts
against the 100,000. Client polling that keeps it small: the online menu asks when it opens and every 90 s while open (not in a staging room); the
main menu and the launcher ask once when shown and then at most every 5 minutes while visible (not minimized, not in the background; a number
that is not renewed for two intervals is hidden); never in a game; a turned-off setting asks nothing.

| Concurrent players, all day | online menu (stats 40 / h each) | main menu or launcher in view (12 / h each) | staging rooms (the existing held poll, 20 s: 180 / h each; stats add 0) |
|---|---|---|---|
| 10 | 9,600 / day | 2,880 / day each | 43,200 / day |
| 100 | 96,000 / day | 28,800 / day each | 432,000 / day |
| 1,000 | 960,000 / day | 288,000 / day each | 4,320,000 / day |

**Known limit (the owner decides later whether to go paid).** These are 24-hour constant loads; a real day is far lighter. Without this feature the
free plan already runs out at about 23 players constantly in staging rooms (100,000 / (180 x 24) = 23.1); the feature adds nothing there. For players parked in the
online menu it adds 40 requests an hour each (about 104 constantly parked players would use the whole allowance by themselves); a player who opens the
main menu or the launcher for 15 minutes a day costs about 3 requests each. At 1,000 players a day, each opening both once for 15 minutes: about
6,000 requests a day for the feature. If the allowance gets tight, raise `ONLINE_SECONDS` (godot/scripts/online_stats.gd) and `PRESENCE_S` (lobby.mjs)
together, lengthen the staging poll, or move to the paid plan.

**Restarts.** A game's end is kept in its stored record (`finished`, one save when the relay's log arrives), so a restarted lobby does not count a
finished game's players again. Presence is in memory only: a restarted lobby counts the people in lobbies from zero and recovers within 150 s.
