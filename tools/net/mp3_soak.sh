#!/bin/bash
# Lane MP-3 soak (NET-4): a series of long headless LAN games, one peer process (tools: openbfme_peer) per human slot (4, or 8 in games 8 .. 11), on
# impaired links (the transport's fault
# injector, GameNetwork/NetImpairment.h), with the skirmish AI filling the other slots: across the series every faction plays and several maps are used.
# Every peer writes its report and per-frame hashes; the script compares them per game, plays the host's recording back, and sums up frames, games and
# desyncs. A desync leaves its DESYNC-*.txt dumps (the per-subsystem breakdown naming the first differing section and objects) in the game's directory.
#
#   tools/net/mp3_soak.sh <openbfme_peer> <out dir> [frames=9000] [seed=3000] [port=27000]
# Environment:
#   SOAK_LINK   the faults of every peer's outgoing datagrams (default "latency=150,jitter=50,loss=50,dup=10,reorder=10")
#   SOAK_GAMES  which games of the table below to play (default "1 2 3"; 1 .. 7 four peers, 8 .. 11 eight peers)
#   SOAK_NICE   the nice level of the peers (default 15: the soak yields to builds)
#   SOAK_RUNAHEAD the lobby's run-ahead (default 2; the adaptive run-ahead raises it)
#   SOAK_EXTRA  extra arguments for every peer (e.g. "--paced" for a game clock in real time)
# Needs ROTWK_INSTALL / BFME2_INSTALL. Exit 0 when every game finished every frame on every peer with zero desyncs, equal hashes and a matching replay.
set -u
PEER="$1"; OUT="$2"; FRAMES="${3:-9000}"; SEED="${4:-3000}"; PORT="${5:-27000}"
LINK="${SOAK_LINK:-latency=150,jitter=50,loss=50,dup=10,reorder=10}"
GAMES="${SOAK_GAMES:-1 2 3}"
NICE="${SOAK_NICE:-15}"
read -r -a EXTRA <<< "${SOAK_EXTRA:-}"
mkdir -p "$OUT"

# game table (the maps' start positions: MapCache numPlayers 8 / 4 / 4 / 8 / 6 / 4 / 8): map | the host's --slot list (the first four human slots are the four peers; the joiners take them in join order)
game_map() {
    case "$1" in
        1) echo "maps/map mp fall back 8p/map mp fall back 8p.map" ;;
        2) echo "maps/map mp fall back 4p/map mp fall back 4p.map" ;;
        3) echo "maps/map mp grey mountains/map mp grey mountains.map" ;;
        4) echo "maps/map mp tournament hills/map mp tournament hills.map" ;;
        5) echo "maps/map mp anfalas/map mp anfalas.map" ;;
        6) echo "maps/map mp weathertop/map mp weathertop.map" ;;
        7) echo "maps/map mp adorn river/map mp adorn river.map" ;;
        8) echo "maps/map mp fall back 8p/map mp fall back 8p.map" ;;
        9) echo "maps/map mp tournament hills/map mp tournament hills.map" ;;
        10) echo "maps/map mp adorn river/map mp adorn river.map" ;;
        11) echo "maps/map mp evendim/map mp evendim.map" ;;
    esac
}
game_slots() {
    case "$1" in
        1) echo "--slot human,FactionMen,0,0 --slot human,FactionElves,1,0 --slot human,FactionIsengard,2,1 --slot human,FactionDwarves,3,1 --slot hard,FactionMordor,4,1 --slot brutal,FactionWild,5,0 --slot medium,FactionAngmar,6,0" ;;
        2) echo "--slot human,FactionMordor,0,0 --slot human,FactionAngmar,1,1 --slot human,FactionWild,2,0 --slot human,FactionMen,3,1" ;;
        3) echo "--slot human,FactionElves,0,0 --slot human,FactionIsengard,1,1 --slot human,FactionDwarves,2,0 --slot human,FactionAngmar,3,1" ;;
        4) echo "--slot human,FactionWild,0,0 --slot human,FactionAngmar,1,0 --slot human,FactionMordor,2,1 --slot human,FactionMen,3,1 --slot hard,FactionElves,4,1 --slot hard,FactionDwarves,5,0 --slot hard,FactionIsengard,6,0" ;;
        5) echo "--slot human,FactionDwarves,0,0 --slot human,FactionMordor,1,1 --slot human,FactionElves,2,0 --slot human,FactionWild,3,1 --slot brutal,FactionAngmar,4,0 --slot hard,FactionIsengard,5,1" ;;
        6) echo "--slot human,FactionMen,0,0 --slot human,FactionIsengard,1,0 --slot human,FactionAngmar,2,1 --slot human,FactionElves,3,1" ;;
        7) echo "--slot human,FactionIsengard,0,0 --slot human,FactionMen,1,1 --slot human,FactionWild,2,0 --slot human,FactionMordor,3,1 --slot medium,FactionDwarves,4,0 --slot brutal,FactionElves,5,1 --slot easy,FactionAngmar,6,0" ;;
        8) echo "--slot human,FactionMen,0,0 --slot human,FactionElves,1,0 --slot human,FactionDwarves,2,0 --slot human,FactionAngmar,3,0 --slot human,FactionMordor,4,1 --slot human,FactionIsengard,5,1 --slot human,FactionWild,6,1 --slot human,FactionMen,7,1" ;;
        9) echo "--slot human,FactionWild,0,0 --slot human,FactionMordor,1,1 --slot human,FactionAngmar,2,0 --slot human,FactionIsengard,3,1 --slot human,FactionElves,4,0 --slot human,FactionDwarves,5,1 --slot human,FactionMen,6,0 --slot human,FactionElves,7,1" ;;
        10) echo "--slot human,FactionDwarves,0,0 --slot human,FactionIsengard,1,1 --slot human,FactionMen,2,0 --slot human,FactionMordor,3,1 --slot human,FactionElves,4,0 --slot human,FactionWild,5,1 --slot human,FactionAngmar,6,0 --slot human,FactionMordor,7,1" ;;
        11) echo "--slot human,FactionAngmar,0,0 --slot human,FactionMen,1,1 --slot human,FactionMordor,2,0 --slot human,FactionElves,3,1 --slot human,FactionIsengard,4,0 --slot human,FactionDwarves,5,1 --slot human,FactionWild,6,0 --slot human,FactionAngmar,7,1" ;;
    esac
}

rc=0
total_frames=0; total_games=0; total_desyncs=0
echo "soak: frames $FRAMES per game, seed $SEED, link $LINK, games $GAMES" | tee "$OUT/summary.txt"
for g in $GAMES; do
    MAP="$(game_map "$g")"
    if [ -z "$MAP" ]; then echo "soak: no game $g in the table" | tee -a "$OUT/summary.txt"; rc=1; continue; fi
    read -r -a SLOTS <<< "$(game_slots "$g")"
    D="$OUT/game$g"; mkdir -p "$D"
    P=$((PORT + g * 10)); S=$((SEED + g))
    COMMON=(--map "$MAP" --seed "$S" --script --crc-interval 100 --run-ahead "${SOAK_RUNAHEAD:-2}" --frames "$FRAMES" --lobby-timeout 900 --link "$LINK"
            --desync-dir "$D" --logic-threads 2 "${EXTRA[@]}")
    start=$(date +%s)
    N=$(grep -o "human," <<< "${SLOTS[*]}" | wc -l)
    # the peers vary their clocks: plain, the logic worker at a 16 ms render step, plain, the worker at 45 ms (repeated)
    VARIANTS=("" "--logic-thread --step-ms 16" "" "--logic-thread --step-ms 45")
    PIDS=()
    for i in $(seq 0 $((N - 1))); do
        read -r -a V <<< "${VARIANTS[$((i % 4))]}"
        if [ "$i" = 0 ]; then
            nice -n "$NICE" "$PEER" "${COMMON[@]}" --host "$P" "${SLOTS[@]}" --record "$D/host.replay" --report "$D/p0.txt" --hashes "$D/h0.txt" > "$D/p0.log" 2>&1 &
        else
            nice -n "$NICE" "$PEER" "${COMMON[@]}" --join "127.0.0.1:$P" --name "Peer$i" "${V[@]}" --report "$D/p$i.txt" --hashes "$D/h$i.txt" > "$D/p$i.log" 2>&1 &
        fi
        PIDS+=($!)
        sleep 1
    done
    grc=0
    for p in "${PIDS[@]}"; do
        wait "$p" || grc=1
    done
    end=$(date +%s)
    echo "game $g: $MAP, seed $S, $((end - start)) s" | tee -a "$OUT/summary.txt"
    for i in $(seq 0 $((N - 1))); do
        echo "  peer $i: $(grep -E '^(slot|frames|desyncs|crc_checks_passed|final_hash|stalled_frames|input_latency_ms|stall_ms|run_ahead) ' "$D/p$i.txt" 2>/dev/null | tr '\n' ' ')" | tee -a "$OUT/summary.txt"
        grep -q "^desyncs 0$" "$D/p$i.txt" 2>/dev/null || grc=1
        grep -q "^frames $FRAMES$" "$D/p$i.txt" 2>/dev/null || grc=1
        if grep -q "network_error" "$D/p$i.txt" 2>/dev/null; then grc=1; fi
        d=$(grep -E '^desyncs ' "$D/p$i.txt" 2>/dev/null | awk '{print $2}')
        total_desyncs=$((total_desyncs + ${d:-0}))
    done
    for i in $(seq 1 $((N - 1))); do
        if ! cmp -s "$D/h0.txt" "$D/h$i.txt"; then echo "  the hashes of peer $i differ from the host's" | tee -a "$OUT/summary.txt"; grc=1; fi
    done
    "$PEER" --replay "$D/host.replay" --report "$D/replay.txt" > "$D/replay.log" 2>&1 || grc=1
    echo "  replay: $(grep -E '^(hashes_compared|mismatches|final_hash) ' "$D/replay.txt" 2>/dev/null | tr '\n' ' ')" | tee -a "$OUT/summary.txt"
    ls "$D"/DESYNC-* > /dev/null 2>&1 && echo "  desync dumps: $(ls "$D"/DESYNC-* | tr '\n' ' ')" | tee -a "$OUT/summary.txt"
    echo "  game $g: $([ $grc -eq 0 ] && echo ok || echo FAILED)" | tee -a "$OUT/summary.txt"
    total_games=$((total_games + 1))
    f=$(grep -E '^frames ' "$D/p0.txt" 2>/dev/null | awk '{print $2}')
    total_frames=$((total_frames + ${f:-0}))
    [ $grc -eq 0 ] || rc=1
done
echo "soak totals: games $total_games, frames $total_frames (host), desyncs $total_desyncs" | tee -a "$OUT/summary.txt"
echo "soak result: $([ $rc -eq 0 ] && echo ok || echo FAILED)" | tee -a "$OUT/summary.txt"
exit $rc
