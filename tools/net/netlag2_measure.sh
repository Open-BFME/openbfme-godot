#!/bin/bash
# Lane NETLAG-2: click-to-effect latency of online games through a deployed relay (the staging relay), with 2..4 headless players whose scripted
# orders land anywhere in a frame (openbfme_peer --script-phase random), as a player's clicks do. One scenario = one lobby game: the lobby client
# hosts and joins here (tools/net/online_client.py), every peer runs here (--paced, 5 Hz clocks), and the per-peer report lines are summed up.
#
#   tools/net/netlag2_measure.sh game <lobby URL> <out dir> <name> <players 2..4> <frames> [peer args...]
#     prints "NETLAG2 <name> ..." summary lines and "RESULT netlag2 <name>: PASS|FAIL" (every peer ran the frames, one final hash, no desync)
#   tools/net/netlag2_measure.sh lan <out dir> <name> <players 2..4> <frames> [peer args...]
#     the same game over a direct UDP lobby on 127.0.0.1 (no relay): the LAN baseline (add --link latency=MS for a slow LAN)
#   tools/net/netlag2_measure.sh fixture <relay host:port> <key hex> <mode> <out dir> <name> <players 2..4> <frames> [peer args...]
#     the same game with signed fixture STARTs (tools/net/online_client.py fixture) for a relay keyed with <key hex> (no lobby); <mode> 0 = today's
#     lockstep, 1 = the relay keeps the beat (ROUTER-1); a scenario name "fx<mode>-..." in a batch runs this with $FX_RELAY / $FX_KEY
#   tools/net/netlag2_measure.sh batch <lobby URL> <out dir> <control dir> <scenario>...
#     scenario "name:players:frames[:peer arg,peer arg...]": before each one it writes <control dir>/want.<i> (the name) and waits for
#     <control dir>/go.<i> (the operator has set the network conditions for it, e.g. netem on the relay), then writes done.<i>; a scenario whose name
#     starts with "base-" runs the peers of $BASE_B instead of $B (an A/B of two builds under the same conditions)
# Peer args "@N:ARG" go to slot N only (for example "@1:--hitch-at" "@1:60:1500": slot 1's client stops for 1.5 s at frame 60).
# $B: the build directory (openbfme_peer); ROTWK_INSTALL / BFME2_INSTALL: the game files.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
MAP="maps/map mp fall back 4p/map mp fall back 4p.map"
PEER="$B/openbfme_peer"

slot_args() { # <slot> <args...>: the args for that slot ("@N:" ones only for slot N)
    local s="$1" a; shift; SA=()
    for a in "$@"; do
        if [[ "$a" =~ ^@([0-9]+):(.*)$ ]]; then [ "${BASH_REMATCH[1]}" = "$s" ] && SA+=("${BASH_REMATCH[2]}"); else SA+=("$a"); fi
    done
}

summarize() { # <dir> <name> <players> <frames>
    local D="$1" N="$2" P="$3" F="$4" s ok=1
    local finals desyncs frames_ok reports rcs
    # Sol r1: a peer that failed or wrote no full report fails the scenario (its exit code, and a final_hash in every report)
    reports=$(for s in $(seq 0 $((P - 1))); do grep -c "^final_hash " "$D/r$s.txt" 2> /dev/null || echo 0; done | awk '$1 == 1' | wc -l)
    rcs=$(cat "$D"/rc*.txt 2> /dev/null | awk '$1 != 0' | wc -l)
    finals=$(for s in $(seq 0 $((P - 1))); do awk '$1 == "final_hash" { print $2 }' "$D/r$s.txt" 2> /dev/null; done | sort -u | wc -l)
    desyncs=$(for s in $(seq 0 $((P - 1))); do awk '$1 == "desyncs" { print $2 }' "$D/r$s.txt" 2> /dev/null; done | awk '{ t += $1 } END { print t + 0 }')
    frames_ok=$(for s in $(seq 0 $((P - 1))); do awk '$1 == "frames" { print $2 }' "$D/r$s.txt" 2> /dev/null; done | awk -v n="$F" '$1 >= n' | wc -l)
    for s in $(seq 0 $((P - 1))); do
        grep -h "^command_effect_ms\|^input_latency_ms\|^command_delay_frames\|^stall_ms\|^run_ahead\|^relay \|^seconds\|^early_orders" "$D/r$s.txt" 2> /dev/null | sed "s/^/NETLAG2 $N slot $s /"
    done
    [ "$finals" = 1 ] || ok=0; [ "$desyncs" = 0 ] || ok=0; [ "$frames_ok" = "$P" ] || ok=0; [ "$reports" = "$P" ] || ok=0; [ "$rcs" = 0 ] || ok=0
    echo "RESULT netlag2 $N: $([ $ok = 1 ] && echo PASS || echo FAIL) (players $P, frames $F, final hashes $finals, desyncs $desyncs, peers done $frames_ok, full reports $reports, failed peers $rcs)"
}

game() { # <lobby> <out> <name> <players> <frames> [peer args...]
    local LOBBY="$1" OUT="$2" N="$3" P="$4" F="$5"; shift 5
    local D="$OUT/$N" PROFILE C pids=() s
    rm -rf "$D"; mkdir -p "$D"
    PROFILE="$("$PEER" --print-profile | head -n1)"
    C="python3 $HERE/online_client.py"
    local fac=(3 8 5 7) # FactionMen, FactionMordor, FactionElves, FactionIsengard (openbfme_peer --print-profile)
    local slots=(--slot "player,${fac[0]},0,0,0")
    for s in $(seq 1 3); do [ "$s" -lt "$P" ] && slots+=(--slot open) || slots+=(--slot closed); done
    $C host --lobby "$LOBBY" --name "Lag$N" --profile "$PROFILE" --map "$MAP" "${slots[@]}" --players "$P" --id-file "$D/id.txt" \
        --token-file "$D/t0" --out "$D/s0.json" --timeout 300 > "$D/host.log" 2>&1 &
    pids+=($!)
    for _ in $(seq 1 100); do [ -s "$D/id.txt" ] && break; sleep 0.2; done
    for s in $(seq 1 $((P - 1))); do
        $C join --lobby "$LOBBY" --name "Lag${N}j$s" --profile "$PROFILE" --game "$(cat "$D/id.txt")" --faction "${fac[$s]}" --color "$s" --start-pos "$s" \
            --team "$s" --token-file "$D/t$s" --out "$D/s$s.json" > "$D/j$s.log" 2>&1 &
        pids+=($!)
    done
    wait "${pids[@]}" || { echo "RESULT netlag2 $N: FAIL (the lobby did not start the game)"; tail -n 3 "$D"/*.log; return 1; }
    pids=()
    for s in $(seq 0 $((P - 1))); do
        slot_args "$s" "$@"
        ( "$PEER" --online-start "$D/s$s.json" --script --script-phase random --paced --frames "$F" --logic-threads 1 --report "$D/r$s.txt" "${SA[@]}" \
            > "$D/peer$s.log" 2>&1; echo $? > "$D/rc$s.txt" ) &
        pids+=($!)
    done
    wait "${pids[@]}"
    summarize "$D" "$N" "$P" "$F"
}

fixture() { # <relay> <key hex> <mode> <out> <name> <players> <frames> [peer args...]
    local RELAY="$1" KEY="$2" MODE="$3" OUT="$4" N="$5" P="$6" F="$7"; shift 7
    local D="$OUT/$N" PROFILE pids=() s
    rm -rf "$D"; mkdir -p "$D"
    PROFILE="$("$PEER" --print-profile | head -n1)"
    local fac=(3 8 5 7) slots=()
    for s in $(seq 0 3); do [ "$s" -lt "$P" ] && slots+=(--slot "player,${fac[$s]},$s,$s,$s") || slots+=(--slot closed); done
    python3 "$HERE/online_client.py" fixture --key-hex "$KEY" --game "$(printf '%016x' $((0x0E300000 + RANDOM)))" --relay "$RELAY" --profile "$PROFILE" \
        --map "$MAP" --mode "$MODE" "${slots[@]}" --out-dir "$D" > "$D/fixture.log" 2>&1 || { echo "RESULT netlag2 $N: FAIL (fixture)"; return 1; }
    for s in $(seq 0 $((P - 1))); do
        slot_args "$s" "$@"
        ( "$PEER" --online-start "$D/start-$s.json" --script --script-phase random --paced --frames "$F" --logic-threads 1 --report "$D/r$s.txt" "${SA[@]}" \
            > "$D/peer$s.log" 2>&1; echo $? > "$D/rc$s.txt" ) &
        pids+=($!)
    done
    wait "${pids[@]}"
    summarize "$D" "$N" "$P" "$F"
}

lan() { # <out> <name> <players> <frames> [peer args...]
    local OUT="$1" N="$2" P="$3" F="$4"; shift 4
    local D="$OUT/$N" PORT pids=() s
    rm -rf "$D"; mkdir -p "$D"
    PORT=$((20000 + RANDOM % 20000))
    local fac=(FactionMen FactionMordor) slots=()
    for s in $(seq 0 $((P - 1))); do slots+=(--slot "human,${fac[$((s % 2))]},$s,$s"); done
    slot_args 0 "$@"
    ( "$PEER" --host "$PORT" --map "$MAP" --seed 1234 "${slots[@]}" --script --script-phase random --paced --frames "$F" --logic-threads 1 \
        --report "$D/r0.txt" "${SA[@]}" > "$D/peer0.log" 2>&1; echo $? > "$D/rc0.txt" ) &
    pids+=($!)
    sleep 1
    for s in $(seq 1 $((P - 1))); do
        slot_args "$s" "$@"
        ( "$PEER" --join "127.0.0.1:$PORT" --name "Lan$s" --script --script-phase random --paced --frames "$F" --logic-threads 1 --report "$D/r$s.txt" "${SA[@]}" \
            > "$D/peer$s.log" 2>&1; echo $? > "$D/rc$s.txt" ) &
        pids+=($!)
    done
    wait "${pids[@]}"
    summarize "$D" "$N" "$P" "$F"
}

case "${1:-}" in
    game) shift; game "$@" ;;
    lan) shift; lan "$@" ;;
    fixture) shift; fixture "$@" ;;
    batch)
        LOBBY="$2"; OUT="$3"; CTL="$4"; shift 4; i=0; mkdir -p "$CTL"
        for sc in "$@"; do
            IFS=: read -r N P F A <<< "$sc"; IFS=, read -r -a ARGS <<< "${A:-}"
            PEER="$B/openbfme_peer"; [[ "$N" == base-* ]] && PEER="${BASE_B:?BASE_B}/openbfme_peer"
            echo "$N" > "$CTL/want.$i"
            for _ in $(seq 1 3600); do [ -e "$CTL/go.$i" ] && break; sleep 1; done
            [ -e "$CTL/go.$i" ] || { echo "RESULT netlag2 $N: FAIL (no go from the operator)"; exit 1; }
            if [ "$P" = lan2 ] || [ "$P" = lan4 ]; then lan "$OUT" "$N" "${P#lan}" "$F" "${ARGS[@]}"
            elif [[ "$N" =~ ^(base-)?fx([0-9])- ]]; then fixture "${FX_RELAY:?FX_RELAY}" "${FX_KEY:?FX_KEY}" "${BASH_REMATCH[2]}" "$OUT" "$N" "$P" "$F" "${ARGS[@]}"
            else game "$LOBBY" "$OUT" "$N" "$P" "$F" "${ARGS[@]}"; fi
            touch "$CTL/done.$i"; i=$((i + 1))
        done
        touch "$CTL/finished" ;;
    *) echo "usage: see the header" >&2; exit 2 ;;
esac
