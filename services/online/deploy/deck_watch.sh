#!/bin/bash
# Watches the watcher (lane INFRA-2 r2, Sol r1: nothing noticed if bfme-pve's monitor itself stopped). From the Deck, every 10 minutes (a systemd
# --user timer), one line per run in the coordinator's logs folder (~/.cache/openbfme-recover/logs/infra-monitor-watch.log):
#   <Deck UTC time> MONITOR OK: last OK line ... || <pve's newest status line>
#   <Deck UTC time> MONITOR STALE: the last OK line (...) is 47 min old; ... || ...     (the monitor, its timer or every check failing for 30 min)
#   <Deck UTC time> MONITOR UNREACHABLE: ssh bfme-pve failed (exit 255)                 (pve down, or the Deck's network)
#   deck_watch.sh install   copy this script to ~/.local/share/openbfme-ops/ and enable openbfme-monitor-watch.timer (the worktree may go away)
#   deck_watch.sh run       one check (what the timer runs)
set -uo pipefail
HOST="${WATCH_HOST:-bfme-pve}"
LOG="${WATCH_LOG:-$HOME/.cache/openbfme-recover/logs/infra-monitor-watch.log}"
case "${1:-}" in
  run)
    now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    out="$(ssh -o BatchMode=yes -o ConnectTimeout=20 "$HOST" 'python3 ~/openbfme-ops/monitor.py stale --log ~/openbfme-monitor/status.log; echo "|| $(tail -1 ~/openbfme-monitor/status.log 2>/dev/null)"' < /dev/null 2>&1)"
    rc=$?
    if [ $rc = 255 ]; then line="MONITOR UNREACHABLE: ssh $HOST failed (exit 255): $(tail -1 <<< "$out" | cut -c1-200)"
    else line="$(tr '\n' ' ' <<< "$out" | sed 's/ *$//')"; fi
    mkdir -p "$(dirname "$LOG")"
    echo "$now $line" >> "$LOG"
    echo "$now $line"
    grep -q "^MONITOR OK" <<< "$line"
    ;;
  install)
    D="$HOME/.local/share/openbfme-ops"; U="$HOME/.config/systemd/user"
    mkdir -p "$D" "$U"
    install -m 755 "$0" "$D/deck_watch.sh"
    cat > "$U/openbfme-monitor-watch.service" <<UNIT
[Unit]
Description=OpenBFME: is bfme-pve's online monitor alive (INFRA-2)

[Service]
Type=oneshot
ExecStart=$D/deck_watch.sh run
UNIT
    cat > "$U/openbfme-monitor-watch.timer" <<UNIT
[Unit]
Description=OpenBFME monitor watch, every 10 minutes

[Timer]
OnCalendar=*:5/10
Persistent=true

[Install]
WantedBy=timers.target
UNIT
    systemctl --user daemon-reload
    systemctl --user enable --now openbfme-monitor-watch.timer
    systemctl --user start openbfme-monitor-watch.service || true
    tail -1 "$LOG"
    ;;
  *) echo "usage: deck_watch.sh install | run" >&2; exit 2 ;;
esac
