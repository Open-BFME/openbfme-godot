#!/bin/bash
# Lane ONLINE-3 spectator proof pictures: a 2-player online game (both headless peers on one host, through a deployed lobby + relay) watched by the game
# client (Godot, on this machine's display) as a spectator: the whole map without shroud or fog ("every player"), then one player's view (retail's
# observeNextPlayer) with that player's resources on screen; both look at Mordor's base (start position 3), which Gondor's view has under its shroud.
# The client runs with the neutral HOME given, emptied first (no user name or path on screen, no note of an earlier run).
#   tools/net/online3_spectate_proof.sh <lobby URL> <out dir> <neutral HOME>
#   RUN_HOST: runs its last argument (a bash snippet) in this repository on the peers' host ($B its build); PROFILE: the profile MD5 of both builds;
#   ROTWK_INSTALL / BFME2_INSTALL here; GODOT (default godot); DISPLAY (default :1)
set -u
LOBBY="$1"; OUT="$2"; PHOME="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
: "${RUN_HOST:?RUN_HOST}"; : "${PROFILE:?PROFILE}"
GODOT="${GODOT:-godot}"; export DISPLAY="${DISPLAY:-:1}"
rm -rf "$PHOME"; mkdir -p "$OUT" "$PHOME"
C="python3 $HERE/online_client.py"
rm -f "$OUT/spectate-id.txt"
$C host --lobby "$LOBBY" --name Gondor --profile "$PROFILE" --map "maps/map mp fall back 4p/map mp fall back 4p.map" --slot "player,3,0,0,0" \
    --slot open --slot "hard,5,2,2,1" --players 2 --id-file "$OUT/spectate-id.txt" --out "$OUT/spectate-s0.json" --timeout 300 > "$OUT/spectate-host.log" 2>&1 &
HP=$!
for _ in $(seq 1 100); do [ -s "$OUT/spectate-id.txt" ] && break; sleep 0.2; done
ID="$(cat "$OUT/spectate-id.txt")"
$C join --lobby "$LOBBY" --name Mordor --profile "$PROFILE" --game "$ID" --faction 8 --color 1 --start-pos 3 --team 1 --out "$OUT/spectate-s1.json" > "$OUT/spectate-join.log" 2>&1 &
wait "$HP" $! || { echo "proof: the lobby did not start the game"; exit 1; }
b64() { base64 -w0 < "$1"; }
$RUN_HOST "bash tools/net/online3_homelab.sh peer 0 $(b64 "$OUT/spectate-s0.json") 1500 0 & bash tools/net/online3_homelab.sh peer 1 $(b64 "$OUT/spectate-s1.json") 1500 0; wait" > "$OUT/spectate-peers.txt" 2>&1 &
HPID=$!
sleep 20 # mid-game: the spectator asks once the players are in
HOME="$PHOME" XDG_DATA_HOME="$PHOME/.local/share" XDG_CONFIG_HOME="$PHOME/.config" "$GODOT" --path "$REPO/godot" --resolution 1280x720 -- \
    --online-lobby="$LOBBY" --online-name=Watcher --online-spectate="$ID" --net-camera=5090,150,-1216.6 --net-frames=1100 --net-observe-at=850 \
    --net-shot="$OUT/spectate-every-player.png:800" --net-shot-observe="$OUT/spectate-observing-player.png" > "$OUT/spectate-client.log" 2>&1
echo "client exit $?"
grep -h "GAME NET spectate\|GAME NET observe\|GAME ONLINE\|GAME NET frame" "$OUT/spectate-client.log" | head -30
wait "$HPID"
grep -h "^REPORT [01] \(final_hash\|frames\|desyncs\)\|^PEER" "$OUT/spectate-peers.txt"
grep -h '^GAME NET status' "$OUT/spectate-client.log" | head -c 600; echo
ls -la "$OUT"/spectate-*.png
