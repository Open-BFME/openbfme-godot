#!/bin/bash
# Lane MPDROP-3 (stop S-1127): a LAN skirmish whose last joiner's process HANGS at protocol frame 200 (openbfme_peer --hitch-at: its loop sleeps,
# nothing is sent and nothing reads its socket, which fills and loses datagrams) for HANG seconds, then runs again. The others drop it (NetworkDisconnectTime 2 s, NetworkPlayerTimeoutTime 8 s here) and play
# on in step; once it runs again it must learn at once that it was dropped ("dropped by the other players"), run no frame from the drop's frame on, drop
# nobody itself (no split game) and exit.
#   tools/net/mpdrop3_hang.sh <build dir> <out dir> [players=2] [hang seconds=20] [frames=900]
# Needs ROTWK_INSTALL / BFME2_INSTALL. Prints "RESULT mpdrop3 hang <players> <hang>s: PASS|FAIL (...)" and exits 0 on PASS.
set -u
B="$1"; OUT="$2"; N="${3:-2}"; HANG="${4:-20}"; FRAMES="${5:-900}"
PEER="$B/openbfme_peer"
D="$OUT/hang${N}_${HANG}"; rm -rf "$D"; mkdir -p "$D"
[ -x "$PEER" ] || { echo "RESULT mpdrop3 hang $N ${HANG}s: FAIL (no $PEER)"; exit 1; }
PORT=$((28000 + RANDOM % 1000))
FACTIONS=(FactionMen FactionMordor FactionElves FactionIsengard)
SLOTS=(); for i in $(seq 0 $((N - 1))); do SLOTS+=(--slot "human,${FACTIONS[$i]},$i,$((i % 2))"); done
COMMON=(--map "maps/map mp fall back 4p/map mp fall back 4p.map" --seed $((3000 + RANDOM % 1000)) --script --crc-interval 100 --run-ahead 2 --frames "$FRAMES"
        --paced --disconnect-ms 2000 --player-timeout-ms 8000 --lobby-timeout 600 --desync-dir "$D")
T0=$(date +%s)
"$PEER" "${COMMON[@]}" --host "$PORT" "${SLOTS[@]}" --report "$D/p0.txt" > "$D/p0.log" 2>&1 & PIDS=($!)
for i in $(seq 1 $((N - 1))); do
    sleep 1
    EXTRA=(); [ "$i" = $((N - 1)) ] && EXTRA=(--hitch-at "200:$((HANG * 1000))") # the game is well under way (frame 200: 40 s of play)
    "$PEER" "${COMMON[@]}" --join "127.0.0.1:$PORT" --name "Joiner$i" "${EXTRA[@]}" --report "$D/p$i.txt" > "$D/p$i.log" 2>&1 & PIDS+=($!)
done
H=${PIDS[$((N - 1))]}
wait "$H"; rv=$?; echo "the hung peer exited $rv" > "$D/hang.txt"
rc=0; for i in $(seq 0 $((N - 2))); do wait "${PIDS[$i]}" || rc=1; done
SECS=$(( $(date +%s) - T0 ))
HR="$D/p$((N - 1)).txt"; V=$(awk '$1 == "slot" { print $2; exit }' "$HR" 2> /dev/null); V="${V:--1}"
SURV=(); for i in $(seq 0 $((N - 2))); do SURV+=("$D/p$i.txt"); done
DROPF=$(grep -h "^disconnect_log slot $V disconnected at frame" "$D/p0.txt" | head -1 | awk '{print $NF}')
HF=$(awk '$1 == "frames" { print $2; exit }' "$HR" 2> /dev/null)
ok=1; : > "$D/result.txt"
check() { if ! eval "$1"; then ok=0; echo "  check failed: $2" >> "$D/result.txt"; fi; }
check '[ "$rc" = 0 ]' "every survivor exited 0"
check '[ "$(cat "${SURV[@]}" | awk -v f="$FRAMES" "\$1 == \"frames\" && \$2 == f" | wc -l)" = "${#SURV[@]}" ]' "every survivor ran $FRAMES frames"
check '[ "$(cat "${SURV[@]}" | grep "^final_hash" | sort -u | wc -l)" = 1 ]' "one final hash among the survivors"
check '[ "$(cat "${SURV[@]}" | awk "\$1 == \"desyncs\" { s += \$2 } END { print s + 0 }")" = 0 ]' "no desync"
check '[ -n "$DROPF" ]' "the survivors dropped the hung slot $V"
check 'grep -q "^disconnect_quit dropped by the other players" "$HR"' "the hung peer learned it was dropped (disconnect_quit)"
QMS=$(awk '$1 == "quit_after_resume_ms" { print $2; exit }' "$HR" 2> /dev/null); QMS="${QMS:--1}"
check '[ "$QMS" -ge 0 ] && [ "$QMS" -le 3000 ]' "it learned it was dropped within 3 s of running again (quit_after_resume_ms $QMS)"
check '[ -n "$HF" ] && [ -n "$DROPF" ] && [ "$HF" -le "$DROPF" ]' "it ran no frame from the drop's frame on (its frames $HF, drop frame $DROPF)"
check '! grep -q "^disconnect_log slot [0-9]* disconnected at frame" "$HR"' "it dropped nobody (no split game)"
{ echo "lan players $N hang ${HANG}s frames $FRAMES seconds $SECS: the hung joiner slot $V"; cat "$D/hang.txt"; grep -h "^final_hash\|^disconnect_log\|^disconnect_quit\|^quit_after_resume_ms\|^frames " "${SURV[@]}" "$HR" | sort | uniq -c | head -30; cat "$D/result.txt"; } > "$D/summary.txt"
cat "$D/summary.txt"
echo "RESULT mpdrop3 hang $N ${HANG}s: $([ "$ok" = 1 ] && echo PASS || echo FAIL) (frames $FRAMES, ${SECS}s)"
[ "$ok" = 1 ]
