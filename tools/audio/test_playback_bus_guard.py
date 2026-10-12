"""CRASH-2 / QACRASH-1: no game code may replace a playing sound's bus details in Godot 4.7.2.

Godot 4.7.2 frees a playback's replaced AudioStreamPlaybackBusDetails two main-loop AudioServer::update() calls later
(servers/audio/audio_server.cpp: set_playback_bus_volumes_linear -> bus_details_graveyard -> bus_details_graveyard_frame_old), whether or not
the mix thread is still between loading that pointer and copying it (AudioServer::_mix_step, `AudioStreamPlaybackBusDetails bus_details =
*bus_details_ptr;` and the thread_find_bus_index lookup after it). With the mix thread descheduled for two frames the copy reads freed memory:
the QA farm's audio-thread SIGSEGVs, exit -6 through Godot's crash handler (QACRASH-1: godot+0x371947c, the bus lookup; CRASH-2, FACT-B's
002-qa-Dwarves on Umbar: godot+0x44f7c18, StringName's copy constructor called from the bus_details copy at godot+0x3719884).
What replaces the bus details of a playing sound (Godot 4.7.2 source):
- AudioStreamPlayer.volume_db / volume_linear (scene/audio/audio_stream_player.cpp: set_volume_db -> set_playback_all_bus_volumes_linear);
- AudioStreamPlayer.bus (set_bus -> set_playback_bus_exclusive);
- AudioStreamPlayer2D / AudioStreamPlayer3D at all: they replace them every physics frame while playing
  (scene/2d/audio_stream_player_2d.cpp: set_playback_bus_exclusive; scene/3d/audio_stream_player_3d.cpp: set_playback_bus_volumes_linear).
So a voice's volume goes through its slot bus's AudioEffectAmplify (GodotGameAudio.cpp), the bus is set once when the slot's player is made,
and positional sound is the core's (GameAudio) gains on a 2D AudioStreamPlayer. This test scans the game's C++ and Godot files for the three.
The runtime scenario is godot/tests/audio_stress_test.gd under tools/audio/starve_mix_thread.sh (CRASH-2, Deck, 120 s runs, 6 at a time):
with 23af52ae's volume_db write put back, 1 of 34 starved runs aborted in glibc's free() on the main thread, deleting a 0xf8-byte
AudioStreamPlaybackBusDetails from the graveyard (godot+0x36fc1b0) while the mix thread was in _mix_step: the copy's StringName "ref" on the
freed block had bumped the allocator's tcache. At 18f027af 18 of 18 passed. The race is rare per run, so the stress test is not a gate.
"""
import os
import re

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
POSITIONAL = re.compile(r'\bAudioStreamPlayer[23]D\b|audio_stream_player_[23]d\.hpp')
CPP_VOLUME = re.compile(r'([A-Za-z_][\w\]\)]*)\s*(?:->|\.)\s*set_volume_(?:db|linear)\s*\(')
CPP_BUS = re.compile(r'(?:->|\.)\s*set_bus\s*\(')
GD_VOLUME = re.compile(r'(?:\b([A-Za-z_]\w*)\s*\.\s*)?\bvolume_(?:db|linear)\s*(?:[-+*/]?=)(?!=)|(?:\b([A-Za-z_]\w*)\s*\.\s*)?\bset_volume_(?:db|linear)\s*\(')
GD_PROPERTY = re.compile(r'''(?:\bset|\bset_deferred|\btween_property)\s*\([^)]*["'&](?:volume_db|volume_linear|bus)["']''')
GD_BUS = re.compile(r'\.\s*bus\s*=(?!=)|\bset_bus\s*\(')
# the one set_bus allowed: the slot's player gets its bus when the device makes it, before it ever plays
BUS_SETTER = ('engine/src/GodotDevice/GodotGameAudio.cpp', 'GodotAudioDevice::GodotAudioDevice(')


def strip_cpp_comments(src):
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith('//', i):
            j = src.find('\n', i)
            i = n if j < 0 else j
        elif src.startswith('/*', i):
            j = src.find('*/', i + 2)
            out.append('\n' * src.count('\n', i, n if j < 0 else j))
            i = n if j < 0 else j + 2
        elif src[i] in '"\'':
            q, j = src[i], i + 1
            while j < n and src[j] != q:
                j += 2 if src[j] == '\\' else 1
            out.append(src[i:j + 1])
            i = j + 1
        else:
            out.append(src[i])
            i += 1
    return ''.join(out)


def strip_gd_comment(line):
    q = None
    for i, c in enumerate(line):
        if q:
            if c == q:
                q = None
        elif c in '"\'':
            q = c
        elif c == '#':
            return line[:i]
    return line


def body_span(src, signature):
    """[start, end) of the brace block of the function whose definition starts with `signature` (None if absent)."""
    at = src.find(signature)
    if at < 0:
        return None
    open_at = src.find('{', at)
    depth = 0
    for i in range(open_at, len(src)):
        depth += {'{': 1, '}': -1}.get(src[i], 0)
        if depth == 0:
            return open_at, i + 1
    return None


def check_cpp(rel, text):
    src = strip_cpp_comments(text)
    found = []
    line = lambda pos: src.count('\n', 0, pos) + 1
    for m in POSITIONAL.finditer(src):
        found.append('%s:%d: %s replaces its bus details every physics frame' % (rel, line(m.start()), m.group(0)))
    for m in CPP_VOLUME.finditer(src):
        if not m.group(1).lower().endswith('amplify'):
            found.append('%s:%d: %s: a voice volume goes to its slot bus\'s AudioEffectAmplify, never a player\'s volume' % (rel, line(m.start()), m.group(0)))
    span = body_span(src, BUS_SETTER[1]) if rel == BUS_SETTER[0] else None
    for m in CPP_BUS.finditer(src):
        if not (span and span[0] <= m.start() < span[1]):
            found.append('%s:%d: set_bus outside %s (a playing player\'s bus change replaces its bus details)' % (rel, line(m.start()), BUS_SETTER[1]))
    return found


def check_gd(rel, text):
    found = []
    tscn = not rel.endswith('.gd')
    for no, raw in enumerate(text.splitlines(), 1):
        code = raw if tscn else strip_gd_comment(raw)
        if POSITIONAL.search(code):
            found.append('%s:%d: AudioStreamPlayer2D / 3D replaces its bus details every physics frame' % (rel, no))
        if tscn:
            continue  # a scene's stored volume / bus is set before the player plays
        for m in GD_VOLUME.finditer(code):
            receiver = m.group(1) or m.group(2) or ''
            if not receiver.lower().endswith('amplify'):
                found.append('%s:%d: %s: a player volume write replaces its bus details' % (rel, no, m.group(0).strip()))
        if GD_PROPERTY.search(code) or GD_BUS.search(code):
            found.append('%s:%d: a player volume / bus write replaces its bus details' % (rel, no))
    return found


def scan():
    found = []
    for top, exts, check in (('engine/src', ('.cpp', '.h', '.inl'), check_cpp), ('godot', ('.gd', '.tscn', '.tres'), check_gd)):
        for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, top)):
            dirnames[:] = sorted(d for d in dirnames if d not in ('.godot', 'bin'))
            for f in sorted(filenames):
                if f.endswith(exts):
                    path = os.path.join(dirpath, f)
                    rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
                    with open(path, encoding='utf-8', errors='replace') as fh:
                        found += check(rel, fh.read())
    return found


def test_no_game_code_replaces_a_playing_sounds_bus_details():
    found = scan()
    assert not found, '\n'.join(found)


def test_the_guard_sees_the_scanned_files():
    # the device that owns every voice is scanned and its two allowed forms are there (a moved file must not turn this test vacuous)
    with open(os.path.join(ROOT, BUS_SETTER[0]), encoding='utf-8') as fh:
        src = strip_cpp_comments(fh.read())
    assert body_span(src, BUS_SETTER[1]) is not None
    assert CPP_BUS.search(src) and any(m.group(1).endswith('amplify') for m in CPP_VOLUME.finditer(src))


def test_the_guard_flags_the_fact_b_build():
    # 23af52ae (FACT-B's QA run, the 002-qa-Dwarves crash): GodotGameAudio.cpp set every playing voice's volume_db each audio tick
    rel = BUS_SETTER[0]
    assert check_cpp(rel, 'void f(Slot &slot) { slot.player->set_volume_db(toDb(volume)); }')
    assert check_cpp(rel, 'void g() { for (Slot &s : m_slots) s.player->set_volume_linear(0.5f); }')
    assert check_cpp(rel, 'void h(Slot &slot) { slot.player->set_bus("Master"); }')
    assert check_cpp(rel, '#include <godot_cpp/classes/audio_stream_player3d.hpp>\nAudioStreamPlayer3D *p;')
    assert not check_cpp(rel, 'void f(Slot &slot) { slot.amplify->set_volume_db(-80.0f); } // player->set_volume_db(0)')
    assert not check_cpp(rel, 'GodotAudioDevice::GodotAudioDevice(Node *o) { if (x) { y(); } slot.player->set_bus(n); }')
    assert check_cpp(rel, 'GodotAudioDevice::GodotAudioDevice(Node *o) { } void k() { p->set_bus(n); }')
    assert not check_cpp(rel, 'void m() { server->set_bus_volume_db(0, -6.0f); server->set_bus_send(1, "Master"); }')
    assert check_gd('godot/scripts/x.gd', '\tplayer.volume_db = -6.0')
    assert check_gd('godot/scripts/x.gd', '\tvolume_linear *= 0.5')
    assert check_gd('godot/scripts/x.gd', '\tcreate_tween().tween_property(music, "volume_db", -80.0, 1.0)')
    assert check_gd('godot/scripts/x.gd', '\tp.bus = &"Music"')
    assert check_gd('godot/scenes/x.tscn', '[node name="Fx" type="AudioStreamPlayer3D" parent="."]')
    assert not check_gd('godot/scripts/x.gd', '\tif players[i].volume_db != 0.0: # player.volume_db = 1')
    assert not check_gd('godot/scripts/x.gd', '\tAudioServer.set_bus_volume_db(master, -6.0)')
    assert not check_gd('godot/scripts/x.gd', '\tslot_amplify.volume_db = -6.0')
