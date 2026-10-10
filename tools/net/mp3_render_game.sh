#!/bin/bash
# Lane MP-3: the owner's rendered measurements of an 8-player LAN game. Headless peers (openbfme_peer, scripted humans, impaired links) fill seven of the
# eight human slots; one rendered Godot client (game.gd: --net-join, scripted, vsync off) takes the last one and logs every rendered frame (--perf-log) and
# the per-logic-frame census (--net-census); tools/net/mp3_perf_table.py turns them into the table.
#
#   tools/net/mp3_render_game.sh peers  <openbfme_peer> <out dir> <frames> <port> [bind ip]   the host + joiners (headless), waiting for the client
#   tools/net/mp3_render_game.sh client <godot> <out dir> <frames> <host ip:port> [res=1280x800]  the rendered client (DISPLAY must be set)
#   tools/net/mp3_render_game.sh both   <openbfme_peer> <godot> <out dir> <frames> <port> [res] everything on this machine (localhost)
# Environment: ROTWK_INSTALL / BFME2_INSTALL; MP3_LINK (default "latency=150,jitter=50,loss=50") the peers' faults; MP3_SEED (default 7001);
# MP3_MAP (default Fall Back 8p); MP3_SLOTS the host's --slot list (default eight humans; e.g. four humans and four brutal AIs for battles);
# MP3_GODOT_ARGS extra Godot arguments (e.g. "--rendering-driver opengl3" under WSLg, where only OpenGL reaches the GPU,
# through Mesa's d3d12 driver: GALLIUM_DRIVER=d3d12); MP3_CLIENT_ARGS extra game.gd arguments (e.g. "--perf"). Run from the repository root (the client uses ./godot).
set -u
MAP="${MP3_MAP:-maps/map mp fall back 8p/map mp fall back 8p.map}"
SEED="${MP3_SEED:-7001}"
LINK="${MP3_LINK:-latency=150,jitter=50,loss=50}"
SLOTS=(--slot human,FactionMen,0,0 --slot human,FactionElves,1,0 --slot human,FactionDwarves,2,0 --slot human,FactionAngmar,3,0
       --slot human,FactionMordor,4,1 --slot human,FactionIsengard,5,1 --slot human,FactionWild,6,1 --slot human,FactionMen,7,1)
if [ -n "${MP3_SLOTS:-}" ]; then read -r -a SLOTS <<< "$MP3_SLOTS"; fi
# the headless peers: every human slot but the one the rendered client takes
HEADLESS=$(( $(grep -o "human," <<< "${SLOTS[*]}" | wc -l) - 1 ))

peers() {
    local PEER=$1 OUT=$2 FRAMES=$3 PORT=$4 BIND=${5:-127.0.0.1}
    mkdir -p "$OUT"
    local C=(--map "$MAP" --seed "$SEED" --script --crc-interval 100 --run-ahead 2 --frames "$FRAMES" --lobby-timeout 900 --link "$LINK" --logic-threads 2
             --bind "$BIND" --desync-dir "$OUT")
    local pids=()
    "$PEER" "${C[@]}" --host "$PORT" "${SLOTS[@]}" --record "$OUT/host.replay" --census "$OUT/census-host.csv" --report "$OUT/p0.txt" \
        --hashes "$OUT/h0.txt" > "$OUT/p0.log" 2>&1 &
    pids+=($!)
    for i in $(seq 1 $((HEADLESS - 1))); do
        sleep 1
        "$PEER" "${C[@]}" --join "$BIND:$PORT" --name "Peer$i" --report "$OUT/p$i.txt" --hashes "$OUT/h$i.txt" > "$OUT/p$i.log" 2>&1 &
        pids+=($!)
    done
    local rc=0
    for p in "${pids[@]}"; do wait "$p" || rc=1; done
    for i in $(seq 1 $((HEADLESS - 1))); do cmp -s "$OUT/h0.txt" "$OUT/h$i.txt" || { echo "peer $i hashes differ"; rc=1; }; done
    for i in $(seq 0 $((HEADLESS - 1))); do echo "peer $i: $(grep -E '^(frames|desyncs|crc_checks_passed|final_hash) ' "$OUT/p$i.txt" | tr '\n' ' ')"; done
    return $rc
}

client() {
    local GODOT=$1 OUT=$2 FRAMES=$3 HOSTPORT=$4 RES=${5:-1280x800}
    mkdir -p "$OUT"
    local O; O=$(cd "$OUT" && pwd)
    read -r -a GA <<< "${MP3_GODOT_ARGS:-}"
    read -r -a CA <<< "${MP3_CLIENT_ARGS:-}"
    "$GODOT" --path godot --resolution "$RES" "${GA[@]}" -- --net-join="$HOSTPORT" --net-name=Rendered --net-script --net-frames="$FRAMES" --vsync=off --res="$RES" \
        --no-intro --no-record --perf-log="$O/frames.log" --net-census="$O/census-client.csv" "${CA[@]}" > "$O/client.log" 2>&1
    local rc=$?
    echo "client exit $rc: $(grep -E 'GAME NET status|FRAME PACING|GAME NET census' "$O/client.log" | cut -c1-200 | tr '\n' ' ')"
    return $rc
}

case "${1:-}" in
    peers) shift; peers "$@" ;;
    client) shift; client "$@" ;;
    both)
        shift
        PEER=$1 GODOT=$2 OUT=$3 FRAMES=$4 PORT=$5 RES=${6:-1280x800}
        peers "$PEER" "$OUT" "$FRAMES" "$PORT" &
        PP=$!
        sleep 8
        client "$GODOT" "$OUT" "$FRAMES" "127.0.0.1:$PORT" "$RES"
        crc=$?
        wait $PP
        prc=$?
        exit $((crc != 0 || prc != 0)) ;;
    *) echo "usage: $0 peers|client|both ..." >&2; exit 2 ;;
esac
