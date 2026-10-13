#!/bin/bash
# Lane SELPERF-1: a frame-time measurement on JonathanPC's desktop under briefs/jpc.md's rule (2026-10-11):
# - the wait for an idle machine holds nothing (the release and the devlog go before measurements: no jpc-paused held while a QA farm runs);
# - once no jpc job runs and the WSL load is below IDLE_LOAD (8): the desktop lock, jpc-paused (removed by a trap whatever happens), the idle
#   check again (a job that started meanwhile: the pause is released and the wait goes on), the game at normal priority;
# - a watcher notes every jpc job and the load every 30 s of the measurement (jpc-qa.sh does not wait for jpc-paused): any job or a load above
#   IDLE_LOAD makes the batch CONTAMINATED (exit 4); IDLE THROUGHOUT otherwise.
# Usage: tools/perf/selperf_win_idle.sh <command ...> (e.g. a series of selperf_win.py runs); IDLE_WAIT=<s> bounds the wait (exit 3).
set -u
R="${OPENBFME_RECOVER:-$HOME/.cache/openbfme-recover}"
LIM="${IDLE_LOAD:-8}"
mkdir -p "$R/locks"
jobs_running() { "$R/jpc-status.sh" 2>/dev/null | awk '/^== /{j=($2=="jpc")} j && /^  /' | grep -c . ; }
jpc_load() { "$R/jpc-status.sh" 2>/dev/null | awk '/^== jpc/{getline; sub(/.*load /, ""); sub(/,.*/, ""); print; exit}'; }
idle() { [ "$(jobs_running)" -eq 0 ] && awk -v l="$(jpc_load)" -v lim="$LIM" 'BEGIN { exit !(l != "" && l + 0 < lim) }'; }
t0=$(date +%s)
while :; do
	until idle; do
		if [ $(( $(date +%s) - t0 )) -gt "${IDLE_WAIT:-5400}" ]; then echo "$(date +%H:%M:%S) jpc did not get idle within ${IDLE_WAIT:-5400} s"; exit 3; fi
		sleep 30
	done
	exec 9>"$R/locks/jpc-desktop.lock"
	flock 9
	touch "$R/jpc-paused"
	trap 'rm -f "$R/jpc-paused"; echo "$(date +%H:%M:%S) jpc-paused removed"' EXIT INT TERM
	if idle; then
		break
	fi
	echo "$(date +%H:%M:%S) a job started meanwhile: the pause is released, the wait goes on"
	rm -f "$R/jpc-paused"
	trap - EXIT INT TERM
	flock -u 9
	exec 9>&-
done
echo "$(date +%H:%M:%S) jpc idle after $(( $(date +%s) - t0 )) s, desktop lock held, jpc-paused set: $("$R/jpc-status.sh" 2>/dev/null | sed -n 2p)"
W=$(mktemp)
( while :; do echo "$(date +%H:%M:%S) jobs $(jobs_running) load $(jpc_load)" >> "$W"; sleep 30; done ) &
WP=$!
WIN_PRIORITY=normal "$@"
rc=$?
kill $WP 2>/dev/null
echo "$(date +%H:%M:%S) after the measurement: $("$R/jpc-status.sh" 2>/dev/null | sed -n 2p)"
cat "$W"
if awk -v lim="$LIM" '$3 > 0 || $5 > lim { bad = 1 } END { exit !bad }' "$W"; then
	echo "CONTAMINATED: a jpc job ran or the load rose above $LIM during the measurement"
	rm -f "$W"
	exit 4
fi
rm -f "$W"
echo "IDLE THROUGHOUT"
exit $rc
