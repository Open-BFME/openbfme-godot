#!/bin/bash
# Lane ONLINE-3: how long a rejoining client's catch-up takes (docs/ONLINE.md "Phase O3"): a game of 3 scripted humans and a hard AI is played at full
# speed through a local relay (no pacing), then the relay's log is re-simulated from frame 0 as a rejoin does (openbfme_peer --relay-log --catch-up:
# CommandLogCatchUp, the players' CRCs compared) to each target frame, timed. 3,000 frames are 10 minutes of a game at 5 logic frames a second.
#   tools/net/online3_catchup.sh <build dir> <out dir> [targets="3000 9000"] [map]
# Needs ROTWK_INSTALL / BFME2_INSTALL. Prints one "catch_up ..." line per target and "RESULT online3 catch-up: ...".
set -u
B="$1"; OUT="$2"; TARGETS="${3:-3000 9000}"; MAP="${4:-maps/map mp fall back 4p/map mp fall back 4p.map}"
PEER="$B/openbfme_peer"; RELAY="$B/relay/openbfme_relay"; HERE="$(cd "$(dirname "$0")" && pwd)"
FRAMES=0; for t in $TARGETS; do [ "$t" -gt "$FRAMES" ] && FRAMES=$t; done
FRAMES=$((FRAMES + 10))
D="$OUT/catchup"; rm -rf "$D"; mkdir -p "$D/relaylog"
PROFILE_LINES="$("$PEER" --print-profile)" || exit 1
PROFILE="$(head -n1 <<< "$PROFILE_LINES")"
fac() { awk -v n="$1" '$1 == "faction" && $3 == n { print $2 }' <<< "$PROFILE_LINES"; }
KEY_HEX="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"; printf '1:%s\n' "$KEY_HEX" > "$D/keys.txt"; chmod 600 "$D/keys.txt"
RADDR="127.0.0.1:$((29800 + RANDOM % 150))"
"$RELAY" --bind "$RADDR" --keys "$D/keys.txt" --log-dir "$D/relaylog" --idle-s 120 > "$D/relay.log" 2>&1 & RPID=$!
sleep 0.5
python3 "$HERE/online_client.py" fixture --key-hex "$KEY_HEX" --game "$(printf '%016x' $((0x0C470000 + RANDOM)))" --relay "$RADDR" --profile "$PROFILE" \
    --map "$MAP" --slot "player,$(fac FactionMen),0,0,0" --slot "player,$(fac FactionMordor),1,1,1" --slot "player,$(fac FactionElves),2,2,0" \
    --slot "hard,$(fac FactionIsengard),3,3,1" --seed 4242 --out-dir "$D" > /dev/null || exit 1
T0=$(date +%s); PIDS=()
for s in 0 1 2; do
    "$PEER" --online-start "$D/start-$s.json" --script --frames "$FRAMES" --logic-threads 2 --report "$D/p$s.txt" > "$D/p$s.log" 2>&1 & PIDS+=($!)
done
rc=0; for p in "${PIDS[@]}"; do wait "$p" || rc=1; done
PLAY=$(( $(date +%s) - T0 ))
for _ in $(seq 1 50); do ls "$D"/relaylog/*.obfmerlog > /dev/null 2>&1 && break; sleep 0.2; done
kill -TERM "$RPID" 2> /dev/null; wait "$RPID" 2> /dev/null
LOG="$(ls "$D"/relaylog/*.obfmerlog | head -n1)"
echo "game: $FRAMES frames played in ${PLAY}s (peers exit $rc), final hashes $(grep -h ^final_hash "$D"/p*.txt | sort -u | wc -l), log $(stat -c %s "$LOG") bytes, $(nproc) CPUs, load $(cut -d' ' -f1-3 /proc/loadavg)" | tee "$D/summary.txt"
ok=$((rc == 0))
for t in $TARGETS; do
    "$PEER" --relay-log "$LOG" --online-start "$D/start-0.json" --catch-up "$t" --report "$D/catchup-$t.txt" > "$D/catchup-$t.log" 2>&1 || ok=0
    echo "target $t ($((t / 300)) min): $(cat "$D/catchup-$t.txt")" | tee -a "$D/summary.txt"
done
echo "RESULT online3 catch-up: $([ "$ok" = 1 ] && echo PASS || echo FAIL)" | tee -a "$D/summary.txt"
[ "$ok" = 1 ]
