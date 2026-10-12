#!/bin/bash
# Lane ONLINE-1: the bandwidth of one networked game, for the relay's budget (docs/ONLINE.md). N headless peer processes (tools: openbfme_peer), every
# slot a scripted human (ScriptedPlayer: one action every 15 logic frames), play one game in real time (--realtime: about 5 logic frames a second) on clean
# localhost links (no injected faults: the steady state, no resends); every peer's report carries its transport counters (Transport::Stats: datagrams,
# UDP payload bytes, ack-only datagrams, commands). The script prints them per peer and summed, per second of the host's game time, and the size of the
# host's replay (plain and gzipped: the server-side game log's storage).
#
#   tools/net/online1_bandwidth.sh <openbfme_peer> <out dir> <peers: 2 | 4 | 8> [frames=3000] [seed=4100] [port=28000]
# Needs ROTWK_INSTALL / BFME2_INSTALL. Exit 0 when every peer finished every frame with zero desyncs.
set -u
PEER="$1"; OUT="$2"; N="$3"; FRAMES="${4:-3000}"; SEED="${5:-4100}"; PORT="${6:-28000}"
mkdir -p "$OUT"
case "$N" in
    2) MAP="maps/map mp fords of isen ii/map mp fords of isen ii.map"
       SLOTS=(--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1) ;;
    4) MAP="maps/map mp fall back 4p/map mp fall back 4p.map"
       SLOTS=(--slot human,FactionMordor,0,0 --slot human,FactionAngmar,1,1 --slot human,FactionWild,2,0 --slot human,FactionMen,3,1) ;;
    8) MAP="maps/map mp fall back 8p/map mp fall back 8p.map"
       SLOTS=(--slot human,FactionMen,0,0 --slot human,FactionElves,1,0 --slot human,FactionDwarves,2,0 --slot human,FactionAngmar,3,0
              --slot human,FactionMordor,4,1 --slot human,FactionIsengard,5,1 --slot human,FactionWild,6,1 --slot human,FactionMen,7,1) ;;
    *) echo "online1_bandwidth: peers must be 2, 4 or 8" >&2; exit 2 ;;
esac
COMMON=(--map "$MAP" --seed "$SEED" --script --realtime --crc-interval 100 --run-ahead 2 --frames "$FRAMES" --lobby-timeout 900 --desync-dir "$OUT"
        --logic-threads 1)
PIDS=()
for i in $(seq 0 $((N - 1))); do
    if [ "$i" = 0 ]; then
        nice -n 10 "$PEER" "${COMMON[@]}" --host "$PORT" "${SLOTS[@]}" --record "$OUT/host.replay" --report "$OUT/p0.txt" > "$OUT/p0.log" 2>&1 &
    else
        nice -n 10 "$PEER" "${COMMON[@]}" --join "127.0.0.1:$PORT" --name "Peer$i" --report "$OUT/p$i.txt" > "$OUT/p$i.log" 2>&1 &
    fi
    PIDS+=($!)
    sleep 1
done
rc=0
for p in "${PIDS[@]}"; do
    wait "$p" || rc=1
done
for i in $(seq 0 $((N - 1))); do
    grep -q "^desyncs 0$" "$OUT/p$i.txt" 2>/dev/null || rc=1
    grep -q "^frames $FRAMES$" "$OUT/p$i.txt" 2>/dev/null || rc=1
done
# per peer: seconds, datagrams sent, payload bytes sent / received, ack-only datagrams, commands sent
awk -v n="$N" -v frames="$FRAMES" '
    FNR == 1 { peer = FILENAME; sub(/.*\//, "", peer) }
    $1 == "seconds" { sec[peer] = $2 }
    $1 == "transport" && $2 == "packets_sent" { pk[peer] = $3 }
    $1 == "transport_bytes" { bs[peer] = $3; br[peer] = $5; ao[peer] = $7; cs[peer] = $9 }
    END {
        for (p in bs) {
            printf "%s: seconds %.1f packets_sent %d bytes_sent %d bytes_received %d ack_only %d commands %d -> %.0f B/s up (payload), %.0f B/s up (wire, +28 B per datagram), %.1f datagrams/s\n",
                p, sec[p], pk[p], bs[p], br[p], ao[p], cs[p], bs[p] / sec[p], (bs[p] + 28 * pk[p]) / sec[p], pk[p] / sec[p]
            tb += bs[p]; tp += pk[p]; tc += cs[p]; if (sec[p] > s) s = sec[p]
        }
        printf "game: %d peers, %d frames, %.1f s: every peer sent %d payload bytes (%d with IPv4 / UDP headers) in %d datagrams, %d commands\n", n, frames, s, tb, tb + 28 * tp, tp, tc
        printf "game: %.0f B/s payload, %.0f B/s wire, %.0f B/s wire per player; %.1f bytes per logic frame per directed link\n", tb / s, (tb + 28 * tp) / s, (tb + 28 * tp) / s / n, tb / frames / (n * (n - 1))
    }' "$OUT"/p*.txt | tee "$OUT/bandwidth.txt"
echo "replay: $(wc -c < "$OUT/host.replay" 2>/dev/null || echo none) bytes, $(gzip -9c "$OUT/host.replay" 2>/dev/null | wc -c) gzipped" | tee -a "$OUT/bandwidth.txt"
echo "online1_bandwidth: $([ $rc -eq 0 ] && echo ok || echo FAILED)" | tee -a "$OUT/bandwidth.txt"
exit $rc
