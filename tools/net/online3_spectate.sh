#!/bin/bash
# Lane ONLINE-3 spectators on a deployed lobby + relay (the home lab: services/online/deploy/selfhost.sh): a 2-player game (slot 0 on host A, slot 1 on
# host B, a hard AI in slot 2) watched by spectators (openbfme_peer with the lobby's spectator START: it catches up from the relay's log and follows it,
# comparing its state with every CRC the players logged, and stops at the players' last frame so its final hash is compared with theirs).
#
#   tools/net/online3_spectate.sh <lobby URL> <out dir> <scenario> [frames=900]
#     start  two spectators (one on each host) ask before the game starts; the one on B also sends a forged frame command (the relay must refuse it)
#     mid    a spectator joins 60 s into the game (host A); another (host B) is killed after 30 s and asks again 15 s later (a new spectator place)
#     delay  the host sets a 60 s spectator delay; a spectator on host A, timed against slot 0 on the same host
#     delaypause  the same, and slot 0 pauses the game at frame 300 for 20 s (slot 1 resumes): the delay counts game frames, so the pause holds it
#   RUN_A / RUN_B: as tools/net/online3_homelab.sh (a command running its last argument in this repository on a host with ROTWK_INSTALL / BFME2_INSTALL
#   and $B the build); PROFILE: the peers' profile MD5; RELAY_STATUS (optional): a command printing the relay's status JSON (its spectator counters)
#   tools/net/online3_spectate.sh spect <name> <START as base64> <frames> <kill after s, 0 = never> [peer args...]    (what RUN_A / RUN_B run)
# Prints "RESULT online3 spectate <scenario>: PASS|FAIL ..." with every peer's and spectator's report lines.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ "${1:-}" = spect ]; then
    # SOURCE: "b64|<START as base64>" or "lobby|<URL>|<game id>|<ask after s>" (asked here, at that time); KILL > 0: killed after KILL s, then asks
    # again AGAIN s later (as <name>b, a new spectator place) and watches to the end
    NAME="$2"; SOURCE="$3"; FRAMES="$4"; KILL="$5"; AGAIN="$6"; shift 6
    W="$(mktemp -d)"
    ask() { # name -> $W/<name>.json
        if [ "${SOURCE%%|*}" = b64 ]; then base64 -d <<< "${SOURCE#b64|}" > "$W/$1.json"; return; fi
        IFS='|' read -r _ url id _ <<< "$SOURCE"
        python3 tools/net/online_client.py spectate --lobby "$url" --name "$1" --game "$id" --out "$W/$1.json" > /dev/null
    }
    run() { # name, kill after s, peer args...
        local n="$1" k="$2"; shift 2
        echo "SSTART $n $(python3 -c "import json,sys; d = json.load(open(sys.argv[1])); print(d['spectator'], d['delay'])" "$W/$n.json")"
        "$B/openbfme_peer" --online-start "$W/$n.json" --frames "$FRAMES" --report "$W/$n.txt" "$@" > "$W/$n.log" 2>&1 &
        local p=$!
        if [ "$k" -gt 0 ]; then sleep "$k"; kill -KILL "$p" 2> /dev/null; wait "$p" 2> /dev/null; echo "SPECT $n killed after $k s"
        else wait "$p"; echo "SPECT $n exit $?"; fi
        sed "s/^/SREPORT $n /" "$W/$n.txt" 2> /dev/null | grep -v "^SREPORT $n stop "
        tail -n 6 "$W/$n.log" | sed "s/^/SPECTLOG $n /"
    }
    [ "${SOURCE%%|*}" = lobby ] && sleep "$(cut -d'|' -f4 <<< "$SOURCE")"
    ask "$NAME"
    run "$NAME" "$KILL" "$@"
    if [ "$KILL" -gt 0 ]; then
        sleep "$AGAIN"; ask "${NAME}b"
        run "${NAME}b" 0 "$@"
    fi
    rm -rf "$W"
    exit 0
fi
LOBBY="$1"; OUT="$2"; SCEN="$3"; FRAMES="${4:-900}"
: "${RUN_A:?RUN_A}"; : "${RUN_B:?RUN_B}"; : "${PROFILE:?PROFILE}"
D="$OUT/$SCEN"; rm -rf "$D"; mkdir -p "$D"
C="python3 $HERE/online_client.py"
for run in "$RUN_A" "$RUN_B"; do # every host's build must have the game's profile (a rebuilt host refuses its START)
    got="$($run '$B/openbfme_peer --print-profile | head -n1' 2> /dev/null | grep -E '^[0-9a-f]{32}$' | tail -n1)"
    [ "$got" = "$PROFILE" ] || { echo "RESULT online3 spectate $SCEN: FAIL (a host's profile is ${got:-unknown}, not $PROFILE: rebuild both hosts from one tree)"; exit 1; }
done
relay_counters() { [ -n "${RELAY_STATUS:-}" ] && $RELAY_STATUS 2> /dev/null | tr ',{}' '\n\n\n' | grep -E '"spectator' | tr -d '"' | tr '\n' ' '; echo; }
echo "before $(relay_counters)" > "$D/relay.txt"
DELAY=0; { [ "$SCEN" = delay ] || [ "$SCEN" = delaypause ]; } && DELAY=60
FAC=(${E2E_FACTIONS:-0 1 2})
$C host --lobby "$LOBBY" --name Host0 --profile "$PROFILE" --map "maps/map mp fall back 4p/map mp fall back 4p.map" --slot "player,${FAC[0]},0,0,0" \
    --slot open --slot "hard,${FAC[2]},2,2,1" --players 2 --spectator-delay "$DELAY" --id-file "$D/id.txt" --out "$D/s0.json" --timeout 300 > "$D/host.log" 2>&1 &
HP=$!
for _ in $(seq 1 100); do [ -s "$D/id.txt" ] && break; sleep 0.2; done
ID="$(cat "$D/id.txt")"
SP=()
if [ "$SCEN" = start ]; then # the spectators ask before the host starts the game: the lobby answers once it has
    $C spectate --lobby "$LOBBY" --name Watcher1 --game "$ID" --wait 300 --out "$D/v1.json" > "$D/v1.log" 2>&1 & SP+=($!)
    $C spectate --lobby "$LOBBY" --name Watcher2 --game "$ID" --wait 300 --out "$D/v2.json" > "$D/v2.log" 2>&1 & SP+=($!)
    sleep 2
fi
$C join --lobby "$LOBBY" --name Join1 --profile "$PROFILE" --game "$ID" --faction "${FAC[1]}" --color 1 --start-pos 1 --team 0 --out "$D/s1.json" > "$D/j1.log" 2>&1 &
J1=$!
wait "$HP" "$J1" || { echo "RESULT online3 spectate $SCEN: FAIL (the lobby did not start the game)"; exit 1; }
[ "${#SP[@]}" -gt 0 ] && { wait "${SP[@]}" || { echo "RESULT online3 spectate $SCEN: FAIL (no spectator START)"; exit 1; }; }
b64() { base64 -w0 < "$1"; }
# jpc-run (and a CI runner) runs one job of a lane at a time on a host: each host's players and spectators go in one command
SA=""; SB=""; A0=""; B0=""
case "$SCEN" in
    start) SA="bash tools/net/online3_spectate.sh spect V1 'b64|$(b64 "$D/v1.json")' $FRAMES 0 0"
           SB="bash tools/net/online3_spectate.sh spect V2 'b64|$(b64 "$D/v2.json")' $FRAMES 0 0 --spectate-forge" ;;
    mid)   SA="bash tools/net/online3_spectate.sh spect V1 'lobby|$LOBBY|$ID|60' $FRAMES 0 0"
           SB="bash tools/net/online3_spectate.sh spect V2 'lobby|$LOBBY|$ID|0' $FRAMES 30 15" ;;
    delay) SA="bash tools/net/online3_spectate.sh spect V1 'lobby|$LOBBY|$ID|0' $FRAMES 0 0 --frame-times"; A0="--frame-times" ;;
    delaypause) SA="bash tools/net/online3_spectate.sh spect V1 'lobby|$LOBBY|$ID|0' $FRAMES 0 0 --frame-times"
                A0="--frame-times --net-pause-at 300"; B0="--net-resume-ms 20000" ;;
esac
T0=$(date +%s)
host() { # slot, START, peer args, spectator command
    echo "T=\$(mktemp -d); bash tools/net/online3_homelab.sh peer $1 $(b64 "$2") $FRAMES 0 $3 > \$T/p 2>&1 & ${4:-true} > \$T/s 2>&1; wait; cat \$T/p \$T/s; rm -rf \$T"
}
$RUN_A "$(host 0 "$D/s0.json" "$A0" "$SA")" > "$D/hostA.txt" 2>&1 & PA=$!
$RUN_B "$(host 1 "$D/s1.json" "$B0" "$SB")" > "$D/hostB.txt" 2>&1 & PB=$!
wait "$PA" "$PB"
SECS=$(( $(date +%s) - T0 ))
sleep 35 # the relay drops a spectator silent for 30 s (the killed one is long gone; the finished ones said nothing more)
echo "after $(relay_counters)" >> "$D/relay.txt"
P() { grep -h "^REPORT $1 " "$D"/host*.txt | sed "s/^REPORT $1 //"; }
S() { grep -h "^SREPORT $1 " "$D"/host*.txt | sed "s/^SREPORT $1 //"; }
f() { awk -v k="$1" '$1 == k { $1 = ""; sub(/^ /, ""); print; exit }'; }
idx() { awk -v n="$1" '$1 == "SSTART" && $2 == n { print $3; exit }' "$D"/host*.txt; }
ph0="$(P 0 | f final_hash)"; ph1="$(P 1 | f final_hash)"
ok=1; : > "$D/result.txt"
check() { if ! eval "$1"; then ok=0; echo "  check failed: $2" >> "$D/result.txt"; fi; }
check '[ -n "$ph0" ] && [ "$ph0" = "$ph1" ] && [ "$(P 0 | f desyncs)" = 0 ] && [ "$(P 1 | f desyncs)" = 0 ]' "the players ran $FRAMES frames to one final hash without a desync"
case "$SCEN" in start) WATCH="V1 V2" ;; mid) WATCH="V1 V2b" ;; delay|delaypause) WATCH="V1" ;; esac
for v in $WATCH; do
    check '[ "$(S $v | f final_hash)" = "$ph0" ]' "spectator $v's final hash at frame $FRAMES is the players'"
    check 'S $v | grep -q "^spectate state ended reached $FRAMES .* crc_mismatches 0 "' "spectator $v reached frame $FRAMES with no CRC mismatch"
    check '[ "$(S $v | sed -n "s/.* crcs_compared \([0-9]*\) .*/\1/p")" -ge 1 ]' "spectator $v compared the players' CRCs"
    check 'S $v | grep -q "^local_player ReplayObserver observer 1"' "spectator $v watches as the observer"
    check 'S $v | grep -q "^spectate_relay .* commands_sent 0$"' "spectator $v sent no command (its transport has no peer)"
done
case "$SCEN" in
    start)
        check 'S V2 | grep -q "^spectate_relay bound 1 forged 1 refused 1 "' "the relay refused the forged command from the spectator ticket"
        check '[ -n "$(idx V1)" ] && [ "$(idx V1)" != "$(idx V2)" ]' "two spectator places" ;;
    mid)
        check 'grep -q "^SPECT V2 killed" "$D/hostB.txt"' "spectator V2 was killed"
        check '[ -n "$(idx V2b)" ] && [ "$(idx V2b)" != "$(idx V2)" ]' "its second START is a new spectator place" ;;
    delay|delaypause)
        # the relay serves frame F only once a player logged a command for frame F + 300 (60 s at 5 frames a second): slot 0 had then reached at least
        # F + 300 - 2 x run-ahead (lockstep keeps the players within the run-ahead of each other); the spectator's frame F ran after that (one host clock)
        RA=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['runAhead'])" "$D/s0.json")
        P 0 | awk '$1 == "frame_ms" { print $2, $3 }' > "$D/p0_times.txt"; S V1 | awk '$1 == "frame_ms" { print $2, $3 }' > "$D/v1_times.txt"
        lags="$(awk -v ra="$RA" 'NR == FNR { t[$1] = $2; next } { need = $1 + 300 - 2 * ra - 1; if (need in t) { printf "%d:%d:%s ", $1, $2 - t[$1], ($2 >= t[need] ? "ok" : "EARLY") } }' "$D/p0_times.txt" "$D/v1_times.txt")"
        echo "delay lags (frame:ms behind slot 0:bound) $lags" > "$D/events.txt"
        check '[ -n "$lags" ] && ! grep -q EARLY <<< "$lags"' "the delayed spectator saw no frame before the players were 60 s of game time past it"
        check 'S V1 | grep -q "^spectator [0-9]* delay 60"' "the spectator START carries the host's delay"
        if [ "$SCEN" = delaypause ]; then
            check 'P 0 | grep -q "^pause pauses_seen 1 " && P 1 | grep -q "^pause pauses_seen 1 "' "both players paused once"
        fi ;;
esac
{
    echo "spectate scenario $SCEN game $ID frames $FRAMES seconds $SECS"
    cat "$D/events.txt" 2> /dev/null
    for s in 0 1; do echo "slot $s: $(P $s | grep -E "^(final_hash|desyncs|crc_checks_passed|stall_ms|pause)" | tr '\n' ' ')"; done
    for v in V1 V2 V2b; do S "$v" | grep -q . && echo "$v: $(S "$v" | grep -E "^(spectator|local_player|final_hash|spectate |spectate_relay|spectate_failure)" | tr '\n' ' ')"; done
    grep -h "^SPECT \|^SSTART " "$D"/host*.txt
    cat "$D/relay.txt"
    cat "$D/result.txt"
} > "$D/summary.txt"
cat "$D/summary.txt"
echo "RESULT online3 spectate $SCEN: $([ "$ok" = 1 ] && echo PASS || echo FAIL) (game $ID, ${SECS}s)"
[ "$ok" = 1 ]
