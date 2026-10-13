#!/bin/bash
# Self-hosted lobby + relay on one Linux host without root (a home-lab VM or a VPS user account): builds the relay from this checkout, runs both as
# systemd --user services (they survive logouts with `loginctl enable-linger`), with the secrets from SECRETS (make_keys.sh) and the state in STATE.
#   services/online/deploy/selfhost.sh <secrets dir> <state dir> <relay public address a.b.c.d:port> <lobby public URL> [lobby bind ip:port]
# Needs cmake, a C++20 compiler, node 18+ (NODE=/path/to/node otherwise) and python3. Prints the health check at the end.
set -euo pipefail
SECRETS="$(cd "$1" && pwd)"; STATE="$2"; RELAY_PUBLIC="$3"; PUBLIC_URL="$4"; LOBBY_BIND="${5:-0.0.0.0:8787}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
NODE="${NODE:-$(command -v node)}"
RELAY_PORT="${RELAY_PUBLIC##*:}"
mkdir -p "$STATE"/{relay-logs,lobby,bin}; STATE="$(cd "$STATE" && pwd)"
cmake -S "$HERE/relay" -B "$STATE/relay-build" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$STATE/relay-build" -j 4 > /dev/null
cp "$STATE/relay-build/openbfme_relay" "$STATE/bin/openbfme_relay.new"
mv -f "$STATE/bin/openbfme_relay.new" "$STATE/bin/openbfme_relay"   # a rename: the running relay keeps its file until the restart below
U="$HOME/.config/systemd/user"; mkdir -p "$U"
cat > "$STATE/lobby.env" <<ENV
LOBBY_BIND=$LOBBY_BIND
LOBBY_DATA=$STATE/lobby
TICKET_KEYS_FILE=$SECRETS/ticket.keys
SESSION_KEY_FILE=$SECRETS/session.key
UPLOAD_KEY_FILE=$SECRETS/upload.key
RELAYS=selfhost=$RELAY_PUBLIC
PUBLIC_URL=$PUBLIC_URL
ENV
chmod 600 "$STATE/lobby.env"
cat > "$U/openbfme-relay.service" <<UNIT
[Unit]
Description=OpenBFME online relay (self-hosted)
[Service]
ExecStart=$STATE/bin/openbfme_relay --bind 0.0.0.0:$RELAY_PORT --keys $SECRETS/ticket.keys --log-dir $STATE/relay-logs --status $STATE/relay-status.json
Restart=always
RestartSec=2
[Install]
WantedBy=default.target
UNIT
cat > "$U/openbfme-lobby.service" <<UNIT
[Unit]
Description=OpenBFME online lobby (self-hosted)
[Service]
EnvironmentFile=$STATE/lobby.env
ExecStart=$NODE $HERE/lobby/src/server.mjs
Restart=always
RestartSec=2
[Install]
WantedBy=default.target
UNIT
cat > "$U/openbfme-relay-upload.service" <<UNIT
[Unit]
Description=OpenBFME relay log upload (self-hosted)
[Service]
Type=oneshot
ExecStart=/usr/bin/env python3 $HERE/relay/upload_logs.py --log-dir $STATE/relay-logs --lobby http://${LOBBY_BIND/0.0.0.0/127.0.0.1} --upload-key-file $SECRETS/upload.key
UNIT
cat > "$U/openbfme-relay-upload.timer" <<UNIT
[Unit]
Description=Upload the OpenBFME relay logs every minute
[Timer]
OnBootSec=1min
OnUnitActiveSec=1min
[Install]
WantedBy=timers.target
UNIT
systemctl --user daemon-reload
systemctl --user enable --now openbfme-relay.service openbfme-lobby.service openbfme-relay-upload.timer > /dev/null
systemctl --user restart openbfme-relay.service openbfme-lobby.service
sleep 1
curl -fsS "http://${LOBBY_BIND/0.0.0.0/127.0.0.1}/v1/health" && echo
systemctl --user --no-pager --lines=3 status openbfme-relay.service | head -5
