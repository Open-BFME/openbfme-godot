#!/bin/bash
# Lane ONLINE-3 end-to-end runs (docs/ONLINE.md "Phase O3"): headless peers (openbfme_peer, the scripted humans, a skirmish AI) play through the online relay
# in real time (--paced: 5 logic frames a second) while one of them crashes, rejoins, is voted out or times out, or the game is paused; afterwards the
# relay's command log is turned into a replay and played back ("CRC parity" with the game that was played).
#
#   tools/net/online3_e2e.sh <build dir> <out dir> <scenario> [frames=900]
# Scenarios (3 humans in slots 0 .. 2 and a hard AI on Fall Back 4p; slot 2 is the one that fails):
#   rejoin  slot 2 is killed (SIGKILL) at E2E_KILL_S seconds and restarted E2E_RESTART_S seconds later with a rejoin START (a version 2 ticket): it
#           re-simulates the relay's log and plays on; every peer must end on one hash
#   vote    slot 2 is killed; slots 0 and 1 press Kick on the disconnect screen: its army is handed over at once
#   window  slot 2 is killed and never returns: it is held, then handed over when the rejoin window (E2E_WINDOW_MS, default 30000) ends
#   pause   slot 1 pauses at frame 200, slot 2 resumes after 8 s: every peer stops before the same frame
#   soak    rejoin, with every peer's datagrams impaired (E2E_LINK, default latency=80,jitter=30,loss=30,dup=10,reorder=10)
#   late    lane MP-DROP-1 (S-4556): slot 2 is killed and restarted E2E_LATE_S (default 45) seconds later, while the survivors' disconnect screens are
#           up (retail's 15 s + 60 s): its new incarnation's first datagrams must not freeze the drop; it rejoins and every peer ends on one hash
#   hang    lane MP-DROP-1: slot 2's process is stopped (SIGSTOP: a hung game, no socket closed) at E2E_KILL_S and continued E2E_HANG_S (default
#           100) seconds later, after its drop: the survivors hold it, hand it over when the window ends and play on in step
#   rejoindrop lane MP-DROP-1: rejoin, and slot 1 is killed for good E2E_SECOND_S (default 5) seconds after slot 2 restarted (during its catch-up):
#           slot 1 is held, then handed over; slot 2 rejoins; slots 0 and 2 end on one hash
#   routerrejoin lane MP-DROP-1: slot 0 (the packet router) is killed and rejoins; E2E_SECOND_S (default 40) seconds after its restart slot 2 is
#           killed for good: slot 1 decides that drop with slot 0 counted gone in the agreed order (its earlier incarnation departed), the rejoined
#           slot 0 must accept it; slots 0 and 1 end on one hash
#   loaddeath lane MP-DROP-1: slot 2 is killed E2E_LOAD_KILL_S (default 3) seconds after it starts, on the load screen: the survivors leave the load
#           barrier after 4 x 15 s + 60 s (RotWK's silence limit below frame 6), drop it at frame 0 and play every frame in step
# Environment: E2E_KILL_S (default 60), E2E_RESTART_S (default 25), E2E_NICE (default 10); the peers use retail's NetworkDisconnectTime (15 s) and
# NetworkPlayerTimeoutTime (60 s). Online batch: E2E_RELAY (default <build dir>/relay/openbfme_relay): another relay binary (a previous release's peers
# through the current relay: <build dir> is the previous release's); E2E_SPECTATE_S: a spectator (a fixture spectator START) starts that many seconds
# into the game and watches to the end: its final hash must be the players' and its CRCs must match. A negative E2E_SPECTATE_S starts it that
# many seconds BEFORE the players (it HELLOs first: the relay answers NOT_BOUND until a player opened the game).
# A local relay with a throw-away key on 127.0.0.1 (tools/net/online_client.py fixture signs the STARTs).
# Needs ROTWK_INSTALL / BFME2_INSTALL. Prints "RESULT online3 <scenario>: ..." and exits 0 when the scenario's checks hold.
set -u
B="$1"; OUT="$2"; SCEN="$3"; FRAMES="${4:-900}"
PEER="$B/openbfme_peer"; RELAY="${E2E_RELAY:-$B/relay/openbfme_relay}"
HERE="$(cd "$(dirname "$0")" && pwd)"
KILL_S="${E2E_KILL_S:-60}"; RESTART_S="${E2E_RESTART_S:-25}"; NICE="${E2E_NICE:-10}"; WINDOW_MS="${E2E_WINDOW_MS:-30000}"
LINKARGS=()
[ "$SCEN" = soak ] && LINKARGS=(--link "${E2E_LINK:-latency=80,jitter=30,loss=30,dup=10,reorder=10}")
D="$OUT/$SCEN"; rm -rf "$D"; mkdir -p "$D/relaylog"
[ -x "$PEER" ] && [ -x "$RELAY" ] || { echo "RESULT online3 $SCEN: FAIL (no $PEER or $RELAY)"; exit 1; }
PROFILE_LINES="$("$PEER" --print-profile)" || { echo "RESULT online3 $SCEN: FAIL (--print-profile)"; exit 1; }
PROFILE="$(head -n1 <<< "$PROFILE_LINES")"
fac() { awk -v n="$1" '$1 == "faction" && $3 == n { print $2 }' <<< "$PROFILE_LINES"; }
MAP="maps/map mp fall back 4p/map mp fall back 4p.map"
SLOTS=(--slot "player,$(fac FactionMen),0,0,0" --slot "player,$(fac FactionMordor),1,1,1" --slot "player,$(fac FactionElves),2,2,0" --slot "hard,$(fac FactionIsengard),3,3,1")
GAME_ID=$(printf '%016x' $((0x0E300000 + RANDOM)))
KEY_HEX="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
printf '1:%s\n' "$KEY_HEX" > "$D/keys.txt"; chmod 600 "$D/keys.txt"
RPORT=$((29300 + RANDOM % 500)); RADDR="127.0.0.1:$RPORT"
"$RELAY" --bind "$RADDR" --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/relay-status.json" --stats-s 30 --idle-s 120 > "$D/relay.log" 2>&1 &
RPID=$!
sleep 0.5
python3 "$HERE/online_client.py" fixture --key-hex "$KEY_HEX" --game "$GAME_ID" --relay "$RADDR" --profile "$PROFILE" --map "$MAP" "${SLOTS[@]}" \
    --seed $((9000 + RANDOM % 1000)) --rejoin 2:1 --rejoin 0:1 --spectator 0:0 --out-dir "$D" > /dev/null || { echo "RESULT online3 $SCEN: FAIL (fixture)"; kill "$RPID"; exit 1; }

peer() { # slot, START, extra args... (always run with &: the background subshell becomes the peer process, so its PID can be killed)
    local s="$1" start="$2"; shift 2
    exec nice -n "$NICE" "$PEER" --online-start "$start" --script --paced --frames "$FRAMES" --logic-threads 2 --desync-dir "$D" "${LINKARGS[@]}" "$@" \
        > "$D/p$s.log" 2>&1
}
EXTRA0=(); EXTRA1=(); EXTRA2=()
case "$SCEN" in
    vote) EXTRA0=(--kick); EXTRA1=(--kick) ;;
    window|hang|loaddeath|rejoindrop) EXTRA0=(--rejoin-window-ms "$WINDOW_MS"); EXTRA1=(--rejoin-window-ms "$WINDOW_MS") ;;
    pause) EXTRA1=(--net-pause-at 200); EXTRA2=(--net-resume-ms 8000) ;;
esac
T0=$(date +%s)
SP=""
if [ -n "${E2E_SPECTATE_S:-}" ]; then
    ( [ "$E2E_SPECTATE_S" -gt 0 ] && sleep "$E2E_SPECTATE_S"; exec nice -n "$NICE" "$PEER" --online-start "$D/spectate-0.json" --frames "$FRAMES" --report "$D/s0.txt" > "$D/s0.log" 2>&1 ) &
    SP=$!
    [ "$E2E_SPECTATE_S" -lt 0 ] && sleep "${E2E_SPECTATE_S#-}"
fi
peer 0 "$D/start-0.json" --report "$D/p0.txt" --hashes "$D/h0.txt" "${EXTRA0[@]}" & P0=$!
peer 1 "$D/start-1.json" --report "$D/p1.txt" --hashes "$D/h1.txt" "${EXTRA1[@]}" & P1=$!
if [ "$SCEN" = pause ]; then
    peer 2 "$D/start-2.json" --report "$D/p2.txt" --hashes "$D/h2.txt" "${EXTRA2[@]}" & P2=$!
else
    nice -n "$NICE" "$PEER" --online-start "$D/start-2.json" --script --paced --frames "$FRAMES" --logic-threads 2 "${LINKARGS[@]}" \
        --report "$D/p2-first.txt" > "$D/p2-first.log" 2>&1 & P2=$!
    if [ "$SCEN" = routerrejoin ]; then
        sleep "$KILL_S"
        kill -KILL "$P0" 2> /dev/null; wait "$P0" 2> /dev/null
        echo "killed slot 0 (the packet router) at $(( $(date +%s) - T0 )) s" > "$D/events.txt"
        sleep "$RESTART_S"
        peer 0 "$D/start-0-rejoin1.json" --report "$D/p0.txt" --hashes "$D/h0.txt" & P0=$!
        echo "restarted slot 0 with its rejoin START at $(( $(date +%s) - T0 )) s" >> "$D/events.txt"
        sleep "${E2E_SECOND_S:-40}"
        kill -KILL "$P2" 2> /dev/null; wait "$P2" 2> /dev/null
        echo "killed slot 2 for good at $(( $(date +%s) - T0 )) s (slot 0 rejoined before)" >> "$D/events.txt"
    elif [ "$SCEN" = hang ]; then
        sleep "$KILL_S"
        kill -STOP "$P2"
        echo "stopped slot 2 (SIGSTOP) at $(( $(date +%s) - T0 )) s" > "$D/events.txt"
        sleep "${E2E_HANG_S:-100}"
        kill -CONT "$P2"
        echo "continued slot 2 (SIGCONT) at $(( $(date +%s) - T0 )) s" >> "$D/events.txt"
        HUNG=$P2
    else
        [ "$SCEN" = loaddeath ] && sleep "${E2E_LOAD_KILL_S:-3}" || sleep "$KILL_S"
        kill -KILL "$P2" 2> /dev/null; wait "$P2" 2> /dev/null
        echo "killed slot 2 at $(( $(date +%s) - T0 )) s" > "$D/events.txt"
    fi
    [ "$SCEN" = late ] && RESTART_S="${E2E_LATE_S:-45}"
    if [ "$SCEN" = rejoin ] || [ "$SCEN" = soak ] || [ "$SCEN" = late ] || [ "$SCEN" = rejoindrop ]; then
        sleep "$RESTART_S"
        echo "restarted slot 2 with its rejoin START at $(( $(date +%s) - T0 )) s" >> "$D/events.txt"
        peer 2 "$D/start-2-rejoin1.json" --report "$D/p2.txt" --hashes "$D/h2.txt" & P2=$!
        if [ "$SCEN" = rejoindrop ]; then
            sleep "${E2E_SECOND_S:-5}"
            kill -KILL "$P1" 2> /dev/null; wait "$P1" 2> /dev/null
            echo "killed slot 1 for good at $(( $(date +%s) - T0 )) s (slot 2 catching up)" >> "$D/events.txt"
            P1=""
        fi
    else
        P2=""
    fi
fi
rc=0
wait "$P0" || rc=1
[ -n "$P1" ] && { wait "$P1" || rc=1; }
[ -n "$P2" ] && { wait "$P2" || rc=1; }
src=0
[ -n "$SP" ] && { wait "$SP" || src=1; }
if [ -n "${HUNG:-}" ]; then
    # the hung game woke up after its drop: whatever it does then (it finds itself dropped) is not the survivors' game; it is stopped here
    sleep 5; kill -TERM "$HUNG" 2> /dev/null; wait "$HUNG" 2> /dev/null; echo "hung slot 2 exit $?" >> "$D/events.txt"
fi
SECS=$(( $(date +%s) - T0 ))
for _ in $(seq 1 50); do ls "$D"/relaylog/*.obfmerlog > /dev/null 2>&1 && break; sleep 0.2; done
kill -TERM "$RPID" 2> /dev/null; wait "$RPID" 2> /dev/null
LOG="$(ls "$D"/relaylog/*.obfmerlog 2> /dev/null | head -n1)"
conv=1; play=1
if [ -n "$LOG" ]; then
    "$PEER" --relay-log "$LOG" --online-start "$D/start-0.json" --record "$D/server.replay" --report "$D/convert.txt" > "$D/convert.log" 2>&1; conv=$?
    [ -f "$D/server.replay" ] && { "$PEER" --replay "$D/server.replay" --report "$D/replay.txt" > "$D/replay.log" 2>&1; play=$?; }
fi
v() { awk -v k="$2" '$1 == k { $1 = ""; sub(/^ /, ""); print; exit }' "$1" 2> /dev/null; }
REPORTS=("$D/p0.txt"); [ "$SCEN" != rejoindrop ] && REPORTS+=("$D/p1.txt"); [ -f "$D/p2.txt" ] && REPORTS+=("$D/p2.txt")
finals=$(cat "${REPORTS[@]}" 2> /dev/null | grep "^final_hash" | sort -u | wc -l)
desyncs=$(cat "${REPORTS[@]}" 2> /dev/null | awk '$1 == "desyncs" { s += $2 } END { print s + 0 }')
frames_ok=$(cat "${REPORTS[@]}" 2> /dev/null | awk -v f="$FRAMES" '$1 == "frames" && $2 >= f' | wc -l)
compared=$(v "$D/replay.txt" hashes_compared); mism=$(v "$D/replay.txt" mismatches)
ok=1
check() { if ! eval "$1"; then ok=0; echo "  check failed: $2" >> "$D/result.txt"; fi; }
: > "$D/result.txt"
check '[ "$rc" = 0 ]' "every peer process exited 0"
check '[ "$frames_ok" = "${#REPORTS[@]}" ]' "every peer ran $FRAMES frames"
check '[ "$desyncs" = 0 ]' "no desync"
check '[ "$finals" = 1 ]' "one final hash"
check '[ "$conv" = 0 ] && [ "$play" = 0 ] && [ "${compared:-0}" -ge 1 ] && [ "${mism:-1}" = 0 ]' "the relay log's replay matches the players' CRCs"
if [ -n "$SP" ]; then
    check '[ "$src" = 0 ] && grep -q "^spectate state ended reached $FRAMES .* crc_mismatches 0 " "$D/s0.txt"' "the spectator watched to frame $FRAMES with no CRC mismatch"
    check '[ "$(v "$D/s0.txt" final_hash)" = "$(v "$D/p0.txt" final_hash)" ]' "the spectator's final hash is the players'"
fi
case "$SCEN" in
    rejoin|soak|late)
        check 'grep -q "^rejoin generation 1 live_frame" "$D/p2.txt"' "slot 2 rejoined"
        check 'grep -q "crc_mismatches 0" "$D/p2.txt"' "its catch-up matched every CRC in the log"
        check '[ "$(v "$D/p2.txt" crc_checks_passed)" -ge 1 ]' "the rejoined slot took part in a CRC exchange"
        check 'grep -q "^online_slots .* rejoined 2@" "$D/p0.txt" && grep -q "^online_slots .* rejoined 2@" "$D/p1.txt"' "both others took slot 2 back"
        check '[ "$(grep "^online_slots" "$D/p0.txt" | sed "s/.* rejoined //; s/ .*//")" = "$(grep "^online_slots" "$D/p1.txt" | sed "s/.* rejoined //; s/ .*//")" ]' "at one frame"
        check 'grep -q "^self_destruct executed 0 " "$D/p0.txt"' "no hand-over" ;;
    vote)
        check 'grep -q "^online_slots held none handed_over 2" "$D/p0.txt" && grep -q "^online_slots held none handed_over 2" "$D/p1.txt"' "slot 2 handed over"
        check 'grep -q "^self_destruct executed 1 transfers 1" "$D/p0.txt"' "its army went to an ally"
        check 'grep -q "^disconnect_log vote of slot 0 to drop slot 2" "$D/p1.txt"' "by a Kick vote"
        check '! grep -q "holding slot 2" "$D/p0.txt"' "not held" ;;
    routerrejoin)
        check 'grep -q "^rejoin generation 1 live_frame" "$D/p0.txt"' "slot 0 rejoined"
        check 'grep -q "^disconnect_log dropping slot 2 at frame" "$D/p1.txt"' "slot 1 decided the drop of slot 2"
        check 'grep -q "^disconnect_log slot 2 disconnected at frame" "$D/p0.txt"' "the rejoined slot 0 applied it"
        check '! grep -q "DISCONNECTPLAYER of slot [0-9]* from slot [0-9]*.*: refused$" "$D/p0.txt" "$D/p0.log"' "nothing refused" ;;
    rejoindrop)
        check 'grep -q "^rejoin generation 1 live_frame" "$D/p2.txt"' "slot 2 rejoined"
        check 'grep -q "crc_mismatches 0" "$D/p2.txt"' "its catch-up matched every CRC in the log"
        check 'grep -q "rejoin_log slot 1 held for a rejoin" "$D/p0.txt"' "slot 1 held"
        check 'grep -q "^online_slots held none handed_over 1" "$D/p0.txt" && grep -q "^online_slots held none handed_over 1" "$D/p2.txt"' "slot 1 handed over on both" ;;
    window|hang|loaddeath)
        check 'grep -q "rejoin_log slot 2 held for a rejoin" "$D/p0.txt"' "slot 2 held"
        check 'grep -q "^rejoin_log the rejoin window of slot 2 ended" "$D/p0.txt" || grep -q "^rejoin_log the rejoin window of slot 2 ended" "$D/p1.txt"' "the window ended (packet router)"
        check 'grep -q "^online_slots held none handed_over 2" "$D/p0.txt" && grep -q "^online_slots held none handed_over 2" "$D/p1.txt"' "then handed over"
        check '[ "$(grep -h "^self_destruct_checkpoint" "$D/p0.txt")" = "$(grep -h "^self_destruct_checkpoint" "$D/p1.txt")" ]' "on one frame, to the same ally"
        [ "$SCEN" = loaddeath ] && check 'grep -q "^disconnect_log slot 2 disconnected at frame 0$" "$D/p0.txt"' "dropped at frame 0 (the load barrier)" ;;
    pause)
        stops=$(grep -h "^pause " "${REPORTS[@]}" | awk '{print $5}' | sort -u)
        check '[ "$(wc -w <<< "$stops")" = 1 ] && [ "$stops" -gt 0 ]' "every peer stopped before the same frame ($stops)"
        check '[ "$(grep -h "^pause " "${REPORTS[@]}" | awk "\$7 >= 7000" | wc -l)" = 3 ]' "for at least 7 s on every peer"
        check 'grep -q "^pause_log resumed by this peer" "$D/p2.txt"' "resumed by slot 2" ;;
esac
{
    echo "scenario $SCEN frames $FRAMES seconds $SECS game $GAME_ID"
    cat "$D/events.txt" 2> /dev/null
    [ -n "$SP" ] && grep -h "^spectate \|^final_hash" "$D/s0.txt" 2> /dev/null | sed "s/^/spectator /"
    grep -h "^slot \|^final_hash\|^crc_checks_passed\|^desyncs\|^rejoin \|^online_slots\|^pause \|^self_destruct \|^stall_ms\|^relay " "${REPORTS[@]}" 2> /dev/null
    echo "relay_log $(basename "${LOG:-none}") convert_exit $conv replay_exit $play hashes_compared ${compared:-0} mismatches ${mism:-?}"
    cat "$D/result.txt"
} > "$D/summary.txt"
cat "$D/summary.txt"
echo "RESULT online3 $SCEN: $([ "$ok" = 1 ] && echo PASS || echo FAIL) (frames $FRAMES, ${SECS}s, final hashes $finals, desyncs $desyncs, replay CRCs ${compared:-0} compared ${mism:-?} mismatched)"
[ "$ok" = 1 ]
