// OpenBFME. GPL-3.0.
//
// Lane PORT-38 (issue #123): the AI player's team build tempo — the three arithmetic rules AIData's TeamSeconds, Poor, Wealthy, TeamsPoorRate,
// TeamsWealthyRate and TeamResourcesToStart feed. Targets: RotWK 2.01 game.dat (caveat S-001), AIPlayer.cpp.
//
// WHAT ROTWK DOES (disassembled; the instruction addresses are in the .cpp):
//   * creation (AIPlayer ctor RW 0x8F7FF4 .. 0x8F8001): the AI player stores `(int) AIData TeamSeconds` (+ 0x8, row RW 0xC1CB30, parseReal
//     RW 0x42ED00) in its + 0x20 with `cvttss2si`, i.e. truncated toward zero (retail aidata.ini 0.8 -> 0), and 10 in + 0x20 before that (RW 0x8F7F73).
//   * the delay after a team (AIPlayer::selectTeamToBuild RW 0x8F805D, slot 0x58 of the vtable RW 0xC79788, thunk RW 0x8F3EB2; RW 0x8F8219 .. 0x8F825E):
//     frames = (base seconds) x LOGICFRAMES_PER_SECOND (RW 0xD9F608 = 5, `imul` at RW 0x8F8223); the player's money (`[Player + 0x94]`, RW 0x8F8236)
//     below AIData Poor (+ 0x10, row RW 0xC1CB50, parseInt RW 0x42EC5E; unsigned `cmp` / `jae` RW 0x8F823C) divides it by TeamsPoorRate (+ 0x24, row
//     RW 0xC1CBA0), above Wealthy (+ 0xC, row RW 0xC1CB40; RW 0x8F824C / 0x8F824F) by TeamsWealthyRate (+ 0x1C, row RW 0xC1CB80), both with `divss`
//     and a final `cvttss2si` into + 0x18 (RW 0x8F8245 / 0x8F8255 / 0x8F825A). Between the two thresholds the plain frames stand.
//   * the team start's money gate (RW 0x8F54BA, called from RW 0x8F6DE9 = AIPlayer slot 0x24 and RW 0x8F417A / 0x8F5EEB = slot 0x64; RW 0x8F55BF ..
//     0x8F55E3): `fild` the team's unit count, `fmul [AIData + 0x28]` (TeamResourcesToStart, row RW 0xC1CBB0), `_ftol2` (RW 0xA3CFA4: FISTP qword,
//     truncation toward zero), then the player's money compared with it unsigned: below it the out flag is set and the team is not started.
//
// NOT PORTED, and the reason this lane's rules have no caller yet: they are consumed by the AI team build, which builds Team instances from the
// player's TeamPrototypes (slot 0x68 RW 0x8F994F -> slot 0x58 RW 0x8F805D -> slot 0x24 RW 0x8F6DE9 -> RW 0x8F9487) and needs TeamPrototype's
// home position (+ 0x1DC, the "has home position" byte + 0x1E8, RW 0x8F94E7 / RW 0x8F81B6), its priority (+ 0x21C, RW 0x8F80AB), its unit list
// (+ 0x134 / + 0x1D8, RW 0x8F54E4) and Team::update's production — the port's Team is a script/production subset without recruitment or AI state
// (Common/Team.h). The timer that spends this delay (AIPlayer slot 0x48 RW 0x8F3EFE: + 0x14 the ready flag, + 0x18 the countdown clamped to 3 x
// LOGICFRAMES_PER_SECOND = 15 at RW 0x8F3F2A, + 0x28 the 2 x LOGICFRAMES_PER_SECOND = 10 frame cadence at RW 0x8F3F51, then slot 0x68 when ready)
// is not ported either. Stop S-7186 in docs/STOPS.md names the whole chain; the commit that ports the AI team build calls these three rules and
// then hashes the six AIData values in GameLogicSettings::crc.
//
// The arithmetic is the port's own spelling of the retail instructions, not a re-design: every conversion goes through SimMath (the registered
// float/int facade), so the truncations, the unsigned comparisons and the division by a zero rate (retail's +inf / NaN -> the integer indefinite
// 0x80000000) are the image's, not a compiler's.

#pragma once

struct GameLogicSettings;

namespace AITeamTempo
{
constexpr int kLogicFramesPerSecond = 5; // RW 0xD9F608 (the dword the `imul` at RW 0x8F8223 reads)

// RW 0x8F7FF4 .. 0x8F8001: what the AI player keeps as its base team seconds (+ 0x20): `cvttss2si [AIData TeamSeconds]`, truncated toward zero, so
// retail's 0.8 is 0 seconds. `--` (a settings object without an AIData block) keeps the constructor's 0.
int baseSeconds(const GameLogicSettings &settings);

// RW 0x8F8219 .. 0x8F825E: the frames the AI player waits before it builds its next team, by its money. See the header comment for the branches.
int delayFrames(const GameLogicSettings &settings, int money);

// RW 0x8F55C4 .. 0x8F55CD: the money a team of `unitCount` units needs before it starts: `_ftol2(unitCount x TeamResourcesToStart)`, truncated.
// `unitCount` is RW 0x8F54BA's own accumulator (RW 0x8F5521 .. 0x8F55A3): for each unit entry of the team prototype's list ([arg1 + 0x1D8] entries of
// 0x18 bytes at [arg1 + 0x134], name at entry + 0xC, resolved with RW 0x6D1305) it adds `(mid of the entry's min/max counts x 0.5 (RW 0xBD869C))
// x the player's count of that template` (RW 0x73C25F / player + 0x758 RW 0x780AD3; 0 when the entry's template has KindOf bit 0x20 at [template +
// 0x11B]), each step truncated back to an int (RW 0x8F559F). So the gate is the money per unit the player has of the team's templates. The caller
// supplies that count, because the port has no TeamTemplateInfo (stop S-7186).
int startCost(const GameLogicSettings &settings, int unitCount);

// RW 0x8F55D2 .. 0x8F55E3: whether the player's money (`[Player + 0x94]`, compared unsigned) reaches that cost. False = RW's "not enough" flag: the
// team is not started.
bool canStartTeam(const GameLogicSettings &settings, int unitCount, int money);
} // namespace AITeamTempo
