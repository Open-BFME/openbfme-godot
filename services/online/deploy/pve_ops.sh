#!/bin/bash
# Installs the online service's backup and monitor on a home-lab host (lane INFRA-2; default bfme-pve) as systemd --user timers:
#   openbfme-lobby-backup.timer  daily 04:30 UTC: lobby_backup.py pull into ~/openbfme-backups/lobby (14 days), log ~/openbfme-backups/backup.log
#   openbfme-monitor.timer       every 10 minutes: monitor.py, one line in ~/openbfme-monitor/status.log
# The scripts go to ~/openbfme-ops/. The backup needs the admin key there first (backup_keys.sh push). Needs linger for the user (loginctl
# enable-linger) so the timers run without a login; the script checks it. Runs both services once at the end and prints their lines.
#   services/online/deploy/pve_ops.sh [host]
set -euo pipefail
HOST="${1:-bfme-pve}"
D="$(cd "$(dirname "$0")" && pwd)"
ssh "$HOST" 'set -e; [ "$(loginctl show-user "$USER" -p Linger --value)" = yes ] || { echo "pve_ops: linger is off for $USER (loginctl enable-linger)" >&2; exit 1; }
  [ -f ~/openbfme-keys/online-secrets/admin.key ] || { echo "pve_ops: no admin key: run backup_keys.sh push first" >&2; exit 1; }
  umask 077; mkdir -p ~/openbfme-ops ~/openbfme-backups/lobby ~/openbfme-monitor ~/.config/systemd/user'
scp -q "$D/lobby_backup.py" "$D/monitor.py" "$HOST:openbfme-ops/"
scp -q "$D/openbfme-lobby-backup.service" "$D/openbfme-lobby-backup.timer" "$D/openbfme-monitor.service" "$D/openbfme-monitor.timer" "$HOST:.config/systemd/user/"
ssh "$HOST" 'set -e; systemctl --user daemon-reload
  systemctl --user enable --now openbfme-lobby-backup.timer openbfme-monitor.timer
  systemctl --user start openbfme-lobby-backup.service; tail -1 ~/openbfme-backups/backup.log
  systemctl --user start openbfme-monitor.service || true; tail -1 ~/openbfme-monitor/status.log
  systemctl --user list-timers --no-pager | grep openbfme-'
