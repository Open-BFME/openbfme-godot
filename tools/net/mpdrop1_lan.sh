#!/bin/bash
# Lane MP-DROP-1: a LAN game whose last joiner crashes mid-game (openbfme_peer --vanish-at: the process stops, nothing more is sent, no leave). The
# others must show the disconnect screen after NetworkDisconnectTime (15 s), drop it when its bar runs out (60 s, nobody presses Kick), hand its army
# over at once (retail's LAN rule: MSG_SELF_DESTRUCT) and play every frame in step.
#   tools/net/mpdrop1_lan.sh <build dir> <out dir> [players=2] [frames=600] [vanish at frame=200]
# Needs ROTWK_INSTALL / BFME2_INSTALL. Prints "RESULT mpdrop1 lan <players>: PASS|FAIL (...)" and exits 0 on PASS.
set -u
B="$1"; OUT="$2"; N="${3:-2}"; FRAMES="${4:-600}"; VANISH="${5:-200}"
PEER="$B/openbfme_peer"
D="$OUT/lan$N"; rm -rf "$D"; mkdir -p "$D"
[ -x "$PEER" ] || { echo "RESULT mpdrop1 lan $N: FAIL (no $PEER)"; exit 1; }
PORT=$((27000 + RANDOM % 1000))
FACTIONS=(FactionMen FactionMordor FactionElves FactionIsengard)
SLOTS=(); for i in $(seq 0 $((N - 1))); do SLOTS+=(--slot "human,${FACTIONS[$i]},$i,$((i % 2))"); done
COMMON=(--map "maps/map mp fall back 4p/map mp fall back 4p.map" --seed $((3000 + RANDOM % 1000)) --script --crc-interval 100 --run-ahead 2 --frames "$FRAMES"
        --lobby-timeout 600 --desync-dir "$D")
T0=$(date +%s)
"$PEER" "${COMMON[@]}" --host "$PORT" "${SLOTS[@]}" --report "$D/p0.txt" > "$D/p0.log" 2>&1 & PIDS=($!)
for i in $(seq 1 $((N - 1))); do
    sleep 1
    EXTRA=(); [ "$i" = $((N - 1)) ] && EXTRA=(--vanish-at "$VANISH")
    "$PEER" "${COMMON[@]}" --join "127.0.0.1:$PORT" --name "Joiner$i" "${EXTRA[@]}" --report "$D/p$i.txt" > "$D/p$i.log" 2>&1 & PIDS+=($!)
done
rc=0; for i in $(seq 0 $((N - 2))); do wait "${PIDS[$i]}" || rc=1; done
wait "${PIDS[$((N - 1))]}"; rv=$?
SECS=$(( $(date +%s) - T0 ))
# the crashed joiner's slot is the one its report names (the LAN lobby gives slots in the order the joins arrive)
V=$(awk '$1 == "slot" { print $2; exit }' "$D/p$((N - 1)).txt" 2> /dev/null); V="${V:--1}"; SURV=(); for i in $(seq 0 $((N - 2))); do SURV+=("$D/p$i.txt"); done
ok=1; : > "$D/result.txt"
check() { if ! eval "$1"; then ok=0; echo "  check failed: $2" >> "$D/result.txt"; fi; }
check '[ "$rc" = 0 ]' "every survivor exited 0"
check '[ "$V" -ge 1 ] && grep -q "^vanished_at " "$D/p$((N - 1)).txt"' "the last joiner (slot $V) vanished (its report says where)"
check '[ "$(cat "${SURV[@]}" | awk -v f="$FRAMES" "\$1 == \"frames\" && \$2 == f" | wc -l)" = "${#SURV[@]}" ]' "every survivor ran $FRAMES frames"
check '[ "$(cat "${SURV[@]}" | grep "^final_hash" | sort -u | wc -l)" = 1 ]' "one final hash"
check '[ "$(cat "${SURV[@]}" | awk "\$1 == \"desyncs\" { s += \$2 } END { print s + 0 }")" = 0 ]' "no desync"
check 'grep -q "^disconnect_log slot $V disconnected at frame" "$D/p0.txt"' "dropped"
check 'grep -q "^disconnect_log automatic vote for slot $V" "$D/p0.txt"' "by its bar (the screen's automatic vote, nobody pressed Kick)"
check '[ "$(grep -h "^self_destruct executed" "${SURV[@]}" | sort -u | wc -l)" = 1 ] && grep -q "^self_destruct executed 1 " "$D/p0.txt"' "its army handed over at once, alike on every survivor"
{ echo "lan players $N frames $FRAMES seconds $SECS vanished: the last joiner, slot $V (exit $rv)"; grep -h "^final_hash\|^disconnect_log\|^self_destruct " "${SURV[@]}" | sort | uniq -c | head -30; cat "$D/result.txt"; } > "$D/summary.txt"
cat "$D/summary.txt"
echo "RESULT mpdrop1 lan $N: $([ "$ok" = 1 ] && echo PASS || echo FAIL) (frames $FRAMES, ${SECS}s)"
[ "$ok" = 1 ]
