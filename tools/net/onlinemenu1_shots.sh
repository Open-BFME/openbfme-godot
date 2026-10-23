#!/bin/bash
# Lane ONLINEMENU-1: proof captures of the online screens on a CI host with a display: a local lobby (services/online/lobby/src/server.mjs), a
# local relay (openbfme_relay) and two WINDOWED game clients that walk the screens through the movies' own commands (game.gd --online-walk=host /
# join: sign-in, home, the server browser, Create Game, the staging room's tabs, CONNECTIONS, ready, start). Every screen is saved by the host and
# the joiner as online-<screen>-<W>x<H>.png. Nothing touches a live service; HOME is a fresh directory (no user names on screen).
#   tools/net/onlinemenu1_shots.sh <out dir> [WxH]      (from the worktree; DISPLAY set; RELAY=<openbfme_relay>, default $B/relay/openbfme_relay)
set -u
WT=$(pwd)
OUT=${1:?usage: onlinemenu1_shots.sh <out dir> [WxH]}
RES=${2:-1280x800}
RELAY=${RELAY:-${B:-}/relay/openbfme_relay}
GODOT=${GODOT:-godot}
[ -n "${DISPLAY:-}" ] || { echo "SHOTS: no DISPLAY (the captures need a window)"; exit 2; }
[ -x "$RELAY" ] || { echo "SHOTS: no relay at $RELAY"; exit 2; }
command -v node > /dev/null || { echo "SHOTS: no node"; exit 2; }
mkdir -p "$OUT/shots"
port() { python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.$1);s.bind(('127.0.0.1',0));print(s.getsockname()[1])"; }
LPORT=$(port SOCK_STREAM); RPORT=$(port SOCK_DGRAM)
D=$(mktemp -d /tmp/openbfme-om1-shots.XXXX)
mkdir -p "$D/data" "$D/home-a" "$D/home-b" "$D/relaylog"
KEY=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
printf '1:%s\n' $KEY > "$D/keys.txt"
"$RELAY" --bind 127.0.0.1:$RPORT --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/rs.json" --stats-s 0 --idle-s 120 > "$OUT/relay.log" 2>&1 & RPID=$!
LOBBY_BIND=127.0.0.1:$LPORT LOBBY_DATA="$D/data" TICKET_KEYS="1:$KEY" SESSION_KEY=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf RELAYS="local=127.0.0.1:$RPORT" \
  MOTD="Welcome to the OpenBFME test lobby." node "$WT/services/online/lobby/src/server.mjs" > "$OUT/lobby.log" 2>&1 & LPID=$!
sleep 2
cd "$WT/godot"
COMMON="--online-lobby=http://127.0.0.1:$LPORT --online-shots=$OUT/shots --online-players=2 --net-frames=60 --net-script --movies=off --res=$RES"
HOME="$D/home-a" XDG_DATA_HOME="$D/home-a/.local/share" timeout 600 "$GODOT" --audio-driver Dummy --path . -- --online-walk=host --online-name=Host \
  $COMMON > "$OUT/host.log" 2>&1 & AP=$!
sleep 12
HOME="$D/home-b" XDG_DATA_HOME="$D/home-b/.local/share" timeout 600 "$GODOT" --audio-driver Dummy --path . -- --online-walk=join --online-name=Joiner \
  $COMMON --online-shots=$OUT/shots/joiner > "$OUT/joiner.log" 2>&1 & BP=$!
wait $AP; AR=$?; wait $BP; BR=$?
kill $LPID $RPID 2>/dev/null; wait 2>/dev/null
rm -rf "$D"
grep -h 'GAME ONLINE' "$OUT/host.log" | sed 's/^/host: /' | head -60
grep -h 'GAME ONLINE' "$OUT/joiner.log" | sed 's/^/joiner: /' | head -40
echo "client exits: $AR $BR"
ls "$OUT/shots" "$OUT/shots/joiner" 2>/dev/null
n=$(ls "$OUT/shots"/*.png "$OUT/shots/joiner"/*.png 2>/dev/null | wc -l)
[ "$n" -gt 0 ] || { echo "SHOTS FAIL: no captures"; exit 1; }
echo "SHOTS: $n captures"
