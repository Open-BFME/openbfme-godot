#!/bin/bash
# Lane SMOOTH-1 (S-810): the race and memory checks of the logic worker. Builds the core library under ThreadSanitizer or ASan/UBSan (Godot off) and
# links the threaded tests into a small executable, then runs them against the retail install:
#   test_smooth_threaded.cpp  the worker against the single thread (hashes, RNG, events) at every render rate, snapshot immutability, switch, shutdown
#   test_smooth_live.cpp      the render side's per-frame work beside the worker: radar blips from the snapshot, camera follow through the snapshot,
#                             worker timing, the idle-gated HUD update, combat with projectiles, churn (review r2's live-game probe, fixed form)
#   test_smooth_races.cpp     review r4: the audio bindings / queue generations / diagnostics and StancesBehavior's statistics beside a worker
#   test_smooth_net.cpp       review r4 (MP-1): lockstep batches on the worker against the single thread / six ticks, stalls, pause, driver replacement,
#                             lossy UDP; the replay baseline
#   test_smooth_interpolation.cpp / test_smooth_footprints.cpp (asan only: no threads of their own)
# usage: tools/smooth/sanitize_threaded.sh tsan|asan <build dir>   (ROTWK_INSTALL / BFME2_INSTALL set; exit 0 = no report and every test passed)
set -e
MODE="$1"; B="$2"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$MODE" = tsan ]; then FLAGS="-fsanitize=thread"; else FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
cmake -S "$SRC/engine" -B "$B" -G Ninja -DOPENBFME_BUILD_GODOT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$FLAGS -O1" -DCMAKE_C_FLAGS="$FLAGS -O1" \
	-DCMAKE_EXE_LINKER_FLAGS="$FLAGS" > "$B.cfg.log"
cmake --build "$B" -j"${JOBS:-3}" --target openbfme_core openbfme_lua
TESTS="test_main test_smooth_threaded test_smooth_live test_smooth_races test_smooth_net StartTestUtil RetailTestMount"
[ "$MODE" = asan ] && TESTS="$TESTS test_smooth_interpolation test_smooth_footprints"
OBJS=""
for t in $TESTS; do OBJS="$OBJS CMakeFiles/openbfme_tests.dir/tests/$t.cpp.o"; done
(cd "$B" && ninja $OBJS && c++ $FLAGS -o smooth1_${MODE}_tests $OBJS libopenbfme_core.a libopenbfme_lua.a -lpthread)
if [ "$MODE" = tsan ]; then
	TSAN_OPTIONS="halt_on_error=1 report_signal_unsafe=0" "$B/smooth1_${MODE}_tests"
else
	ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" "$B/smooth1_${MODE}_tests"
fi
