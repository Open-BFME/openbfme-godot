#!/bin/bash
# Lane ONLINE-3 on a deployed lobby + relay (the home lab: services/online/deploy/selfhost.sh) with headless peers on two hosts: the lobby client runs
# here (tools/net/online_client.py: host, joins, the rejoin START), the peers run where RUN_A / RUN_B send them. Scenarios as tools/net/online3_e2e.sh
# (rejoin, late, vote, window, pause, soak; "late" restarts slot 2 only after the others held it, while their game goes on).
#
#   tools/net/online3_homelab.sh <lobby URL> <out dir> <scenario> [frames=900]
#   RUN_A / RUN_B: a command that runs its last argument (a bash snippet) in this repository on a host with ROTWK_INSTALL / BFME2_INSTALL set and $B the
#     build directory (for example a CI runner); slots 0 and 1 run on A, slot 2 (the one that fails and comes back) on B. PROFILE: the peers' profile MD5
#     (openbfme_peer --print-profile; both hosts must agree). E2E_KILL_S (60), E2E_RESTART_S (25; "late": 110), E2E_LINK (soak), E2E_WINDOW_MS (30000).
#   tools/net/online3_homelab.sh peer <slot> <START as base64> <frames> <kill after s, 0 = never> [peer args...]    (what RUN_A / RUN_B run)
# Prints "RESULT online3 homelab <scenario>: PASS|FAIL ..." with every peer's report lines; the relay's log of the game is fetched from the lobby
# (/v1/replays/<id>/log, uploaded by the relay a minute after the game) and replayed on host A ("CRC parity").
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ "${1:-}" = peer ]; then
    SLOT="$2"; START_B64="$3"; FRAMES="$4"; KILL="$5"; shift 5
    W="$(mktemp -d)"; base64 -d <<< "$START_B64" > "$W/start.json"
    "$B/openbfme_peer" --online-start "$W/start.json" --script --paced --frames "$FRAMES" --logic-threads 2 --report "$W/report.txt" "$@" > "$W/peer.log" 2>&1 &
    P=$!
    if [ "$KILL" -gt 0 ]; then
        sleep "$KILL"; kill -KILL "$P" 2> /dev/null; wait "$P" 2> /dev/null; echo "PEER $SLOT killed after $KILL s"
    else
        wait "$P"; echo "PEER $SLOT exit $?"
    fi
    sed "s/^/REPORT $SLOT /" "$W/report.txt" 2> /dev/null | grep -v "^REPORT $SLOT stop "
    tail -n 8 "$W/peer.log" | sed "s/^/PEERLOG $SLOT /"
    rm -rf "$W"
    exit 0
fi
if [ "${1:-}" = replay ]; then
    # on host A: the lobby's copy of the relay log and the published START, assembled into a replay and played back
    LOBBY="$2"; ID="$3"; W="$(mktemp -d)"
    # the lobby serves a replay only to one of the game's players (DEPLOY-2: LOBBY_TOKEN, a player's guest session; the replay downloads are not public)
    AUTH=(-H "authorization: Bearer ${LOBBY_TOKEN:?LOBBY_TOKEN}")
    curl -fsS "${AUTH[@]}" "$LOBBY/v1/replays/$ID/log" -o "$W/game.obfmerlog" && curl -fsS "${AUTH[@]}" "$LOBBY/v1/replays/$ID/start" -o "$W/start.json" || { echo "REPLAY no log"; exit 1; }
    "$B/openbfme_peer" --relay-log "$W/game.obfmerlog" --online-start "$W/start.json" --record "$W/server.replay" --report "$W/convert.txt" > "$W/convert.log" 2>&1
    echo "REPLAY convert_exit $?"; grep -h "^relay_log" "$W/convert.txt" | sed "s/^/REPLAY /"; tail -n 3 "$W/convert.log" | sed "s/^/REPLAY convert_log /"
    "$B/openbfme_peer" --replay "$W/server.replay" --report "$W/replay.txt" > "$W/replay.log" 2>&1
    echo "REPLAY replay_exit $?"; sed "s/^/REPLAY /" "$W/replay.txt"
    rm -rf "$W"
    exit 0
fi
LOBBY="$1"; OUT="$2"; SCEN="$3"; FRAMES="${4:-900}"
: "${RUN_A:?RUN_A}"; : "${RUN_B:?RUN_B}"; : "${PROFILE:?PROFILE}"
KILL_S="${E2E_KILL_S:-60}"; RESTART_S="${E2E_RESTART_S:-25}"; WINDOW_MS="${E2E_WINDOW_MS:-30000}"
[ "$SCEN" = late ] && RESTART_S="${E2E_RESTART_S:-110}"
D="$OUT/$SCEN"; rm -rf "$D"; mkdir -p "$D"
C="python3 $HERE/online_client.py"
for run in "$RUN_A" "$RUN_B"; do # every host's build must have the game's profile (the build is part of it): a rebuilt host refuses its START
    got="$($run '$B/openbfme_peer --print-profile | head -n1' 2> /dev/null | grep -E '^[0-9a-f]{32}$' | tail -n1)"
    [ "$got" = "$PROFILE" ] || { echo "RESULT online3 homelab $SCEN: FAIL (a host's profile is ${got:-unknown}, not $PROFILE: rebuild both hosts from one tree)"; exit 1; }
done
FAC=(${E2E_FACTIONS:-0 1 2 3}) # player template indices (openbfme_peer --print-profile's "faction" lines)
$C host --lobby "$LOBBY" --name Host0 --profile "$PROFILE" --map "maps/map mp fall back 4p/map mp fall back 4p.map" --slot "player,${FAC[0]},0,0,0" \
    --slot open --slot open --slot "hard,${FAC[3]},3,3,1" --players 3 --id-file "$D/id.txt" --token-file "$D/t0" --out "$D/s0.json" --timeout 300 > "$D/host.log" 2>&1 &
HP=$!
for _ in $(seq 1 100); do [ -s "$D/id.txt" ] && break; sleep 0.2; done
ID="$(cat "$D/id.txt")"
$C join --lobby "$LOBBY" --name Join1 --profile "$PROFILE" --game "$ID" --faction "${FAC[1]}" --color 1 --start-pos 1 --team 1 --token-file "$D/t1" --out "$D/s1.json" > "$D/j1.log" 2>&1 &
J1=$!
$C join --lobby "$LOBBY" --name Join2 --profile "$PROFILE" --game "$ID" --faction "${FAC[2]}" --color 2 --start-pos 2 --team 0 --token-file "$D/t2" --out "$D/s2.json" > "$D/j2.log" 2>&1 &
J2=$!
wait "$HP" "$J1" "$J2" || { echo "RESULT online3 homelab $SCEN: FAIL (the lobby did not start the game)"; exit 1; }
LINK=(); [ "$SCEN" = soak ] && LINK=(--link "${E2E_LINK:-latency=80,jitter=30,loss=30,dup=10,reorder=10}")
A0=(); A1=(); A2=()
case "$SCEN" in
    vote) A0=(--kick); A1=(--kick) ;;
    window) A0=(--rejoin-window-ms "$WINDOW_MS"); A1=(--rejoin-window-ms "$WINDOW_MS") ;;
    pause) A1=(--net-pause-at 200); A2=(--net-resume-ms 8000) ;;
esac
b64() { base64 -w0 < "$1"; }
T0=$(date +%s)
$RUN_A "bash tools/net/online3_homelab.sh peer 0 $(b64 "$D/s0.json") $FRAMES 0 ${LINK[*]} ${A0[*]} & bash tools/net/online3_homelab.sh peer 1 $(b64 "$D/s1.json") $FRAMES 0 ${LINK[*]} ${A1[*]}; wait" > "$D/hostA.txt" 2>&1 &
PA=$!
KILL=$KILL_S; [ "$SCEN" = pause ] && KILL=0
$RUN_B "bash tools/net/online3_homelab.sh peer 2 $(b64 "$D/s2.json") $FRAMES $KILL ${LINK[*]} ${A2[*]}" > "$D/hostB.txt" 2>&1
echo "slot 2 first process ended at $(( $(date +%s) - T0 )) s" > "$D/events.txt"
if [ "$SCEN" = rejoin ] || [ "$SCEN" = late ] || [ "$SCEN" = soak ]; then
    sleep $(( RESTART_S > 0 ? RESTART_S : 1 ))
    $C rejoin --lobby "$LOBBY" --token-file "$D/t2" --game "$ID" --out "$D/r2.json" > "$D/rejoin.log" 2>&1
    echo "slot 2 rejoin START at $(( $(date +%s) - T0 )) s: $(tail -1 "$D/rejoin.log")" >> "$D/events.txt"
    $RUN_B "bash tools/net/online3_homelab.sh peer 2 $(b64 "$D/r2.json") $FRAMES 0 ${LINK[*]}" > "$D/hostB2.txt" 2>&1
fi
wait "$PA"
SECS=$(( $(date +%s) - T0 ))
for _ in $(seq 1 60); do # the relay uploads a finished log within about a minute (openbfme-relay-upload.timer)
    curl -fsS -H "authorization: Bearer $(cat "$D/t0")" -o /dev/null "$LOBBY/v1/replays/$ID/log" 2> /dev/null && break; sleep 5
done
$RUN_A "LOBBY_TOKEN=$(cat "$D/t0") bash tools/net/online3_homelab.sh replay $LOBBY $ID" > "$D/replay.txt" 2>&1
R() { grep -h "^REPORT $1 " "$D"/host*.txt | sed "s/^REPORT $1 //"; }
f() { R "$1" | awk -v k="$2" '$1 == k { $1 = ""; sub(/^ /, ""); print; exit }'; }
REPORTERS="0 1"; ( [ "$SCEN" = pause ] || [ "$SCEN" = rejoin ] || [ "$SCEN" = late ] || [ "$SCEN" = soak ] ) && REPORTERS="0 1 2"
finals=$(for s in $REPORTERS; do f "$s" final_hash; done | sort -u | wc -l)
desyncs=$(for s in $REPORTERS; do f "$s" desyncs; done | awk '{ t += $1 } END { print t + 0 }')
frames_ok=$(for s in $REPORTERS; do f "$s" frames; done | awk -v n="$FRAMES" '$1 >= n' | wc -l)
compared=$(awk '$2 == "hashes_compared" { print $3 }' "$D/replay.txt"); mism=$(awk '$2 == "mismatches" { print $3 }' "$D/replay.txt")
ok=1; : > "$D/result.txt"
check() { if ! eval "$1"; then ok=0; echo "  check failed: $2" >> "$D/result.txt"; fi; }
check '[ "$frames_ok" = "$(wc -w <<< "$REPORTERS")" ]' "every reporting peer ran $FRAMES frames"
check '[ "$desyncs" = 0 ]' "no desync"
check '[ "$finals" = 1 ]' "one final hash"
check '[ "${compared:-0}" -ge 1 ] && [ "${mism:-1}" = 0 ]' "the relay log's replay matches the players' CRCs"
case "$SCEN" in
    rejoin|late|soak)
        check 'R 2 | grep -q "^rejoin generation 1 live_frame"' "slot 2 rejoined"
        check 'R 2 | grep -q "crc_mismatches 0"' "its catch-up matched the log's CRCs"
        check '[ "$(f 2 crc_checks_passed)" -ge 1 ]' "it took part in a CRC exchange"
        check '[ "$(R 0 | grep "^online_slots" | sed "s/.* rejoined //; s/ .*//")" = "$(R 1 | grep "^online_slots" | sed "s/.* rejoined //; s/ .*//")" ]' "taken back at one frame" ;;
    vote) check 'R 0 | grep -q "^online_slots held none handed_over 2" && R 1 | grep -q "^online_slots held none handed_over 2"' "handed over by the vote" ;;
    window) check 'R 0 | grep -q "^online_slots held none handed_over 2" && R 0 | grep -q "held for a rejoin"' "held, then handed over" ;;
    pause) check '[ "$(for s in 0 1 2; do R $s | awk "\$1 == \"pause\" { print \$5 }"; done | sort -u | wc -l)" = 1 ]' "one stop frame" ;;
esac
{
    echo "homelab scenario $SCEN game $ID frames $FRAMES seconds $SECS"
    cat "$D/events.txt"
    for s in 0 1 2; do R "$s" | grep "^slot \|^final_hash\|^crc_checks_passed\|^desyncs\|^rejoin \|^online_slots\|^pause \|^self_destruct \|^relay \|^stall_ms"; done
    grep -h "^REPLAY \(convert_exit\|replay_exit\|hashes_compared\|mismatches\|final_hash\)" "$D/replay.txt"
    cat "$D/result.txt"
} > "$D/summary.txt"
cat "$D/summary.txt"
echo "RESULT online3 homelab $SCEN: $([ "$ok" = 1 ] && echo PASS || echo FAIL) (game $ID, ${SECS}s, final hashes $finals, desyncs $desyncs, replay CRCs ${compared:-0} compared ${mism:-?} mismatched)"
[ "$ok" = 1 ]
