#!/bin/bash
# Lane MP-DROP-1 (stop S-4556): ONLINE-5's three-player crash with the real game client. Three Godot clients play one online game through a local relay
# (the STARTs signed here, tools/net/online_client.py fixture: no lobby service needed); Gimli (slot 2) is killed (SIGKILL) at frame KILL_AT and its game
# is started again RESTART_S seconds later with its rejoin START. NOBODY presses Kick. Before MP-DROP-1 the survivors' disconnect screens stayed up for good
# with every vote cast (ONLINE-5 waited 12 minutes). Now both survivors must drop Gimli, Gimli's restarted game must rejoin, the three must end on one
# hash, and a spectator (openbfme_peer, the relay's log) must follow the whole game with every CRC matching the players'.
#   tools/net/mpdrop1_godot.sh <build dir> <out dir> [frames=600] [kill at frame=200]
# Headless (godot --headless) unless MPDROP1_DISPLAY is set (then windowed on that display, with the host's disconnect screen saved to
# <out>/host-disconnect-screen.png and the rejoin's catch-up to <out>/rejoin-catchup.png). MPDROP1_RESTART_S (default 5). Needs ROTWK_INSTALL /
# BFME2_INSTALL, godot on the PATH and the project imported. Prints "RESULT mpdrop1 godot: PASS|FAIL (...)" and exits 0 on PASS.
set -u
B="$1"; OUT="$2"; FRAMES="${3:-600}"; KILL_AT="${4:-200}"
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
PEER="$B/openbfme_peer"; RELAY="$B/relay/openbfme_relay"
mkdir -p "$OUT"
[ -x "$PEER" ] && [ -x "$RELAY" ] || { echo "RESULT mpdrop1 godot: FAIL (no $PEER or $RELAY)"; exit 1; }
PROFILE_LINES="$("$PEER" --print-profile)" || { echo "RESULT mpdrop1 godot: FAIL (--print-profile)"; exit 1; }
PROFILE="$(head -n1 <<< "$PROFILE_LINES")"
fac() { awk -v n="$1" '$1 == "faction" && $3 == n { print $2 }' <<< "$PROFILE_LINES"; }
RPORT=$(python3 -c 'import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(("127.0.0.1",0));print(s.getsockname()[1])')
D=$(mktemp -d /tmp/openbfme-mpdrop1-godot.XXXX); mkdir -p "$D/relaylog" "$D/home-0" "$D/home-1" "$D/home-2"
KEY="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
printf '1:%s\n' "$KEY" > "$D/keys.txt"; chmod 600 "$D/keys.txt"
"$RELAY" --bind "127.0.0.1:$RPORT" --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/rs.json" --stats-s 0 --idle-s 300 > "$OUT/relay.log" 2>&1 & RPID=$!
cleanup() { kill "$RPID" 2> /dev/null; rm -rf "$D"; }
trap cleanup EXIT
sleep 0.5
python3 "$REPO/tools/net/online_client.py" fixture --key-hex "$KEY" --game "$(printf '%016x' $((0x0D400000 + RANDOM)))" --relay "127.0.0.1:$RPORT" \
    --profile "$PROFILE" --map "maps/map mp fall back 4p/map mp fall back 4p.map" --seed $((5000 + RANDOM % 1000)) \
    --slot "player,$(fac FactionMen),0,0,0" --slot "player,$(fac FactionMordor),1,1,1" --slot "player,$(fac FactionElves),2,2,0" \
    --rejoin 2:1 --spectator 0:0 --out-dir "$D" > /dev/null || { echo "RESULT mpdrop1 godot: FAIL (fixture)"; exit 1; }
VIEW=(--headless); SHOT_HOST=(); SHOT_REJOIN=()
if [ -n "${MPDROP1_DISPLAY:-}" ]; then
    export DISPLAY="$MPDROP1_DISPLAY"; VIEW=(); SHOT_HOST=(--net-shot-disconnect="$OUT/host-disconnect-screen.png"); SHOT_REJOIN=(--net-shot-rejoin="$OUT/rejoin-catchup.png")
fi
client() { # slot, START, log, game arguments... (run with &: the subshell becomes a process group of its own, `timeout` and the game: kill -- -PID
    local slot="$1" start="$2" log="$3"; shift 3
    cd "$REPO/godot" && HOME="$D/home-$slot" XDG_DATA_HOME="$D/home-$slot/.local/share" XDG_CONFIG_HOME="$D/home-$slot/.config" \
        exec setsid timeout 1500 godot "${VIEW[@]}" --path . -- --online-start-file="$start" --net-frames="$FRAMES" --net-script --res=800x600 "$@" > "$log" 2>&1
}
T0=$(date +%s)
client 0 "$D/start-0.json" "$OUT/host.log" "${SHOT_HOST[@]}" & HP=$!
client 1 "$D/start-1.json" "$OUT/a.log" & AP=$!
client 2 "$D/start-2.json" "$OUT/c.log" & CP=$!
"$PEER" --online-start "$D/spectate-0.json" --frames "$FRAMES" --report "$OUT/spectator.txt" > "$OUT/spectator.log" 2>&1 & SP=$!
for _ in $(seq 1 900); do grep -q "GAME NET frame $KILL_AT " "$OUT/c.log" 2> /dev/null && break; sleep 1; done
grep -q "GAME NET frame $KILL_AT " "$OUT/c.log" || { echo "RESULT mpdrop1 godot: FAIL (Gimli never reached frame $KILL_AT)"; kill -- -"$HP" -"$AP" -"$CP"; kill "$SP"; exit 1; }
kill -KILL -- -"$CP"; wait "$CP" 2> /dev/null
for _ in $(seq 1 50); do pgrep -g "$CP" > /dev/null || break; sleep 0.2; done # the killed processes are gone once reaped
pgrep -g "$CP" > /dev/null && { echo "RESULT mpdrop1 godot: FAIL (Gimli's game survived the kill)"; kill -KILL -- -"$HP" -"$AP" -"$CP"; kill "$SP"; exit 1; }
echo "killed Gimli (SIGKILL) at $(( $(date +%s) - T0 )) s" > "$OUT/events.txt"
sleep "${MPDROP1_RESTART_S:-5}"
client 2 "$D/start-2-rejoin1.json" "$OUT/c2.log" "${SHOT_REJOIN[@]}" & CP2=$!
echo "started Gimli's game again with its rejoin START at $(( $(date +%s) - T0 )) s" >> "$OUT/events.txt"
wait "$HP"; rh=$?; wait "$AP"; ra=$?; wait "$CP2"; rc=$?; wait "$SP"; rs=$?
echo "exits: host $rh, Boromir $ra, Gimli (rejoined) $rc, spectator $rs after $(( $(date +%s) - T0 )) s" >> "$OUT/events.txt"
# the latest logic frame whose state hash all three logged ("GAME NET frame F hash H (frame M)": H is the hash of frame M), and the three hashes there
read -r common fh fa fc <<< "$(python3 - "$OUT/host.log" "$OUT/a.log" "$OUT/c2.log" <<'PY'
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
check '[ "$rh" = 0 ] && [ "$ra" = 0 ] && [ "$rc" = 0 ]' "every client exited 0"
check 'grep -q "GAME NET disconnect screen: " "$OUT/host.log" && grep -q "GAME NET disconnect screen: " "$OUT/a.log"' "both survivors saw the disconnect screen"
check 'grep -q "GAME NET disconnect log: slot 2 disconnected at frame" "$OUT/host.log" && grep -q "GAME NET disconnect log: slot 2 disconnected at frame" "$OUT/a.log"' "both survivors dropped Gimli"
check '! grep -q "GAME NET disconnect button" "$OUT/host.log" "$OUT/a.log"' "nobody pressed Kick"
check 'grep -q "GAME NET rejoin: live at frame" "$OUT/c2.log"' "Gimli's restarted game rejoined"
check '[ "$common" -ge $((FRAMES - 50)) ] && [ "$fh" = "$fa" ] && [ "$fh" = "$fc" ]' "one hash at frame $common, near the end (host $fh, Boromir $fa, Gimli $fc)"
check '[ "$rs" = 0 ] && grep -Eq "^spectate state ended reached $FRAMES .* crcs_compared [1-9][0-9]* crc_mismatches 0( |$)" "$OUT/spectator.txt"' "the spectator followed the game to frame $FRAMES, every CRC matching"
grep -h "GAME NET disconnect log\|GAME NET disconnect screen\|GAME NET rejoin" "$OUT/host.log" "$OUT/a.log" "$OUT/c2.log" | head -60 > "$OUT/summary.txt"
grep -h "^spectate\|^final_hash\|^frames" "$OUT/spectator.txt" >> "$OUT/summary.txt" 2> /dev/null
cat "$OUT/events.txt" "$OUT/result.txt" "$OUT/summary.txt"
echo "RESULT mpdrop1 godot: $([ "$ok" = 1 ] && echo PASS || echo FAIL) (frames $FRAMES, at frame $common hashes host $fh Boromir $fa Gimli $fc)"
[ "$ok" = 1 ]
