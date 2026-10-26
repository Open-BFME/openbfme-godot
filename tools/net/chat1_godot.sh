#!/bin/bash
# Lane CHAT-1: in-game chat and beacons with the real game client. Three Godot clients play one online game through a local relay (the STARTs signed
# here, tools/net/online_client.py fixture: no lobby needed): slot 0 (Men) and slot 1 (Elves) are allies on team 0, slot 2 (Mordor) is the enemy on
# team 1. Each types lines through the chat window (game.gd --chat-at: the window opens, the text is typed into its entry, Send is pressed) to
# everyone and to its allies; slot 0 places a beacon (game.gd --beacon-at: Ctrl+B through the HUD's key path, then a left click). Then:
#   * every line reaches exactly the players its mode names (Everyone: all three; Allies: the sender's team only), in send order, with the sender's echo;
#   * the beacon is shown to the placer and its ally and hidden on the enemy's screen;
#   * the three clients end on one state hash (chat never enters the simulation; the beacon is simulation on every peer).
#   tools/net/chat1_godot.sh <build dir> <out dir> [frames=500]
# Headless (godot --headless) unless CHAT1_DISPLAY / CHAT1_WAYLAND is set: then slot 0 runs windowed (X display / Wayland socket) and saves
# <out>/chat.png and <out>/beacon.png; CHAT1_MOVIE=<file.avi> records slot 0 with Godot's Movie Maker (30 fps); CHAT1_PROJECT=<dir> runs another copy
# of godot/; CHAT1_CLIENT_ARGS=<args> go to every client (e.g. --net-disconnect-ms=120000 when a recording slows the host down). Needs ROTWK_INSTALL / BFME2_INSTALL,
# godot on the PATH and the project imported. Prints "RESULT chat1 godot: PASS|FAIL (...)" and exits 0 on PASS.
set -u
B="$1"; OUT="$2"; FRAMES="${3:-500}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PROJ="${CHAT1_PROJECT:-$REPO/godot}" # CHAT1_PROJECT: a copy of godot/ with the GDExtension to run (a running build cannot disturb it)
PEER="$B/openbfme_peer"; RELAY="$B/relay/openbfme_relay"
mkdir -p "$OUT"
[ -x "$PEER" ] && [ -x "$RELAY" ] || { echo "RESULT chat1 godot: FAIL (no $PEER or $RELAY)"; exit 1; }
PROFILE_LINES="$("$PEER" --print-profile)" || { echo "RESULT chat1 godot: FAIL (--print-profile)"; exit 1; }
PROFILE="$(head -n1 <<< "$PROFILE_LINES")"
fac() { awk -v n="$1" '$1 == "faction" && $3 == n { print $2 }' <<< "$PROFILE_LINES"; }
RPORT=$(python3 -c 'import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
D=$(mktemp -d /tmp/openbfme-chat1-godot.XXXX); mkdir -p "$D/relaylog" "$D/home-0" "$D/home-1" "$D/home-2"
KEY="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
printf '1:%s\n' "$KEY" > "$D/keys.txt"; chmod 600 "$D/keys.txt"
"$RELAY" --bind "127.0.0.1:$RPORT" --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/rs.json" --stats-s 0 --idle-s 300 > "$OUT/relay.log" 2>&1 & RPID=$!
cleanup() { kill "$RPID" 2> /dev/null; rm -rf "$D"; }
trap cleanup EXIT
sleep 0.5
# STATE,faction,color,startPos,team,name
python3 "$REPO/tools/net/online_client.py" fixture --key-hex "$KEY" --game "$(printf '%016x' $((0x0C4A0000 + RANDOM)))" --relay "127.0.0.1:$RPORT" \
    --profile "$PROFILE" --map "maps/map mp fall back 4p/map mp fall back 4p.map" --seed $((7000 + RANDOM % 1000)) \
    --slot "player,$(fac FactionMen),0,0,0,Aragorn" --slot "player,$(fac FactionElves),1,1,0,Galadriel" --slot "player,$(fac FactionMordor),2,2,1,Gothmog" \
    --out-dir "$D" > "$OUT/fixture.log" 2>&1 || { echo "RESULT chat1 godot: FAIL (fixture)"; cat "$OUT/fixture.log"; exit 1; }
HOSTVIEW=(--headless); SHOTS=(); MOVIE=()
if [ -n "${CHAT1_WAYLAND:-}" ]; then
    export WAYLAND_DISPLAY="$CHAT1_WAYLAND"; HOSTVIEW=(--display-driver wayland --rendering-driver vulkan --resolution 1280x720)
    SHOTS=(--chat-shot="$OUT/chat.png" --beacon-shot="$OUT/beacon.png" --chat-open-shot="$OUT/chat-window.png")
elif [ -n "${CHAT1_DISPLAY:-}" ]; then
    export DISPLAY="$CHAT1_DISPLAY"; HOSTVIEW=(); SHOTS=(--chat-shot="$OUT/chat.png" --beacon-shot="$OUT/beacon.png" --chat-open-shot="$OUT/chat-window.png")
fi
[ -n "${CHAT1_MOVIE:-}" ] && MOVIE=(--write-movie "$CHAT1_MOVIE" --fixed-fps 30)
client() { # slot, view args (array name), log, game arguments...
    local slot="$1" log="$2"; shift 2
    local view=(--headless); [ "$slot" = 0 ] && view=("${HOSTVIEW[@]}" "${MOVIE[@]}")
    cd "$PROJ" && HOME="$D/home-$slot" XDG_DATA_HOME="$D/home-$slot/.local/share" XDG_CONFIG_HOME="$D/home-$slot/.config" \
        exec setsid timeout "${CHAT1_TIMEOUT:-1500}" godot "${view[@]}" --path . -- --online-start-file="$D/start-$slot.json" --net-frames="$FRAMES" --res=1280x720 ${CHAT1_CLIENT_ARGS:-} "$@" > "$log" 2>&1
}
T0=$(date +%s)
client 0 "$OUT/aragorn.log" "${SHOTS[@]}" --chat-at="40:everyone:Good luck, have fun!" --chat-at="130:allies:Gathering at the ford, follow my beacon" \
    --beacon-at=110 & P0=$!
client 1 "$OUT/galadriel.log" --chat-at="70:allies:On my way with archers" --chat-at="160:everyone:gl hf" & P1=$!
client 2 "$OUT/gothmog.log" --chat-at="90:allies:Their ford is weak, attack now" --chat-at="100:everyone:You will all fall" & P2=$!
wait "$P0"; r0=$?; wait "$P1"; r1=$?; wait "$P2"; r2=$?
echo "exits: Aragorn $r0, Galadriel $r1, Gothmog $r2 after $(( $(date +%s) - T0 )) s" > "$OUT/events.txt"
lines() { grep -o 'GAME CHAT line slot [0-9]*: .*' "$1" | sed 's/^GAME CHAT line //'; }
lines "$OUT/aragorn.log" > "$OUT/aragorn.lines"; lines "$OUT/galadriel.log" > "$OUT/galadriel.lines"; lines "$OUT/gothmog.log" > "$OUT/gothmog.lines"
read -r common f0 f1 f2 <<< "$(python3 - "$OUT/aragorn.log" "$OUT/galadriel.log" "$OUT/gothmog.log" <<'PY'
import re, sys
logs = []
for path in sys.argv[1:]:
    seen = {}
    for m in re.finditer(r"GAME NET frame \d+ hash ([0-9A-F]{8}) \(frame (\d+)\)", open(path, errors="replace").read()):
        seen[int(m.group(2))] = m.group(1)
    logs.append(seen)
both = set(logs[0]) & set(logs[1]) & set(logs[2])
f = max(both) if both else -1
print(f, *[(l.get(f, "none")) for l in logs])
PY
)"
ok=1; : > "$OUT/result.txt"
check() { if ! eval "$1"; then ok=0; echo "  check failed: $2" >> "$OUT/result.txt"; fi; }
has() { grep -qF "$2" "$1"; }
check '[ "$r0" = 0 ] && [ "$r1" = 0 ] && [ "$r2" = 0 ]' "every client exited 0"
# Aragorn (team 0): his own lines, Galadriel's allies + everyone lines, Gothmog's everyone line, never Gothmog's allies line
check 'has "$OUT/aragorn.lines" "slot 0: [Aragorn] <Global> Good luck, have fun!"' "Aragorn's everyone line echoed"
check 'has "$OUT/aragorn.lines" "slot 1: [Galadriel] <Team> On my way with archers"' "Aragorn got his ally's team line"
check 'has "$OUT/aragorn.lines" "slot 2: [Gothmog] <Global> You will all fall"' "Aragorn got the enemy's everyone line"
check '! has "$OUT/aragorn.lines" "Their ford is weak"' "the enemy's team line never reached Aragorn"
check '! has "$OUT/galadriel.lines" "Their ford is weak"' "the enemy's team line never reached Galadriel"
check 'has "$OUT/galadriel.lines" "slot 0: [Aragorn] <Team> Gathering at the ford, follow my beacon"' "Galadriel got Aragorn's team line"
check '! has "$OUT/gothmog.lines" "Gathering at the ford" && ! has "$OUT/gothmog.lines" "On my way with archers"' "team 0's team lines never reached Gothmog"
check 'has "$OUT/gothmog.lines" "slot 0: [Aragorn] <Global> Good luck, have fun!" && has "$OUT/gothmog.lines" "slot 1: [Galadriel] <Global> gl hf"' "Gothmog got the everyone lines"
check 'has "$OUT/gothmog.lines" "slot 2: [Gothmog] <Team> Their ford is weak, attack now"' "Gothmog's team line echoed to himself"
check '[ "$(grep -n "Good luck" "$OUT/galadriel.lines" | cut -d: -f1)" -lt "$(grep -n "Gathering at the ford" "$OUT/galadriel.lines" | cut -d: -f1)" ]' "Aragorn's lines arrived in send order"
# the beacon: placed by Aragorn, shown on team 0's screens, hidden on Gothmog's
bstate() { grep "GAME BEACON state: " "$1" | tail -n1; }
check 'bstate "$OUT/aragorn.log" | grep -q "\"placed\":1" && bstate "$OUT/aragorn.log" | grep -q "\"hidden\":0"' "Aragorn placed his beacon and sees it"
check 'bstate "$OUT/galadriel.log" | grep -q "\"placed\":1" && bstate "$OUT/galadriel.log" | grep -q "\"hidden\":0"' "his ally Galadriel sees it"
check 'bstate "$OUT/gothmog.log" | grep -q "\"placed\":1" && bstate "$OUT/gothmog.log" | grep -q "\"hidden\":1"' "the enemy Gothmog's client hides it"
check '[ "$common" -ge $((FRAMES - 60)) ] && [ "$f0" = "$f1" ] && [ "$f0" = "$f2" ]' "one hash at frame $common, near the end (Aragorn $f0, Galadriel $f1, Gothmog $f2)"
if [ $ok = 1 ]; then echo "RESULT chat1 godot: PASS (one hash at frame $common; lines as addressed; beacon placed)"; exit 0; fi
echo "RESULT chat1 godot: FAIL"; cat "$OUT/result.txt"; exit 1
