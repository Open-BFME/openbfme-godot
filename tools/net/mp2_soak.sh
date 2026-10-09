#!/bin/bash
# Lane MP-2 soak: four headless peer processes (tools: openbfme_peer) play one LAN game in lockstep with three skirmish AIs, all seven factions, on
# lossy, jittery links; two peers run the logic on its worker thread at different render rates. Every peer writes its report and its per-frame hashes;
# the script compares them, then plays the host's recording back in a fifth process.
#
#   tools/net/mp2_soak.sh <openbfme_peer> <out dir> [frames=9000] [seed=2026] [port=26000]
# SOAK_LINK overrides the injected link faults (default "--drop 30 --jitter 15 --delay 5": per-mille loss, ms of jitter and delay per datagram).
# Needs ROTWK_INSTALL / BFME2_INSTALL. Exit 0 when every peer finished every frame with zero desyncs and equal hashes and the replay matched.
set -u
PEER="$1"; OUT="$2"; FRAMES="${3:-9000}"; SEED="${4:-2026}"; PORT="${5:-26000}"
mkdir -p "$OUT"
MAP="maps/map mp fall back 8p/map mp fall back 8p.map"
read -r -a LINK <<< "${SOAK_LINK:---drop 30 --jitter 15 --delay 5}"
COMMON=(--map "$MAP" --seed "$SEED" --script --crc-interval 100 --run-ahead 2 --frames "$FRAMES" --lobby-timeout 900 "${LINK[@]}" --desync-dir "$OUT")
SLOTS=(--slot human,FactionMen,0,0 --slot human,FactionElves,1,0 --slot human,FactionIsengard,2,1 --slot human,FactionDwarves,3,1
       --slot hard,FactionMordor,4,1 --slot brutal,FactionWild,5,0 --slot medium,FactionAngmar,6,0)
start=$(date +%s)
"$PEER" "${COMMON[@]}" --host "$PORT" "${SLOTS[@]}" --record "$OUT/host.replay" --report "$OUT/p0.txt" --hashes "$OUT/h0.txt" > "$OUT/p0.log" 2>&1 &
P0=$!
sleep 1
"$PEER" "${COMMON[@]}" --join "127.0.0.1:$PORT" --name Elrond --logic-thread --step-ms 16 --report "$OUT/p1.txt" --hashes "$OUT/h1.txt" > "$OUT/p1.log" 2>&1 &
P1=$!
sleep 1
"$PEER" "${COMMON[@]}" --join "127.0.0.1:$PORT" --name Saruman --report "$OUT/p2.txt" --hashes "$OUT/h2.txt" > "$OUT/p2.log" 2>&1 &
P2=$!
sleep 1
"$PEER" "${COMMON[@]}" --join "127.0.0.1:$PORT" --name Gimli --logic-thread --step-ms 45 --worker-delay-ms 2 --report "$OUT/p3.txt" --hashes "$OUT/h3.txt" > "$OUT/p3.log" 2>&1 &
P3=$!
rc=0
for p in $P0 $P1 $P2 $P3; do
    wait "$p" || rc=1
done
end=$(date +%s)
echo "soak: $FRAMES frames, seed $SEED, $((end - start)) s" | tee "$OUT/summary.txt"
for i in 0 1 2 3; do
    echo "peer $i: $(grep -E '^(frames|desyncs|crc_checks_passed|final_hash|commands_relayed|stalled_frames) ' "$OUT/p$i.txt" | tr '\n' ' ')" | tee -a "$OUT/summary.txt"
    grep -q "^desyncs 0$" "$OUT/p$i.txt" || rc=1
    grep -q "^frames $FRAMES$" "$OUT/p$i.txt" || rc=1
    if grep -q "network_error" "$OUT/p$i.txt"; then rc=1; fi
done
for i in 1 2 3; do
    if ! cmp -s "$OUT/h0.txt" "$OUT/h$i.txt"; then echo "soak: the hashes of peer $i differ from the host's" | tee -a "$OUT/summary.txt"; rc=1; fi
done
"$PEER" --replay "$OUT/host.replay" --report "$OUT/replay.txt" > "$OUT/replay.log" 2>&1 || rc=1
echo "replay: $(grep -E '^(hashes_compared|mismatches|final_hash) ' "$OUT/replay.txt" | tr '\n' ' ')" | tee -a "$OUT/summary.txt"
echo "soak result: $([ $rc -eq 0 ] && echo ok || echo FAILED)" | tee -a "$OUT/summary.txt"
exit $rc
