#!/bin/bash
# Lane SOLO-3 (the owner, 2026-10-30: "online able to start with one player"): one human and a medium AI play an online custom game through a
# local lobby (services/online/lobby/src/server.mjs: SOLO-1 lets a custom game start with one human) and a local relay; at frame 600 the human
# forfeits through the quit menu and the score screen opens (saved as OUT/solo-score.png with a display). Passes when the game started, the score
# screen opened and the game was recorded as an online game that is not reported (a solo result is nobody's statistics, docs/PLATFORM_API.md).
# Nothing touches a live service; HOME is a fresh directory.
#   tools/net/solo3_score.sh <out dir> [WxH]   (from the worktree; B=<build dir> with relay/openbfme_relay; ROTWK_INSTALL / BFME2_INSTALL; node;
#                                               DISPLAY for the picture, else headless)
set -u
WT=$(pwd)
OUT=${1:?usage: solo3_score.sh <out dir> [WxH]}
RES=${2:-1280x800}
RELAY=${RELAY:-${B:-}/relay/openbfme_relay}
GODOT=${GODOT:-godot}
[ -x "$RELAY" ] || { echo "RESULT solo3: FAIL (no relay at $RELAY)"; exit 1; }
command -v node > /dev/null || { echo "RESULT solo3: FAIL (no node)"; exit 1; }
mkdir -p "$OUT"
port() { python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.$1);s.bind(('127.0.0.1',0));print(s.getsockname()[1])"; }
LPORT=$(port SOCK_STREAM); RPORT=$(port SOCK_DGRAM)
D=$(mktemp -d /tmp/openbfme-solo3.XXXX)
mkdir -p "$D/data" "$D/home" "$D/relaylog"
KEY=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
printf '1:%s\n' $KEY > "$D/keys.txt"
"$RELAY" --bind 127.0.0.1:$RPORT --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/rs.json" --stats-s 0 --idle-s 120 > "$OUT/relay.log" 2>&1 & RPID=$!
LOBBY_BIND=127.0.0.1:$LPORT LOBBY_DATA="$D/data" TICKET_KEYS="1:$KEY" SESSION_KEY=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf RELAYS="local=127.0.0.1:$RPORT" \
  node "$WT/services/online/lobby/src/server.mjs" > "$OUT/lobby.log" 2>&1 & LPID=$!
sleep 2
HEADLESS=(); [ -n "${DISPLAY:-}" ] || HEADLESS=(--headless)
cd "$WT/godot"
HOME="$D/home" XDG_DATA_HOME="$D/home/.local/share" timeout 600 "$GODOT" "${HEADLESS[@]}" --audio-driver Dummy --path . -- --online-host \
  --online-name=Solo --online-lobby=http://127.0.0.1:$LPORT --online-players=1 --online-slots="player:-1:0:0;medium:-1:1:1" --net-script \
  --net-score-shot="$OUT/solo-score.png:600" --movies=off --no-intro --no-update-check --res=$RES > "$OUT/client.log" 2>&1 &
GP=$!
# the score screen records the game, then the forfeit flow saves the picture and prints its line
for _ in $(seq 1 280); do grep -aq "^GAME NET score shot" "$OUT/client.log" 2>/dev/null && break; kill -0 $GP 2>/dev/null || break; sleep 2; done
sleep 1
kill $GP $LPID $RPID 2>/dev/null; wait 2>/dev/null
rm -rf "$D"
started=$(grep -ac "^GAME ONLINE start: " "$OUT/client.log")
score=$(grep -a "^GAME NET score shot" "$OUT/client.log" | grep -c "score screen true")
record=$(grep -aE "^GAME RECORD (error|[A-Za-z0-9_-]+ \()" "$OUT/client.log" | head -1)
echo "solo3: started $started, score screen $score, record: ${record:-none}"
if [ "$started" -ge 1 ] && [ "$score" -ge 1 ] && echo "$record" | grep -q "(online): not reported"; then echo "RESULT solo3: PASS"; exit 0; fi
echo "RESULT solo3: FAIL"; exit 1
