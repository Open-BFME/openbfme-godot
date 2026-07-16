# Retail HUD timeline-playback oracle

This oracle narrows `timeline-playback-not-bound` to the exact production HUD
closure. It does not implement a clock and does not import generic Flash
defaults into the retail contract.

The BFME2 1.06 closure contains 22 reachable timeline definitions, 640 authored
frames, and 51 placed instances. Twelve side-button instances share
`ingamesidecommandbar:18`, twelve nested frames share `libingameui:23`, six
command children share `palantir:121`, and the remaining definitions have one
or two instances. Runtime state must therefore belong to the exact instance
path, not the shared definition.

All three owning movies author a 33-ms frame interval. The 66 frame-action
bindings split into 52 exact `stop` programs, four exact
`goto-label; play` programs, and ten typed blocked programs. The four goto
targets are fully resolved:

- `palantir:257` frame 34 -> `_show` at frame 0
- `palantir:293` frames 35 and 51 -> `_up` at frame 10
- `palantir:309` frame 57 -> `_stop` at frame 0

Only `palantir:309` has an explicit frame-zero `stop`. The other 21 definitions
contain no frame-zero timeline-control opcode. The retail engine's default
new-instance playing state is not encoded in APT and remains a trace gate.

No production segment needs a guessed last-frame wrap. Every authored route
reaches a stop or explicit goto before implicit wrapping would be required.
The first safe implementation should therefore fail closed at an unexpected
end instead of adding a generic modulo operation.

The oracle also preserves each source frame-item order. Eleven frames mix an
action with display-list mutation. The strongest discriminator is
`palantir:309` frame 57: source order is one removal, the goto/play action, then
nine placements. Source order is exact; whether the retail scheduler executes
actions in that order or in a separate post-mutation phase is not. The same
trace must establish when target-frame actions run after goto and whether a
delayed update catches up multiple 33-ms frames.

The implementation-ready static slice is intentionally small:

1. keep immutable timeline definitions and independent state per exact path;
2. resolve only inventoried labels;
3. apply explicit stop, play, and goto-then-play effects atomically in authored
   effect order;
4. reuse the already-exact cumulative display list for an explicitly selected
   frame;
5. do not start an automatic clock, recursively execute target-frame actions,
   or add wrapping until the narrow scheduler trace passes;
6. fail closed before any of the ten blocked frame actions unless its typed
   handler is present.

Verification:

```powershell
$env:PYTHONPATH = 'importer'
python -m pytest importer/tests/test_retail_hud_timeline_playback_oracle.py -q
python -m ruff check importer/openbfme_importer/retail_hud_timeline_playback_oracle.py importer/tests/test_retail_hud_timeline_playback_oracle.py
```

The private CLI additionally generates byte-identical contracts from
`hud-261-source-conversion/plan-a.json`, its bundle-A scene contract, and the
hash-pinned effective assets. No converter, runtime, profile, build, or retail
payload file is modified.
