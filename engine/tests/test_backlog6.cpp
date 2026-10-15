// OpenBFME unit tests. GPL-3.0.
// Lane BACKLOG6: the functions behind S-413 (the fortress rebuild RW 0x9BC882), S-412 (AIBuilder::update RW 0x8F0EB4), S-415 (the unit priority RW 0x9EDA17) and
// S-895 (AIGroup RW 0x774897), ported with the Open-BFME-2 byte-matched source as the donor. This file pins the RotWK facts the ports rest on (they SKIP when
// RW_GAME_DAT is unset) and tests the group command's gate and stealth delay on retail units (SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset). The fortress
// rebuild and the processor are tested in test_skirmish_ai_base.cpp (a real AI skirmish).

#include "doctest.h"
#include "HudTestUtil.h"
#include "StealthArena.h"

#include "PeImage.h"
#include "RetailTestMount.h"

#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/KindOfTokens.h"
#include "GameLogic/AI/AIGroup.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Object.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{
using hudtest::shared;
using stealthtest::Arena;

// the target of the rel32 call at `va` (0 when it is no call)
std::uint32_t callTarget(const retailtest::PeImage &pe, std::uint32_t va)
{
	std::vector<std::uint8_t> d;
	if (!pe.read(va, 5, &d) || d[0] != 0xE8)
	{
		return 0;
	}
	return va + 5u + pe.u32At(va + 1);
}

// the draws of AIGroup.cpp 0x874 in the call log
std::vector<GameLogicRandom::Call> stealthDraws(GameLogic &logic)
{
	std::vector<GameLogicRandom::Call> out;
	for (const GameLogicRandom::Call &c : logic.random().callLog())
	{
		if (c.line == 0x874 && c.file == "AIGroup.cpp")
		{
			out.push_back(c);
		}
	}
	return out;
}

bool reported(const AIWorld &ai, const char *prefix)
{
	for (const std::string &s : ai.stops())
	{
		if (s.compare(0, std::strlen(prefix), prefix) == 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("backlog6 binary facts: the fortress rebuild RW 0x9BC882, AIBuilder::update RW 0x8F0EB4, the unit priority RW 0x9EDA17 and AIGroup RW 0x774897")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("backlog6 binary facts (RW_GAME_DAT unset)");
		return;
	}
	// S-413, RW 0x9BC882: called by the base's update for a base without a task; the slot's player template + 0x44 (the StartingBuilding row), canBuild, the
	// phase >= 1, the free dozer count, the 350^2 radius, the ArmyDefinition + 0x20 priority and the reserve flag + 0x21
	CHECK(callTarget(*pe, 0x9BCA73) == 0x9BC882u);
	CHECK(callTarget(*pe, 0x9BC895) == 0x90CE35u);
	CHECK(pe->hex(0x9BC8AF, 3) == "83c044");          // add eax, 0x44
	CHECK(pe->cstring(pe->u32At(0xBF81F8)) == "StartingBuilding");
	CHECK(pe->u32At(0xBF81F8 + 0xC) == 0x44u);
	CHECK(callTarget(*pe, 0x9BC8BF) == 0x6AC927u);    // Player::canBuild
	CHECK(pe->hex(0x9BC8E3, 3) == "83f801");          // cmp eax, 1 (the phase)
	CHECK(callTarget(*pe, 0x9BC8F5) == 0x9BC7B6u);    // the list count of the dozer manager's free dozers (+ 0x140)
	CHECK(pe->u32At(0xC8A4B4) == 0x47EF4200u);        // 122500.0f
	CHECK(pe->hex(0x9BC9E9, 3) == "8b4020");          // mov eax, [eax + 0x20] (FortressRebuildPriority)
	CHECK(pe->hex(0x9BCA09, 4) == "c6402101");        // mov byte [eax + 0x21], 1
	// S-412, RW 0x8F0EB4: RotWK's one change from the BFME2 source is the code 9 failure beside code 10; the dozer manager and the money saver end it
	CHECK(pe->hex(0x8F1043, 3) == "83f80a");          // cmp eax, 0xA
	CHECK(pe->hex(0x8F1048, 3) == "83f809");          // cmp eax, 9
	CHECK(callTarget(*pe, 0x8F1138) == 0x9A23A4u);
	CHECK(callTarget(*pe, 0x8F113F) == 0x8F0502u);
	// the money saver RW 0x8F0502 (not ported, S-413): the limit max(unit builder + 0x48, difficulty * 5000), PercentToSave (+ 0xB8) by the phase, the deposit
	// RW 0x99E773, the next run 2 * LOGICFRAMES_PER_SECOND frames later
	CHECK(pe->hex(0x8F0563, 6) == "69c088130000");    // imul eax, eax, 0x1388
	CHECK(pe->hex(0x8F05AD, 7) == "d8bc88b8000000");  // fdivr dword [eax + ecx * 4 + 0xB8]
	CHECK(callTarget(*pe, 0x8F05E6) == 0x99E773u);
	CHECK(pe->hex(0x8F05F4, 3) == "8d0443");          // lea eax, [ebx + eax * 2]
	// S-415, RW 0x9EDA17: the BFME2 source's + 0xB8 row is + 0xC4 in RotWK (LowUnitPriorityModifier); 2 economy structures
	CHECK(pe->hex(0x9EDA35, 3) == "83f802");          // cmp eax, 2
	CHECK(pe->hex(0x9EDABB, 9) == "f30f598c81c4000000"); // mulss xmm1, [ecx + eax * 4 + 0xC4]
	// S-895, RW 0x774897: the gate (AIData + 0xBD = UseFormations, 1 from the constructor) to the manager RW 0x7747DE, whose first test is the locomotor
	// template's + 0x78; the stealth delay draw at line 0x874 and the mood check write; the tactics' command sources
	CHECK(pe->hex(0x774973, 6) == "3888bd000000");    // cmp [eax + 0xBD], cl
	CHECK(pe->cstring(pe->u32At(0xC1CE90)) == "UseFormations");
	CHECK(pe->u32At(0xC1CE90 + 0xC) == 0xBDu);
	CHECK(pe->hex(0x6FFDE5, 7) == "c686bd00000001");  // mov byte [esi + 0xBD], 1
	CHECK(callTarget(*pe, 0x774986) == 0x7747DEu);
	CHECK(pe->hex(0x77480D, 4) == "83787800");        // cmp dword [eax + 0x78], 0
	CHECK(callTarget(*pe, 0x774844) == 0x94FCBAu);
	CHECK(callTarget(*pe, 0x774859) == 0x77348Cu);
	CHECK(pe->hex(0x774C6D, 4) == "837d0c00");        // cmp [ebp + 0xC], 0 (the command source)
	CHECK(callTarget(*pe, 0x774C96) == 0x68FC2Fu);
	CHECK(pe->hex(0x774CBF, 4) == "d1e8a801");        // shr eax, 1; test al, 1 (AutoAcquireEnemiesWhenIdle STEALTHED)
	CHECK(callTarget(*pe, 0x774CC7) == 0x68FBD3u);
	CHECK(pe->hex(0x774CD6, 5) == "6874080000");      // push 0x874
	CHECK(callTarget(*pe, 0x774CEB) == 0x6D328Eu);
	CHECK(callTarget(*pe, 0x774D04) == 0x662E01u);
	// the manager's refusals: the member filter RW 0x76FFF0 (held, no AI, no current locomotor, IMMOBILE = template + 0x108 bit 2), the FormationPriority test,
	// the group ground path RW 0x76F115 (MinDistanceForGroup AIData + 0x6C (row RW 0xC1CD20), default 100.0f RW 0xBD88D8, x 4.0f RW 0xBD88C0; kinds 0x1000 / 0x100 / 0x400 / 0xA00)
	CHECK(callTarget(*pe, 0x7747E6) == 0x76FFF0u);
	CHECK(pe->hex(0x76FFFC, 7) == "f687c801000008");
	CHECK(pe->hex(0x770011, 7) == "83bef001000000");
	CHECK(pe->hex(0x77001D, 7) == "f6860801000004");
	CHECK(pe->hex(0x770040, 3) == "83f901");          // the last member: count == 1 refuses the whole order
	CHECK(callTarget(*pe, 0x774822) == 0x76F426u);
	CHECK(callTarget(*pe, 0x77482F) == 0x76F115u);
	CHECK(pe->cstring(pe->u32At(0xC1CD20)) == "MinDistanceForGroup");
	CHECK(pe->u32At(0xC1CD20 + 0xC) == 0x6Cu);
	CHECK(pe->hex(0x6FFD56, 5) == "f30f11466c");
	CHECK(pe->u32At(0xBD88D8) == 0x42C80000u);        // 100.0f
	CHECK(pe->u32At(0xBD88C0) == 0x40800000u);        // 4.0f
	CHECK(pe->hex(0x76F1D4, 7) == "f680c801000008");
	CHECK(pe->hex(0x76F1E1, 7) == "83b86002000000");
	CHECK(pe->hex(0x76F1F7, 4) == "f6c5100f");        // KindOf 0x1000 (AIRCRAFT) skipped
	CHECK(pe->hex(0x76F200, 4) == "f6c50174");        // 0x100 INFANTRY
	CHECK(pe->hex(0x76F20A, 4) == "f6c50474");        // 0x400 MONSTER
	CHECK(pe->hex(0x76F214, 4) == "f6c50a74");        // 0xA00 CAVALRY | MACHINE
	CHECK(callTarget(*pe, 0x8F18C3) == 0x774897u);
	CHECK(pe->hex(0x8F18A3, 4) == "834df4ff");        // or [ebp - 0xC], -1 (no formation type)
	CHECK(pe->hex(0x8F18AA, 1) == "53");              // push ebx (0: the player's source)
	CHECK(callTarget(*pe, 0x9B4D7C) == 0x774897u);
	CHECK(pe->hex(0x9B4D64, 2) == "6a02");            // push 2 (the AI's source)
}

TEST_CASE("backlog6 group: a player's order to one stealth unit draws its stealth delay (AIGroup.cpp 0x874); the AI's order, a stealthed, detected or held unit and a formation manager group do not")
{
	if (!hudtest::haveWorld("backlog6 group"))
	{
		return;
	}
	// the KindOf bits RW 0x76FFF0 / 0x76F115 test as masks (template + 0x108 / + 0x109), by this engine's names
	CHECK(KindOfTokens::indexOf("IMMOBILE") == 2);
	CHECK(KindOfTokens::indexOf("INFANTRY") == 8);
	CHECK(KindOfTokens::indexOf("CAVALRY") == 9);
	CHECK(KindOfTokens::indexOf("MONSTER") == 10);
	CHECK(KindOfTokens::indexOf("MACHINE") == 11);
	CHECK(KindOfTokens::indexOf("AIRCRAFT") == 12);
	const unsigned canStealth = (unsigned)CombatNames::status("CAN_STEALTH");
	const unsigned stealthed = (unsigned)CombatNames::status("STEALTHED");
	const unsigned detected = (unsigned)CombatNames::status("DETECTED");
	const Coord3D goal{ 700.0f, 400.0f, 0.0f };
	// CINE_RohanElvenWarrior: a StealthUpdate and AutoAcquireEnemiesWhenIdle = Yes (it does not acquire while stealthed)
	{
		Arena a(shared());
		Object *u = a.place("CINE_RohanElvenWarrior", 0, 400.0f, 400.0f);
		AIUpdateInterface *ai = u->getAIUpdateInterface();
		REQUIRE(ai != nullptr);
		const StealthUpdate *st = StealthUpdate::of(*u);
		REQUIRE(st != nullptr);
		REQUIRE(u->testStatus(canStealth));
		REQUIRE_FALSE(u->testStatus(stealthed));
		REQUIRE_FALSE(u->testStatus(detected));
		REQUIRE(ai->autoAcquireFlags() == 1u);
		a.logic.random().enableCallLog(true);
		AIGroup group(a.logic, { u->getID() });
		REQUIRE(group.formationManager(goal, false, CMD_FROM_PLAYER, false) == AIGroup::FORMATION_NOT_OFFERED); // one unit
		group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		std::vector<GameLogicRandom::Call> draws = stealthDraws(a.logic);
		REQUIRE(draws.size() == 1);
		CHECK(draws[0].lo == 0);
		CHECK(draws[0].hi == 5);
		CHECK_FALSE(draws[0].real);
		CHECK(ai->nextMoodCheckTime() == a.logic.getFrame() + (unsigned)draws[0].result + st->data()->m_stealthDelay);
		// controls on the same unit: each takes one condition away
		const unsigned kept = ai->nextMoodCheckTime();
		a.logic.random().clearCallLog();
		group.groupMoveToPosition(goal, false, CMD_FROM_AI);
		CHECK(stealthDraws(a.logic).empty());
		u->setStatus(stealthed, true);
		group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		CHECK(stealthDraws(a.logic).empty());
		u->setStatus(stealthed, false);
		u->setStatus(detected, true);
		group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		CHECK(stealthDraws(a.logic).empty());
		u->setStatus(detected, false);
		u->setStatus(canStealth, false);
		group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		CHECK(stealthDraws(a.logic).empty());
		u->setStatus(canStealth, true);
		CHECK(ai->nextMoodCheckTime() == kept);
		// a held unit takes no part in the group's move: no draw and it stays where it is, while the same order moves it once released
		u->setDisabled(DISABLED_HELD, a.logic.getFrame() + 1000u);
		REQUIRE(u->isDisabledByType(DISABLED_HELD));
		AIGroup(a.logic, { u->getID() }).groupIdle(CMD_FROM_PLAYER);
		a.run(3);
		const Coord3D before = *u->getPosition();
		a.logic.random().clearCallLog();
		group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		CHECK(stealthDraws(a.logic).empty());
		a.run(15);
		CHECK(u->getPosition()->x == before.x);
		CHECK(u->getPosition()->y == before.y);
	}
	// the same unit, not held: the order moves it (the control of the held case)
	{
		Arena a(shared());
		Object *u = a.place("CINE_RohanElvenWarrior", 0, 400.0f, 400.0f);
		a.run(3);
		const Coord3D before = *u->getPosition();
		AIGroup(a.logic, { u->getID() }).groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		a.run(15);
		CHECK(u->getPosition()->x > before.x); // tolerance: the control only shows that the unit left its place; how far it walks in 15 frames is not this test's subject
	}
	// NoldorWarriorHorde: AutoAcquireEnemiesWhenIdle = Yes ATTACK_BUILDINGS STEALTHED (it acquires while stealthed): no delay, no draw
	{
		Arena a(shared());
		Object *u = a.place("NoldorWarriorHorde", 0, 400.0f, 400.0f);
		REQUIRE(u->getAIUpdateInterface() != nullptr);
		REQUIRE(StealthUpdate::of(*u) != nullptr);
		REQUIRE(u->testStatus(canStealth));
		REQUIRE_FALSE(u->testStatus(stealthed));
		REQUIRE((u->getAIUpdateInterface()->autoAcquireFlags() & 2u) != 0u);
		a.logic.random().enableCallLog(true);
		AIGroup(a.logic, { u->getID() }).groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		CHECK(stealthDraws(a.logic).empty());
	}
	// two units, one with a locomotor FormationPriority, a player's plain move 300 away (the group ground path RW 0x76F115 accepts: nearest member 270 from the
	// goal, extent 30): RW hands it to the formation group manager (the formation is not ported: no draw, S-895 reported). Controls, each one condition away:
	// UseFormations = No, a waypoint, the AI's source, a formation type (the gate is shut: the generic path draws for the stealth unit); a goal 100 away
	// (RW 0x76F115 refuses a group nearer than MinDistanceForGroup = 200: the manager returns 0, the generic path draws); a held member (RW 0x76FFF0 takes it
	// out of the group: the elf alone has no FormationPriority locomotor, the manager refuses, the elf draws and the held horde does not move)
	for (int variant = 0; variant < 8; ++variant)
	{
		INFO("variant " << variant);
		Arena a(shared());
		REQUIRE(a.logic.settings().useFormations); // retail aidata.ini: UseFormations = Yes
		REQUIRE(a.logic.settings().minDistanceForGroup == 200.0f); // retail aidata.ini: MinDistanceForGroup = 200.0
		Object *elf = a.place("CINE_RohanElvenWarrior", 0, 400.0f, 400.0f);
		Object *horde = a.place("GondorFighterHorde", 0, 430.0f, 400.0f);
		a.run(1);
		REQUIRE_FALSE(elf->testStatus(stealthed));
		const AIUpdateInterface *hordeAI = horde->getAIUpdateInterface();
		REQUIRE(hordeAI != nullptr);
		REQUIRE(hordeAI->curLocomotor() != nullptr);
		REQUIRE(hordeAI->curLocomotor()->getTemplate().m_formationPriority != FORMATION_NO_FORMATION);
		AIGroup group(a.logic, { elf->getID(), horde->getID() });
		a.logic.random().enableCallLog(true);
		const Coord3D near{ 500.0f, 400.0f, 0.0f };
		const Coord3D horizon{ 430.0f, 400.0f, 0.0f };
		(void)horizon;
		if (variant == 0)
		{
			CHECK(group.formationManager(goal, false, CMD_FROM_PLAYER, false) == AIGroup::FORMATION_TAKEN);
			CHECK(group.size() == 2);
			group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
			CHECK(stealthDraws(a.logic).empty());
			CHECK(reported(*a.ai, "[S-895] AIGroup RW 0x774897"));
		}
		else if (variant == 1)
		{
			a.logic.settings().useFormations = false;
			CHECK(group.formationManager(goal, false, CMD_FROM_PLAYER, false) == AIGroup::FORMATION_NOT_OFFERED);
			group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
			CHECK(stealthDraws(a.logic).size() == 1); // the elf; the horde has no StealthUpdate
			CHECK_FALSE(reported(*a.ai, "[S-895] AIGroup RW 0x774897"));
		}
		else if (variant == 2)
		{
			CHECK(group.formationManager(goal, true, CMD_FROM_PLAYER, false) == AIGroup::FORMATION_NOT_OFFERED);
			group.groupMoveToPosition(goal, true, CMD_FROM_PLAYER);
			CHECK(stealthDraws(a.logic).size() == 1);
			CHECK_FALSE(reported(*a.ai, "[S-895] AIGroup RW 0x774897"));
		}
		else if (variant == 3)
		{
			CHECK(group.formationManager(goal, false, CMD_FROM_AI, false) == AIGroup::FORMATION_NOT_OFFERED);
			CHECK(group.formationManager(goal, false, CMD_FROM_PLAYER, true) == AIGroup::FORMATION_NOT_OFFERED);
		}
		else if (variant == 4)
		{
			// the goal 100 from the nearest member: refused by RW 0x76F115, so the generic path runs with its draw
			CHECK(group.formationManager(near, false, CMD_FROM_PLAYER, false) == AIGroup::FORMATION_REJECTED);
			group.groupMoveToPosition(near, false, CMD_FROM_PLAYER);
			CHECK(stealthDraws(a.logic).size() == 1);
			CHECK_FALSE(reported(*a.ai, "[S-895] AIGroup RW 0x774897"));
		}
		else if (variant == 5 || variant == 6)
		{
			// the held horde leaves the group (RW 0x76FFF0); the elf alone has a FormationPriority locomotor: 300 from the goal the manager takes it (no draw), 100
			// from the goal RW 0x76F115 refuses it and the generic path draws for the elf
			horde->setDisabled(DISABLED_HELD, a.logic.getFrame() + 1000u);
			REQUIRE(horde->isDisabledByType(DISABLED_HELD));
			const Coord3D &to = variant == 5 ? goal : near;
			CHECK(group.formationManager(to, false, CMD_FROM_PLAYER, false) == (variant == 5 ? AIGroup::FORMATION_TAKEN : AIGroup::FORMATION_REJECTED));
			CHECK(group.members().size() == 1); // the held horde left the group
			CHECK(group.members()[0] == elf);
			AIGroup moved(a.logic, { elf->getID(), horde->getID() }); // a fresh group: the filter above left one member in `group`
			moved.groupMoveToPosition(to, false, CMD_FROM_PLAYER);
			CHECK(stealthDraws(a.logic).size() == (variant == 5 ? 0u : 1u));
		}
		else
		{
			// every member refused by RW 0x76FFF0: the last one ends the order and nothing moves, no draw
			elf->setDisabled(DISABLED_HELD, a.logic.getFrame() + 1000u);
			horde->setDisabled(DISABLED_HELD, a.logic.getFrame() + 1000u);
			a.run(3);
			const Coord3D e0 = *elf->getPosition(), h0 = *horde->getPosition();
			CHECK(group.formationManager(goal, false, CMD_FROM_PLAYER, false) == AIGroup::FORMATION_NOTHING);
			CHECK(group.members().size() == 1);
			AIGroup again(a.logic, { elf->getID(), horde->getID() });
			again.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
			CHECK(stealthDraws(a.logic).empty());
			a.run(15);
			CHECK(elf->getPosition()->x == e0.x);
			CHECK(horde->getPosition()->x == h0.x);
		}
	}
	// a group without any FormationPriority member is refused by the manager's first test (RW 0x7747DE returns 0): the generic path, one draw per stealth unit
	{
		Arena a(shared());
		Object *e1 = a.place("CINE_RohanElvenWarrior", 0, 400.0f, 400.0f);
		Object *e2 = a.place("CINE_RohanElvenWarrior", 0, 430.0f, 400.0f);
		a.run(1);
		REQUIRE_FALSE(e1->testStatus(stealthed));
		REQUIRE_FALSE(e2->testStatus(stealthed));
		bool anyPriority = false;
		for (Object *o : { e1, e2 })
		{
			const Locomotor *loco = o->getAIUpdateInterface() ? o->getAIUpdateInterface()->curLocomotor() : nullptr;
			anyPriority = anyPriority || (loco && loco->getTemplate().m_formationPriority != FORMATION_NO_FORMATION);
		}
		a.logic.random().enableCallLog(true);
		AIGroup group(a.logic, { e1->getID(), e2->getID() });
		CHECK((group.formationManager(goal, false, CMD_FROM_PLAYER, false) == AIGroup::FORMATION_REJECTED) == !anyPriority);
		group.groupMoveToPosition(goal, false, CMD_FROM_PLAYER);
		CHECK(stealthDraws(a.logic).size() == (anyPriority ? 0u : 2u));
	}
}

TEST_CASE("backlog6 group: AIData UseFormations is read from aidata.ini and a value other than the constructor's changes the settings hash")
{
	if (!hudtest::haveWorld("backlog6 settings"))
	{
		return;
	}
	Arena a(shared());
	CHECK(a.logic.settings().useFormations);
	GameLogicSettings parsed;
	std::string error;
	REQUIRE_MESSAGE(GameLogicSettingsLoader::scanAIData("AIData\n  EnableRepulsors = Yes\n  RepulsedDistance = 1\n  UseFormations = No\nEnd\n", parsed, &error), error);
	CHECK_FALSE(parsed.useFormations);
	GameLogicSettings on = a.logic.settings(), off = a.logic.settings();
	off.useFormations = false;
	StateHasher h1, h2;
	on.crc(h1);
	off.crc(h2);
	CHECK(h1.value() != h2.value());
}
