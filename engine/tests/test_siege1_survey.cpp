// OpenBFME unit tests, lane SIEGE-1: the survey of the retail siege units against Helm's Deep's gate and walls. GPL-3.0.
//
// A diagnostic, not a pin: every retail siege unit is made outside Helm's Deep's fortress (the computer player owns the fortress, the human is Mordor at
// start 1), ordered to attack the main gate and then a wall section, and the frames until its first damage, the damage after 120 s and where it stopped are
// printed. Run with `--no-skip -tc="siege1 survey*"`; it is skipped by default (about 10 s per unit and target). The pins are test_siege1_attack.cpp's.
// SKIPs loudly without the installs.

#include "Siege1TestUtil.h"

#include <cstdio>

using namespace siege1test;

TEST_CASE("siege1 survey: every retail siege unit against Helm's Deep's main gate and a wall section (diagnostic)" * doctest::skip(true))
{
	OPENBFME_REQUIRE_START(s);
	const struct
	{
		const char *name;
		float d;
	} units[] = {
		{ "IsengardBatteringRam", 250.0f }, { "MordorBatteringRam", 250.0f }, { "DwarvenDemolisher", 250.0f }, { "MordorGrond", 300.0f },
		{ "GondorTrebuchet", 500.0f },      { "ArnorTrebuchet", 500.0f },     { "MordorCatapult", 500.0f },    { "IsengardBallista", 400.0f },
		{ "DwarvenCatapult", 500.0f },      { "AngmarTrollSling", 500.0f },   { "MordorMumakil", 300.0f },     { "RohanEntBase", 250.0f },
		{ "WildMountainGiant", 250.0f },    { "MordorAttackTroll", 250.0f },  { "IsengardSiegeLadder", 250.0f }, { "MordorSiegeTower", 300.0f },
	};
	for (const auto &u : units)
	{
		for (int wall = 0; wall < 2; ++wall)
		{
			const AttackResult r = siegeAttack(*s, "maps/map wor helms deep/map wor helms deep.map", "Main Gate", u.name, wall != 0, u.d, 600);
			MESSAGE(u.name << " vs " << r.target << (wall ? " (wall)" : " (gate)") << ": ordered " << r.ordered << ", first damage at frame " << r.firstHit << " from "
						  << r.distAtHit << " with slot " << r.slotAtHit << ", health " << r.healthBefore << " -> " << r.healthAfterFirst << " -> " << r.healthAfter
						  << (r.dead ? " (dead at frame " + std::to_string(r.deathFrame) + ")" : std::string()) << ", victim then " << r.victimAfter);
		}
	}
}
