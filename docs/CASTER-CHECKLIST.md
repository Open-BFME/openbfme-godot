# Caster checklist

Use the same engine/data profile as the recording or online players. Retail
files, recordings and captures are local inputs, never repository assets.

## Before going on air

- [ ] Choose 1920×1080 or 2560×1440, confirm the capture source has no desktop UI.
- [ ] Load an OpenBFME replay or join the online game as a spectator, not a player.
- [ ] For public live games, confirm **Public delay 120s** (or the host's deliberate
      override). The policy is relay-enforced; the caster cannot bypass it.
- [ ] Confirm recorded CRC comparisons are advancing with zero mismatches.
- [ ] Select all vision with **0**, a player with **1–8**, or allies with **T**.
      Fog and object visibility must change even while paused. Selection outlines
      come from the observed player, not a local gameplay selection.
- [ ] The current format has no recorded camera track: **Free camera — no recorded
      camera** is intentional, not a claim to reproduce the player's camera.
- [ ] Select panels with **F1–F6**: players, production, composition, graphs,
      heroes and events. Graph gaps denote unvisited/unavailable data, not zero.
- [ ] Tune director priorities/dwell/idle return, then enable **D**. Pan/zoom
      manually to take over; check the return timer before leaving it unattended.
- [ ] Pin the other location with **Shift+F7**. **F7** toggles the inset; its wheel
      zoom does not move the main camera. Swap exchanges the targets.
- [ ] Enter clean mode with **F9**. Chosen overlays and PiP remain, other HUD
      controls disappear. **Escape** restores the ordinary quit-menu interaction.

## Transport rehearsal

- [ ] **Space** pauses/resumes, **.** steps exactly one frame, speed spans 0.25–8x.
- [ ] Drag the timeline or choose an event marker. Seeking shows progress and
      preserves the view, camera, speed, director, panel choices and PiP pin.
- [ ] Rewind is **replay-from-start fast-forward**, not world snapshots. A minute-20
      replay seek measured 26.4s headlessly on the development machine, including
      reload, with 6,000 recorded hashes checked. Map/machine/render costs vary.
- [ ] Live: pause and observe the received edge advancing while the playhead stays
      fixed. Scrubbing cannot go beyond that edge. **G / Go live** catches up only
      to the delayed received edge, then resumes at the selected speed.
- [ ] Treat a CRC mismatch, relay refusal or corrupt/missing log as a stopped
      broadcast source. Do not describe unchecked continuation as deterministic.

## Evidence checklist

Checked items record the original caster qualification in `CASTER.md`
(2026-10-16), not a fresh gate run for every later revision.

- [x] Vision/POV, recorded selections, transport/rewind and scripted director
      tests; rendered checks at 1080p and 1440p.
- [x] Overlays, clean mode, PiP and paused-vision red/green regression; rendered
      1080p/1440p captures; 675 encounter hashes unchanged.
- [x] Live delay/default-policy, paused reception, received-edge rewind and
      Go live tests; rendered 1080p/1440p captures.
- [x] Full native/simulation-audit/Godot smoke/shader gate completed and qualified
      in `CASTER.md`: five missing-fixture native failures passed on the authorized
      reference-deck rerun; default Vulkan passed; forced Compatibility failed
      seven checks; Windows-only oracles remain unexecuted on Linux.
- [x] Final default-Vulkan live walk: 50 checks at each resolution, zero failures;
      seven captures per resolution inspected. No hardware/OBS pacing claim.
- Report the final commit as `MERGE CANDIDATE <sha>` with these qualifications,
  not as a clean single-invocation/all-platform gate. Do not merge automatically.

This is an OpenBFME presentation feature. It does not close the existing engine's
retail-parity stops or use native save snapshots for caster rewind. Native saves
already have a separate snapshot-load path; caster rewind still rebuilds from
the recording's start.
