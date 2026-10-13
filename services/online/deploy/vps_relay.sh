#!/bin/bash
# Installs the relay on a Linux VPS over ssh (Debian / Ubuntu; the ssh user needs sudo): copies the relay's source (and the two engine files it shares),
# builds it there, installs /opt/openbfme, the openbfme user, /etc/openbfme/ticket.keys + upload.key, the systemd units, opens UDP PORT in ufw when ufw
# is active, and starts it.
#   services/online/deploy/vps_relay.sh <ssh host> <secrets dir> <lobby public URL> [udp port=7350]
# RELAY_SSH_OPTS: extra ssh / scp options (INFRA-2: aws_relay.py passes the relay key and its own known_hosts file). A fresh instance is fine: it
# waits for cloud-init and refreshes the package lists first.
set -euo pipefail
read -r -a SO <<< "${RELAY_SSH_OPTS:-}"
HOST="$1"; SECRETS="$2"; LOBBY="$3"; PORT="${4:-7350}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mkdir -p "$T/src/services/online" "$T/src/engine/src/GameNetwork" "$T/src/engine/src/Common"
cp -r "$ROOT/services/online/relay" "$ROOT/services/online/deploy" "$T/src/services/online/"
cp "$ROOT/engine/src/GameNetwork/RelayProtocol."{h,cpp} "$T/src/engine/src/GameNetwork/"
cp "$ROOT/engine/src/Common/Sha256."{h,cpp} "$ROOT/engine/src/Common/Crc32.h" "$T/src/engine/src/Common/"
tar -C "$T/src" -czf "$T/relay.tgz" .
scp -q "${SO[@]}" "$T/relay.tgz" "$SECRETS/ticket.keys" "$SECRETS/upload.key" "$HOST:/tmp/"
ssh "${SO[@]}" "$HOST" "PORT=$PORT LOBBY=$LOBBY bash -s" <<'REMOTE'
set -euo pipefail
if command -v cloud-init > /dev/null; then sudo cloud-init status --wait > /dev/null || true; fi  # its own apt run holds the lock on a new instance
sudo apt-get update -qq
sudo apt-get install -y -qq cmake g++ python3 > /dev/null
rm -rf /tmp/openbfme-relay-src && mkdir -p /tmp/openbfme-relay-src && tar -C /tmp/openbfme-relay-src -xzf /tmp/relay.tgz
cmake -S /tmp/openbfme-relay-src/services/online/relay -B /tmp/openbfme-relay-build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build /tmp/openbfme-relay-build -j 2 > /dev/null
id openbfme > /dev/null 2>&1 || sudo useradd --system --home /var/lib/openbfme --shell /usr/sbin/nologin openbfme
sudo install -d -m 755 /opt/openbfme/bin /opt/openbfme/relay
sudo install -d -m 750 -o root -g openbfme /etc/openbfme
sudo install -m 755 /tmp/openbfme-relay-build/openbfme_relay /opt/openbfme/bin/
sudo install -m 644 /tmp/openbfme-relay-src/services/online/relay/upload_logs.py /opt/openbfme/relay/
sudo install -m 640 -o root -g openbfme /tmp/ticket.keys /tmp/upload.key /etc/openbfme/
rm -f /tmp/ticket.keys /tmp/upload.key /tmp/relay.tgz
# INFRA-2 r2: the uploader posts its status under the relay's public address (the instance's outbound address: its static IP on Lightsail)
RELAY_ID="$(curl -fsS --max-time 15 https://checkip.amazonaws.com | tr -d '[:space:]'):$PORT"
[[ "$RELAY_ID" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+:[0-9]+$ ]] || { echo "vps_relay: cannot learn the public address ($RELAY_ID)" >&2; exit 1; }
printf 'PUBLIC_URL=%s\nRELAY_ID=%s\n' "$LOBBY" "$RELAY_ID" | sudo tee /etc/openbfme/lobby.env > /dev/null
sed "s/0.0.0.0:7350/0.0.0.0:$PORT/" /tmp/openbfme-relay-src/services/online/deploy/openbfme-relay.service | sudo tee /etc/systemd/system/openbfme-relay.service > /dev/null
sudo install -m 644 /tmp/openbfme-relay-src/services/online/deploy/openbfme-relay-upload.{service,timer} /etc/systemd/system/
if command -v ufw > /dev/null && sudo ufw status | grep -q active; then sudo ufw allow "$PORT/udp"; fi
sudo systemctl daemon-reload
sudo systemctl enable --now openbfme-relay.service openbfme-relay-upload.timer
sudo systemctl restart openbfme-relay.service
sleep 1; systemctl --no-pager status openbfme-relay.service | head -5
REMOTE
