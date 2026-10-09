#!/bin/bash
# Lane SMOOTH-1 review r4 (the MP-1 integration): two headless peer processes with the logic worker under ThreadSanitizer or ASan/UBSan. Builds the
# openbfme_peer target in a sanitizer build directory made by tools/smooth/sanitize_threaded.sh (same mode), then plays a lockstep game on localhost: both
# peers on the worker, different render steps and worker delays, lossy reordering links, a pause, a driver replacement and a worker switch, CRC every 50
# frames, both recording. Checks: both exit 0 with no sanitizer report, equal hash / RNG lines after every frame, equal replay hashes, and the host's
# recording played back (single thread) with no mismatch.
# usage: tools/smooth/net_threaded.sh tsan|asan <build dir> [frames]   (ROTWK_INSTALL / BFME2_INSTALL set; exit 0 = all good)
set -e
MODE="$1"; B="$2"; FRAMES="${3:-300}"
cmake --build "$B" -j"${JOBS:-3}" --target openbfme_peer
P="$B/openbfme_peer"
W="$B/net_threaded_run"
mkdir -p "$W"
cd "$W"
if [ "$MODE" = tsan ]; then
	export TSAN_OPTIONS="halt_on_error=1 report_signal_unsafe=0"
else
	export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"
fi
PORT=$((24000 + $$ % 4000))
COMMON=(--map "maps/map mp evendim/map mp evendim.map" --seed 31 --script --crc-interval 50 --run-ahead 2 --logic-thread --drop 100 --jitter 20
	--frames "$FRAMES" --lobby-timeout 600)
"$P" "${COMMON[@]}" --host "$PORT" --slot human,FactionMen,0,0 --slot human,FactionMordor,1,1 --record host.replay --step-ms 16 --worker-delay-ms 15 \
	--pause-at 120:600 --replace-driver-at 200 --report host.txt --hashes host_hashes.txt > host.log 2>&1 &
HOST=$!
sleep 1
set +e
"$P" "${COMMON[@]}" --join "127.0.0.1:$PORT" --name Joiner --record join.replay --step-ms 45 --toggle-thread-at 150 --report join.txt \
	--hashes join_hashes.txt > join.log 2>&1
JRC=$?
wait $HOST
HRC=$?
set -e
echo "host exit $HRC, joiner exit $JRC"
grep -E "^(frames|final_hash|crc_checks_passed|desyncs|stalled_frames|transport) " host.txt join.txt || true
FAIL=0
if [ $HRC -ne 0 ] || [ $JRC -ne 0 ]; then FAIL=1; tail -40 host.log join.log; fi
if grep -l "WARNING: ThreadSanitizer\|ERROR: AddressSanitizer\|runtime error:\|ERROR: LeakSanitizer" host.log join.log; then FAIL=1; fi
if ! cmp -s host_hashes.txt join_hashes.txt; then echo "the peers' per-frame hashes / RNG differ"; FAIL=1; fi
[ "$(wc -l < host_hashes.txt)" -eq "$((FRAMES - 1))" ] || { echo "host hash lines: $(wc -l < host_hashes.txt), expected $((FRAMES - 1))"; FAIL=1; }
"$P" --replay host.replay --report replay.txt > replay.log 2>&1 || { echo "replay playback failed"; cat replay.txt; FAIL=1; }
grep -E "^(hashes_compared|mismatches|final_hash) " replay.txt || true
grep -q "^mismatches 0$" replay.txt || FAIL=1
if grep -l "WARNING: ThreadSanitizer\|ERROR: AddressSanitizer\|runtime error:\|ERROR: LeakSanitizer" replay.log; then FAIL=1; fi
echo "net_threaded $MODE: $([ $FAIL -eq 0 ] && echo PASS || echo FAIL)"
exit $FAIL
