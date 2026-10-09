# OpenBFME plan

Goal: RotWK 2.01 in Godot, faithful to retail, loading the player's own game
files. Long term, also run BFME2/RotWK mods and play against retail clients.

## Two profiles, designed in from the start

| | Retail-compatible | Enhanced |
|---|---|---|
| Rules | exact 2.01 behaviour | 2.01 plus named fixes and improvements |
| Logic rate | 5 frames/s | 5 frames/s (another rate would be a separate, later profile) |
| Numerics | reproduces retail results (see rule 3) | native |
| Replays | plays retail .BfME2Replay files | own replays |
| Multiplayer | goal: join retail 2.01 LAN games (gates below) | OpenBFME players only |
| Mods | retail `-mod` semantics | same |

- **A profile has an immutable internal identifier.** It covers engine
  features, the numeric mode and the effective mounted data: archive hashes,
  the contents of every directory mod, and the final mount order and
  precedence.
  - **Retail-compatible profile:** retail formats are never changed. Replays,
    saves and the network handshake carry only what retail carries (INI
    checksum, map CRC, options string, and so on). Identity is derived from
    that verified retail metadata. Extra OpenBFME metadata, if any, lives in a
    separate sidecar file next to the replay or save, never inside the retail
    format.
  - **Enhanced profile:** our own formats carry the identifier directly, and
    peers with different identifiers refuse to play.
- **Rendering runs at any frame rate and interpolates between logic frames.**
  It can never change authoritative state or consume simulation RNG, and the
  logic frame order is independent of render rate.

## Standing engineering rules

1. **Port, don't invent. Sources, in priority order:**
   1. the target binary (a verified clean RotWK 2.01 engine, see rule 9);
   2. Open-BFME-2 matching C++ (BFME2 1.06);
   3. Open-BFME-1 matching C++ (BFME1);
   4. Zero Hour source.

   A lower source never overrides an established BFME/RotWK difference. A ZH
   citation alone isn't enough where BFME may differ. Every port comment
   separates target facts, donor facts and inference.
2. **5 logic frames per second.** Each INI parser keeps its own conversion,
   taken from the binary.
   - `parseDurationUnsignedInt` (0x73A429, sequence at 0x73A440-0x73A458):
     1. load the integer onto the FPU (`fild`), applying the unsigned
        correction if needed;
     2. multiply by the stored float32 constant `0.005f` under the game's FPU
        state (24-bit precision, round-to-nearest);
     3. store as a double;
     4. call MSVCR71 `ceil`.

     The input is never first rounded to float32. That extra step changes
     results: 52,428,805 ms gives 262145 frames in retail but 262144 with an
     early float32 conversion.
   - `parseDurationReal` (0x73A403): the product is stored without `ceil`.
   - The velocity, angle and other parsers follow the INI spec.
3. **Numerics are specified, not assumed.**
   - Retail mixes x87 and SSE: `moveForward` at 0x5E586F uses both.
   - `setFPMode` (0x440809) selects 24-bit precision and round-to-nearest, and
     functions change the control word temporarily.
   - The game calls the MSVCR71 CRT for `sqrt`, `sin`, `cos`, `atan2`, ...

   All simulation maths goes through one facade so the implementation can
   change. That alone does not deliver parity: the retail-compatible profile
   also needs per-function operation order, precision state, conversions and
   CRT-identical results. Until that is shown on replays, numeric parity is an
   open research item, not a solved design.
4. **Retail message numbering.** Player commands use RotWK's GameMessage types
   1001-1147 (`MSG_LOGIC_CRC` = 1098). This fixes command names only; the live
   network serialization is a separate, unverified item.
5. **Checksums are reproduced exactly, from the binary.**
   - The logic and INI CRC step at 0xA211DF is `crc = rotl32(crc, 1) + le_word`
     (mod 2^32), with one rotate-add per trailing byte. Transfer-buffer
     boundaries matter.
   - The stored INI checksum adds a further, still unidentified subsystem value
     (0x63C809 / 0x63C820).
   - Implementation waits until the full algorithm and the input sequence are
     documented.
6. **Mods use retail `-mod` semantics.**
   - `-mod` (handler 0x7BADB9) takes a directory or archive and switches file
     precedence to prefer local files (flag 0xDEC490, used by the file-open
     path at 0xA149A2).
   - Layering is proven first with a simple data-only mod.
   - The 2.02 patch is not an ordinary mod: per its install guide it also
     changes the executable. Which binary modifications belong to which 2.02
     release is unverified. It becomes a pinned data-plus-engine profile later.
   - Mods that patch `game.dat` need those changes reimplemented as profile
     features.
   - **Mod compatibility is a design constraint, not a later feature.** A mod
     that runs in retail RotWK runs here unmodified, with its INI, W3D,
     textures, APT, maps, Lua and audio read at runtime from the mounted mod
     exactly as retail reads them. Therefore:
     - engine code never hard-codes retail content (template names, counts,
       faction lists, file lists); retail facts live only in tests and golden
       data;
     - every registry is the binary's full registry, not the subset retail
       data happens to use (e.g. every module class `ModuleFactory` registers,
       not only the ones retail objects reference);
     - every parser accepts exactly what retail accepts and fails where retail
       fails, so a mod that loads in retail loads here, and its errors are
       reported the way retail reports them;
     - a mod corpus (real mods on the developer's machine, never committed)
       is a standing gate next to the retail corpus.
7. **Loose files follow verified precedence; nothing is silently skipped.**
   - In the 2.01 baseline, any loose file in the install folders that isn't
     part of retail is reported as contamination and refused. That includes
     the two zero-byte `.map` files in `F:\RotWK\libraries`.
   - Precedence for explicitly mounted mods reproduces the binary's
     conditional ordering.
8. **Lua is part of the retail baseline, not a mod feature.**
   - Both games ship `data\scripts\scripts.lua` and `ScriptEvents.xml`. RotWK
     embeds Lua 4.0.1 and loads them at 0x73A0B9-0x73A0CD.
   - Retail gameplay depends on it: e.g. troll creation grants
     `Upgrade_SwitchToRockThrowing` through `scripts.lua:33` via
     `scriptevents.xml:91`.
   - Needs a Lua 4.0.1-compatible runtime plus the SAGE bindings and event
     dispatch.
9. **Verified target binary.**
   - `F:\RotWK\game.dat` is community-modified: added `stxt*`, `.mackt` and
     `.danetta` sections, with active calls from 0x5D8A64 and 0x5D8AF1 into
     `.danetta`.
   - It is not trusted as the 2.01 oracle until we have a clean 2.01 binary, or
     a complete, independently verified inventory of every modified site and
     every function that reaches added code.
   - Until then, findings from it carry that caveat, and lanes touching patched
     areas stop.
10. **No silent fallbacks.** Missing or unknown data is an error that reaches
    a test or report.
11. **Pure 2.01 data.** Never read 2.02/HD archives, `F:\RotWK\asset.dat` or
    patched trees, except through an explicitly mounted profile.
12. **Update order is retail's.** The six-phase logic update and object order
    from the INI/object spec are preserved exactly.

## Acceptance stops

An item is blocked, not accepted, when any of these hold:
- the target's provenance is disputed;
- a donor-vs-target difference is unresolved;
- a required field is unidentified;
- no external expected result exists.

What a blocked item requires:
- **Report it at runtime.** The code reports it explicitly (an error, an
  `unverified` list, a thrown stop), never a silent default or guess.
- **Pin it in a test.** A test asserts the exact stop, so it can't
  silently disappear or grow.
- **Register it.** It is listed in `docs/STOPS.md` with evidence, impact, and
  the cheapest way to resolve it.

Under those conditions the rest of the lane can be accepted and merged. The
stop blocks only what depends on that item: for example, CRC parity can't
be claimed while a stop affects CRC input. Reviewers judge whether a stop is
properly reported and registered, not whether it exists.

## Oracles and gates

- **Retail replays.** MP replays carry `MSG_LOGIC_CRC` at the configured
  interval: 100 normally, suppressed under deep/lite CRC or interval 1.
  Skirmish replays carry none, so 2.01 LAN replays are needed. Replay CRC
  parity is necessary evidence for the retail-compatible profile. It is not
  sufficient for cross-play.
- **Cross-play has separate gates, each verified against retail, not inferred
  from replay headers:**
  1. packet encoding and transport;
  2. LAN discovery and lobby;
  3. version/INI/executable compatibility checks as RotWK actually performs
     them;
  4. map transfer;
  5. initial RNG, player and hero state;
  6. frame-command completeness and router pacing;
  7. replay CRC parity;
  8. a live game with a retail client.
- **Before CRC parity:** frame-counted recordings and memory traces of retail.

## Roles

Opus plans and orchestrates, Sonnet implements, Sol 6.1 reviews every lane
before it is merged.
