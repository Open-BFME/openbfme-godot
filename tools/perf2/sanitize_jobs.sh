#!/bin/bash
# Lane PERF-2: the race and memory checks of the job system and every parallel phase (tools/smooth/sanitize_threaded.sh style). Builds the core library
# under ThreadSanitizer or ASan/UBSan (Godot off) and links these tests into a small executable, then runs them against the retail install:
#   test_perf2_jobs.cpp         the pool itself: chunking, ordered results with 1 .. N threads, the FP environment checks, errors, nested and concurrent
#                               dispatches, thread-count changes
#   test_perf2_determinism.cpp  the big battle (collision pass on the logic pool) and the 4-player AI skirmish at 1, 2, 4 and N logic threads against the
#                               single-threaded reference hashes; its world load runs the parallel loading (archive MD5s, base layouts, INI pre-parse)
#   test_perf2_client.cpp       60 particle systems built on the client pool at 1, 2, 4 and N threads (batches and notes equal)
#   test_fx_draw.cpp            the particle batch rules (sprite / quad / butterfly / streak geometry on the client pool)
# usage: tools/perf2/sanitize_jobs.sh tsan|asan <build dir>   (ROTWK_INSTALL / BFME2_INSTALL set; exit 0 = no report and every test passed)
set -e
MODE="$1"; B="$2"
SRC="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$MODE" = tsan ]; then FLAGS="-fsanitize=thread"; else FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
cmake -S "$SRC/engine" -B "$B" -G Ninja -DOPENBFME_BUILD_GODOT=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS="$FLAGS -O1" -DCMAKE_C_FLAGS="$FLAGS -O1" \
	-DCMAKE_EXE_LINKER_FLAGS="$FLAGS" > "$B.cfg.log"
cmake --build "$B" -j"${JOBS:-3}" --target openbfme_core openbfme_lua
TESTS="test_main test_perf2_jobs test_perf2_determinism test_perf1_bench test_perf2_client test_fx_draw StartTestUtil RetailTestMount"
OBJS=""
for t in $TESTS; do OBJS="$OBJS CMakeFiles/openbfme_tests.dir/tests/$t.cpp.o"; done
(cd "$B" && ninja $OBJS && c++ $FLAGS -o perf2_${MODE}_tests $OBJS libopenbfme_core.a libopenbfme_lua.a -lpthread)
# the long benchmarks stay skipped; the perf1 determinism test (6000 frames) is not part of this run
FILTER="-tc=perf2*,draw:*"
if [ "$MODE" = tsan ]; then
	TSAN_OPTIONS="halt_on_error=1 report_signal_unsafe=0" "$B/perf2_${MODE}_tests" "$FILTER"
else
	ASAN_OPTIONS="detect_leaks=1:halt_on_error=1" UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" "$B/perf2_${MODE}_tests" "$FILTER"
fi
