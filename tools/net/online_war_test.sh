#!/bin/bash
# WOTR2 milestone 9: two independent online War of the Ring clients through a local lobby and classic relay.
# tools/net/online_war_test.sh [turns] [turns|rts|rtsexit|save|crash|stall] (on a game-data build with godot on PATH).
set -u
TURNS=${1:-3}
MODE=${2:-turns}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
RELAY=${RELAY:-${B:-}/relay/openbfme_relay}
GODOT=${GODOT:-godot}
if [ ! -x "$RELAY" ]; then echo "ONLINE WAR TEST FAIL: no relay executable"; exit 2; fi
port() { python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.$1);s.bind(('127.0.0.1',0));print(s.getsockname()[1])"; }
LPORT=$(port SOCK_STREAM)
RPORT=$(port SOCK_DGRAM)
WORK=$(mktemp -d)
mkdir -p "$WORK/data" "$WORK/host" "$WORK/joiner" "$WORK/relaylog"
KEY=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
printf '1:%s\n' "$KEY" > "$WORK/keys.txt"
chmod 600 "$WORK/keys.txt"
pids=()
cleanup() {
  for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done
  wait 2>/dev/null
  if [ -n "${KEEP_LOGS:-}" ]; then
    cp "$WORK"/*.log "$KEEP_LOGS"/ 2>/dev/null
  fi
  rm -rf "$WORK" # created by this test alone
}
trap cleanup EXIT
"$RELAY" --bind "127.0.0.1:$RPORT" --keys "$WORK/keys.txt" --log-dir "$WORK/relaylog" --status "$WORK/rs.json" --stats-s 0 --idle-s 120 > "$WORK/relay.log" 2>&1 &
pids+=($!)
LOBBY_BIND="127.0.0.1:$LPORT" LOBBY_DATA="$WORK/data" TICKET_KEYS="1:$KEY" SESSION_KEY=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf \
  RELAYS="local=127.0.0.1:$RPORT" node "$ROOT/services/online/lobby/src/server.mjs" > "$WORK/lobby.log" 2>&1 &
pids+=($!)
sleep 2
cd "$ROOT/godot" || exit 2
COMMON=(--headless --path . --audio-driver Dummy -- --movies=off --no-intro --no-update-check
  "--online-lobby=http://127.0.0.1:$LPORT" "--wotr-turns=$TURNS" --net-disconnect-ms=2000 --net-player-timeout-ms=8000)
EXTRA=()
JOIN_EXTRA=()
case "$MODE" in
  turns) ;;
  rts) EXTRA=(--lan-wotr-rts --lan-wotr-rts-battles=1) ;;
  rtsexit) EXTRA=(--lan-wotr-rts --lan-wotr-rts-exit) ;;
  save) EXTRA=(--lan-wotr-save-turn=3) ;;
  crash) JOIN_EXTRA=(--lan-wotr-crash-turn=2) ;;
  stall) JOIN_EXTRA=(--lan-wotr-stall-turn=2 "--lan-wotr-stall-seconds=${STALL_SECONDS:-20}") ;;
  *) echo "ONLINE WAR TEST FAIL: unknown mode $MODE"; exit 2 ;;
esac
run_pair() {
  HOME="$WORK/host" XDG_DATA_HOME="$WORK/host/.local/share" XDG_CONFIG_HOME="$WORK/host/.config" \
    timeout 900 "$GODOT" "${COMMON[@]}" --online-walk=warhost --online-name=Host "${EXTRA[@]}" "$@" > "$WORK/host.log" 2>&1 &
  HP=$!
  pids+=("$HP")
  sleep 12
  HOME="$WORK/joiner" XDG_DATA_HOME="$WORK/joiner/.local/share" XDG_CONFIG_HOME="$WORK/joiner/.config" \
    timeout 900 "$GODOT" "${COMMON[@]}" --online-walk=warjoin --online-name=Joiner "${EXTRA[@]}" "${JOIN_EXTRA[@]}" > "$WORK/joiner.log" 2>&1 &
  JP=$!
  pids+=("$JP")
  if [ "$MODE" = crash ] || [ "$MODE" = stall ]; then
    local marker="GAME WOTR NET $MODE: turn 2"
    for attempt in {1..120}; do
      grep -q "$marker" "$WORK/joiner.log" 2>/dev/null && break
      kill -0 "$JP" 2>/dev/null || break
      sleep 1
    done
    # An online relay holds a missing slot instead of declaring it surrendered. Observe the survivor before stopping this test's own process.
    sleep "$([ "$MODE" = stall ] && echo "$((${STALL_SECONDS:-20} + 20))" || echo 25)"
    kill "$HP" 2>/dev/null
    if [ "$MODE" = stall ]; then kill "$JP" 2>/dev/null; fi
  fi
  wait "$HP"; HRC=$?
  wait "$JP"; JRC=$?
  pids=("${pids[0]}" "${pids[1]}")
}
run_pair
rc=0
if [ "$MODE" != crash ] && [ "$MODE" != stall ]; then
  [ "$HRC" = 0 ] && [ "$JRC" = 0 ] || rc=1
fi
if [ "$MODE" = crash ] || [ "$MODE" = stall ]; then
  grep -q "GAME WOTR NET $MODE: turn 2" "$WORK/joiner.log" || { echo "FAIL: the joiner did not $MODE in turn 2"; rc=1; }
  grep -q '^GAME WOTR NET turn 2 frame' "$WORK/host.log" || { echo "FAIL: the survivor ran no matching strategic turn"; rc=1; }
  grep -q '^GAME WOTR NET turn 2 frame' "$WORK/joiner.log" || { echo "FAIL: the dropped peer ran no strategic turn"; rc=1; }
  if grep -qE '^GAME WOTR NET (surrender:|RESULT |left:)' "$WORK/host.log"; then
    echo "FAIL: the survivor advanced past the relay-held slot"; rc=1;
  fi
  if grep -q '^GAME FAIL:' "$WORK/host.log"; then
    echo "FAIL: the survivor failed rather than waiting"; rc=1;
  fi
  grep -E '^GAME WOTR NET turn [0-9]+ frame [0-9]+ hash [0-9]+' "$WORK/host.log" > "$WORK/host.turns"
  grep -E '^GAME WOTR NET turn [0-9]+ frame [0-9]+ hash [0-9]+' "$WORK/joiner.log" > "$WORK/joiner.turns"
  [ -s "$WORK/host.turns" ] && cmp -s "$WORK/host.turns" "$WORK/joiner.turns" || {
    echo "FAIL: the two peers diverged before the relay held the slot"; rc=1;
  }
  if [ "$rc" != 0 ]; then
    for peer in host joiner; do
      echo "$peer: last war events"; grep -E 'GAME FAIL|GAME WOTR NET|GAME ONLINE WAR' "$WORK/$peer.log" | tail -12
    done
  fi
  echo "ONLINE WAR TEST $([ "$rc" = 0 ] && echo PASS || echo FAIL): mode $MODE, relay held a peer after $(wc -l < "$WORK/host.turns") turns"
  exit "$rc"
fi
if [ "$MODE" = save ]; then
  for peer in host joiner; do
    cp "$WORK/$peer.log" "$WORK/$peer.saved.log"
    grep -q 'GAME WOTR NET SAVE RESULT ok' "$WORK/$peer.saved.log" || { echo "FAIL: $peer did not save and leave"; rc=1; }
    grep -E '^GAME WOTR NET saved lanwar:' "$WORK/$peer.saved.log" | sed 's/ (.*//' > "$WORK/$peer.saveline"
  done
  [ -s "$WORK/host.saveline" ] && cmp -s "$WORK/host.saveline" "$WORK/joiner.saveline" || {
    echo "FAIL: the saved copies differ in frame, MD5 or hash"; rc=1;
  }
  for peer in host joiner; do
    [ -n "$(find "$WORK/$peer" -name lanwar.obfmesave -print -quit)" ] || { echo "FAIL: $peer has no saved war"; rc=1; }
  done
  if [ "$rc" = 0 ]; then
    EXTRA=()
    run_pair --lan-wotr-resume=lanwar
    [ "$HRC" = 0 ] && [ "$JRC" = 0 ] || rc=1
    for peer in host joiner; do
      grep -q 'GAME WOTR NET resumed the saved war' "$WORK/$peer.log" || { echo "FAIL: $peer did not resume the saved war"; rc=1; }
      grep -E '^GAME WOTR NET turn [0-9]+ frame [0-9]+ hash [0-9]+' "$WORK/$peer.log" > "$WORK/$peer.resumed"
    done
    [ -s "$WORK/host.resumed" ] && cmp -s "$WORK/host.resumed" "$WORK/joiner.resumed" || {
      echo "FAIL: the resumed turns differ"; rc=1;
    }
  fi
  if [ "$rc" != 0 ]; then
    for peer in host joiner; do
      echo "$peer: saved-war diagnosis"; grep -E 'GAME FAIL|SCRIPT ERROR|GAME WOTR NET|GAME ONLINE WAR' "$WORK/$peer.log" | tail -12
      echo "$peer: $(wc -l < "$WORK/$peer.log") log lines; first error"; grep -m1 -E 'ERROR:|SCRIPT ERROR|GAME FAIL|FAIL:' "$WORK/$peer.log"
      echo "$peer: last screen stages"; grep -E 'GAME ONLINE WALK|GAME ONLINE page|GAME ONLINE resume|GAME ONLINE saved-war' "$WORK/$peer.log" | tail -8
      echo "$peer: timed-out stage"; grep 'GAME ONLINE WALK timeout waiting for' "$WORK/$peer.log" | sed -E 's/^.*timeout waiting for ([^:]*):.*$/\1/'
    done
  fi
  echo "ONLINE WAR TEST $([ "$rc" = 0 ] && echo PASS || echo FAIL): save and resume, copies $(wc -l < "$WORK/host.saveline") and $(wc -l < "$WORK/joiner.saveline")"
  exit "$rc"
fi
for peer in host joiner; do
  if [ "$MODE" = rtsexit ]; then
    grep -q "GAME WOTR NET WAR END RESULT ok" "$WORK/$peer.log" || { echo "FAIL: $peer did not leave the battle and war (exit $HRC/$JRC)"; rc=1; }
  else
    grep -q "GAME WOTR NET RESULT ok" "$WORK/$peer.log" || { echo "FAIL: $peer did not finish the war (exit $HRC/$JRC)"; rc=1; }
  fi
  grep -q "GAME ONLINE WAR .*room" "$WORK/$peer.log" || { echo "FAIL: $peer did not enter the online strategic staging room"; rc=1; }
  grep -q "GAME WOTR NET turn" "$WORK/$peer.log" || { echo "FAIL: $peer ran no strategic turn"; rc=1; }
  grep -E "^GAME WOTR NET turn [0-9]+ frame [0-9]+ hash [0-9]+" "$WORK/$peer.log" > "$WORK/$peer.turns"
done
if [ ! -s "$WORK/host.turns" ] || ! cmp -s "$WORK/host.turns" "$WORK/joiner.turns"; then
  echo "FAIL: the strategic turns' frames or hashes differ"
  diff "$WORK/host.turns" "$WORK/joiner.turns"
  rc=1
fi
if [ "$MODE" = rts ]; then
  grep -E "^GAME WOTR NET back on the map" "$WORK/host.log" > "$WORK/host.battle"
  grep -E "^GAME WOTR NET back on the map" "$WORK/joiner.log" > "$WORK/joiner.battle"
  [ -s "$WORK/host.battle" ] && cmp -s "$WORK/host.battle" "$WORK/joiner.battle" || {
    echo "FAIL: the fought battle did not return to the same war hash"; rc=1;
  }
fi
if [ "$MODE" = rtsexit ]; then
  grep -q '^GAME WOTR NET exit:' "$WORK/host.log" || { echo "FAIL: the host did not exit the fought battle"; rc=1; }
  if grep -q '^GAME WOTR NET back on the map' "$WORK/host.log" "$WORK/joiner.log"; then
    echo "FAIL: the abandoned battle returned to the war"; rc=1;
  fi
fi
if [ "$rc" != 0 ]; then
  echo "lobby game counts: $(curl -fsS --max-time 3 "http://127.0.0.1:$LPORT/v1/stats" | python3 -c 'import json,sys; x=json.load(sys.stdin); print(\"open=%s running=%s\" % (x.get(\"games_open\"), x.get(\"games_running\")))' 2>/dev/null)"
  for peer in host joiner; do
    echo "$peer: first game failure"; grep -m1 -E 'GAME FAIL|SCRIPT ERROR|GAME ONLINE error|GAME ONLINE WAR:|online-start-refused' "$WORK/$peer.log"
    echo "$peer: start stages (reply/net poll): $(grep -c '^GAME ONLINE start:' "$WORK/$peer.log") / $(grep -c '^GAME NET lobby started:' "$WORK/$peer.log")"
    echo "$peer: last War of the Ring lines"; grep -E 'GAME WOTR NET|GAME ONLINE WAR|GAME ONLINE WALK' "$WORK/$peer.log" | tail -14
  done
fi
echo "ONLINE WAR TEST $([ "$rc" = 0 ] && echo PASS || echo FAIL): mode $MODE, turns $(wc -l < "$WORK/host.turns") and $(wc -l < "$WORK/joiner.turns")"
exit "$rc"
