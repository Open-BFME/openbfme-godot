# WOTR2 milestone 9: online war verification

The OnlineStrategic page now hosts and joins a War of the Ring on the classic
relay. A war START (version 2; skirmishes retain version 1) carries scenario,
regions, rules, and the saved war's MD5 and state hash. Load Game uses the
existing LAN strategic save block (version 7), seat and rules checks, and each
peer's copy check. No strategic save format changed in this milestone. Before
a real-time battle or a return to the strategic map, the lobby issues a new
phase START (version 2) and relay ticket; players keep their lobby slot for
relay authentication while the battle maps them to tactical slots.

Retail provenance: OnlineStrategic and Open Play share the custom-match screen
with a strategic flag (BFME2 decomp `AptOnlineCustomMatchCtor.cpp`); its Load
Game calls `AptSaveLoad::OpenScreen(2, 16, false)` (BFME2 decomp
`AptOnlineCustomMatchCallbacks.cpp`, 0x0059ECC1). The strategic scenario and
start-region behavior follows the LAN strategic setup (RW 0x8445C9). Fresh
relay tickets and signed phase records are OpenBFME decisions, not retail
GameSpy behavior.

## Verified on a game-data build

- `openbfme` and `openbfme_tests` built; Godot editor script import passed.
- Filtered WotR, LAN, network, online relay, online screen, and save doctests
  passed: respectively 101/3791, 26/1379, 56/12092, 42/3009, 43/3454,
  and 128/90916 cases/assertions. Skirmish pin filters (`perf1`, `perf2`,
  `uber1`, `ai1`-`ai5`, `save`) passed 155 cases/177853 assertions; two
  Windows-specific checks were skipped.
- Local lobby service passed 68 tests, including phase ticket identity, slot
  masks, rejoin ticket generation, signed log upload, and linked phase replay
  records.
- `tools/net/online_war_test.sh`: turns (four matching turns), one fought
  battle and map return (four matching turns), battle early exit, and
  cross-session save/resume (one copy per peer, matching MD5/hash and continued
  turns) passed. Crash and 20/120-second stall modes passed as *hold* checks:
  the survivor did not surrender or advance past the absent peer. These modes
  do not prove that a restarted strategic peer can rejoin.
- LAN war: crash, early battle exit, save/resume and missing/differing copy
  refusals, 20-second stall, isolated 120-second stall, and a one-battle
  two-turn real-time run passed. The LAN scripts were not changed.

## Failing checks and open stop

- `tools/net/lan_war_test.sh 5 rts`: second battle's first load error:
  `movement failure of CINE_Egret_flying: S-084: ZAxisBehavior other than
  NO_Z_MOTIVE_FORCE, FLOATING_Z, SEA_LEVEL, SURFACE_RELATIVE_HEIGHT,
  ABSOLUTE_HEIGHT and SCALING_WALLS is not ported`. Both peers then reported
  `GAME FAIL: start_new_game failed`. A two-turn run with one battle passed;
  the second battle is not verified.
- Combined LAN matrix, 120-second stall: `FAIL: the stalled Bob did not leave
  by itself to the LAN lobby`, followed by a split-game surrender complaint.
  The isolated 120-second rerun passed. This intermittent failure is not
  explained and must not be presented as a stable combined-matrix pass.
- S-4075 remains open. The relay holds a dropped strategic slot, but
  `GameWorld::lan_strategic_begin` refuses a rejoin generation because the
  restarted strategic logic cannot apply the relay's command log. The lobby
  stores phase replay metadata and signed logs, but no client playback driver
  reconstructs the strategic and tactical phases. The two possible designs
  are deterministic strategic command-log catch-up (preferred, reusing the
  classic relay's authenticated log) or a validated strategic state snapshot
  followed by the log tail; the latter needs a new transfer/trust contract.
  The coordinator must choose and assign the protocol owner before this
  dependent work proceeds. The online strategic spectator remains explicitly
  refused. The open-stop count is 831.

Adversarial review: two-peer equal hashes and a fought battle do not prove
late rejoin, phase replay playback, or long-session relay integrity. The
lobby service tests prove stored log authorization and phase linkage, not
client playback. Only RotWK game data was exercised; the shared online
changes affect both editions, but BFME2 runtime behavior and exact retail
online battle hand-off were not verified. This milestone is not complete.
