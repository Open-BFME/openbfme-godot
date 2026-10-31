#!/bin/bash
# Lane ONLINE16-FIX: the legacy (lockstep) online games through real relays, with the game files, headless (openbfme_peer --online-start), in one run:
#   A  two current peers, authenticated (fixture --auth: the START's session_key) and tag-REQUIRED (--mode 4: TICKET_MODE_AUTH signed into the tickets)
#   B  two current peers, untagged (no session_key: an older lobby's START)
#   C  two previous-release peers ($OLD/openbfme_peer: the version-3 envelope, version-1 tickets) on the SAME relay, at the same time as A and B
#   D  two current peers without a session_key through the previous release's relay ($OLD/openbfme_relay, version 3 only): they drop to its envelope
#   E  one current peer WITH a session_key at the previous release's relay: a clear refusal, nothing unauthenticated sent
# then the relay logs of A, B and C are assembled and replayed (openbfme_peer --relay-log --record, then --replay: the CRCs equal); C's is a
# version-2 log (a game the previous release opened, online batch) that the previous release's peer assembles and replays itself.
#   tools/net/online16_e2e.sh <out dir> <frames>
# $B: this build (openbfme_peer, openbfme_relay); $OLD: the previous release's build; ROTWK_INSTALL / BFME2_INSTALL: the game files.
# Prints "E2E <game> ..." lines and "RESULT online16_e2e: PASS|FAIL".
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$1"; F="$2"
MAP="maps/map mp fall back 4p/map mp fall back 4p.map"
C="python3 $HERE/online_client.py"
KEY=$(python3 -c 'import secrets; print(secrets.token_hex(32))')
rm -rf "$OUT"; mkdir -p "$OUT/logs-new" "$OUT/logs-old"
ok=1
fail() { echo "E2E FAIL: $*"; ok=0; }
port() { python3 -c 'import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])'; }
echo "1:$KEY" > "$OUT/keys.txt"; chmod 600 "$OUT/keys.txt"
NEWP=$(port); OLDP=$(port)
"$B/openbfme_relay" --bind "127.0.0.1:$NEWP" --keys "$OUT/keys.txt" --log-dir "$OUT/logs-new" --status "$OUT/status-new.json" --stats-s 5 > "$OUT/relay-new.log" 2>&1 &
NEWR=$!
"$OLD/openbfme_relay" --bind "127.0.0.1:$OLDP" --keys "$OUT/keys.txt" --log-dir "$OUT/logs-old" --status "$OUT/status-old.json" --stats-s 5 > "$OUT/relay-old.log" 2>&1 &
OLDR=$!
sleep 1
NEWPROF="$("$B/openbfme_peer" --print-profile | head -n1)"
OLDPROF="$("$OLD/openbfme_peer" --print-profile | head -n1)"
# <dir> <relay port> <profile> <game id> [fixture args...]: the two STARTs (slots 0 and 1)
fixture() {
    local D="$1" P="$2" PROF="$3" G="$4"; shift 4; mkdir -p "$D"
    $C fixture --key-hex "$KEY" --game "$G" --relay "127.0.0.1:$P" --profile "$PROF" --map "$MAP" --slot "player,3,0,0,0" --slot "player,8,1,1,1" \
        --slot closed --slot closed --crc-interval 10 --run-ahead 2 --out-dir "$D" "$@" > "$D/fixture.log" 2>&1 || fail "fixture $D"
}
# <dir> <peer binary>: both peers of the game, in the background (pids in PIDS)
PIDS=()
play() {
    local D="$1" PEER="$2" s
    for s in 0 1; do
        ( "$PEER" --online-start "$D/start-$s.json" --script --frames "$F" --logic-threads 1 --report "$D/r$s.txt" > "$D/peer$s.log" 2>&1; echo $? > "$D/rc$s.txt" ) &
        PIDS+=($!)
    done
}
fixture "$OUT/A" "$NEWP" "$NEWPROF" 00000000000a0001 --auth --mode 4
fixture "$OUT/B" "$NEWP" "$NEWPROF" 00000000000b0001
fixture "$OUT/C" "$NEWP" "$OLDPROF" 00000000000c0001
fixture "$OUT/D" "$OLDP" "$NEWPROF" 00000000000d0001
fixture "$OUT/E" "$OLDP" "$NEWPROF" 00000000000e0001 --auth
play "$OUT/A" "$B/openbfme_peer"
play "$OUT/B" "$B/openbfme_peer"
play "$OUT/C" "$OLD/openbfme_peer"
play "$OUT/D" "$B/openbfme_peer"
( timeout 600 "$B/openbfme_peer" --online-start "$OUT/E/start-0.json" --script --frames "$F" --logic-threads 1 --report "$OUT/E/r0.txt" > "$OUT/E/peer0.log" 2>&1
  echo $? > "$OUT/E/rc0.txt" ) &
PIDS+=($!)
wait "${PIDS[@]}"
for g in A B C D; do
    D="$OUT/$g"
    rcs=$(cat "$D"/rc0.txt "$D"/rc1.txt | tr '\n' ' ')
    finals=$(awk '$1 == "final_hash" { print $2 }' "$D"/r0.txt "$D"/r1.txt | sort -u | wc -l)
    frames=$(awk '$1 == "frames" { print $2 }' "$D"/r0.txt "$D"/r1.txt | tr '\n' ' ')
    desyncs=$(awk '$1 == "desyncs" { t += $2 } END { print t + 0 }' "$D"/r0.txt "$D"/r1.txt)
    auth=$(awk '$1 == "relay_auth" { print $3 "/" $5 "/" $7 }' "$D"/r0.txt "$D"/r1.txt | tr '\n' ' ')
    echo "E2E $g exit $rcs frames $frames final_hashes $finals desyncs $desyncs auth/dropped/legacy ${auth:-n/a (previous release)}"
    [ "$rcs" = "0 0 " ] && [ "$finals" = 1 ] && [ "$desyncs" = 0 ] || fail "game $g"
done
grep -q "^relay_auth authenticated 1 dropped 0 legacy_envelope 0" "$OUT/A/r0.txt" && grep -q "^relay_auth authenticated 1 dropped 0 legacy_envelope 0" "$OUT/A/r1.txt" || fail "A is not authenticated"
grep -q "^relay_auth authenticated 0 dropped 0 legacy_envelope 0" "$OUT/B/r0.txt" || fail "B is not a plain v4 game"
grep -q "^relay_auth authenticated 0 dropped 0 legacy_envelope 1" "$OUT/D/r0.txt" && grep -q "^relay_auth authenticated 0 dropped 0 legacy_envelope 1" "$OUT/D/r1.txt" || fail "D did not speak version 3"
echo "E2E E exit $(cat "$OUT/E/rc0.txt") $(grep -m1 -o 'the relay is an older release[^"]*' "$OUT/E/peer0.log" "$OUT/E/r0.txt" 2> /dev/null | head -n1)"
[ "$(cat "$OUT/E/rc0.txt")" != 0 ] && grep -q "the relay is an older release" "$OUT/E/peer0.log" "$OUT/E/r0.txt" || fail "E was not refused clearly"
kill -TERM "$NEWR" "$OLDR"; wait "$NEWR" "$OLDR"
python3 - "$OUT" << 'EOF' || ok=0
import json, sys
out = sys.argv[1]
new, old = (json.load(open(f"{out}/status-{n}.json")) for n in ("new", "old"))
print("E2E relay-new " + " ".join(f"{k} {new[k]}" for k in ("gamesOpened", "gamesClosed", "refused", "bad", "authDropped", "slotMismatches", "logGaps", "logErrors")))
print("E2E relay-old " + " ".join(f"{k} {old[k]}" for k in ("gamesOpened", "gamesClosed", "refused", "bad")))
bad = [k for k in ("refused", "bad", "authDropped", "slotMismatches", "logGaps", "logErrors", "invalidPackets", "accountingErrors") if new[k] != 0]
if new["gamesOpened"] != 3 or new["gamesClosed"] != 3 or bad:
    print(f"E2E FAIL: the current relay: {bad}"); sys.exit(1)
if old["gamesOpened"] != 1 or old["gamesClosed"] != 1:
    print("E2E FAIL: the previous release's relay played one game (D)"); sys.exit(1)
EOF
# every game's relay log replays: the players' CRCs agree in the log, the recorded replay plays back with them
# C: a game the previous release opened gets a version-2 log (no INPUT records; online batch, Sol's blocking 2): the previous release's own
# peer reads it (what its rejoin catch-up and its spectators read through LOG_CHUNK); A and B are version 4 (lane ANTICHEAT-1's INPUT records)
for g in A B C; do
    id=$(python3 -c "import json; print(json.load(open('$OUT/$g/start-0.json'))['game'])")
    log="$OUT/logs-new/$id.obfmerlog"
    PEER="$B/openbfme_peer"
    [ "$g" = C ] && PEER="$OLD/openbfme_peer"
    echo "E2E $g relay_log version $(python3 -c "import struct,sys; print(struct.unpack_from('<I', open(sys.argv[1], 'rb').read(), 8)[0])" "$log")"
    "$PEER" --relay-log "$log" --online-start "$OUT/$g/start-0.json" --record "$OUT/$g/relay.replay" --report "$OUT/$g/relaylog.txt" > "$OUT/$g/relaylog.log" 2>&1
    rc1=$?
    "$PEER" --replay "$OUT/$g/relay.replay" --report "$OUT/$g/replay.txt" > "$OUT/$g/replay.log" 2>&1
    rc2=$?
    echo "E2E $g relay_log rc $rc1 $(grep -h '^relay_log_crc_frames' "$OUT/$g/relaylog.txt") replay rc $rc2 $(grep -h '^crc\|^hashes\|^final' "$OUT/$g/replay.txt" | tr '\n' ' ')"
    [ "$rc1" = 0 ] && [ "$rc2" = 0 ] || fail "the relay log of $g"
done
echo "RESULT online16_e2e: $([ $ok = 1 ] && echo PASS || echo FAIL)"
[ $ok = 1 ]
