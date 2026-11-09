// OpenBFME. GPL-3.0.
//
// PORT-38 (issue #123): the AIData team build tempo. The expectations below come from the RotWK 2.01 image (game.dat, caveat S-001) and from the
// data the mod ships, never from this port's code:
//   * the rows and their retail values: the AIData table that starts at RW 0xC1CB20 — TeamSeconds (+ 0x8, row RW 0xC1CB30, parseReal RW 0x42ED00),
//     Wealthy (+ 0xC, RW 0xC1CB40, parseInt RW 0x42EC5E), Poor (+ 0x10, RW 0xC1CB50), TeamsWealthyRate (+ 0x1C, RW 0xC1CB80), TeamsPoorRate
//     (+ 0x24, RW 0xC1CBA0), TeamResourcesToStart (+ 0x28, RW 0xC1CBB0). default/aidata.ini (the mod keeps this file; field audit 2026-11-08: one
//     definition each, data/ini/default/aidata.ini:13..21): 0.8 / 4000 / 1200 / 2.5 / 1.1 / 0.1. The AIData constructor RW 0x6FFBEF writes 0 to all
//     six, so a data set without them divides by a zero rate exactly as the image does.
//   * the AI player's delay: RW 0x8F7FF4 .. 0x8F8001 stores `cvttss2si(AIData TeamSeconds)` (truncation), RW 0x8F8219 .. 0x8F825E multiplies it by
//     LOGICFRAMES_PER_SECOND (RW 0xD9F608 = 5) and divides it by TeamsPoorRate when the player's money is below Poor (RW 0x8F823C / 0x8F8245) or by
//     TeamsWealthyRate when it is above Wealthy (RW 0x8F824C / 0x8F8255), the result truncated by `cvttss2si` (RW 0x8F825A).
//   * the team start's money gate: RW 0x8F55C4 .. 0x8F55E3 `_ftol2(count x TeamResourcesToStart)` (RW 0xA3CFA4: truncation) against the player's
//     money, unsigned; below it the team is not started.
// The neighbouring behaviour these tests hold: StructuresPoorRate / StructuresWealthyRate / StructureSeconds (the same table, + 0x20 / + 0x18 /
// + 0x4) are NOT read by the image (triage batch-12 rows 24-25: no read anywhere), so they must not move either rule.

#include "doctest.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/SkirmishAI/AITeamTempo.h"

namespace L = GameLogicSettingsLoader;

namespace
{
// default/aidata.ini: the values Ennorath's data reaches (the mod's own data/ini/aidata.ini does not override the AIData block, field audit 2026-11-08)
const char kRetailAIData[] =
	"AIData\n"
	"  TeamSeconds = 0.8\n"
	"  Wealthy = 4000\n"
	"  Poor = 1200\n"
	"  TeamsWealthyRate = 2.5\n"
	"  TeamsPoorRate = 1.1\n"
	"  TeamResourcesToStart = 0.1\n"
	"End\n";

bool parseAIData(const std::string &text, GameLogicSettings &out, std::string &error)
{
	return L::scanAIData(text, out, &error);
}

// the six values this lane reads, set directly: the rules are pure functions of them
GameLogicSettings tempo(float teamSeconds, int poor, int wealthy, float poorRate, float wealthyRate, float resources)
{
	GameLogicSettings s;
	s.teamSeconds = teamSeconds;
	s.poorThreshold = poor;
	s.wealthyThreshold = wealthy;
	s.teamsPoorRate = poorRate;
	s.teamsWealthyRate = wealthyRate;
	s.teamResourcesToStart = resources;
	return s;
}
} // namespace

TEST_CASE("PORT-38 AIData team tempo: the six rows are read, and an AIData block without them keeps the constructor's zeros")
{
	GameLogicSettings r;
	std::string err;
	REQUIRE_MESSAGE(parseAIData(kRetailAIData, r, err), err);
	CHECK(r.teamSeconds == 0.8f);                              // row RW 0xC1CB30, parseReal RW 0x42ED00 (the nearest float32, exact)
	CHECK(r.wealthyThreshold == 4000);                         // row RW 0xC1CB40, parseInt RW 0x42EC5E
	CHECK(r.poorThreshold == 1200);                            // row RW 0xC1CB50
	CHECK(r.teamsWealthyRate == 2.5f);                         // row RW 0xC1CB80
	CHECK(r.teamsPoorRate == 1.1f);                            // row RW 0xC1CBA0
	CHECK(r.teamResourcesToStart == 0.1f);                     // row RW 0xC1CBB0
	// they are read now, so the catch-all row (stop S-152) no longer records them
	CHECK(r.unappliedFields.count("AIData.TeamSeconds") == 0);
	CHECK(r.unappliedFields.count("AIData.Poor") == 0);
	CHECK(r.unappliedFields.count("AIData.Wealthy") == 0);
	CHECK(r.unappliedFields.count("AIData.TeamsPoorRate") == 0);
	CHECK(r.unappliedFields.count("AIData.TeamsWealthyRate") == 0);
	CHECK(r.unappliedFields.count("AIData.TeamResourcesToStart") == 0);

	// the field names are the table's, case included: another spelling is not this row and stays recorded
	GameLogicSettings lower;
	REQUIRE_MESSAGE(parseAIData("AIData\n  teamseconds = 9.0\nEnd\n", lower, err), err);
	CHECK(lower.teamSeconds == 0.0f);
	CHECK(lower.unappliedFields.count("AIData.teamseconds") == 1);

	// an AIData block that names none of the six: the constructor's zeros (RW 0x6FFBEF), never a silent retail default
	GameLogicSettings empty;
	REQUIRE_MESSAGE(parseAIData("AIData\n  EnableRepulsors = No\nEnd\n", empty, err), err);
	CHECK(empty.teamSeconds == 0.0f);
	CHECK(empty.poorThreshold == 0);
	CHECK(empty.wealthyThreshold == 0);
	CHECK(empty.teamsPoorRate == 0.0f);
	CHECK(empty.teamsWealthyRate == 0.0f);
	CHECK(empty.teamResourcesToStart == 0.0f);

	// a later block overrides field by field, as the loader does for every AIData row
	GameLogicSettings twice;
	REQUIRE_MESSAGE(parseAIData(kRetailAIData, twice, err), err);
	REQUIRE_MESSAGE(parseAIData("AIData\n  Poor = 900\nEnd\n", twice, err), err);
	CHECK(twice.poorThreshold == 900);
	CHECK(twice.wealthyThreshold == 4000);
}

TEST_CASE("PORT-38 AIData team tempo: TeamSeconds is truncated to whole seconds before it becomes frames")
{
	// RW 0x8F7FFC: cvttss2si, i.e. toward zero, so retail's 0.8 is 0 seconds and every money band yields a zero frame delay
	GameLogicSettings retail;
	std::string err;
	REQUIRE_MESSAGE(parseAIData(kRetailAIData, retail, err), err);
	CHECK(AITeamTempo::baseSeconds(retail) == 0);
	CHECK(AITeamTempo::delayFrames(retail, 0) == 0);        // below Poor
	CHECK(AITeamTempo::delayFrames(retail, 1199) == 0);
	CHECK(AITeamTempo::delayFrames(retail, 1200) == 0);     // Poor reached: the plain band
	CHECK(AITeamTempo::delayFrames(retail, 4000) == 0);
	CHECK(AITeamTempo::delayFrames(retail, 4001) == 0);     // above Wealthy
	CHECK(AITeamTempo::delayFrames(retail, 1000000) == 0);

	// 2.9 seconds is 2 whole seconds (10 frames), not 3 (15): a round would answer 15, the image truncates
	CHECK(AITeamTempo::baseSeconds(tempo(2.9f, 1000, 3000, 1.0f, 1.0f, 0.1f)) == 2);
	CHECK(AITeamTempo::delayFrames(tempo(2.9f, 1000, 3000, 1.0f, 1.0f, 0.1f), 2000) == 10);
	// a negative value truncates toward zero too (cvttss2si)
	CHECK(AITeamTempo::baseSeconds(tempo(-2.9f, 0, 0, 1.0f, 1.0f, 0.1f)) == -2);
}

TEST_CASE("PORT-38 AIData team tempo: the AI player's money picks the poor, the plain or the wealthy delay")
{
	// 20 seconds -> 100 frames; TeamsPoorRate 2.0 -> 50 frames below Poor; TeamsWealthyRate 4.0 -> 25 frames above Wealthy (RW 0x8F8219 .. 0x8F825E)
	const GameLogicSettings s = tempo(20.0f, 1000, 3000, 2.0f, 4.0f, 0.1f);
	CHECK(AITeamTempo::delayFrames(s, 0) == 50);   // poor: 100 / 2
	CHECK(AITeamTempo::delayFrames(s, 999) == 50);
	// RW 0x8F823F is `jae`: money == Poor leaves the poor band, so the plain frames stand
	CHECK(AITeamTempo::delayFrames(s, 1000) == 100);
	CHECK(AITeamTempo::delayFrames(s, 2000) == 100);
	// RW 0x8F824F is `jbe`: money == Wealthy is still the plain band
	CHECK(AITeamTempo::delayFrames(s, 3000) == 100);
	CHECK(AITeamTempo::delayFrames(s, 3001) == 25); // wealthy: 100 / 4
	CHECK(AITeamTempo::delayFrames(s, 100000) == 25);

	// the division truncates (RW 0x8F825A cvttss2si): 3 seconds -> 15 frames, halved is 7.5 -> 7, quartered 3.75 -> 3
	const GameLogicSettings half = tempo(3.0f, 1000, 3000, 2.0f, 4.0f, 0.1f);
	CHECK(AITeamTempo::delayFrames(half, 10) == 7);
	CHECK(AITeamTempo::delayFrames(half, 99999) == 3);

	// a rate the data left at the constructor's 0 divides by zero as the image does (`divss` by 0 -> inf, `cvttss2si` -> the integer indefinite
	// 0x80000000): no silent unit rate and no silent zero
	const GameLogicSettings unset = tempo(20.0f, 1000, 3000, 0.0f, 0.0f, 0.1f);
	CHECK(AITeamTempo::delayFrames(unset, 10) == (int)0x80000000u);
	CHECK(AITeamTempo::delayFrames(unset, 99999) == (int)0x80000000u);
	// 0 seconds over a zero rate is 0/0 = NaN, which is the same integer indefinite
	const GameLogicSettings unsetZero = tempo(0.0f, 1000, 3000, 0.0f, 0.0f, 0.1f);
	CHECK(AITeamTempo::delayFrames(unsetZero, 10) == (int)0x80000000u);
}

TEST_CASE("PORT-38 AIData team tempo: a team is not started when the money is below count x TeamResourcesToStart")
{
	// RW 0x8F55C4 .. 0x8F55E3: cost = _ftol2(count x TeamResourcesToStart); the team starts when the money reaches it
	GameLogicSettings retail;
	std::string err;
	REQUIRE_MESSAGE(parseAIData(kRetailAIData, retail, err), err); // 0.1 per unit
	CHECK(AITeamTempo::startCost(retail, 100) == 10);              // 10.000001 truncated
	CHECK_FALSE(AITeamTempo::canStartTeam(retail, 100, 9));
	CHECK(AITeamTempo::canStartTeam(retail, 100, 10));
	CHECK(AITeamTempo::canStartTeam(retail, 100, 11));
	// the truncation is _ftol2's (RW 0xA3CFA4), not a round: 15 units cost 1, not 2 (1.5 -> 1)
	CHECK(AITeamTempo::startCost(retail, 15) == 1);
	CHECK_FALSE(AITeamTempo::canStartTeam(retail, 15, 0));
	CHECK(AITeamTempo::canStartTeam(retail, 15, 1));
	// a whole-unit rate: 7 units of a 1.0 rate cost 7
	const GameLogicSettings whole = tempo(20.0f, 1000, 3000, 2.0f, 4.0f, 1.0f);
	CHECK(AITeamTempo::startCost(whole, 7) == 7);
	CHECK_FALSE(AITeamTempo::canStartTeam(whole, 7, 6));
	CHECK(AITeamTempo::canStartTeam(whole, 7, 7));
	// an empty team costs nothing and starts
	CHECK(AITeamTempo::startCost(retail, 0) == 0);
	CHECK(AITeamTempo::canStartTeam(retail, 0, 0));
	// the rate the data left at the constructor's 0: every team costs 0 (RW: fmul by 0.0), not a silent 1.0
	const GameLogicSettings unset = tempo(20.0f, 1000, 3000, 2.0f, 4.0f, 0.0f);
	CHECK(AITeamTempo::startCost(unset, 500) == 0);
	CHECK(AITeamTempo::canStartTeam(unset, 500, 0));
}

TEST_CASE("PORT-38 AIData team tempo: the building rows of the same table move neither rule")
{
	// triage batch-12 rows 24 / 25: RotWK reads StructureSeconds (+ 0x4), StructuresPoorRate (+ 0x20) and StructuresWealthyRate (+ 0x18) nowhere
	// (the constructor RW 0x6FFBEF, the copy RW 0x6FEF29 and xfer RW 0x6FF50D only), so a data set that changes them must change neither the team
	// delay nor the team cost
	GameLogicSettings s;
	std::string err;
	REQUIRE_MESSAGE(parseAIData("AIData\n"
		"  TeamSeconds = 20.0\n"
		"  Wealthy = 3000\n"
		"  Poor = 1000\n"
		"  TeamsWealthyRate = 4.0\n"
		"  TeamsPoorRate = 2.0\n"
		"  TeamResourcesToStart = 0.1\n"
		"End\n", s, err), err);
	const int poorDelay = AITeamTempo::delayFrames(s, 10);
	const int wealthyDelay = AITeamTempo::delayFrames(s, 99999);
	const int cost = AITeamTempo::startCost(s, 100);
	CHECK(poorDelay == 50);
	CHECK(wealthyDelay == 25);
	CHECK(cost == 10);

	GameLogicSettings withStructures;
	REQUIRE_MESSAGE(parseAIData("AIData\n"
		"  StructureSeconds = 30.0\n"
		"  StructuresPoorRate = 7.0\n"
		"  StructuresWealthyRate = 0.25\n"
		"  TeamSeconds = 20.0\n"
		"  Wealthy = 3000\n"
		"  Poor = 1000\n"
		"  TeamsWealthyRate = 4.0\n"
		"  TeamsPoorRate = 2.0\n"
		"  TeamResourcesToStart = 0.1\n"
		"End\n", withStructures, err), err);
	CHECK(AITeamTempo::delayFrames(withStructures, 10) == poorDelay);
	CHECK(AITeamTempo::delayFrames(withStructures, 99999) == wealthyDelay);
	CHECK(AITeamTempo::startCost(withStructures, 100) == cost);
	// the three building rows stay with the catch-all (stop S-152): the port did not claim them
	CHECK(withStructures.unappliedFields.count("AIData.StructureSeconds") == 1);
	CHECK(withStructures.unappliedFields.count("AIData.StructuresPoorRate") == 1);
	CHECK(withStructures.unappliedFields.count("AIData.StructuresWealthyRate") == 1);
}
