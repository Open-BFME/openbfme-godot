#!/bin/bash
# Lane ONLINEMENU-1: a short video of the FRIENDS page clicked through in the real game on a CI host with a display: a local lobby + relay, a second
# player (onlinemenu1_friend_helper.mjs: "Elrond", accepts the request, writes a chat line) and one WINDOWED game client running game.gd
# --online-walk=friends (real pointer clicks and key presses). ffmpeg records the game's own window only (x11grab -window_id: nothing else of the
# desktop), HOME is a fresh directory. Nothing touches a live service.
#   tools/net/onlinemenu1_friends_video.sh <out dir> [WxH]      (from the worktree; DISPLAY set; RELAY=<openbfme_relay>, default $B/relay/openbfme_relay)
set -u
WT=$(pwd)
OUT=${1:?usage: onlinemenu1_friends_video.sh <out dir> [WxH]}
RES=${2:-1280x800}
RELAY=${RELAY:-${B:-}/relay/openbfme_relay}
GODOT=${GODOT:-godot}
[ -n "${DISPLAY:-}" ] || { echo "VIDEO: no DISPLAY"; exit 2; }
command -v node > /dev/null || { echo "VIDEO: no node"; exit 2; }
command -v ffmpeg > /dev/null || { echo "VIDEO: no ffmpeg"; exit 2; }
mkdir -p "$OUT"
port() { python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.$1);s.bind(('127.0.0.1',0));print(s.getsockname()[1])"; }
LPORT=$(port SOCK_STREAM); RPORT=$(port SOCK_DGRAM)
D=$(mktemp -d /tmp/openbfme-om1-video.XXXX)
mkdir -p "$D/data" "$D/home" "$D/relaylog"
KEY=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
printf '1:%s\n' $KEY > "$D/keys.txt"
"$RELAY" --bind 127.0.0.1:$RPORT --keys "$D/keys.txt" --log-dir "$D/relaylog" --status "$D/rs.json" --stats-s 0 --idle-s 120 > "$OUT/relay.log" 2>&1 & RPID=$!
LOBBY_BIND=127.0.0.1:$LPORT LOBBY_DATA="$D/data" TICKET_KEYS="1:$KEY" SESSION_KEY=a0a1a2a3a4a5a6a7a8a9aaabacadaeaf RELAYS="local=127.0.0.1:$RPORT" \
  MOTD="Welcome to the OpenBFME test lobby." node "$WT/services/online/lobby/src/server.mjs" > "$OUT/lobby.log" 2>&1 & LPID=$!
sleep 2
node "$WT/tools/net/onlinemenu1_friend_helper.mjs" "http://127.0.0.1:$LPORT" "$D/code.txt" > "$OUT/helper.log" 2>&1 & HPID=$!
for i in $(seq 1 20); do [ -s "$D/code.txt" ] && break; sleep 0.5; done
CODE=$(cat "$D/code.txt" 2>/dev/null)
[ -n "$CODE" ] || { echo "VIDEO: the helper has no code"; kill $LPID $RPID $HPID; exit 1; }
cd "$WT/godot"
HOME="$D/home" XDG_DATA_HOME="$D/home/.local/share" timeout 400 "$GODOT" --audio-driver Dummy --path . -- --online-walk=friends --online-name=Aragorn \
  --friends-code="$CODE" --online-lobby=http://127.0.0.1:$LPORT --movies=off --res=$RES > "$OUT/game.log" 2>&1 & GP=$!
# the game's window: its id from the window manager's client list (xprop), by its title. The window can be replaced or resized after it appears
# (x11grab -window_id then stops: "Cannot get the image data"): the recording is made in segments, the window found again for each, and joined
# Only this client's window: other games may share the display (a CI host's other lanes); it is the window whose _NET_WM_PID is the game's
# process (timeout's child)
find_wid() {
  local pid
  pid=$(pgrep -P $GP | head -1); pid=${pid:-$GP}
  for w in $(xprop -root _NET_CLIENT_LIST 2>/dev/null | sed 's/.*# //; s/,//g'); do
    if xprop -id "$w" _NET_WM_PID 2>/dev/null | grep -q "= $pid\$"; then echo "$w"; return; fi
  done
}
n=0
while kill -0 $GP 2>/dev/null; do
  WID=$(find_wid)
  if [ -z "$WID" ]; then sleep 1; continue; fi
  ffmpeg -hide_banner -loglevel error -f x11grab -window_id "$WID" -framerate 20 -i "$DISPLAY" -t 400 \
    -vf "scale=1280:800:force_original_aspect_ratio=decrease,pad=1280:800:(ow-iw)/2:(oh-ih)/2,setsar=1" -c:v libx264 -preset veryfast -crf 26 \
    -pix_fmt yuv420p "$OUT/seg$(printf %03d $n).mp4" >> "$OUT/ffmpeg.log" 2>&1 & FP=$!
  while kill -0 $FP 2>/dev/null && kill -0 $GP 2>/dev/null; do sleep 1; done
  kill -INT $FP 2>/dev/null; wait $FP 2>/dev/null
  n=$((n+1)); sleep 0.5
done
echo "VIDEO: $n segment(s)"
wait $GP; GR=$?
: > "$OUT/list.txt"
for f in "$OUT"/seg*.mp4; do
  [ -s "$f" ] || continue
  d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$f" 2>/dev/null)
  python3 -c "import sys; sys.exit(0 if float('${d:-0}' or 0) >= 1.0 else 1)" && echo "file '$f'" >> "$OUT/list.txt"
done
[ -s "$OUT/list.txt" ] && ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i "$OUT/list.txt" -c copy "$OUT/friends-page.mp4"
rm -f "$OUT"/seg*.mp4 "$OUT/list.txt"
kill $LPID $RPID $HPID 2>/dev/null; wait 2>/dev/null
rm -rf "$D"
grep -h "FRIENDS WALK\|GAME ONLINE WALK" "$OUT/game.log" | tail -20
grep -h "helper\|accept\|chat" "$OUT/helper.log" | head
echo "client exit: $GR"
if [ -s "$OUT/friends-page.mp4" ]; then
  # the owner's 10-30 s: the same recording sped up to fit 28 s (the full one is kept beside it)
  DUR=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$OUT/friends-page.mp4" 2>/dev/null)
  F=$(python3 -c "d=float('${DUR:-0}' or 0); print(max(1.0, d / 28.0))")
  ffmpeg -hide_banner -loglevel error -y -i "$OUT/friends-page.mp4" -vf "setpts=PTS/$F" -an -c:v libx264 -preset veryfast -crf 26 -pix_fmt yuv420p \
    "$OUT/friends-page-short.mp4"
fi
ls -la "$OUT"/friends-page*.mp4 2>/dev/null
grep -q "FRIENDS WALK ok" "$OUT/game.log" && [ -s "$OUT/friends-page.mp4" ] && { echo "VIDEO OK"; exit 0; }
echo "VIDEO FAIL"; exit 1
