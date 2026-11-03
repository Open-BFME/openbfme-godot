#!/bin/bash
# WOTR2 milestone 8 (step 2b): a LAN War of the Ring between three headless Godot clients on one machine (127.0.0.1): the host opens the LanStrategic page,
# sets the scenario, every player picks a start region, the host starts; each client plays its turns (scripted orders, END PHASE, its own battles
# auto-resolved). The third player's process is killed in turn 2's orders phase: the others' disconnect path drops it and every survivor runs its 0x6B8 on
# the same frame. Passes when both survivors print the same "GAME WOTR NET turn T frame F hash H" lines for every turn both played, the same surrender
# frame, no desync, and "GAME WOTR NET RESULT ok".
#   tools/net/lan_war_test.sh [TURNS] [crash|stall|rts|rtsexit|save]   (run from the repository; needs godot on PATH, the retail installs and the built GDExtension)
# stall: the third player's process hangs 20 s in turn 2 instead of dying: the others drop it, and once it runs again it must leave by itself (its network
# asks it to) and its score screen's Continue must show the LAN lobby again, without a surrender of its own (no split game); STALL_SECONDS sets the hang
# (20 by default; MPDROP-3 / S-1127: a hang far past NetworkPlayerTimeoutTime too, 120). Required, as crash mode (S-1341 closed)
# rts (WOTR2 milestone 8 step 3): two human clients (no AI: the battles stay two-sided, S-1342) vote real time; the first battle between the two humans is fought over the LAN (every human
# of the war is in it, RW 0x6B5E3E), the host forfeits it through the quit menu (0x448); both must print the same war hash back on the map
# rtsexit: as rts, but the host leaves the battle through the quit menu's Exit: the war must end on both peers (retail's QuitWar), no result applied
# save (WOTR2 milestone 8 step 4): save the war in turn 3 on both PCs (Yes, then the unanimous leave), resume it from the LAN lobby and play on in step,
# then the lobby's named refusals on every PC: Alice's copy missing, Alice's copy differing (one byte flipped); then the lone host's
# start refused (its setup's start check)
# Each client has its own throwaway HOME (profiles, settings); everything it starts is stopped by PID at the end.
set -u
TURNS=${1:-5}
MODE=${2:-crash}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PORT=$((47000 + RANDOM % 2000))
WORK=$(mktemp -d)
pids=()
cleanup() { for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done; wait 2>/dev/null; [ -n "${KEEP_LOGS:-}" ] && cp "$WORK"/*.log "$KEEP_LOGS"/ 2>/dev/null; rm -rf "$WORK"; }
trap cleanup EXIT
cd "$ROOT/godot" || exit 2
# a fresh checkout has no .godot/extension_list.cfg: import once so Godot registers the openbfme extension (a first import may abort: retry), as verify.sh
for i in 1 2; do [ -f .godot/extension_list.cfg ] || timeout 900 godot --headless --path . --import > "$WORK/import.log" 2>&1; done
[ -f .godot/extension_list.cfg ] || { echo "LAN WAR TEST FAIL: the project import did not register the extension"; tail -20 "$WORK/import.log"; exit 1; }
COMMON=(--headless --path . --audio-driver Dummy -- --res=1280x720 --movies=off --no-intro --no-update-check --lan-wotr --lan-targets=127.0.0.1
  --lan-port-base=$PORT --lan-players=3 --wotr-turns=$TURNS --net-disconnect-ms=2000 --net-player-timeout-ms=8000)
run() { # name role region extra...
  local name=$1 role=$2 region=$3; shift 3
  mkdir -p "$WORK/$name"
  # its own XDG dirs too: a CI wrapper's XDG_DATA_HOME would otherwise give every client the same user folder (saves, Options.ini)
  HOME="$WORK/$name" XDG_DATA_HOME="$WORK/$name/.local/share" XDG_CONFIG_HOME="$WORK/$name/.config" timeout 1500 godot "${COMMON[@]}" --lan=$role --net-name=$name --lan-wotr-region=$region "$@" > "$WORK/$name.log" 2>&1 &
  pids+=($!)
}
if [ "$MODE" = save ]; then
  # WOTR2 milestone 8 (step 4): two humans save the war (turn 3: Host asks, Alice says Yes, both vote to leave for now), then resume it from the LAN
  # lobby and play on in step; then the lobby's named refusals: Alice's copy missing, Alice's copy differing; the lone host's start refused
  fail=0
  keep() { for n in Host Alice; do [ -f "$WORK/$n.log" ] && cp "$WORK/$n.log" "$WORK/$n.$1.log"; done; }
  waitall() { for p in "${pids[@]}"; do wait "$p"; done; pids=(); }
  run Host host 0 --lan-players=2 --lan-ai=0 --lan-wotr-save-turn=3; sleep 20; run Alice join 1 --lan-players=2 --lan-wotr-save-turn=3; waitall; keep saved
  for n in Host Alice; do grep -q "GAME WOTR NET SAVE RESULT ok" "$WORK/$n.saved.log" || { echo "FAIL: $n did not save and leave"; grep "GAME WOTR NET" "$WORK/$n.saved.log" | tail -6; fail=1; }; done
  for n in Host Alice; do grep -E "^GAME WOTR NET saved lanwar:" "$WORK/$n.saved.log" | sed 's/ (.*//' > "$WORK/$n.saveline"; done
  [ -s "$WORK/Host.saveline" ] && cmp -s "$WORK/Host.saveline" "$WORK/Alice.saveline" || { echo "FAIL: the two copies differ (frame, md5, hash)"; cat "$WORK/Host.saveline" "$WORK/Alice.saveline"; fail=1; }
  ASAVE=$(find "$WORK/Alice" -name lanwar.obfmesave | head -1)
  [ -n "$ASAVE" ] && cp "$ASAVE" "$WORK/alice-lanwar.bak" || { echo "FAIL: Alice has no lanwar.obfmesave"; fail=1; }
  run Host host 0 --lan-players=2 --lan-ai=0 --lan-wotr-resume=lanwar; sleep 20; run Alice join 1 --lan-players=2; waitall; keep resumed
  for n in Host Alice; do grep -q "GAME WOTR NET resumed the saved war" "$WORK/$n.resumed.log" || { echo "FAIL: $n did not resume the war"; grep "GAME WOTR NET\|GAME FAIL\|GAME LAN script" "$WORK/$n.resumed.log" | tail -6; fail=1; }; done
  for n in Host Alice; do grep -E "^GAME WOTR NET turn " "$WORK/$n.resumed.log" > "$WORK/$n.rturns"; done
  [ -s "$WORK/Host.rturns" ] && cmp -s "$WORK/Host.rturns" "$WORK/Alice.rturns" || { echo "FAIL: the resumed war's turns differ"; diff "$WORK/Host.rturns" "$WORK/Alice.rturns"; fail=1; }
  [ -n "$ASAVE" ] && rm -f "$ASAVE"
  run Host host 0 --lan-players=2 --lan-ai=0 --lan-wotr-resume=lanwar --lan-wotr-expect-refusal; sleep 20; run Alice join 1 --lan-players=2 --lan-wotr-expect-refusal; waitall; keep missing
  for n in Host Alice; do grep -q "GAME LAN RESUME REFUSED: Alice cannot resume the saved war: has no copy" "$WORK/$n.missing.log" || { echo "FAIL: $n did not see Alice's missing copy refused by name"; grep "RESUME REFUSED\|system:\|GAME FAIL" "$WORK/$n.missing.log" | tail -4; fail=1; }; done
  [ -n "$ASAVE" ] && cp "$WORK/alice-lanwar.bak" "$ASAVE"
  run Host host 0 --lan-players=2 --lan-ai=0 --lan-wotr-resume=lanwar --lan-wotr-expect-refusal; sleep 20; run Alice join 1 --lan-players=2 --lan-wotr-expect-refusal --lan-wotr-tamper-save=lanwar; waitall; keep differs
  for n in Host Alice; do grep -q "GAME LAN RESUME REFUSED: Alice cannot resume the saved war: its copy of the saved war differs" "$WORK/$n.differs.log" || { echo "FAIL: $n did not see Alice's differing copy refused by name"; grep "RESUME REFUSED\|system:\|GAME FAIL" "$WORK/$n.differs.log" | tail -4; fail=1; }; done
  run Host host 0 --lan-players=1 --lan-ai=0 --lan-wotr-resume=lanwar --lan-wotr-expect-refusal; waitall; keep seat
  # the host alone: the setup's start check refuses a lone human first (RW 0x8431B8); the named open seat (wars of three or more humans) is pinned by
  # test_mp2_lan.cpp's "wotr2 m8 lan" case
  grep -qE "GAME LAN START REFUSED: (GUI:NeedHumanPlayers|LAN:TooFewPlayers)" "$WORK/Host.seat.log" || { echo "FAIL: the lone host's start was not refused"; grep "REFUSED\|system:\|GAME FAIL" "$WORK/Host.seat.log" | tail -4; fail=1; }
  for f in "$WORK"/*.saveline; do echo "$(basename "$f"): $(cat "$f")"; done
  echo "resumed turns compared: $(wc -l < "$WORK/Host.rturns" 2>/dev/null)"
  [ $fail = 0 ] && echo "LAN WAR TEST PASS" || echo "LAN WAR TEST FAIL"
  exit $fail
fi
if [ "$MODE" = rts ] || [ "$MODE" = rtsexit ]; then
  EXTRA=""; [ "$MODE" = rtsexit ] && EXTRA=--lan-wotr-rts-exit
  # two humans and no AI: every battle is the two humans' and two-sided (a third side on a two-start battle map is refused by name, S-1342)
  run Host host 0 --lan-wotr-rts $EXTRA --lan-players=2 --lan-ai=0
  sleep 20
  run Alice join 1 --lan-wotr-rts --lan-players=2
else
  run Host host 0
  sleep 20
  run Alice join 1
  sleep 5
  if [ "$MODE" = stall ]; then run Bob join 2 --lan-wotr-stall-turn=2 --lan-wotr-stall-seconds="${STALL_SECONDS:-20}"; else run Bob join 2 --lan-wotr-crash-turn=2; fi
fi
for i in 0 1; do wait "${pids[$i]}"; echo "${i}: exit $?"; done
fail=0
for name in Host Alice Bob; do
  echo "---- $name"
  grep -E "GAME (FAIL|WOTR NET|LAN War of the Ring|LAN script: (start region|Play Game))|SCRIPT ERROR" "$WORK/$name.log" | tail -30
done
for name in Host Alice; do
  [ "$MODE" = rtsexit ] || grep -q "GAME WOTR NET RESULT ok" "$WORK/$name.log" || { echo "FAIL: $name has no RESULT ok"; fail=1; }
  # Sol review wotr2m8s2b: Save / Load refused in the war, the session detached and the network game over after it, a single player war on the same world
  [ "$MODE" = rtsexit ] || grep -q "GAME WOTR NET AFTER ok" "$WORK/$name.log" || { echo "FAIL: $name's after-war checks"; grep "GAME WOTR NET AFTER" "$WORK/$name.log"; fail=1; }
  grep -E "^GAME WOTR NET turn " "$WORK/$name.log" > "$WORK/$name.turns"
done
# the surrenders while both survivors played (up to the last turn frame both printed): the first survivor to finish its last turn leaves the war, which
# the other then runs as that player's surrender (an end-of-test order, not a difference)
LASTF=$(cat "$WORK/Host.turns" "$WORK/Alice.turns" | awk '{ print $7 }' | sort -n | uniq -d | tail -1)
for name in Host Alice; do
  grep -E "^GAME WOTR NET surrender: player" "$WORK/$name.log" | awk -v last="${LASTF:-0}" '$9 <= last' > "$WORK/$name.surrender"
done
if [ "$MODE" = rtsexit ]; then
  for name in Host Alice; do
    grep -q "GAME WOTR NET WAR END RESULT ok" "$WORK/$name.log" || { echo "FAIL: $name's war did not end with the battle"; grep "GAME WOTR NET" "$WORK/$name.log" | tail -4; fail=1; }
  done
elif [ "$MODE" = rts ]; then
  for name in Host Alice; do grep -E "^GAME WOTR NET back on the map" "$WORK/$name.log" > "$WORK/$name.back"; done
  [ -s "$WORK/Host.back" ] || { echo "FAIL: no real-time battle came back to the map"; fail=1; }
  cmp -s "$WORK/Host.back" "$WORK/Alice.back" || { echo "FAIL: the war hash after the battle differs"; cat "$WORK/Host.back" "$WORK/Alice.back"; fail=1; }
elif [ "$MODE" = stall ]; then
  wait "${pids[2]}"; echo "2: exit $?"
  grep -q "GAME WOTR NET STALL RESULT ok" "$WORK/Bob.log" || { echo "FAIL: the stalled Bob did not leave by itself to the LAN lobby"; grep "GAME WOTR NET" "$WORK/Bob.log" | tail -5; fail=1; }
  # MPDROP-3 (S-1127): no split game: the resumed Bob drops nobody (no surrender of a survivor runs on his map) and learns at once it was dropped
  ! grep -q "^GAME WOTR NET surrender: player" "$WORK/Bob.log" || { echo "FAIL: the stalled Bob ran a surrender of his own (a split game)"; grep "surrender" "$WORK/Bob.log"; fail=1; }
else
  grep -q "GAME WOTR NET crash: turn 2" "$WORK/Bob.log" || { echo "FAIL: Bob did not crash in turn 2"; fail=1; }
fi
[ -s "$WORK/Host.turns" ] || { echo "FAIL: no turn lines"; fail=1; }
cmp -s "$WORK/Host.turns" "$WORK/Alice.turns" || { echo "FAIL: the survivors' turn lines differ"; diff "$WORK/Host.turns" "$WORK/Alice.turns"; fail=1; }
if [ "$MODE" != rts ] && [ "$MODE" != rtsexit ]; then
  [ -s "$WORK/Host.surrender" ] && cmp -s "$WORK/Host.surrender" "$WORK/Alice.surrender" || { echo "FAIL: the surrender differs or is missing"; cat "$WORK/Host.surrender" "$WORK/Alice.surrender"; fail=1; }
fi
echo "turns compared: $(wc -l < "$WORK/Host.turns"); surrender: $(cat "$WORK/Host.surrender")"
[ $fail = 0 ] && echo "LAN WAR TEST PASS" || echo "LAN WAR TEST FAIL"
exit $fail
