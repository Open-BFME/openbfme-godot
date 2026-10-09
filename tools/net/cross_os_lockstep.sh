#!/bin/bash
# Lane WIN-1: a Linux peer and a Windows peer (the Windows openbfme_peer.exe under Wine) play LAN games against each other in lockstep, both ways round
# (Linux hosts, then Windows hosts), with scripted humans and skirmish AIs of every faction; then each side plays the other side's recording back.
# Windows and Linux players must stay in sync: every peer must finish every frame with zero desyncs, all CRC checks passed, and the per-frame hash files
# of the two OSes must be byte-identical; each replay must match on every frame.
#
#   tools/net/cross_os_lockstep.sh <linux openbfme_peer> <windows openbfme_peer.exe> <out dir> [frames=3000] [seed=2026] [port=27000]
# Needs ROTWK_INSTALL / BFME2_INSTALL (Unix paths: Wine maps them through Z:), wine on PATH and WINEPREFIX. Both peers must be built from the same
# tree (the same engine id; a mismatch is refused by the lobby). Exit 0 only when every count matched: all frames, (frames - 3) / 100 CRC checks on
# each peer, frames - 1 identical hash lines, the same engine id, and full replays (frames - 1 hashes compared, 0 mismatches, the game's final hash).
set -u
LIN="$1"; WIN="$2"; OUT="$3"; FRAMES="${4:-3000}"; SEED="${5:-2026}"; PORT="${6:-27000}"
mkdir -p "$OUT"
export WINEDEBUG=${WINEDEBUG:--all}
MAP="maps/map mp fall back 8p/map mp fall back 8p.map"
COMMON=(--map "$MAP" --seed "$SEED" --script --crc-interval 100 --run-ahead 2 --fixed-run-ahead --frames "$FRAMES" --lobby-timeout 600 --desync-dir "$OUT")
SLOTS=(--slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --slot hard,FactionElves,2,0 --slot brutal,FactionIsengard,3,1
       --slot medium,FactionDwarves,4,0 --slot hard,FactionWild,5,1 --slot medium,FactionAngmar,6,0)
rc=0
# play <tag> <host cmd...> -- <joiner cmd...>
game() {
    local tag=$1 hostOs=$2 joinOs=$3 port=$4
    local -a H J
    [ "$hostOs" = linux ] && H=("$LIN") || H=(wine "$WIN")
    [ "$joinOs" = linux ] && J=("$LIN") || J=(wine "$WIN")
    "${H[@]}" "${COMMON[@]}" --host "$port" "${SLOTS[@]}" --record "$OUT/$tag.replay" --report "$OUT/$tag-host-$hostOs.txt" \
        --hashes "$OUT/$tag-host-$hostOs.hashes" < /dev/null > "$OUT/$tag-host-$hostOs.log" 2>&1 &
    local hp=$!
    sleep 2
    "${J[@]}" "${COMMON[@]}" --join "127.0.0.1:$port" --name Joiner --report "$OUT/$tag-join-$joinOs.txt" --hashes "$OUT/$tag-join-$joinOs.hashes" \
        < /dev/null > "$OUT/$tag-join-$joinOs.log" 2>&1 &
    local jp=$!
    wait $hp || { echo "$tag: host ($hostOs) exit $?"; rc=1; }
    wait $jp || { echo "$tag: joiner ($joinOs) exit $?"; rc=1; }
    local f ids=""
    for f in "$OUT/$tag-host-$hostOs.txt" "$OUT/$tag-join-$joinOs.txt"; do
        echo "$tag $(basename "$f" .txt): $(grep -E '^(frames|desyncs|crc_checks_passed|final_hash|engine_id) ' "$f" 2>/dev/null | cut -c1-60 | tr '\n' ' ')"
        # every count is asserted, not only the absence of failures: a peer that ran no CRC check or no frame must fail the gate (Sol r1)
        expect "$f" frames "$FRAMES"
        expect "$f" desyncs 0
        expect "$f" crc_checks_passed "$CRC_CHECKS"
        grep -q "network_error" "$f" 2>/dev/null && { echo "$tag: $(basename "$f"): network error"; rc=1; }
        ids="$ids $(field "$f" engine_id | cut -d' ' -f1)"
    done
    set -- $ids
    [ $# = 2 ] && [ "$1" = "$2" ] || { echo "$tag: the peers' engine ids differ or are missing:$ids"; rc=1; }
    finals["$tag"]=$(field "$OUT/$tag-host-$hostOs.txt" final_hash)
    local lines
    lines=$(wc -l < "$OUT/$tag-host-$hostOs.hashes" 2>/dev/null || echo 0)
    if [ "$lines" = "$((FRAMES - 1))" ] && cmp -s "$OUT/$tag-host-$hostOs.hashes" "$OUT/$tag-join-$joinOs.hashes"; then
        echo "$tag: per-frame hashes identical ($lines lines, $hostOs host vs $joinOs joiner)"
    else
        echo "$tag: PER-FRAME HASHES DIFFER OR INCOMPLETE ($lines of $((FRAMES - 1)) lines, $hostOs host vs $joinOs joiner): $(cmp "$OUT/$tag-host-$hostOs.hashes" "$OUT/$tag-join-$joinOs.hashes" 2>&1 | head -1)"; rc=1
    fi
}
# the value of a report line "<key> <value...>"
field() { grep -E "^$2 " "$1" 2>/dev/null | head -1 | cut -d' ' -f2-; }
expect() {
    local got
    got=$(field "$1" "$2")
    [ "$got" = "$3" ] || { echo "$(basename "$1"): $2 is '$got', expected '$3'"; rc=1; }
}
declare -A finals
# the CRC frames of --crc-interval 100: 100, 200, ... compared two frames later, so a game of N frames checks (N - 3) / 100 times (3000: 29)
CRC_CHECKS=$(( (FRAMES - 3) / 100 ))
[ "$CRC_CHECKS" -ge 1 ] || { echo "frames=$FRAMES gives no CRC check: use at least 103"; exit 2; }
game lin-hosts linux windows "$PORT"
game win-hosts windows linux "$((PORT + 1))"
# the same game seen from both directions must also be the same game (--fixed-run-ahead: the commands run at the same frames whatever the timing)
cmp -s "$OUT/lin-hosts-host-linux.hashes" "$OUT/win-hosts-join-linux.hashes" && echo "both directions: the same hashes" || { echo "both directions: HASHES DIFFER"; rc=1; }
# each OS plays the other OS's host recording back
"$LIN" --replay "$OUT/win-hosts.replay" --report "$OUT/replay-of-win-on-linux.txt" < /dev/null > "$OUT/replay-of-win-on-linux.log" 2>&1 || { echo "linux replay of the windows recording: exit $?"; rc=1; }
wine "$WIN" --replay "$OUT/lin-hosts.replay" --report "$OUT/replay-of-lin-on-windows.txt" < /dev/null > "$OUT/replay-of-lin-on-windows.log" 2>&1 || { echo "windows replay of the linux recording: exit $?"; rc=1; }
for pair in "replay-of-win-on-linux:win-hosts" "replay-of-lin-on-windows:lin-hosts"; do
    f="$OUT/${pair%%:*}.txt"; game="${pair##*:}"
    echo "$(basename "$f" .txt): $(grep -E '^(frames|mismatches|hashes_compared|final_hash) ' "$f" 2>/dev/null | tr '\n' ' ')"
    expect "$f" frames "$FRAMES of $FRAMES"
    expect "$f" hashes_compared "$((FRAMES - 1))"
    expect "$f" mismatches 0
    expect "$f" final_hash "${finals[$game]} recorded ${finals[$game]}"
done
[ $rc = 0 ] && echo "CROSS-OS LOCKSTEP PASS" || echo "CROSS-OS LOCKSTEP FAIL"
exit $rc
