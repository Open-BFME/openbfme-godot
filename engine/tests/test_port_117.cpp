// OpenBFME unit tests: PORT-117 (issue fork issue 301, owner report P-57) — "Angmar's summoner brings a horde back one soldier at a time".
//
// What RotWK 2.01 does (addresses read with tools/mod/rw.py, see PORT117_RW_ANALYSIS.md):
//   * the summon's trigger (RW 0x8B2584) replaces the summoner by its MountedTemplate (RW 0x8B228C), which the build assistant builds at once
//     (RW 0x8B2302: slot 0x38 of the vtable RW 0xC307D8 = RW 0x797796, buildObjectNow); ToggleMountedSpecialAbilityUpdate's replacement (RW 0x8B140D)
//     calls the same slot (RW 0x8B1483);
//   * buildObjectNow gives the new object its producer (RW 0x7978B9 -> Object::setProducer RW 0x68B6A1) but completes a KindOf HORDE object WITHOUT
//     it: RW 0x7979D4 .. 0x7979E8 setProducer(NULL) when the object's template has KindOf bit 109 (template + 0x114 & 0x2000), RW 0x7979EF
//     Object::onBuildComplete (RW 0x68D252), RW 0x7979F4 .. 0x797A02 setProducer(builder) again;
//   * HordeContain's create slot 1 (RW 0x8718ED) makes the payload through its slot 0x70 (RW 0x871B9B), which does nothing for an object that has a
//     producer (RW 0x871BAB .. 0x871BB3) and else runs the InitialPayload loop (RW 0x871BBB -> RW 0x86A1FA: every entry, its whole count);
//   * the banner carrier's update (RW 0x89ACE4) adds at most one member per run (RW 0x89AE61 -> RW 0x89A392) and returns IdleSpawnRate as its sleep
//     on every path (RW 0x89AE67).
// The port's buildObjectNow kept the producer, so the summoned horde stood empty and its banner carrier filled it one member per IdleSpawnRate.
//
// No game files are needed: the summoner, the hordes and the banner carrier are synthetic (the mod's files are never read here).

#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "CombatTestUtil.h"
#include "Horde2TestUtil.h"

#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BannerCarrierUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
// a summoner with the Thrall Master's module shape (SUMMON-1's synthetic one: 1000 ms unpack and preparation, no pack) whose MountedTemplate is `mounted`
std::string summonerIni(const char *name, const char *mounted)
{
	return std::string("Object ") + name + "\n"
		" KindOf = INFANTRY SELECTABLE CAN_ATTACK\n"
		" Body = ActiveBody BodyTag\n  MaxHealth = 300\n End\n"
		" Behavior = AIUpdateInterface AiTag\n End\n"
		" Behavior = SpecialPowerModule SummonStarterTag\n  SpecialPowerTemplate = SpecialAbilityPort117Summon\n  UpdateModuleStartsAttack = Yes\n  StartsPaused = No\n End\n"
		" Behavior = SummonReplacementSpecialAbilityUpdate SummonTag\n"
		"  SpecialPowerTemplate = SpecialAbilityPort117Summon\n  UnpackTime = 1000\n  PreparationTime = 1000\n  PersistentPrepTime = 0\n  PackTime = 0\n"
		"  AwardXPForTriggering = 0\n  MountedTemplate = " + mounted + "\n  IgnoreFacingCheck = Yes\n  MustFinishAbility = Yes\n"
		" End\n"
		" LocomotorSet\n  Locomotor = WalkerLoco\n  Condition = SET_NORMAL\n  Speed = 30\n End\n"
		"End\n";
}

// an object with a horde contain (the horde2 fixtures' shape) of `kindOf`, its contain's lines given by the caller; `contain` is the module's class name,
// `moreModules` the object's other modules
std::string hordeIni(const char *name, const char *kindOf, const char *containLines, const char *contain = "HordeContain", const std::string &moreModules = std::string())
{
	return std::string("Object ") + name + "\n"
		"  KindOf = " + kindOf + "\n"
		"  VisionRange = 200\n"
		"  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY SwordWeapon\n  End\n"
		"  Geometry = BOX\n  GeometryMajorRadius = 30\n  GeometryMinorRadius = 45\n  GeometryHeight = 20\n"
		"  Body = ImmortalBody ModuleTag_Body\n    MaxHealth = 1\n  End\n"
		"  Behavior = HordeAIUpdate ModuleTag_AI\n  End\n"
		"  Behavior = " + contain + " ModuleTag_Contain\n" + containLines + "  End\n" + moreModules +
		"  LocomotorSet\n    Locomotor = HordeLoco\n    Condition = SET_NORMAL\n    Speed = 50\n  End\n"
		"End\n";
}

// the modules of an object that replaces itself with `mounted` through ToggleMountedSpecialAbilityUpdate (the timings of HERO-2's synthetic module,
// test_hero2_core.cpp: no unpack and no pack, a preparation of one millisecond)
std::string toggleModules(const char *mounted)
{
	return std::string("  Behavior = SpecialPowerModule ModuleTag_ToggleStarter\n    SpecialPowerTemplate = SpecialAbilityPort117Toggle\n"
		"    UpdateModuleStartsAttack = Yes\n    StartsPaused = No\n  End\n"
		"  Behavior = ToggleMountedSpecialAbilityUpdate ModuleTag_Toggle\n    SpecialPowerTemplate = SpecialAbilityPort117Toggle\n"
		"    UnpackTime = 0\n    PreparationTime = 1\n    PackTime = 0\n    MountedTemplate = ") + mounted + "\n    IgnoreFacingCheck = Yes\n  End\n";
}

// five soldiers in two ranks and no banner carrier: RotWK's carrier spawn (RW 0x870204) returns at once on an empty BannerCarriersAllowed (RW 0x870233)
const char kFiveSoldiers[] =
	"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    RankInfo = RankNumber:2 UnitType:Swordsman Position:X:30 Y:10 Position:X:30 Y:-10\n"
	"    Slots = 5\n"
	"    InitialPayload = Swordsman 5\n";

// three soldiers and a banner carrier from rank 2 on. BannerCarrierMinLevel 1 keeps this fixture off the carrier RotWK makes at build complete for a
// level of 0 (RW 0x871901 -> RW 0x870204, not ported: S-7317): here the carrier comes from the level test alone (RW 0x871A1B), in RotWK and in the port
const char kThreeSoldiersAndBanner[] =
	"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    Slots = 3\n"
	"    InitialPayload = Swordsman 3\n"
	"    BannerCarriersAllowed = Port117Bannerman\n"
	"    BannerCarrierPosition = UnitType:Swordsman Pos:X:10.0 Y:0.0\n"
	"    BannerCarrierMinLevel = 1\n";

// a horde whose banner carrier is due from the start: BannerCarrierMinLevel 0. RotWK makes this horde's carrier at build complete when it has no producer
// (RW 0x8718F5 .. 0x87190E -> RW 0x870204), which the port does not do (S-7317): only the stop's test uses this fixture
const char kThreeSoldiersAndBannerAtOnce[] =
	"    RankInfo = RankNumber:1 UnitType:Swordsman Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
	"    Slots = 3\n"
	"    InitialPayload = Swordsman 3\n"
	"    BannerCarriersAllowed = Port117Bannerman\n"
	"    BannerCarrierPosition = UnitType:Swordsman Pos:X:10.0 Y:0.0\n"
	"    BannerCarrierMinLevel = 0\n";

// the banner carrier: IdleSpawnRate 2000 ms = 10 logic frames, MeleeFreeUnitSpawnTime 4000 ms = 20 (durations, RW 0x73A429: 5 logic frames a second)
const char kBannerman[] =
	"Object Port117Bannerman\n"
	"  KindOf = INFANTRY SELECTABLE HORDE_BANNER_CARRIER\n"
	"  Geometry = CYLINDER\n  GeometryMajorRadius = 8\n  GeometryMinorRadius = 8\n  GeometryHeight = 20\n"
	"  ArmorSet\n    Conditions = None\n    Armor = PlainArmor\n  End\n"
	"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
	"  Behavior = BannerCarrierUpdate ModuleTag_Banner\n"
	"    IdleSpawnRate = 2000\n"
	"    MeleeFreeUnitSpawnTime = 4000\n"
	"    DiedRespawnTime = 2000\n"
	"    MeleeFreeBannerReSpawnTime = 4000\n"
	"  End\n"
	"  Behavior = DestroyDie ModuleTag_Destroy\n  End\n"
	"  LocomotorSet\n    Locomotor = WalkerLoco\n    Condition = SET_NORMAL\n    Speed = 55\n  End\n"
	"End\n";

const int kIdleSpawnFrames = 10; // IdleSpawnRate = 2000 above

std::string port117Objects()
{
	return std::string("SpecialPower SpecialAbilityPort117Summon\n  Enum = SPECIAL_TOGGLE_MOUNTED\n  ReloadTime = 1000\nEnd\n") +
		"SpecialPower SpecialAbilityPort117Toggle\n  Enum = SPECIAL_TOGGLE_MOUNTED\n  ReloadTime = 1000\nEnd\n" +
		kBannerman +
		hordeIni("Port117Horde", "HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK", kFiveSoldiers) +
		// the same contain on an object that is not KindOf HORDE: RotWK's test is the KindOf bit (RW 0x7979DC), not the module
		hordeIni("Port117NotAHorde", "LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK", kFiveSoldiers) +
		// the cavalry hordes' contain (the wolf rider hordes of the mod's summoners name it). RotWK's HorseHordeContain is a HordeContain: its constructor
		// RW 0x879849 runs HordeContain's (RW 0x879854 -> RW 0x8727F9) and keeps the create interface (RW 0x879890 stores RW 0xC5B470, slot 1 = RW 0x8718ED);
		// slot 0x70 of its vtable RW 0xC5C288 is the same RW 0x871B9B (RW 0xC5C2F8)
		hordeIni("Port117RiderHorde", "HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK", kFiveSoldiers, "HorseHordeContain") +
		hordeIni("Port117BannerHorde", "HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK", kThreeSoldiersAndBanner) +
		hordeIni("Port117BannerAtOnceHorde", "HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK", kThreeSoldiersAndBannerAtOnce) +
		// a horde that turns into another horde: a horde whose ToggleMountedSpecialAbilityUpdate names a KindOf HORDE MountedTemplate (hordes of the mod
		// have this shape, PORT117_RW_ANALYSIS.md section 1)
		hordeIni("Port117TogglingHorde", "HORDE MELEE_HORDE LARGE_RECTANGLE_PATHFIND SELECTABLE CAN_ATTACK", kFiveSoldiers, "HordeContain", toggleModules("Port117Horde")) +
		summonerIni("Port117Summoner", "Port117Horde") +
		summonerIni("Port117BannerSummoner", "Port117BannerHorde");
}

// SUMMON-1's world (test_summon1.cpp): the combat world with the special power, FX list and attribute modifier stores the summon's modules read
struct Port117World : combattest::CombatWorld
{
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	FXListStore fxLists;
	FXListStore *savedFx = TheFXListStore;
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	Port117World()
	{
		TheSpecialPowerStore = &powers;
		TheFXListStore = &fxLists;
		TheAttributeModifierStore = &modifiers;
		modifiers.registerBlock(w.fx.env.blocks);
		w.fx.env.blocks.registerBlock("FXList", [](INI *ini) { ParseFXListDefinitionGlobal(ini); });
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		SpecialAbilityModules::registerAll(w.modules);
		SpecialPowerModules::registerAll(w.modules);
		const std::string err = w.load(port117Objects());
		REQUIRE_MESSAGE(err.empty(), err);
		combat().setAutoAcquireEnabled(false); // nobody fights in these tests: only the summon and the banner carrier act
	}
	~Port117World()
	{
		TheSpecialPowerStore = savedPowers;
		TheFXListStore = savedFx;
		TheAttributeModifierStore = savedModifiers;
	}
	const ThingTemplate &tmpl(const char *name)
	{
		const ThingTemplate *t = w.get(name);
		REQUIRE_MESSAGE(t, name);
		return *t;
	}
	std::vector<Object *> allOf(const char *name)
	{
		std::vector<Object *> out;
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == name)
			{
				out.push_back(o);
			}
		}
		return out;
	}
	// the horde's members of the payload template (the banner carrier is a member of the contain too)
	std::vector<Object *> soldiers(Object *horde)
	{
		std::vector<Object *> out;
		for (Object *m : membersOf(horde))
		{
			if (m->getTemplate()->getName() == "Swordsman")
			{
				out.push_back(m);
			}
		}
		return out;
	}
	// Starts the replacement of `owner` by its module of class Module (the module's initiate, RW 0x8B1FDA, as SUMMON-1's and HERO-2's tests start it) and
	// runs the logic one frame at a time until an object of `mounted` that was not there before exists: the caller looks at it in the frame it
	// appeared. `took` is how many frames that took; null after 40.
	template <class Module>
	Object *replaceThrough(Object &owner, const char *mounted, int &took)
	{
		Module *u = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : owner.modules())
		{
			if (auto *x = dynamic_cast<Module *>(m.get()))
			{
				u = x;
			}
		}
		REQUIRE(u);
		const std::vector<Object *> before = allOf(mounted);
		u->initiateIntentToDoSpecialPower(u->abilityData()->m_specialPowerTemplate, nullptr, nullptr, 0, 0);
		REQUIRE(u->isActive());
		for (took = 1; took <= 40; ++took)
		{
			frames(1);
			for (Object *o : allOf(mounted))
			{
				if (std::find(before.begin(), before.end(), o) == before.end())
				{
					return o;
				}
			}
		}
		return nullptr;
	}
	Object *summon(Object &summoner, const char *mounted, int &took)
	{
		return replaceThrough<SummonReplacementSpecialAbilityUpdate>(summoner, mounted, took);
	}
	// rank 2 for the horde: the fixture has no ExperienceLevel data, so the rank is set as the horde2 tests set it (Horde2TestUtil.h, RW 0x79D72A)
	void rankTwo(Object *horde)
	{
		REQUIRE(horde->getExperienceTracker());
		ExperienceTrackerTestAccess::setRank(*horde->getExperienceTracker(), 2);
	}
	// the update module of the horde's banner carrier (the carrier RW 0x8719E4 made for it)
	BannerCarrierUpdate *carrierOf(Object *horde)
	{
		HordeContain *c = hordeOf(horde);
		REQUIRE(c);
		Object *carrier = byId(c->bannerCarrier());
		REQUIRE(carrier);
		BannerCarrierUpdate *b = dynamic_cast<BannerCarrierUpdate *>(carrier->findModule("BannerCarrierUpdate"));
		REQUIRE(b);
		return b;
	}
};

// Kills `dead` soldiers of every horde in the same frame, then runs `frames` frames. For each horde: the frames (counted from the kill) in which its
// banner carrier's update added a member, one entry per member (two members in one frame are the same number twice).
std::vector<std::vector<int>> refills(Port117World &f, const std::vector<Object *> &hordes, const std::vector<BannerCarrierUpdate *> &carriers, int dead, int frames)
{
	std::vector<unsigned long long> seen;
	for (size_t i = 0; i < hordes.size(); ++i)
	{
		const std::vector<Object *> soldiers = f.soldiers(hordes[i]);
		REQUIRE((int)soldiers.size() == 3);
		for (int k = 0; k < dead; ++k)
		{
			soldiers[(size_t)k]->kill(DAMAGE_UNRESISTABLE, 0);
		}
		seen.push_back(carriers[i]->membersSpawned());
	}
	std::vector<std::vector<int>> out(hordes.size());
	for (int frame = 1; frame <= frames; ++frame)
	{
		f.frames(1);
		for (size_t i = 0; i < hordes.size(); ++i)
		{
			const unsigned long long now = carriers[i]->membersSpawned();
			for (unsigned long long k = seen[i]; k != now; ++k)
			{
				out[i].push_back(frame);
			}
			seen[i] = now;
		}
	}
	return out;
}
} // namespace

// RW 0x8B2302 -> RW 0x797796: the summoner's MountedTemplate is a KindOf HORDE object, so it is completed with no producer (RW 0x7979E8) and its HordeContain
// makes the whole InitialPayload inside that call (RW 0x871BBB -> RW 0x86A1FA); the summoner is its producer again when the call returns (RW 0x797A02).
// Without the fix the horde appeared with no member at all.
TEST_CASE("PORT-117 summon: the summoner's horde stands with its whole InitialPayload in the frame it appears (RW 0x7979E8 / 0x7979EF / 0x797A02)")
{
	Port117World f;
	Object *summoner = f.unit("Port117Summoner", 'A', 300, 300);
	REQUIRE(summoner);
	f.frames(2);
	const ObjectID summonerId = summoner->getID();
	int frames = 0;
	Object *horde = f.summon(*summoner, "Port117Horde", frames);
	REQUIRE(horde);
	MESSAGE("PORT-117: the summoned horde appeared " << frames << " frames after the initiate");
	const ObjectID hordeId = horde->getID();
	// the frame of the replacement: InitialPayload = Swordsman 5, every one of them made by the one call
	const std::vector<Object *> members = f.membersOf(horde);
	CHECK(members.size() == 5u);
	for (Object *m : members)
	{
		CHECK(m->getTemplate()->getName() == "Swordsman");
		CHECK(m->getContainedBy() == horde);
	}
	CHECK(horde->getProducerID() == summonerId); // RW 0x797A02: the producer is back after the payload
	// and nothing arrives later: the payload is made once, and a horde without BannerCarriersAllowed gets no carrier (RW 0x870233)
	f.frames(30);
	REQUIRE(f.byId(hordeId) == horde);
	CHECK(f.membersOf(horde).size() == 5u);
	CHECK(f.soldiers(horde).size() == 5u);
}

// RW 0x797796 itself, without the summon around it. The KindOf bit decides (RW 0x7979DC), not the contain module: the same HordeContain on an object that
// is not KindOf HORDE keeps its producer through onBuildComplete and makes no payload (RW 0x871BAB). The call without a builder is the port's own: RotWK reads
// the builder unchecked (RW 0x7977CC), so no retail call passes NULL. It must stay what it was before the fix: no producer (the store of RW 0x68B6A1 for
// NULL) and the payload made (RW 0x871BAB falls through); the two instructions are RotWK's, the NULL argument is not.
TEST_CASE("PORT-117 build: buildObjectNow completes a KindOf HORDE object without its producer and gives the builder back (RW 0x7979D4 .. 0x797A02)")
{
	Port117World f;
	Object *builder = f.unit("Swordsman", 'A', 200, 200);
	REQUIRE(builder);
	Player *owner = f.playerOf('A');
	REQUIRE(owner);

	Object *horde = Construction::buildObjectNow(*f.logic, builder, f.tmpl("Port117Horde"), Coord3D{ 500.0f, 500.0f, 0.0f }, 0.0f, *owner);
	REQUIRE(horde);
	CHECK(f.soldiers(horde).size() == 5u);                 // RW 0x7979E8: no producer while RW 0x8718ED ran
	CHECK(horde->getProducerID() == builder->getID());     // RW 0x797A02

	// the cavalry hordes' HorseHordeContain makes its payload in the same slot (RW 0xC5C2F8 = RW 0x871B9B, reached from RW 0x8718ED): whole at once too
	Object *riders = Construction::buildObjectNow(*f.logic, builder, f.tmpl("Port117RiderHorde"), Coord3D{ 1100.0f, 500.0f, 0.0f }, 0.0f, *owner);
	REQUIRE(riders);
	CHECK(riders->findModule("HorseHordeContain") != nullptr);
	CHECK(f.soldiers(riders).size() == 5u);
	CHECK(riders->getProducerID() == builder->getID());

	// the port's own case (no RotWK call has a NULL builder, RW 0x7977CC): the same before and after the fix
	Object *unbuilt = Construction::buildObjectNow(*f.logic, nullptr, f.tmpl("Port117Horde"), Coord3D{ 800.0f, 500.0f, 0.0f }, 0.0f, *owner);
	REQUIRE(unbuilt);
	CHECK(f.soldiers(unbuilt).size() == 5u);
	CHECK(unbuilt->getProducerID() == (ObjectID)INVALID_ID); // RW 0x68B6A1 with NULL stores 0

	Object *notAHorde = Construction::buildObjectNow(*f.logic, builder, f.tmpl("Port117NotAHorde"), Coord3D{ 500.0f, 800.0f, 0.0f }, 0.0f, *owner);
	REQUIRE(notAHorde);
	CHECK(f.membersOf(notAHorde).empty());                 // RW 0x7979E2 skips the clear, RW 0x871BAB .. 0x871BB3 then skips the payload
	CHECK(notAHorde->getProducerID() == builder->getID()); // RW 0x7978B9

	Object *soldier = Construction::buildObjectNow(*f.logic, builder, f.tmpl("Swordsman"), Coord3D{ 800.0f, 800.0f, 0.0f }, 0.0f, *owner);
	REQUIRE(soldier);
	CHECK(soldier->getProducerID() == builder->getID());   // RW 0x7978B9: any other object keeps the builder as its producer throughout
}

// The other caller that brings a horde in the mod's data: ToggleMountedSpecialAbilityUpdate's replacement (RW 0x8B140D) builds its MountedTemplate through the
// same slot of the build assistant (RW 0x8B146C .. 0x8B1483: slot 0x38 of the vtable of RW 0xDE8200 = RW 0x797796). A horde that turns into another horde
// this way has the new horde whole when the call returns (RW 0x7979E8 -> RW 0x86A1FA) and is its producer (RW 0x797A02). Without the fix the new horde came
// empty, and one without BannerCarriersAllowed stayed empty: no carrier is made for it (RW 0x870233). What becomes of the old horde and its soldiers is not
// this test's subject (RotWK destroys the old object after its packing, RW 0x8B1EE0; what its HordeContain then does was not read): it is printed only.
TEST_CASE("PORT-117 toggle: the horde ToggleMountedSpecialAbilityUpdate's replacement builds stands whole too (RW 0x8B1483 -> RW 0x797796)")
{
	Port117World f;
	Object *old = f.unit("Port117TogglingHorde", 'A', 300, 300);
	REQUIRE(old);
	f.frames(2);
	REQUIRE(f.soldiers(old).size() == 5u); // the test's own horde: completed with no producer, so it has its payload (RW 0x871BBB)
	const ObjectID oldId = old->getID();
	const size_t soldiersBefore = f.allOf("Swordsman").size();
	int frames = 0;
	Object *horde = f.replaceThrough<ToggleMountedSpecialAbilityUpdate>(*old, "Port117Horde", frames);
	REQUIRE(horde);
	MESSAGE("PORT-117: the toggled horde appeared " << frames << " frame(s) after the initiate");
	const ObjectID hordeId = horde->getID();
	CHECK(f.soldiers(horde).size() == 5u);  // RW 0x7979E8: no producer while RW 0x8718ED ran
	CHECK(horde->getProducerID() == oldId); // RW 0x797A02
	// nothing arrives later: this horde has no BannerCarriersAllowed (RW 0x870233)
	f.frames(30);
	REQUIRE(f.byId(hordeId) == horde);
	CHECK(f.soldiers(horde).size() == 5u);
	const std::string oldHorde = f.byId(oldId) ? "is still in the world" : "is gone";
	MESSAGE("PORT-117: 30 frames after the toggle the old horde " << oldHorde << "; soldiers in the world: " << f.allOf("Swordsman").size() << " ("
	        << soldiersBefore << " before the toggle)");
}

// The neighbour the fix must not move: HordeContain's own guard. A horde that has a producer when it is completed (the order a factory's production
// uses: its members are produced one by one after it) makes no payload (RW 0x871BAB .. 0x871BB3). The fix clears the producer in buildObjectNow only.
TEST_CASE("PORT-117 neighbour: a horde completed while it has a producer still makes no payload (RW 0x871BAB)")
{
	Port117World f;
	Object *maker = f.unit("Swordsman", 'A', 200, 200);
	REQUIRE(maker);
	Object *horde = f.logic->newObject(&f.tmpl("Port117Horde"), f.teamA(), ObjectStatusMaskType{});
	REQUIRE(horde);
	horde->setProducer(maker);
	const Coord3D at{ 600.0f, 600.0f, 0.0f };
	horde->setPosition(&at);
	horde->friend_onBuildComplete(); // RW 0x68D252 -> RW 0x8718ED with Object + 0x78 set
	CHECK(f.membersOf(horde).empty());
	CHECK(horde->getProducerID() == maker->getID());
}

// The other neighbour: the banner carrier's refill of a damaged horde. RW 0x89ACE4 adds at most one member per run (RW 0x89AE61) and sleeps IdleSpawnRate
// after every run (RW 0x89AE67), so two missing soldiers come back IdleSpawnRate apart, never together. The horde here is not summoned: this is the
// timing as it was before the fix.
TEST_CASE("PORT-117 replenish: a damaged horde's banner carrier brings back one member per IdleSpawnRate (RW 0x89ACE4 / 0x89AE67)")
{
	Port117World f;
	Object *horde = f.unit("Port117BannerHorde", 'A', 700, 700);
	REQUIRE(horde);
	f.frames(2);
	REQUIRE(f.soldiers(horde).size() == 3u);
	f.rankTwo(horde); // BannerCarrierMinLevel 1 is below it (RW 0x871A1B): RW 0x8719E4 makes the carrier
	f.frames(2);
	BannerCarrierUpdate *carrier = f.carrierOf(horde);
	CHECK(f.membersOf(horde).size() == 4u); // three soldiers and the carrier
	f.frames(30);                          // the carrier walks to its place and stands (a moving carrier adds nobody, RW 0x89AD76)
	const std::vector<std::vector<int>> back = refills(f, { horde }, { carrier }, 2, 60);
	REQUIRE(back[0].size() == 2);
	MESSAGE("PORT-117: the plain horde's two soldiers came back " << back[0][0] << " and " << back[0][1] << " frames after they died");
	CHECK(back[0][1] - back[0][0] == kIdleSpawnFrames);
	CHECK(f.soldiers(horde).size() == 3u);
	CHECK(f.membersOf(horde).size() == 4u);
}

// The summoned horde's refill is that same one: it starts whole (the fix), and when it is damaged its carrier brings the soldiers back in the very frames
// the carrier of a horde that was never summoned does, one per IdleSpawnRate. Both hordes get their carrier in the same frame and lose two soldiers in
// the same frame, so the two carriers' updates run on the same frames.
TEST_CASE("PORT-117 replenish: a damaged summoned horde is refilled in the same frames as a horde that was not summoned")
{
	Port117World f;
	Object *plain = f.unit("Port117BannerHorde", 'A', 700, 700);
	Object *summoner = f.unit("Port117BannerSummoner", 'A', 300, 300);
	REQUIRE(plain);
	REQUIRE(summoner);
	f.frames(2);
	int frames = 0;
	Object *summoned = f.summon(*summoner, "Port117BannerHorde", frames);
	REQUIRE(summoned);
	REQUIRE(summoned != plain);
	REQUIRE(f.soldiers(summoned).size() == 3u); // whole in the frame it appears (RW 0x7979E8): nothing is left for the carrier to bring
	REQUIRE(f.soldiers(plain).size() == 3u);
	f.frames(10);
	// rank 2 in the same frame for both: BannerCarrierMinLevel 1 is below it (RW 0x871A1B) and RW 0x8719E4 makes each horde its carrier
	f.rankTwo(plain);
	f.rankTwo(summoned);
	f.frames(2);
	BannerCarrierUpdate *plainCarrier = f.carrierOf(plain);
	BannerCarrierUpdate *summonedCarrier = f.carrierOf(summoned);
	CHECK(f.membersOf(summoned).size() == 4u);
	CHECK(f.membersOf(plain).size() == 4u);
	CHECK(summonedCarrier->membersSpawned() == 0u); // the carrier of a whole horde has made nobody
	f.frames(30);
	const std::vector<std::vector<int>> back = refills(f, { plain, summoned }, { plainCarrier, summonedCarrier }, 2, 60);
	REQUIRE(back[0].size() == 2);
	REQUIRE(back[1].size() == 2);
	MESSAGE("PORT-117: plain horde refilled at +" << back[0][0] << " and +" << back[0][1] << ", summoned horde at +" << back[1][0] << " and +" << back[1][1]);
	CHECK(back[1][1] - back[1][0] == kIdleSpawnFrames); // one member per IdleSpawnRate (RW 0x89AE67), not both at once
	CHECK(back[1][0] == back[0][0]);                    // the summon changed nothing in the refill: the same frames as the plain horde
	CHECK(back[1][1] == back[0][1]);
	CHECK(f.soldiers(summoned).size() == 3u);
	CHECK(f.soldiers(plain).size() == 3u);
}

// What stays unported is reported, not hidden (docs/STOPS.md, S-7317). RotWK's HordeContain makes the banner carrier in the SAME call as the payload when the
// horde has no producer and its BannerCarrierMinLevel is 0 (RW 0x8718F5 .. 0x87190E -> RW 0x870204); buildObjectNow clears the producer for that call
// (RW 0x7979E8), so a summoned horde with that level has its carrier when the call returns. The port's carrier comes from the horde's update
// (HordeContain::bannerCheck, RW 0x8719E4, S-587), after the call. The stop must be in the game's live report and have its row; how many frames late the
// port's carrier is, is printed for that row and not asserted (it is the port's gap, not RotWK's behaviour).
TEST_CASE("PORT-117 stop S-7317: the banner carrier RotWK makes at build complete (RW 0x8718F5 -> 0x870204) is reported as not ported")
{
	Port117World f;
	bool reported = false;
	for (const std::string &line : f.logic->report().stops)
	{
		reported = reported || line.rfind("[S-7317]", 0) == 0;
	}
	CHECK(reported);
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE_MESSAGE(static_cast<bool>(in), "cannot read docs/STOPS.md (OPENBFME_DOCS_DIR = " << OPENBFME_DOCS_DIR << ")");
	std::stringstream doc;
	doc << in.rdbuf();
	CHECK(doc.str().find("| S-7317 | code |") != std::string::npos);

	Object *builder = f.unit("Swordsman", 'A', 200, 200);
	REQUIRE(builder);
	Player *owner = f.playerOf('A');
	REQUIRE(owner);
	Object *horde = Construction::buildObjectNow(*f.logic, builder, f.tmpl("Port117BannerAtOnceHorde"), Coord3D{ 500.0f, 500.0f, 0.0f }, 0.0f, *owner);
	REQUIRE(horde);
	CHECK(f.soldiers(horde).size() == 3u); // the ported half: the payload is made in the call (RW 0x7979E8, RW 0x86A1FA)
	// a horde of the game starts at rank 1 (its ExperienceLevel data; this fixture has none: Horde2TestUtil.h), which BannerCarrierMinLevel 0 is below
	REQUIRE(horde->getExperienceTracker());
	ExperienceTrackerTestAccess::setRank(*horde->getExperienceTracker(), 1);
	HordeContain *contain = f.hordeOf(horde);
	REQUIRE(contain);
	int late = 0;
	while (late != 20 && contain->bannerCarrier() == 0)
	{
		f.frames(1);
		++late;
	}
	MESSAGE("PORT-117 / S-7317: the port's banner carrier of a BannerCarrierMinLevel 0 horde exists " << late
	        << " logic frame(s) after buildObjectNow (20 = never in 20; RotWK: in the call itself, RW 0x87190E)");
}
