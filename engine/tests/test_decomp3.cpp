// OpenBFME. GPL-3.0.
// Lane DECOMP-3: the skirmish AI's player info record (AIStatCollector, RW 0x8E46D9) filled by the manager's object events. Binary facts SKIP when RW_GAME_DAT
// is unset; the retail scenarios SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Upgrade.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/AI/TurretAI.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
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

bool contains(const std::vector<ObjectID> &v, ObjectID id)
{
	return std::find(v.begin(), v.end(), id) != v.end();
}

int occurrences(const std::vector<ObjectID> &v, ObjectID id)
{
	return (int)std::count(v.begin(), v.end(), id);
}

// a new object of `name` for `owner` at `at`
Object *make(GameLogic &logic, const char *name, Player &owner, const Coord3D &at)
{
	const ThingTemplate *tt = logic.things().findTemplate(name);
	REQUIRE_MESSAGE(tt, name);
	Object *o = logic.newObject(tt, owner.getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE_MESSAGE(o, name);
	o->setPosition(&at);
	return o;
}

// the position of the player's first structure (its start), moved by (dx, dy)
Coord3D nearStart(GameLogic &logic, const Player &p, float dx, float dy)
{
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == &p && o->isKindOf((unsigned)CombatNames::kindOf("STRUCTURE")))
		{
			return Coord3D{ o->getPosition()->x + dx, o->getPosition()->y + dy, o->getPosition()->z };
		}
	}
	FAIL("the player has no structure");
	return Coord3D{};
}
} // namespace

TEST_CASE("decomp3 binary facts: the player info record's Register / UnRegister and who calls the manager's object events (RW 0x8E4591, 0x8E480C, 0x6A9D9C, 0x6A99CB)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("decomp3 binary facts (RW_GAME_DAT unset)");
		return;
	}
	// Register: the economy, structure, army and ship adds in that order, then the per-template count at + 0x20, else a HERO to + 0x14
	CHECK(callTarget(*pe, 0x8E45A7) == 0x99E84Eu);
	CHECK(callTarget(*pe, 0x8E45C4) == 0x99EA19u);
	CHECK(callTarget(*pe, 0x8E45F8) == 0x99E6C9u);
	CHECK(callTarget(*pe, 0x8E4605) == 0x99E493u);
	CHECK(pe->hex(0x8E463A, 3) == "83c620");          // add esi, 0x20 (the count map)
	CHECK(callTarget(*pe, 0x8E4640) == 0x6B4E8Fu);    // its find
	CHECK(pe->hex(0x8E4664, 7) == "f6801301000004");  // test [template + 0x113], 4 (HERO)
	CHECK(pe->hex(0x8E3FF1, 3) == "8d4e20");          // RW 0x8E3FD6 reads the same map
	CHECK(callTarget(*pe, 0x8E3FF4) == 0x6B4E8Fu);
	// the record update RW 0x8E3CF3: the army list (prune + idle army), the ships, the structures and the economy list (RW 0x65BD07 drops unresolved ids)
	CHECK(callTarget(*pe, 0x8E3CF8) == 0x99E6B9u);
	CHECK(callTarget(*pe, 0x8E3D00) == 0x99E446u);
	CHECK(callTarget(*pe, 0x8E3D08) == 0x65BD07u);
	// the object events: the world entry / exit, the team change, the capture, CommandSetUpgrade, the RingHero check, newGame's pass
	CHECK(callTarget(*pe, 0x68E42D) == 0x6A9D9Cu);
	CHECK(callTarget(*pe, 0x68C205) == 0x6A99CBu);
	CHECK(pe->hex(0x68E32A, 6) == "858810010000");    // the INERT test (template + 0x110) that skips RW 0x68E42D
	CHECK(callTarget(*pe, 0x697CA3) == 0x6A99CBu);
	CHECK(callTarget(*pe, 0x697CBC) == 0x6A9D9Cu);
	CHECK(callTarget(*pe, 0x854712) == 0x6A99CBu);
	CHECK(callTarget(*pe, 0x85474D) == 0x6A9D9Cu);
	CHECK(callTarget(*pe, 0x8B7C8E) == 0x6A99CBu);
	CHECK(callTarget(*pe, 0x8B7C9A) == 0x6A9D9Cu);
	CHECK(callTarget(*pe, 0x8F0E34) == 0x6A99CBu);
	CHECK(callTarget(*pe, 0x8F0E41) == 0x6A9D9Cu);
	CHECK(callTarget(*pe, 0x8F0EF8) == 0x8F0D54u);    // AIPlayer::update runs the RingHero check first
	CHECK(pe->hex(0x8F0DF5, 7) == "f6812001000004");  // DOZER_FACTORY (template + 0x120 bit 2)
	CHECK(pe->hex(0x6A9E73, 6) == "8b8e0c030000");    // newGame's pass walks Player + 0x30C (the default team) ...
	CHECK(callTarget(*pe, 0x6A9E7D) == 0x66362Du);    // ... with Team::iterate_TeamMemberList
	CHECK(callTarget(*pe, 0x6A9E89) == 0x6A9D9Cu);
	// the object's + 0x4D0 byte both events test is the phantom structure flag (DozerAIUpdate::makePhantomStructureInert RW 0x88D0D8)
	CHECK(pe->hex(0x6A9DA3, 7) == "80bed004000000");
	CHECK(pe->hex(0x6A99D0, 7) == "80bed004000000");
	CHECK(pe->hex(0x88D119, 7) == "c686d004000001");
	// the economy list's size is what RW 0x9EDA17 and RW 0x9A03A1 read (record + 0xC)
	CHECK(pe->hex(0x9EDA2D, 3) == "8b480c");
	CHECK(pe->hex(0x9A03DA, 3) == "8b4f0c");
}

TEST_CASE("decomp3 S-418: the record keeps the order of the object events; a dying unit stays listed and counted until it is destroyed")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 11, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	Player *mordor = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[1]);
	REQUIRE(men);
	REQUIRE(mordor);
	REQUIRE(logic.skirmishAI().started());
	REQUIRE(logic.skirmishAI().record(men->getPlayerIndex()));
	REQUIRE(logic.skirmishAI().record(mordor->getPlayerIndex()));
	const AIStatCollector &mine = AITacticalAI::recordOf(logic, men->getPlayerIndex());
	const AIStatCollector &theirs = AITacticalAI::recordOf(logic, mordor->getPlayerIndex());
	// newGame's pass (RW 0x6A9E48): the default team's members, newest first; every listed id is the player's
	std::vector<ObjectID> expected;
	for (const Object *o = men->getDefaultTeam()->getFirstMember(); o; o = o->friend_teamNext())
	{
		if (contains(mine.structures(), o->getID()))
		{
			expected.insert(expected.begin(), o->getID());
		}
	}
	CHECK(mine.structures() == expected);
	CHECK_FALSE(mine.structures().empty());
	for (const std::vector<ObjectID> *l : { &mine.army(), &mine.dozers(), &mine.structures(), &mine.economy() })
	{
		for (ObjectID id : *l)
		{
			const Object *o = logic.findObjectByID(id);
			REQUIRE(o);
			CHECK(o->getControllingPlayer() == men);
		}
	}

	const ThingTemplate *tt = logic.things().findTemplate("GondorFighterHorde");
	REQUIRE(tt);
	const int before = mine.countOf("GondorFighterHorde");
	Object *a = logic.newObject(tt, men->getDefaultTeam(), ObjectStatusMaskType{});
	Object *b = logic.newObject(tt, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(a);
	REQUIRE(b);
	const ObjectID aId = a->getID(), bId = b->getID();
	// the world entry registers in event order: appended
	REQUIRE(mine.army().size() >= 2);
	CHECK(mine.army()[mine.army().size() - 2] == aId);
	CHECK(mine.army().back() == bId);
	CHECK(mine.countOf("GondorFighterHorde") == before + 2);

	// a contain taking `a` out of the world unregisters it (RW 0x68C18F from OpenContain), putting it back appends it after `b`
	logic.friend_containLeaveWorld(*a);
	CHECK_FALSE(contains(mine.army(), aId));
	CHECK(mine.countOf("GondorFighterHorde") == before + 1);
	logic.friend_containEnterWorld(*a);
	CHECK(mine.army().back() == aId);
	CHECK(mine.countOf("GondorFighterHorde") == before + 2);

	// the owner change (RW 0x697C09): removed with the old owner, appended with the new one
	b->setTeam(mordor->getDefaultTeam());
	CHECK_FALSE(contains(mine.army(), bId));
	CHECK(theirs.army().back() == bId);
	CHECK(mine.countOf("GondorFighterHorde") == before + 1);
	CHECK(theirs.countOf("GondorFighterHorde") == 1);
	b->setTeam(men->getDefaultTeam());
	CHECK(mine.army().back() == bId);
	CHECK(theirs.countOf("GondorFighterHorde") == 0); // the key stays at 0 (RW 0x8E48A9)

	// a dying unit keeps its entry and its count (RW unregisters it in the destructor's world exit only): a mountain troll's slow death keeps it in the world
	Object *troll = make(logic, "MordorMountainTroll", *mordor, nearStart(logic, *mordor, 300.0f, 0.0f));
	const ObjectID trollId = troll->getID();
	const int trolls = theirs.countOf("MordorMountainTroll");
	REQUIRE(theirs.army().back() == trollId);
	troll->kill(0);
	logic.runLogicFrame();
	Object *dying = logic.findObjectByID(trollId);
	REQUIRE(dying);
	REQUIRE(dying->isEffectivelyDead());
	CHECK(contains(theirs.army(), trollId));
	CHECK(theirs.countOf("MordorMountainTroll") == trolls);
	for (int i = 0; i < 3000 && logic.findObjectByID(trollId); ++i)
	{
		logic.runLogicFrame(); // its death runs out and the destroy list deletes it (the destructor's world exit RW 0x68C18F -> RW 0x6A99CB)
	}
	REQUIRE_FALSE(logic.findObjectByID(trollId));
	CHECK_FALSE(contains(theirs.army(), trollId));
	CHECK(theirs.countOf("MordorMountainTroll") == trolls - 1);
	logic.destroyObject(a);
	logic.runLogicFrame();
	CHECK_FALSE(contains(mine.army(), aId));
	CHECK(mine.countOf("GondorFighterHorde") == before + 1);
}

TEST_CASE("decomp3 S-418: the record is hashed; an object registered twice (RW's plain push_back) is listed twice and removed once")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 11, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	REQUIRE(men);
	const ThingTemplate *tt = logic.things().findTemplate("GondorFighterHorde");
	REQUIRE(tt);
	Object *a = logic.newObject(tt, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(a);
	const AIStatCollector &mine = AITacticalAI::recordOf(logic, men->getPlayerIndex());
	const int count = mine.countOf("GondorFighterHorde");
	StateHasher h0;
	mine.crc(h0);
	logic.skirmishAI().objectAdded(*a); // what the capture path does after the team change's own add (RW 0x85474D)
	StateHasher h1;
	mine.crc(h1);
	CHECK(h0.value() != h1.value());
	CHECK(occurrences(mine.army(), a->getID()) == 2);
	CHECK(mine.countOf("GondorFighterHorde") == count + 1);
	logic.skirmishAI().objectRemoved(*a);
	CHECK(occurrences(mine.army(), a->getID()) == 1);
	CHECK(mine.countOf("GondorFighterHorde") == count);
}

TEST_CASE("decomp3 binary facts: S-1301 Object::isAbleToAttack (RW 0x691269) and the contain / spawn slots it asks")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("decomp3 binary facts S-1301 (RW_GAME_DAT unset)");
		return;
	}
	CHECK(pe->hex(0x691270, 2) == "6a05");            // NO_ATTACK
	CHECK(pe->hex(0x69127D, 5) == "6881000000");      // the model condition 0x81
	CHECK(pe->hex(0x6912A2, 7) == "f6811501000020");  // a HORDE container (+ 0x115 bit 5) is skipped
	CHECK(pe->hex(0x6912BF, 2) == "6a25");            // CONTESTING_BUILDING
	CHECK(pe->hex(0x6912D5, 6) == "ff90b0000000");    // contain slot 0xB0(this)
	CHECK(pe->hex(0x691303, 6) == "ff92c0000000");    // contain slot 0xC0(this, victim)
	CHECK(pe->hex(0x69132A, 7) == "f6800f01000002");  // PORTABLE_STRUCTURE
	CHECK(pe->hex(0x691333, 7) == "f6801201000010");  // SPAWNS_ARE_THE_WEAPONS
	CHECK(pe->hex(0x69133C, 7) == "f686c801000004");  // ... and DISABLED_EMP (+ 0x1C8 bit 2)
	CHECK(pe->hex(0x691364, 6) == "8d8e4c030000");    // the weapon set (+ 0x34C)
	CHECK(callTarget(*pe, 0x69136A) == 0x6C81B0u);    // getWeaponInWeaponSlot
	CHECK(callTarget(*pe, 0x691380) == 0x66246Bu);    // getWhichTurretForWeaponSlot
	CHECK(callTarget(*pe, 0x69138D) == 0x662364u);    // isTurretEnabled
	CHECK(pe->hex(0x6913B1, 7) == "f6800801000008");  // CAN_ATTACK KindOf
	CHECK(pe->hex(0x6913BE, 2) == "6a01");            // CAN_ATTACK status
	CHECK(pe->hex(0x6913DB, 6) == "ff90b4000000");    // its contain's slot 0xB4
	CHECK(pe->hex(0x6913EA, 6) == "ff9014010000");    // ... and slot 0x114
	CHECK(pe->hex(0x6913FC, 6) == "399e74030000");    // the weapon set's filled slot mask (+ 0x374)
	CHECK(callTarget(*pe, 0x691406) == 0x68C3A3u);    // the spawn interface ...
	CHECK(pe->hex(0x691413, 3) == "ff521c");          // ... slot 0x1C
	CHECK(pe->u32At(0xC58D94 + 0x1C) == 0x862A4Eu);   // SpawnBehavior::canAnySlavesAttack
	// the contain tables: OpenContain, TransportContain, GarrisonContain, TunnelContain
	CHECK(pe->u32At(0xC59AF0 + 0xB4) == 0x865A7Eu);
	CHECK(pe->u32At(0xC59AF0 + 0xC0) == 0x865B33u);
	CHECK(pe->u32At(0xC5A690 + 0xB4) == 0x865A7Eu);
	CHECK(pe->u32At(0xC5C698 + 0xB4) == 0x8BD372u);
	CHECK(pe->u32At(0xC5DCE0 + 0xB4) == 0x8BD372u);
	CHECK(pe->u32At(0xC5C698 + 0xC0) == 0x865B33u);
	CHECK(pe->hex(0x865B44, 3) == "8b51e4");          // slot 0xC0 reads the data (+ 0x68: PassengersTestCollisionHeight)
	CHECK(pe->hex(0x865B4A, 4) == "0f2f6268");
}

TEST_CASE("decomp3 S-1301: Object::isAbleToAttack on a horde, its members and a NO_ATTACK object")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 11, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	REQUIRE(men);
	const ThingTemplate *tt = logic.things().findTemplate("GondorFighterHorde");
	REQUIRE(tt);
	Object *horde = logic.newObject(tt, men->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(horde);
	CHECK(horde->isAbleToAttack()); // CAN_ATTACK KindOf
	REQUIRE(horde->getContain());
	REQUIRE(horde->getContain()->getContainedItemsList());
	REQUIRE_FALSE(horde->getContain()->getContainedItemsList()->empty());
	Object *member = horde->getContain()->getContainedItemsList()->front();
	// a member has no CAN_ATTACK KindOf; its HORDE container is skipped (the horde has no container), it has an AI and a weapon (+ 0x374)
	CHECK_FALSE(member->isKindOf((unsigned)CombatNames::kindOf("CAN_ATTACK")));
	REQUIRE(member->getAIUpdateInterface());
	REQUIRE(member->getWeapons());
	CHECK(member->getWeapons()->hasAnyWeapon());
	CHECK(member->isAbleToAttack());
	horde->setStatus((unsigned)CombatNames::status("NO_ATTACK"), true);
	CHECK_FALSE(horde->isAbleToAttack());
	horde->setStatus((unsigned)CombatNames::status("NO_ATTACK"), false);
	horde->setStatus((unsigned)CombatNames::status("SOLD"), true);
	CHECK_FALSE(horde->isAbleToAttack());
}

TEST_CASE("decomp3 S-418: the object events of an INERT object, a CommandSetUpgrade, the AIPlayer's Upgrade_RingHero check and a capture")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 11, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	Player *mordor = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[1]);
	REQUIRE(men);
	REQUIRE(mordor);
	REQUIRE(logic.skirmishAI().findAI(mordor->getPlayerIndex()));
	const SkirmishAIManager::Counters &c = logic.skirmishAI().counters();
	const AIStatCollector &mine = AITacticalAI::recordOf(logic, men->getPlayerIndex());
	const AIStatCollector &theirs = AITacticalAI::recordOf(logic, mordor->getPlayerIndex());

	// INERT (RW 0x68E32A): the world entry makes no event; the destructor's exit does (RW 0x68C205 is unconditional)
	{
		std::vector<SkirmishAIManager::ObjectEvent> trace;
		logic.skirmishAI().setEventTrace(&trace);
		Object *rubble = make(logic, "StructureRubblePileSmall", *men, nearStart(logic, *men, 300.0f, 0.0f));
		const ObjectID rubbleId = rubble->getID();
		REQUIRE(rubble->isKindOf((unsigned)CombatNames::kindOf("INERT")));
		CHECK(trace.empty()); // the creation's world entry: no event
		logic.destroyObject(rubble);
		logic.runLogicFrame();
		logic.skirmishAI().setEventTrace(nullptr);
		int rubbleEvents = 0;
		for (const SkirmishAIManager::ObjectEvent &e : trace)
		{
			if (e.id == rubbleId)
			{
				++rubbleEvents;
				CHECK_FALSE(e.add);
			}
		}
		CHECK(rubbleEvents == 1); // the destructor's removal only
	}

	// CommandSetUpgrade (RW 0x8B7C68): the removal and the add move the orc pit to the end of the structure list
	{
		Object *pit = make(logic, "MordorOrcPit", *mordor, nearStart(logic, *mordor, 250.0f, 0.0f));
		Object *pit2 = make(logic, "MordorOrcPit", *mordor, nearStart(logic, *mordor, -250.0f, 0.0f));
		REQUIRE(theirs.structures().size() >= 2);
		CHECK(theirs.structures()[theirs.structures().size() - 2] == pit->getID());
		CHECK(theirs.structures().back() == pit2->getID());
		const UpgradeTemplate *level2 = TheUpgradeCenter->findUpgrade("Upgrade_MordorOrcPitLevel2");
		REQUIRE(level2);
		const int pits = theirs.countOf("MordorOrcPit");
		std::vector<SkirmishAIManager::ObjectEvent> trace;
		logic.skirmishAI().setEventTrace(&trace);
		pit->giveUpgradeSelf(level2);
		logic.skirmishAI().setEventTrace(nullptr);
		// exactly two pairs of the pit, within this call (no other object's event): updateUpgradeModules (RW 0x693724) runs the Level2 module's attemptUpgrade
		// (the implementation RW 0x8B7C68: removal RW 0x8B7C8E, add RW 0x8B7C9A), then its postUpgradeCheck RW 0x8B7CAB (removal, slot 0x20, then attemptUpgrade
		// again, slot 4 at RW 0x8B7CF4: the implementation's pair once more)
		REQUIRE(trace.size() == 4);
		for (size_t i = 0; i < trace.size(); ++i)
		{
			INFO("upgrade event " << i);
			CHECK(trace[i].add == (i % 2 == 1));
			CHECK(trace[i].id == pit->getID());
		}
		CHECK(theirs.structures().back() == pit->getID());
		CHECK(occurrences(theirs.structures(), pit->getID()) == 1);
		CHECK(theirs.countOf("MordorOrcPit") == pits);
	}

	// RW 0x8F0D54: when the AI's player gains Upgrade_RingHero, each living DOZER_FACTORY of its structure list is removed and added again (to the end)
	{
		for (int i = 0; i < 12; ++i)
		{
			logic.runLogicFrame(); // the manager updates from logic frame 10
		}
		std::vector<ObjectID> factories;
		for (ObjectID id : theirs.structures())
		{
			const Object *o = logic.findObjectByID(id);
			if (o && o->isKindOf((unsigned)CombatNames::kindOf("DOZER_FACTORY")) && !o->isEffectivelyDead())
			{
				factories.push_back(id);
			}
		}
		REQUIRE_FALSE(factories.empty());
		std::vector<SkirmishAIManager::ObjectEvent> trace;
		logic.skirmishAI().setEventTrace(&trace);
		mordor->addCompletedUpgrade("Upgrade_RingHero");
		logic.runLogicFrame();
		logic.skirmishAI().setEventTrace(nullptr);
		// RW 0x8F0E2C .. 0x8F0E4C: for each collected factory in list order, the removal RW 0x8F0E34 then the add RW 0x8F0E41 (other objects' events of the
		// frame are not the check's)
		std::vector<std::pair<bool, ObjectID>> seen, expected;
		for (ObjectID id : factories)
		{
			expected.push_back({ false, id });
			expected.push_back({ true, id });
		}
		for (const SkirmishAIManager::ObjectEvent &e : trace)
		{
			if (std::find(factories.begin(), factories.end(), e.id) != factories.end())
			{
				seen.push_back({ e.add, e.id });
			}
		}
		CHECK(seen == expected);
		REQUIRE(theirs.structures().size() >= factories.size());
		const std::vector<ObjectID> tail(theirs.structures().end() - (long)factories.size(), theirs.structures().end());
		CHECK(tail == factories);
		// a second frame with the upgrade unchanged (+ 0x154 already set) re-registers nothing
		trace.clear();
		logic.skirmishAI().setEventTrace(&trace);
		logic.runLogicFrame();
		logic.skirmishAI().setEventTrace(nullptr);
		for (const SkirmishAIManager::ObjectEvent &e : trace)
		{
			CHECK(std::find(factories.begin(), factories.end(), e.id) == factories.end());
		}
	}

	// the capture (RW 0x8544F2): a hero takes a neutral CaptureFlag; its linked Outpost goes through the team change's pair and the capture's own pair
	{
		const Coord3D at = nearStart(logic, *men, 0.0f, 400.0f);
		Player *neutral = g.live->players().getNeutralPlayer();
		REQUIRE(neutral);
		Object *flag = make(logic, "CaptureFlag", *neutral, at);
		Object *outpost = make(logic, "Outpost", *neutral, Coord3D{ at.x + 140.0f, at.y, at.z });
		const ThingTemplate *heroT = logic.things().findTemplate(men->getPlayerTemplate()->m_buildableHeroesMP.front());
		REQUIRE(heroT);
		Object *hero = logic.newObject(heroT, men->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(hero);
		const Coord3D hp{ at.x - 60.0f, at.y, at.z };
		hero->setPosition(&hp);
		const SpecialPowerModule *capture = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : hero->modules())
		{
			const SpecialPowerModule *sp = dynamic_cast<const SpecialPowerModule *>(m.get());
			if (sp && sp->getSpecialPowerTemplate() && sp->getSpecialPowerTemplate()->m_type == 0x1D)
			{
				capture = sp;
			}
		}
		REQUIRE(capture);
		logic.runLogicFrame();
		const unsigned long long adds = c.objectAdds, removes = c.objectRemoves;
		const ObjectID outpostId = outpost->getID(), flagId = flag->getID();
		GameMessage cast(MSG_DO_SPECIAL_POWER_AT_OBJECT, men->getPlayerIndex());
		cast.appendIntegerArgument((int)capture->getSpecialPowerTemplate()->getID());
		cast.appendObjectIDArgument(flagId);
		cast.appendIntegerArgument(0);
		cast.appendObjectIDArgument(hero->getID());
		g.live->commands().append(cast);
		std::vector<SkirmishAIManager::ObjectEvent> trace;
		logic.skirmishAI().setEventTrace(&trace);
		for (int i = 0; i < 900; ++i)
		{
			logic.runLogicFrame();
			const Object *o = logic.findObjectByID(outpostId);
			if (o && o->getControllingPlayer() == men)
			{
				break;
			}
		}
		logic.skirmishAI().setEventTrace(nullptr);
		const Object *o = logic.findObjectByID(outpostId);
		REQUIRE(o);
		REQUIRE(o->getControllingPlayer() == men);
		// RW 0x8544F2's events for the flag (the target) and its outpost (the LINKED_TO_FLAG building), in order: the flag's setCapturedTeam (RW 0x854702 ->
		// 0x6996DC -> 0x697C09: removal with the neutral owner RW 0x697CA3, add with the new one RW 0x697CBC); the capture's removal of the outpost
		// (RW 0x854712); the outpost's setCapturedTeam (RW 0x854725 -> 0x697C09: a removal with the neutral owner that finds nothing, then the add); the
		// capture's add (RW 0x85474D). Other objects' events in the same frames (the Mordor AI's porters entering the structures they place, RW 0x88C51B ->
		// 0x68C18F) are not the capture's
		struct Step
		{
			bool add;
			ObjectID id;
			int player;
		};
		const int n = neutral->getPlayerIndex(), m = men->getPlayerIndex();
		const std::vector<Step> expected = { { false, flagId, n }, { true, flagId, m }, { false, outpostId, n }, { false, outpostId, n }, { true, outpostId, m },
			{ true, outpostId, m } };
		std::vector<Step> seen;
		for (const SkirmishAIManager::ObjectEvent &e : trace)
		{
			if (e.id == flagId || e.id == outpostId)
			{
				seen.push_back(Step{ e.add, e.id, e.playerIndex });
			}
		}
		REQUIRE(seen.size() == expected.size());
		for (size_t i = 0; i < expected.size(); ++i)
		{
			INFO("capture event " << i);
			CHECK(seen[i].add == expected[i].add);
			CHECK(seen[i].id == expected[i].id);
			CHECK(seen[i].player == expected[i].player);
		}
		std::uint64_t traceAdds = 0, traceRemoves = 0;
		for (const SkirmishAIManager::ObjectEvent &e : trace)
		{
			(e.add ? traceAdds : traceRemoves) += 1;
		}
		CHECK(c.objectAdds - adds == traceAdds);
		CHECK(c.objectRemoves - removes == traceRemoves);
		CHECK_FALSE(contains(mine.structures(), outpostId)); // LINKED_TO_FLAG: in no list (RW 0x99E8C8 / 0x99E818)
	}
}

TEST_CASE("decomp3 S-1301: isAbleToAttack's EMP rule (MordorBarricade), slave attacks (WargLair), the turret walk (RohanRohirrimArcher), passengers and HordeContain's slot 0xB4")
{
	OPENBFME_REQUIRE_START(s);
	std::string error;
	buildtest::Game g;
	REQUIRE_MESSAGE(buildtest::startGame(*s, "FactionMen", "FactionMordor", 11, g, &error), error);
	GameLogic &logic = g.live->logic();
	Player *men = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[0]);
	Player *mordor = g.live->players().findPlayerWithName(g.live->report().startSlotPlayers[1]);
	REQUIRE(men);
	REQUIRE(mordor);

	// SPAWNS_ARE_THE_WEAPONS (MordorBarricade): the CAN_ATTACK status answers, but not while DISABLED_EMP (RW 0x691327 .. 0x691343, before the CAN_ATTACK tests).
	// Its archers stand on its ARCHER_ bones (SpawnPointProductionExitUpdate, lane PRODUCTION1)
	{
		Object *barricade = make(logic, "MordorBarricade", *mordor, nearStart(logic, *mordor, 300.0f, 0.0f));
		REQUIRE(barricade->isKindOf((unsigned)CombatNames::kindOf("SPAWNS_ARE_THE_WEAPONS")));
		REQUIRE_FALSE(barricade->isKindOf((unsigned)CombatNames::kindOf("CAN_ATTACK")));
		const bool armed = barricade->getWeapons() && barricade->getWeapons()->hasAnyWeapon();
		REQUIRE_FALSE(armed);
		CHECK_FALSE(barricade->isAbleToAttack()); // no weapon, no rider, no slave
		barricade->setStatus((unsigned)CombatNames::status("CAN_ATTACK"), true);
		CHECK(barricade->isAbleToAttack());
		barricade->setDisabled(DISABLED_EMP, logic.getFrame() + 1000);
		CHECK_FALSE(barricade->isAbleToAttack());
		barricade->clearDisabled(DISABLED_EMP);
		CHECK(barricade->isAbleToAttack());
		// lane PRODUCTION1: the SpawnPoint exit finds the ARCHER_ bones through the game's bone provider: the initial burst puts its four archers on them
		// (SpawnNumber = InitialBurst = 4), then KillSpawnsBasedOnModelConditionState stops the spawner after the one try (RW 0x86356C); no S-3800 stop
		const ObjectID barricadeId = barricade->getID();
		const SpawnBehavior *spawn = dynamic_cast<const SpawnBehavior *>(SpawnBehaviorInterface::of(*barricade));
		REQUIRE(spawn);
		for (int i = 0; i < 20; ++i)
		{
			logic.runLogicFrame();
		}
		REQUIRE(logic.findObjectByID(barricadeId));
		CHECK(spawn->spawns().size() == 4);
		CHECK_FALSE(spawn->isSpawnerActive());
		bool noted = false;
		for (const std::string &line : logic.report().stops)
		{
			noted = noted || line.find("[S-3800] SpawnBehavior of MordorBarricade") != std::string::npos;
		}
		CHECK_FALSE(noted);
	}

	// slave attacks (SpawnBehavior::canAnySlavesAttack RW 0x862A4E): a WargLair (no CAN_ATTACK, no weapon) is able while one of its wargs is
	{
		Object *lair = make(logic, "WargLair", *mordor, nearStart(logic, *mordor, -400.0f, 300.0f));
		const ObjectID id = lair->getID();
		REQUIRE_FALSE(lair->isKindOf((unsigned)CombatNames::kindOf("CAN_ATTACK")));
		const SpawnBehavior *spawn = dynamic_cast<const SpawnBehavior *>(SpawnBehaviorInterface::of(*lair));
		REQUIRE(spawn);
		for (int i = 0; i < 300 && spawn->spawns().empty(); ++i)
		{
			logic.runLogicFrame();
		}
		lair = logic.findObjectByID(id);
		REQUIRE(lair);
		REQUIRE_FALSE(spawn->spawns().empty());
		const bool armed = lair->getWeapons() && lair->getWeapons()->hasAnyWeapon();
		REQUIRE_FALSE(armed);
		CHECK(lair->isAbleToAttack());
		for (ObjectID sid : spawn->spawns())
		{
			if (Object *slave = logic.findObjectByID(sid))
			{
				slave->setStatus((unsigned)CombatNames::status("NO_ATTACK"), true);
			}
		}
		CHECK_FALSE(lair->isAbleToAttack()); // no slave can attack: nothing else answers
	}

	// a unit without CAN_ATTACK whose only weapon is on its turret: unable while the turret is disabled (RW 0x662364)
	{
		Object *archer = make(logic, "RohanRohirrimArcher", *men, nearStart(logic, *men, 300.0f, 0.0f));
		REQUIRE_FALSE(archer->isKindOf((unsigned)CombatNames::kindOf("CAN_ATTACK")));
		AIUpdateInterface *ai = archer->getAIUpdateInterface();
		REQUIRE(ai);
		REQUIRE(ai->turret());
		REQUIRE(ai->turret()->isWeaponSlotOnTurret(0));
		CHECK(archer->isAbleToAttack());
		ai->turret()->setTurretEnabled(false);
		CHECK_FALSE(archer->isAbleToAttack());
		ai->turret()->setTurretEnabled(true);
		CHECK(archer->isAbleToAttack());
	}

	// passengers: GondorCastleKeep (GarrisonContain, ObjectStatusOfContained CAN_ATTACK, slot 0xB4 true) and EvilMenTransportShip (HordeTransportContain whose
	// ObjectStatusOfContained lacks CAN_ATTACK)
	{
		Object *keep = make(logic, "GondorCastleKeep", *men, nearStart(logic, *men, -300.0f, 0.0f));
		REQUIRE_FALSE(keep->isKindOf((unsigned)CombatNames::kindOf("CAN_ATTACK")));
		REQUIRE(keep->getContain());
		CHECK(keep->getContain()->isPassengerAllowedToFire());
		const bool emptyKeep = keep->isAbleToAttack();
		Object *horde = make(logic, "GondorFighterHorde", *men, nearStart(logic, *men, -300.0f, 40.0f));
		CHECK_FALSE(horde->getContain()->isPassengerAllowedToFire()); // HordeContain RW 0x86C713: not contained -> false
		REQUIRE(keep->getContain()->addToContain(horde));
		REQUIRE(horde->getContainedBy() == keep);
		CHECK(horde->getContain()->isPassengerAllowedToFire()); // ... contained -> the keep's answer
		CHECK(horde->isAbleToAttack());
		CHECK(keep->isAbleToAttack()); // its riders (slot 0xB4 and slot 0x114 > 0)
		MESSAGE("the empty keep is able to attack: " << emptyKeep);
		CHECK_FALSE(emptyKeep);

		Object *ship = make(logic, "EvilMenTransportShip", *mordor, nearStart(logic, *mordor, -300.0f, 0.0f));
		REQUIRE(ship->getContain());
		CHECK_FALSE(ship->getContain()->isPassengerAllowedToFire());
		Object *orcs = make(logic, "MordorFighterHorde", *mordor, nearStart(logic, *mordor, -300.0f, 40.0f));
		REQUIRE(orcs->isAbleToAttack());
		REQUIRE(ship->getContain()->addToContain(orcs));
		REQUIRE(orcs->getContainedBy() == ship);
		CHECK_FALSE(orcs->isAbleToAttack()); // slot 0xB0 lacks CAN_ATTACK
		CHECK_FALSE(orcs->getContain()->isPassengerAllowedToFire());
	}
}
