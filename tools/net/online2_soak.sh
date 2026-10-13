#!/bin/bash
# Lane ONLINE-2 soak: headless games through the online relay (services/online/relay) on impaired links (the transport's fault injector,
# GameNetwork/NetImpairment.h), then the relay's command log turned into a replay and checked against the game ("CRC parity").
# Per game: the relay starts with a throw-away key; tools/net/online_client.py fixture signs one START per human slot (the lobby service's START,
# GameNetwork/OnlineStart.h); one openbfme_peer per human plays it (--online-start) with the skirmish AI in the other slots; the relay closes the game's log
# when every peer said BYE; openbfme_peer --relay-log turns the log into a replay; openbfme_peer --replay plays it back and compares the players' CRCs.
#
#   tools/net/online2_soak.sh <build dir> <out dir> [frames=3000] [seed=7000]
# Environment:
#   SOAK_LINK   the faults of every peer's outgoing datagrams (default "latency=80,jitter=30,loss=30,dup=10,reorder=10": both legs of a relay hop)
#   SOAK_GAMES  which games of the table below (default "1 2"; 1: 2 humans + 2 AI, 2: 4 humans, 3: 4 humans + 2 AI on another map)
#   SOAK_NICE   nice level of the peers (default 15)
#   RELAY_HOST / RELAY_PORT  use a relay that already runs elsewhere (the home-lab test; then RELAY_KEY_HEX is its key, key id 1, and RELAY_LOG_DIR is
#               fetched by the caller); default: a local relay on 127.0.0.1
# Needs ROTWK_INSTALL / BFME2_INSTALL. Exit 0 when every game ran every frame on every peer with no desync, equal final hashes, a complete relay log and a
# replay from the log whose CRCs all match.
set -u
B="$1"; OUT="$2"; FRAMES="${3:-3000}"; SEED="${4:-7000}"
PEER="$B/openbfme_peer"; RELAY="$B/relay/openbfme_relay"
HERE="$(cd "$(dirname "$0")" && pwd)"
LINK="${SOAK_LINK:-latency=80,jitter=30,loss=30,dup=10,reorder=10}"
GAMES="${SOAK_GAMES:-1 2}"
NICE="${SOAK_NICE:-15}"
mkdir -p "$OUT"
[ -x "$PEER" ] || { echo "soak: no $PEER"; exit 1; }

PROFILE_LINES="$("$PEER" --print-profile)" || { echo "soak: openbfme_peer --print-profile failed"; exit 1; }
PROFILE="$(head -n1 <<< "$PROFILE_LINES")"
fac() { awk -v n="$1" '$1 == "faction" && $3 == n { print $2 }' <<< "$PROFILE_LINES"; }

game_map() {
    case "$1" in
        1) echo "maps/map mp fords of isen ii/map mp fords of isen ii.map" ;;
        2) echo "maps/map mp fall back 4p/map mp fall back 4p.map" ;;
        3) echo "maps/map mp anfalas/map mp anfalas.map" ;;
    esac
}
# STATE,faction,colour,startPos,team (factions by name, turned into this build's indices)
game_slots() {
    case "$1" in
        1) echo "player,FactionMen,0,0,0 player,FactionMordor,1,1,1" ;;
        2) echo "player,FactionElves,0,0,0 player,FactionIsengard,1,1,1 player,FactionDwarves,2,2,0 player,FactionAngmar,3,3,1" ;;
        3) echo "player,FactionWild,0,0,0 player,FactionMen,1,1,1 player,FactionMordor,2,2,0 player,FactionElves,3,3,1 hard,FactionDwarves,4,4,0 brutal,FactionIsengard,5,5,1" ;;
    esac
}

rc=0; total_frames=0; total_games=0
echo "online2 soak: frames $FRAMES, link $LINK, games $GAMES, profile $PROFILE" | tee "$OUT/summary.txt"
for g in $GAMES; do
    D="$OUT/game$g"; rm -rf "$D"; mkdir -p "$D/relaylog"
    MAP="$(game_map "$g")"
    SLOTARGS=(); N=0
    for s in $(game_slots "$g"); do
        IFS=, read -r st f c p t <<< "$s"
        idx="$(fac "$f")"; [ -n "$idx" ] || { echo "soak: no faction $f" | tee -a "$OUT/summary.txt"; rc=1; continue 2; }
        SLOTARGS+=(--slot "$st,$idx,$c,$p,$t"); [ "$st" = player ] && N=$((N + 1))
    done
    GAME_ID=$(printf '%016x' $((0x0B0E0000 + g * 256 + RANDOM % 256)))
    if [ -z "${RELAY_HOST:-}" ]; then
        KEY_HEX="$(head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
        printf '1:%s\n' "$KEY_HEX" > "$D/keys.txt"; chmod 600 "$D/keys.txt"
        RPORT=$((29000 + g)); RADDR="127.0.0.1:$RPORT"
        "$RELAY" --bind "$RADDR" --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/relay-status.json" --stats-s 30 --idle-s 60 > "$D/relay.log" 2>&1 &
        RPID=$!
        sleep 0.5
    else
        KEY_HEX="$RELAY_KEY_HEX"; RADDR="$RELAY_HOST:$RELAY_PORT"; RPID=""
    fi
    python3 "$HERE/online_client.py" fixture --key-hex "$KEY_HEX" --game "$GAME_ID" --relay "$RADDR" --profile "$PROFILE" --map "$MAP" "${SLOTARGS[@]}" \
        --seed $((SEED + g)) --out-dir "$D" > /dev/null || { echo "soak: fixture failed" | tee -a "$OUT/summary.txt"; rc=1; continue; }
    start=$(date +%s); PIDS=()
    for f in "$D"/start-*.json; do
        i="$(basename "$f" .json)"; i="${i#start-}"
        nice -n "$NICE" "$PEER" --online-start "$f" --script --frames "$FRAMES" --link "$LINK" --logic-threads 2 --desync-dir "$D" \
            --report "$D/p$i.txt" --hashes "$D/h$i.txt" > "$D/p$i.log" 2>&1 &
        PIDS+=($!)
    done
    grc=0
    for p in "${PIDS[@]}"; do wait "$p" || grc=1; done
    secs=$(( $(date +%s) - start ))
    # the relay closes the log when every peer said BYE; stop it either way (SIGTERM closes what is left)
    if [ -n "$RPID" ]; then
        for _ in $(seq 1 50); do ls "$D"/relaylog/*.obfmerlog > /dev/null 2>&1 && break; sleep 0.2; done
        kill -TERM "$RPID" 2> /dev/null; wait "$RPID" 2> /dev/null
    fi
    LOG="$(ls "$D"/relaylog/*.obfmerlog 2> /dev/null | head -n1)"
    finals=$(grep -h "^final_hash" "$D"/p*.txt | sort -u | wc -l)
    desyncs=$(grep -h "^desyncs" "$D"/p*.txt | awk '{s += $2} END {print s + 0}')
    frames_ok=$(grep -h "^frames " "$D"/p*.txt | awk -v f="$FRAMES" '$2 >= f' | wc -l)
    bound=$(grep -h "^relay " "$D"/p*.txt | grep -c " bound 1 ")
    first="$(ls "$D"/start-*.json | head -n1)"
    conv=1; play=1
    if [ -n "$LOG" ]; then
        "$PEER" --relay-log "$LOG" --online-start "$first" --record "$D/server.replay" --report "$D/convert.txt" > "$D/convert.log" 2>&1; conv=$?
        if [ -f "$D/server.replay" ]; then
            "$PEER" --replay "$D/server.replay" --report "$D/replay.txt" > "$D/replay.log" 2>&1; play=$?
        fi
    fi
    compared=$(awk '/^hashes_compared/ {print $2}' "$D/replay.txt" 2> /dev/null); mism=$(awk '/^mismatches/ {print $2}' "$D/replay.txt" 2> /dev/null)
    line="game $g: $N peers, ${secs}s, peers_ok $((grc == 0)) frames_ok $frames_ok/$N relay_bound $bound/$N desyncs $desyncs final_hashes $finals log $([ -n "$LOG" ] && echo yes || echo no) convert_exit $conv replay_exit $play crc_compared ${compared:-0} mismatches ${mism:-?}"
    echo "$line" | tee -a "$OUT/summary.txt"
    grep -h "^transport_bytes\|^relay \|^stall_ms" "$D"/p*.txt | sed 's/^/    /' >> "$OUT/summary.txt"
    if [ "$grc" != 0 ] || [ "$frames_ok" != "$N" ] || [ "$bound" != "$N" ] || [ "$desyncs" != 0 ] || [ "$finals" != 1 ] || [ "$conv" != 0 ] || [ "$play" != 0 ] \
        || [ "${compared:-0}" -lt 1 ] || [ "${mism:-1}" != 0 ]; then
        rc=1
    fi
    total_frames=$((total_frames + FRAMES)); total_games=$((total_games + 1))
done
echo "RESULT online2 soak: games $total_games frames $total_frames exit $rc" | tee -a "$OUT/summary.txt"
exit $rc
