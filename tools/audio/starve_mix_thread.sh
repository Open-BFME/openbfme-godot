#!/bin/bash
# starve_mix_thread.sh <seconds> <godot args...>  (QACRASH-1)
# Runs godot and starves its audio mix thread: the thread is pinned to one CPU at nice 19 beside two busy loops, so it is descheduled for long
# stretches in the middle of AudioServer::_mix_step while the main loop keeps running. That is the condition under which Godot 4.7.2's
# bus-details graveyard (freed two main-loop frames after replacement) is read after free. Linux only; set STARVE_CPU (default: the last CPU
# this shell may use). The busy loops run at nice 10.
# The mix thread is the dummy / real audio driver's thread: the first thread named "godot" created after the WorkerThreadPool's threads.
# The run's output is in starve.out (or $STARVE_OUT).
# Exit status: godot's own (128 + signal when a signal ended it) when it exits within <seconds>; 124 when it is still running then (it is
# stopped); 2 when the mix thread cannot be found or starved (godot is stopped). The busy loops and godot never outlive the script.
set -u
if [ $# -lt 2 ]; then echo "usage: $0 <seconds> <godot args...>" >&2; exit 2; fi
secs=$1; shift
cpu=${STARVE_CPU:-$(python3 -c 'import os; print(max(os.sched_getaffinity(0)))')}
out=${STARVE_OUT:-starve.out}
GP=; H1=; H2=
cleanup() {
  for p in $H1 $H2; do kill "$p" 2>/dev/null; done
  if [ -n "$GP" ] && kill -0 "$GP" 2>/dev/null; then kill "$GP" 2>/dev/null; wait "$GP" 2>/dev/null; fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM
fail() { echo "starve_mix_thread: $*" >&2; exit 2; }

godot "$@" > "$out" 2>&1 & GP=$!
sleep "${STARVE_DELAY:-6}"
kill -0 "$GP" 2>/dev/null || { wait "$GP"; rc=$?; GP=; echo "godot exited $rc before the mix thread was starved"; exit $rc; }
lastw=0; mix=
for t in $(ls /proc/$GP/task | sort -n); do
  case "$(cat /proc/$GP/task/$t/comm 2>/dev/null)" in WorkerThread*) lastw=$t;; esac
done
[ "$lastw" -gt 0 ] || fail "no WorkerThreadPool thread in godot $GP"
for t in $(ls /proc/$GP/task | sort -n); do
  [ "$t" -gt "$lastw" ] && [ "$(cat /proc/$GP/task/$t/comm 2>/dev/null)" = godot ] && { mix=$t; break; }
done
[ -n "$mix" ] || fail "no mix thread found in godot $GP"
taskset -pc "$cpu" "$mix" >/dev/null || fail "taskset of thread $mix to CPU $cpu failed"
[ "$(taskset -pc "$mix" 2>/dev/null | sed 's/.*: *//')" = "$cpu" ] || fail "thread $mix is not pinned to CPU $cpu"
renice -n 19 -p "$mix" >/dev/null || fail "renice of thread $mix failed"
[ "$(awk '{print $19}' /proc/$GP/task/$mix/stat 2>/dev/null)" = 19 ] || fail "thread $mix does not run at nice 19"
nice -n 10 taskset -c "$cpu" sh -c 'while :; do :; done' & H1=$!
nice -n 10 taskset -c "$cpu" sh -c 'while :; do :; done' & H2=$!
echo "mix thread $mix starved on CPU $cpu"

end=$((SECONDS + secs))
while kill -0 "$GP" 2>/dev/null && [ $SECONDS -lt $end ]; do sleep 1; done
if kill -0 "$GP" 2>/dev/null; then
  echo "alive after ${secs}s (stopped)"
  exit 124
fi
wait "$GP"; rc=$?; GP=
echo "godot exited $rc"
exit $rc
