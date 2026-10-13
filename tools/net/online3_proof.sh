#!/bin/bash
# Lane ONLINE-3 proof pictures: the game client (Godot, on this machine's display) in an online game with a headless host peer elsewhere, through a
# deployed lobby + relay: "Game paused by ..." (the host pauses at frame 200 and resumes after 15 s) and "Rejoining the game" (the client is killed
# and started again with --online-rejoin). The client runs with the neutral HOME given (no user name or path on screen).
#   tools/net/online3_proof.sh <lobby URL> <out dir> <neutral HOME>
#   RUN_HOST: runs its last argument (a bash snippet) in this repository on the host of the headless peer ($B its build); PROFILE: the profile MD5 of
#   both builds; ROTWK_INSTALL / BFME2_INSTALL here; GODOT (default godot); DISPLAY (default :1)
set -u
LOBBY="$1"; OUT="$2"; PHOME="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
: "${RUN_HOST:?RUN_HOST}"; : "${PROFILE:?PROFILE}"
GODOT="${GODOT:-godot}"; export DISPLAY="${DISPLAY:-:1}"
mkdir -p "$OUT" "$PHOME"
C="python3 $HERE/online_client.py"
client() { # extra game arguments...
    HOME="$PHOME" XDG_DATA_HOME="$PHOME/.local/share" XDG_CONFIG_HOME="$PHOME/.config" exec "$GODOT" --path "$REPO/godot" --resolution 1280x720 -- \
        --online-lobby="$LOBBY" --online-name=Rohan "$@"
}
game() { # name, host peer arguments..., then "--", client arguments...
    local name="$1"; shift
    local hostargs=() ; while [ "$1" != "--" ]; do hostargs+=("$1"); shift; done; shift
    rm -f "$OUT/$name-id.txt" "$OUT/$name-s0.json"
    $C host --lobby "$LOBBY" --name Gondor --profile "$PROFILE" --map "maps/map mp fall back 4p/map mp fall back 4p.map" --slot "player,3,0,0,0" \
        --slot "open" --slot "hard,8,2,2,1" --players 2 --id-file "$OUT/$name-id.txt" --out "$OUT/$name-s0.json" --timeout 600 > "$OUT/$name-host.log" 2>&1 &
    for _ in $(seq 1 100); do [ -s "$OUT/$name-id.txt" ] && break; sleep 0.2; done
    ID="$(cat "$OUT/$name-id.txt")"
    client --online-join="$ID" "$@" > "$OUT/$name-client.log" 2>&1 &
    CPID=$!
    for _ in $(seq 1 600); do [ -s "$OUT/$name-s0.json" ] && break; sleep 0.5; done
    [ -s "$OUT/$name-s0.json" ] || { echo "proof $name: no START"; return 1; }
    $RUN_HOST "bash tools/net/online3_homelab.sh peer 0 $(base64 -w0 < "$OUT/$name-s0.json") ${FRAMES:-900} 0 ${hostargs[*]}" > "$OUT/$name-peer.txt" 2>&1 &
    HPID=$!
}
# 1. the pause
FRAMES=700 game pause --net-pause-at 200 --net-resume-ms 15000 -- --net-shot-pause="$OUT/pause-paused-by-host.png" --net-overlay
for _ in $(seq 1 900); do grep -q "GAME NET pause screenshot" "$OUT/pause-client.log" 2> /dev/null && break; sleep 1; done
grep -h "GAME NET pause\|GAME NET paused\|GAME NET resumed\|GAME ONLINE" "$OUT/pause-client.log" | head -12
wait "$HPID"; kill "$CPID" 2> /dev/null; wait "$CPID" 2> /dev/null
# 2. the rejoin: the client is killed in the game and started again with --online-rejoin
FRAMES=1200 game rejoin -- --net-overlay
for _ in $(seq 1 900); do grep -qE "GAME NET frame (2[5-9][0-9]|[3-9][0-9][0-9]) " "$OUT/rejoin-client.log" 2> /dev/null && break; sleep 1; done
kill -KILL "$CPID"; wait "$CPID" 2> /dev/null
sleep 20
client --online-rejoin="$ID" --net-shot-rejoin="$OUT/rejoin-catching-up.png" --net-overlay > "$OUT/rejoin-client2.log" 2>&1 &
CPID=$!
for _ in $(seq 1 900); do grep -q "GAME NET rejoin: live" "$OUT/rejoin-client2.log" 2> /dev/null && break; sleep 1; done
sleep 8
grep -h "GAME NET rejoin\|GAME ONLINE" "$OUT/rejoin-client2.log" | head -12
wait "$HPID"; kill "$CPID" 2> /dev/null; wait "$CPID" 2> /dev/null
grep -h "^REPORT 0 \(final_hash\|online_slots\|pause \|desyncs\)\|^PEER" "$OUT/pause-peer.txt" "$OUT/rejoin-peer.txt"
ls -la "$OUT"/*.png
