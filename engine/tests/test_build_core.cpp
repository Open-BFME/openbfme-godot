// OpenBFME unit tests (lane BUILD-1): construction on a synthetic base: a build plot (FoundationAIUpdate + CastleMemberBehavior), a building (GettingBuiltBehavior) and a castle
// centre (CastleBehavior) with a layout of the CastleTemplateStore. The numbers come from the disassembly of RotWK (caveat S-001): the foundation creation RW 0x858701 (the price
// at placement, percent 0 and the model conditions, BuildVariation), the completion (RW 0x6AA7A8 -> 0x68E0C2: the command points join at completion), the castle's transform of an
// entry (RW 0x7987EE: position = castle matrix * entry, angle sum normalised) and the footprint tests of ZH's BuildAssistant.  GPL-3.0.

#include "Common/Audio/AudioEntryPoints.h"
#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/StateHash.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Damage.h"
#include "GameLogic/BuildCommands.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ConstructionModules.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>

using namespace prodtest;

namespace
{
const char kBuildObjects[] =
	"Object Hall\n"
	"  BuildCost = 300\n"
	"  BuildTime = 20.0\n"
	"  CommandPointBonus = 50\n"
	"  KindOf = STRUCTURE SELECTABLE NEED_BASE_FOUNDATION\n"
	"  Behavior = GettingBuiltBehavior ModuleTag_GB\n"
	"    SpawnTimer = 300\n"
	"  End\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 1000\n"
	"    MaxHealthDamaged = 500\n"
	"    MaxHealthReallyDamaged = 100\n"
	"  End\n"
	"End\n"
	"Object Plot\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE BASE_FOUNDATION\n"
	"  CommandSet = PlotSet\n"
	"  Behavior = FoundationAIUpdate ModuleTag_FA\n"
	"    BuildVariation = 2\n"
	"  End\n"
	"  Behavior = CastleMemberBehavior ModuleTag_CM\n"
	"  End\n"
	"End\n"
	"Object Keep\n"
	"  KindOf = STRUCTURE SELECTABLE IMMOBILE COMMANDCENTER\n"
	"End\n"
	"Object Rock\n"
	"  KindOf = IMMOBILE\n"
	"End\n"
	"Object AlphaCastle\n"
	"  KindOf = IMMOBILE CASTLE_CENTER\n"
	"  Behavior = CastleBehavior ModuleTag_castle\n"
	"    CastleToUnpackForFaction = Alpha Base_Alpha\n"
	"    CastleToUnpackForFaction = Beta Base_Beta\n"
	"    FilterValidOwnedEntries = ANY +STRUCTURE\n"
	"    InstantUnpack = Yes\n"
	"  End\n"
	"End\n";
const char kBuildCommands[] =
	"CommandButton Command_BuildHall\n  Command = FOUNDATION_CONSTRUCT\n  Object = Hall\nEnd\n"
	"CommandSet PlotSet\n  1 = Command_BuildHall\nEnd\n";

struct BuildFx : ProdWorld
{
	Object *plot = nullptr;
	GameLogicDispatch dispatch;
	BuildCommands commands;
	BuildFx() : dispatch(*logic)
	{
		load(kBuildObjects);
		load(kBuildCommands);
		logic->settings().buildRulesLoaded = true;
		dispatch.registerHandler(MSG_QUEUE_UPGRADE, "test", [](GameLogic &, const GameMessage &) { return true; }); // (not used: keeps the handler list non-trivial)
		commands.registerHandlers(dispatch);
		plot = make("Plot", teamOf("Alice"));
		REQUIRE(plot != nullptr);
		Coord3D p{ 500.0f, 600.0f, 0.0f };
		plot->setPosition(&p);
		plot->setOrientation(0.25f);
	}
	void select(Object *o)
	{
		alice()->selection().clear();
		alice()->selection().push_back(o->getID());
	}
	GameMessage construct(const char *what, const Coord3D &where)
	{
		GameMessage m(MSG_FOUNDATION_CONSTRUCT, alice()->getPlayerIndex());
		m.appendIntegerArgument((int)w.get(what)->getTemplateID());
		m.appendLocationArgument(where);
		m.appendRealArgument(0.0f);
		return m;
	}
	Object *find(const char *name)
	{
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == name)
			{
				return o;
			}
		}
		return nullptr;
	}
};
} // namespace

TEST_CASE("construction: a plot's MSG_FOUNDATION_CONSTRUCT pays at placement, the foundation rises over BuildTime frames and the command points join at completion")
{
	BuildFx f;
	const ThingTemplate *hall = f.w.get("Hall");
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	const int limit0 = f.alice()->commandPointLimit();
	f.select(f.plot);
	REQUIRE(BuildAssistant::canMakeUnit(*f.plot, hall, -1) == CANMAKE_OK);
	CHECK(BuildAssistant::calcTimeToBuild(*hall, f.alice(), f.plot, -1, f.logic->productionSettings(), *f.logic) == 100); // 20 s = 100 frames
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	Object *building = f.find("Hall");
	REQUIRE(building != nullptr);
	CHECK(f.commands.stats().foundations == 1);
	CHECK(f.alice()->getMoney()->countMoney() == money - 300);
	CHECK(building->getBuildCostPaid() == 300.0f);
	CHECK(building->isUnderConstruction());
	CHECK(building->getConstructionPercent() == 0.0f);
	CHECK(building->getProducerID() == f.plot->getID());
	// the pad's BuildVariation 2 and the angle of NEED_BASE_FOUNDATION (the plot's orientation + PlacementViewAngle 0)
	CHECK(building->testModelCondition(Construction::modelConditionIndex("BUILD_VARIATION_TWO")));
	CHECK(building->testModelCondition(Construction::modelConditionIndex("PARTIALLY_CONSTRUCTED")));
	CHECK(building->testModelCondition(Construction::modelConditionIndex("ACTIVELY_BEING_CONSTRUCTED")));
	CHECK_FALSE(building->testModelCondition(Construction::modelConditionIndex("AWAITING_CONSTRUCTION")));
	CHECK(building->getOrientation() == doctest::Approx(0.25f));
	CHECK(f.plot->testModelCondition(Construction::modelConditionIndex("CONSTRUCTION_COMPLETE"))); // the pad is drawn away
	CHECK(f.alice()->commandPointLimit() == limit0 + 50); // retail counts at birth for a plot's building (the status is set after newObject): see S-302
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(building->findModule("GettingBuiltBehavior"));
	REQUIRE(gb != nullptr);
	// lane BUILD-2: the foundation's body starts at 1.0 health (RW 0x85895D) and the module has not started yet (it builds itself on its first update: no WorkerName)
	CHECK(building->getBodyModule()->getHealth() == 1.0f);
	CHECK(gb->buildFrames() == 0u);
	CHECK_FALSE(gb->isConstructing());
	// a second order on the same plot is refused and costs nothing
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	CHECK(f.commands.stats().foundations == 1);
	CHECK(f.commands.refusals().size() == 1);
	CHECK(f.alice()->getMoney()->countMoney() == money - 300);
	// the rise is the body's health: frame 1 starts the self-build (calcTimeToBuild = 100 frames, the percent = 1 / 1000 * 100), the update sleeps 5 frames, then every
	// frame heals 1000 / 100 = 10 and the percent follows the health; the 100th heal reaches 1000 health = 100 % and completes it (frame 6 + 99 = 105)
	int frames = 0, heals = 0;
	float lastHealth = building->getBodyModule()->getHealth();
	while (building->isUnderConstruction() && frames < 300)
	{
		f.logic->runLogicFrame();
		++frames;
		const float h = building->getBodyModule()->getHealth();
		if (h != lastHealth)
		{
			++heals;
			CHECK(h == doctest::Approx(lastHealth + 10.0f > 1000.0f ? 1000.0f : lastHealth + 10.0f));
			if (building->isUnderConstruction())
			{
				CHECK(building->getConstructionPercent() == h / 1000.0f * 100.0f);
			}
			lastHealth = h;
		}
		if (frames == 1)
		{
			CHECK(gb->isConstructing());
			CHECK(gb->buildFrames() == 100u);
			CHECK(building->getConstructionPercent() == 0.1f);
			CHECK(building->getBuilderID() == building->getID());
		}
	}
	CHECK(heals == 100);
	CHECK(frames == 105);
	CHECK(building->getBodyModule()->getHealth() == 1000.0f);
	CHECK(gb->completedOnce());
	CHECK(gb->buildFrames() == 300u); // the next self-repair lasts 5 * RebuildTimeSeconds (default 60) frames (RW 0x8574AE)
	CHECK(building->getBuilderID() == INVALID_ID);
	CHECK(building->getConstructionPercent() == -1.0f);
	// RW 0x856992 clears AWAITING / PARTIALLY / ACTIVELY and sets nothing (no CONSTRUCTION_COMPLETE: that is BUILD-1's dozer-path completion, S-650)
	CHECK_FALSE(building->testModelCondition(Construction::modelConditionIndex("PARTIALLY_CONSTRUCTED")));
	CHECK_FALSE(building->testModelCondition(Construction::modelConditionIndex("ACTIVELY_BEING_CONSTRUCTED")));
	CHECK(building->isCountedInCommandPoints());
	CHECK(f.alice()->commandPointLimit() == limit0 + 50);
}

TEST_CASE("construction: MSG_DOZER_CANCEL_CONSTRUCT refunds the price paid, destroys the foundation and frees the plot")
{
	BuildFx f;
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	f.select(f.plot);
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	Object *building = f.find("Hall");
	REQUIRE(building != nullptr);
	for (int i = 0; i < 20; ++i)
	{
		f.logic->runLogicFrame();
	}
	f.select(building);
	f.dispatch.dispatch(GameMessage(MSG_DOZER_CANCEL_CONSTRUCT, f.alice()->getPlayerIndex()));
	CHECK(f.commands.stats().cancels == 1);
	CHECK(f.alice()->getMoney()->countMoney() == money);
	f.logic->runLogicFrame();
	CHECK(f.find("Hall") == nullptr);
	CHECK_FALSE(f.plot->testModelCondition(Construction::modelConditionIndex("CONSTRUCTION_COMPLETE")));
	// the plot takes the next order
	f.select(f.plot);
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	CHECK(f.commands.stats().foundations == 2);
}

TEST_CASE("construction: the same commands twice give the same frame hashes, and construction state is hashed")
{
	std::vector<std::uint32_t> a, b;
	for (std::vector<std::uint32_t> *out : { &a, &b })
	{
		BuildFx f;
		f.select(f.plot);
		out->push_back(f.logic->computeStateHash());
		f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
		for (int i = 0; i < 30; ++i)
		{
			f.logic->runLogicFrame();
			out->push_back(f.logic->computeStateHash());
		}
	}
	CHECK(a == b);
	CHECK(a[1] != a[0]);
	CHECK(a[10] != a[9]); // the percent moves every frame: it is in the hash
}

TEST_CASE("castle: the instant unpack applies the castle's transform to every layout entry and gives the owner the entries its filter allows")
{
	BuildFx f;
	CastleTemplate layout;
	layout.name = "Base_Alpha";
	layout.version = 5;
	layout.entries.push_back({ "", "Keep", 0.0f, 0.0f, 0.0f, 0.0f, 40, 1 });
	layout.entries.push_back({ "", "Plot", 50.0f, 0.0f, 0.0f, 1.0f, 40, 1 });
	layout.entries.push_back({ "", "Rock", 0.0f, 80.0f, 0.0f, 0.0f, 40, 1 }); // not a STRUCTURE: the filter gives it to the neutral player
	layout.entries.push_back({ "", "NoSuchTemplate", 5.0f, 5.0f, 0.0f, 0.0f, 40, 1 });
	f.logic->castleTemplates().add(layout);
	Object *castle = f.make("AlphaCastle", f.teamOf("Alice"));
	REQUIRE(castle != nullptr);
	Coord3D at{ 1000.0f, 2000.0f, 5.0f };
	castle->setPosition(&at);
	castle->setOrientation(0.5f);
	castle->friend_onBuildComplete(); // the object is complete at birth: InstantUnpack sets m_needInstantBuild (RW 0x798238)
	CastleBehavior *cb = dynamic_cast<CastleBehavior *>(castle->findModule("CastleBehavior"));
	REQUIRE(cb != nullptr);
	CHECK(cb->needsInstantBuild());
	CHECK(cb->state() == CastleBehavior::STATE_IDLE);
	CHECK(castle->testModelCondition(Construction::modelConditionIndex("INVULNERABLE")));
	CHECK(castle->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNATTACKABLE")));
	f.logic->runLogicFrame(); // the castle's first update does the unpack (RW 0x79CF2A state 0 -> 0x79C265)
	CHECK(cb->state() == CastleBehavior::STATE_UNPACKED);
	CHECK_FALSE(cb->needsInstantBuild());
	CHECK(castle->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE")));
	CHECK(castle->testModelCondition(Construction::modelConditionIndex("JUST_BUILT")));
	CHECK(cb->lastError().empty());
	// three entries became objects (the fourth names no template: reported, not made)
	REQUIRE(cb->ownedObjects().size() == 3);
	const float c = std::cos(0.5f), s = std::sin(0.5f);
	Object *keep = f.logic->findObjectByID(cb->ownedObjects()[0]);
	Object *plot = f.logic->findObjectByID(cb->ownedObjects()[1]);
	Object *rock = f.logic->findObjectByID(cb->ownedObjects()[2]);
	REQUIRE(keep);
	REQUIRE(plot);
	REQUIRE(rock);
	CHECK(keep->getTemplate()->getName() == "Keep");
	CHECK(keep->getPosition()->x == doctest::Approx(1000.0f));
	CHECK(keep->getPosition()->z == doctest::Approx(5.0f));
	CHECK(plot->getPosition()->x == doctest::Approx(1000.0f + 50.0f * c).epsilon(1e-5));
	CHECK(plot->getPosition()->y == doctest::Approx(2000.0f + 50.0f * s).epsilon(1e-5));
	CHECK(plot->getOrientation() == doctest::Approx(1.5f)); // 0.5 + 1.0
	CHECK(rock->getPosition()->x == doctest::Approx(1000.0f - 80.0f * s).epsilon(1e-5));
	CHECK(rock->getPosition()->y == doctest::Approx(2000.0f + 80.0f * c).epsilon(1e-5));
	CHECK(keep->getControllingPlayer() == f.alice());
	CHECK(plot->getControllingPlayer() == f.alice());
	CHECK(rock->getControllingPlayer() == f.players.getNeutralPlayer());
	CastleMemberBehavior *member = dynamic_cast<CastleMemberBehavior *>(plot->findModule("CastleMemberBehavior"));
	REQUIRE(member != nullptr);
	CHECK(member->castleId() == castle->getID());
	CHECK(f.logic->report().errors.size() == 1); // the unknown template
	// a faction without an entry of its own is an error, never a default layout
	Object *other = f.make("AlphaCastle", f.teamOf("Bob"));
	other->friend_onBuildComplete();
	f.logic->runLogicFrame();
	CastleBehavior *cb2 = dynamic_cast<CastleBehavior *>(other->findModule("CastleBehavior"));
	CHECK(cb2->ownedObjects().empty());
	const GameLogic::Report rep = f.logic->report();
	REQUIRE(rep.errors.size() == 2); // + the layout 'Base_Beta' that no loader can read
	CHECK(rep.errors[1].find("Base_Beta") != std::string::npos);
	CHECK(cb2->lastError().find("Base_Beta") != std::string::npos);
}

TEST_CASE("placement: footprints collide as oriented rectangles and discs (ZH geomCollidesWithGeom, footprint only)")
{
	using BuildPlacement::Footprint;
	using BuildPlacement::footprintsCollide;
	Footprint a;
	a.type = 0;
	a.major = 10.0f;
	a.minor = 5.0f;
	Footprint b = a;
	b.x = 19.0f; // 10 + 10 = 20 > 19: overlap along the length
	CHECK(footprintsCollide(a, b));
	b.x = 21.0f;
	CHECK_FALSE(footprintsCollide(a, b));
	// turned a quarter: the length now points across, so the same offset on x overlaps (half length 10 along y, half width 5 along x -> reach 5 + 10)
	b.x = 14.0f;
	b.angle = 1.5707964f;
	CHECK(footprintsCollide(a, b)); // a reaches 10, b's width reaches 5 from x = 14: 14 - 5 = 9 < 10
	b.x = 16.0f;
	CHECK_FALSE(footprintsCollide(a, b)); // 16 - 5 = 11 > 10
	// a disc against a box
	Footprint d;
	d.type = 2;
	d.major = 4.0f;
	d.x = 13.0f;
	d.y = 0.0f;
	CHECK(footprintsCollide(a, d)); // the box ends at 10, the disc starts at 9
	d.x = 15.0f;
	CHECK_FALSE(footprintsCollide(a, d));
	d.x = 13.0f;
	d.y = 8.0f; // the corner (10, 5) is 3 on x and 3 on y away: 4.24 > 4
	CHECK_FALSE(footprintsCollide(a, d));
	d.y = 7.0f; // 3 on x and 2 on y: 3.6 < 4
	CHECK(footprintsCollide(a, d));
	// two discs
	Footprint e;
	e.type = 1;
	e.major = 3.0f;
	e.x = 5.5f;
	Footprint g;
	g.type = 2;
	g.major = 3.0f;
	CHECK(footprintsCollide(e, g));
	e.x = 6.5f;
	CHECK_FALSE(footprintsCollide(e, g));
}

TEST_CASE("stops S-300 .. S-307 and S-650 .. S-657: every construction stop is in the live report, and a PreBuiltList castle notes its stop at runtime")
{
	BuildFx f;
	const GameLogic::Report before = f.logic->report();
	std::vector<int> ids;
	for (int id = 300; id <= 307; ++id)
	{
		ids.push_back(id);
	}
	for (int id = 650; id <= 657; ++id)
	{
		ids.push_back(id); // lane BUILD-2
	}
	for (int id : ids)
	{
		const std::string tag = "[S-" + std::to_string(id) + "]";
		bool found = false;
		for (const std::string &line : before.stops)
		{
			found = found || line.rfind(tag, 0) == 0;
		}
		INFO(tag);
		CHECK(found);
	}
	// a castle with a PreBuiltList: the layout is unpacked, the unported pre-built structures are noted as a stop (not an error)
	f.load("Object PrebuiltCastle\n  KindOf = IMMOBILE CASTLE_CENTER\n  Behavior = CastleBehavior ModuleTag_castle\n    CastleToUnpackForFaction = Alpha Base_Alpha\n"
				   "    PreBuiltList = Hall 1\n    PreBuiltPlyr = Alice\n    InstantUnpack = Yes\n  End\nEnd\n");
	CastleTemplate layout;
	layout.name = "Base_Alpha";
	layout.version = 5;
	f.logic->castleTemplates().add(layout);
	Object *castle = f.make("PrebuiltCastle", f.teamOf("Alice"));
	castle->friend_onBuildComplete();
	f.logic->runLogicFrame();
	const GameLogic::Report after = f.logic->report();
	CHECK(after.errors.empty());
	bool noted = false;
	for (const std::string &line : after.stops)
	{
		noted = noted || line.find("PreBuiltList") != std::string::npos;
	}
	CHECK(noted);
}

// ---- lane BUILD-2: the health-driven construction of RotWK's GettingBuiltBehavior (RW 0x857E77) ----------------------------------------------------------------------
namespace
{
void hit(Object &o, float amount)
{
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_amount = amount;
	info.m_input.m_sourceID = 0;
	o.attemptDamage(info);
}
} // namespace

TEST_CASE("build rate: damage while a structure builds itself sets the percent back, the build pauses for 4 seconds after the last hit, then resumes from the health")
{
	BuildFx f;
	f.select(f.plot);
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	Object *building = f.find("Hall");
	REQUIRE(building != nullptr);
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(building->findModule("GettingBuiltBehavior"));
	REQUIRE(gb != nullptr);
	for (int i = 0; i < 30; ++i)
	{
		f.logic->runLogicFrame();
	}
	// frame 1 starts it, frames 6 .. 30 heal 25 times: 1 + 250 health
	CHECK(building->getBodyModule()->getHealth() == 251.0f);
	CHECK(building->getConstructionPercent() == doctest::Approx(25.1f).epsilon(1e-6));
	const UnsignedInt hitFrame = f.logic->getFrame();
	hit(*building, 100.0f);
	CHECK(building->getBodyModule()->getHealth() == 151.0f);
	f.logic->runLogicFrame();
	// the update saw the hit (RW 0x68C933: within 4 * 5 frames): the self-build stops (RW 0x856644: UNDER_CONSTRUCTION and the build model conditions go) and sleeps a second
	CHECK_FALSE(gb->isConstructing());
	CHECK_FALSE(building->isUnderConstruction());
	CHECK_FALSE(building->testModelCondition(Construction::modelConditionIndex("ACTIVELY_BEING_CONSTRUCTED")));
	CHECK(building->getConstructionPercent() == doctest::Approx(25.1f).epsilon(1e-6)); // the percent is only recomputed by a heal or a restart
	UnsignedInt restartFrame = 0;
	for (int i = 0; i < 60 && !restartFrame; ++i)
	{
		f.logic->runLogicFrame();
		if (gb->isConstructing())
		{
			restartFrame = f.logic->getFrame();
		}
	}
	REQUIRE(restartFrame != 0u);
	CHECK(restartFrame > hitFrame + 20u); // not while the hit is recent
	CHECK(restartFrame <= hitFrame + 26u); // the sleeping update checks every 5 frames
	CHECK(building->getConstructionPercent() == doctest::Approx(15.1f).epsilon(1e-6)); // the restart takes the percent from the health (RW 0x8568A3)
	CHECK(building->isUnderConstruction());
	CHECK(gb->buildFrames() == 100u); // the first build's length is kept
	int frames = 0;
	while (building->getConstructionPercent() != -1.0f && frames < 400)
	{
		f.logic->runLogicFrame();
		++frames;
	}
	CHECK(building->getConstructionPercent() == -1.0f);
	CHECK(building->getBodyModule()->getHealth() == 1000.0f);
	CHECK(gb->completedOnce());
	// 151 -> 1000 at 10 per frame: 85 heals after the restart's own sleep of up to 5 frames
	CHECK(frames >= 85);
	CHECK(frames <= 91);
}

TEST_CASE("build rate: a finished structure without a worker repairs itself 4 seconds after the last hit, over 5 * RebuildTimeSeconds frames, for free")
{
	BuildFx f;
	f.select(f.plot);
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	Object *building = f.find("Hall");
	REQUIRE(building != nullptr);
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(building->findModule("GettingBuiltBehavior"));
	for (int i = 0; i < 120 && building->getConstructionPercent() != -1.0f; ++i)
	{
		f.logic->runLogicFrame();
	}
	REQUIRE(building->getConstructionPercent() == -1.0f);
	CHECK(gb->spawnTimer() == 300.0f); // the data's SpawnTimer once complete (RW 0x857DBE)
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	hit(*building, 300.0f);
	REQUIRE(building->getBodyModule()->getHealth() == 700.0f);
	UnsignedInt start = 0;
	for (int i = 0; i < 60 && !start; ++i)
	{
		f.logic->runLogicFrame();
		start = gb->isConstructing() ? f.logic->getFrame() : 0u;
	}
	REQUIRE(start != 0u);
	// RW 0x8566DF with force: no price; a structure completed once is UNDERGOING_REPAIR and UNDER_CONSTRUCTION again, the percent is its health
	CHECK(f.alice()->getMoney()->countMoney() == money);
	CHECK(building->testStatus(20));
	CHECK(building->isUnderConstruction());
	CHECK(building->getConstructionPercent() == 70.0f);
	const float step = 1000.0f / 300.0f; // RW 0x857FC4: max / 300 in the FPU, stored as a float
	int frames = 0;
	float last = building->getBodyModule()->getHealth();
	int heals = 0;
	while (building->getConstructionPercent() != -1.0f && frames < 400)
	{
		f.logic->runLogicFrame();
		++frames;
		const float h = building->getBodyModule()->getHealth();
		if (h != last)
		{
			++heals;
			last = h;
		}
	}
	int expected = 0;
	for (volatile float h = 700.0f; h < 1000.0f; ++expected)
	{
		h = h + step; // the independent float model of the heals (clamped at the max by the body)
	}
	CHECK(building->getConstructionPercent() == -1.0f);
	CHECK_FALSE(building->testStatus(20));
	CHECK(building->getBodyModule()->getHealth() == 1000.0f);
	CHECK(heals == expected); // 300 health at 3.3333333 per frame: 91 float steps (90 leave 999.99997)
	CHECK(expected == 91);
	CHECK(f.alice()->getMoney()->countMoney() == money);
}

TEST_CASE("build rate: the sole benefactor rule lets one healer at a time heal (RW 0x690584), a SWARM_DOZER always")
{
	BuildFx f;
	f.load("Object Swarm\n  KindOf = INFANTRY SWARM_DOZER\nEnd\nObject Single\n  KindOf = INFANTRY\nEnd\n");
	Object *keep = f.make("Hall", f.teamOf("Alice"));
	Object *a = f.make("Single", f.teamOf("Alice"));
	Object *b = f.make("Single", f.teamOf("Alice"));
	Object *s = f.make("Swarm", f.teamOf("Alice"));
	REQUIRE((keep && a && b && s));
	f.logic->runLogicFrame(); // a fresh object's claim (frame 0) has not run out on frame 0
	hit(*keep, 500.0f);
	CHECK(keep->attemptHealingFromSoleBenefactor(10.0f, a, 2));
	CHECK(keep->soleHealingBenefactor() == a->getID());
	CHECK_FALSE(keep->attemptHealingFromSoleBenefactor(10.0f, b, 2)); // a holds the claim until frame now + 2
	CHECK(keep->attemptHealingFromSoleBenefactor(10.0f, s, 2));        // a swarm dozer heals anyway and takes no claim
	CHECK(keep->soleHealingBenefactor() == a->getID());
	CHECK(keep->getBodyModule()->getHealth() == 520.0f);
	CHECK_FALSE(keep->attemptHealingFromSoleBenefactor(10.0f, nullptr, 2));
	for (int i = 0; i < 3; ++i)
	{
		f.logic->runLogicFrame();
	}
	CHECK(keep->attemptHealingFromSoleBenefactor(10.0f, b, 2)); // the claim ran out
	CHECK(keep->soleHealingBenefactor() == b->getID());
}

TEST_CASE("build rate: the builder, the sole benefactor and the GettingBuiltBehavior state are in the state hash")
{
	BuildFx f;
	f.select(f.plot);
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	Object *building = f.find("Hall");
	REQUIRE(building != nullptr);
	for (int i = 0; i < 8; ++i)
	{
		f.logic->runLogicFrame();
	}
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(building->findModule("GettingBuiltBehavior"));
	const std::uint32_t h0 = f.logic->computeStateHash();
	building->setBuilder(f.plot);
	CHECK(f.logic->computeStateHash() != h0);
	building->setBuilder(building);
	CHECK(f.logic->computeStateHash() == h0);
	gb->setForceComplete(true);
	CHECK(f.logic->computeStateHash() != h0);
	gb->setForceComplete(false);
	CHECK(f.logic->computeStateHash() == h0);
	CHECK(f.plot->attemptHealingFromSoleBenefactor(0.0f, building, 7)); // the plot's claim
	CHECK(f.logic->computeStateHash() != h0);
}

TEST_CASE("build rate: RebuildWhenDead - a dead structure kept by KeepObjectDie rebuilds itself after its spawn timer unless a structure of the filter stands within the range")
{
	BuildFx f;
	f.load("Object Lair\n  KindOf = STRUCTURE SELECTABLE IMMOBILE\n  BuildCost = 100\n  BuildTime = 10.0\n"
		   "  Behavior = GettingBuiltBehavior ModuleTag_GB\n    RebuildWhenDead = Yes\n    UseSpawnTimerWithoutWorker = Yes\n    SpawnTimer = 3\n"
		   "    DisallowRebuildFilter = ANY +STRUCTURE\n    DisallowRebuildRange = 300\n  End\n"
		   "  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 500\n    MaxHealthDamaged = 250\n    MaxHealthReallyDamaged = 100\n  End\n"
		   "  Behavior = KeepObjectDie ModuleTag_Keep\n  End\nEnd\n");
	Object *lair = f.make("Lair", f.teamOf("Alice"));
	REQUIRE(lair != nullptr);
	Coord3D at{ 2000.0f, 2000.0f, 0.0f };
	lair->setPosition(&at);
	Object *keep = f.make("Keep", f.teamOf("Alice"));
	Coord3D near{ 2100.0f, 2000.0f, 0.0f };
	keep->setPosition(&near);
	for (int i = 0; i < 10; ++i)
	{
		f.logic->runLogicFrame();
	}
	hit(*lair, 1000.0f);
	f.logic->runLogicFrame();
	REQUIRE(lair->isEffectivelyDead());
	REQUIRE_FALSE(lair->isDestroyed());
	GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(lair->findModule("GettingBuiltBehavior"));
	for (int i = 0; i < 100; ++i)
	{
		f.logic->runLogicFrame();
	}
	CHECK(lair->isEffectivelyDead()); // the Keep (a STRUCTURE within 300) blocks the rebuild (RW 0x857873)
	CHECK_FALSE(gb->isConstructing());
	f.logic->destroyObject(keep);
	int frames = 0;
	while (lair->isEffectivelyDead() && frames < 100)
	{
		f.logic->runLogicFrame();
		++frames;
	}
	CHECK_FALSE(lair->isEffectivelyDead());
	CHECK(gb->isConstructing());
	CHECK(frames <= 25); // the spawn timer (3, counted once per 5 frame update) runs out
	CHECK(lair->getConstructionPercent() == 0.0f);
	while (lair->getConstructionPercent() != -1.0f && frames < 1000)
	{
		f.logic->runLogicFrame();
		++frames;
	}
	CHECK(lair->getBodyModule()->getHealth() == 500.0f);
	CHECK(lair->getConstructionPercent() == -1.0f);
}

// ---- lane BUILD-2 review fixes: wall spans on a synthetic base, the clear-path gate --------------------------------------------------------------------------------------
#include "GameLogic/WallSpan.h"

namespace
{
std::string wallObjects(const char *stagger)
{
	return std::string("Object WallSeg\n  KindOf = STRUCTURE SELECTABLE IMMOBILE\n  BuildCost = 10\n  BuildTime = 2.0\n  Geometry = BOX\n  GeometryMajorRadius = 20\n  GeometryMinorRadius = 20\n  GeometryHeight = 10\n"
		"  Behavior = GettingBuiltBehavior ModuleTag_GB\n  End\n  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 50\n    MaxHealthReallyDamaged = 10\n  End\nEnd\n"
		"Object WallHubT\n  KindOf = STRUCTURE SELECTABLE IMMOBILE WALL_HUB\n  BuildCost = 20\n  BuildTime = 2.0\n  Geometry = BOX\n  GeometryMajorRadius = 20\n  GeometryMinorRadius = 20\n  GeometryHeight = 10\n"
		"  Behavior = GettingBuiltBehavior ModuleTag_GB\n  End\n  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 100\n    MaxHealthDamaged = 50\n    MaxHealthReallyDamaged = 10\n  End\n"
		"  Behavior = WallHubBehavior ModuleTag_WH\n    Options = OPTION_ONE\n    SegmentTemplateName = WallSeg\n    HubCapTemplateName = WallHubT\n    DefaultSegmentTemplateName = WallSeg\n"
		"    BuilderRadius = 20\n    StaggeredBuildFactor = ") + stagger + "\n  End\nEnd\n"
		"Object ImmDozer\n  KindOf = STRUCTURE IMMOBILE DOZER\nEnd\nObject Walker\n  KindOf = INFANTRY\nEnd\n";
}

struct WallFx : BuildFx
{
	explicit WallFx(const char *stagger = "20")
	{
		load(wallObjects(stagger));
		logic->settings().maxLineBuildObjects = 50;
	}
	Object *hubAt(float x, float y)
	{
		Object *h = make("WallHubT", teamOf("Alice"));
		Coord3D p{ x, y, 0.0f };
		h->setPosition(&p);
		return h;
	}
};
} // namespace

TEST_CASE("walls: StaggeredBuildFactor 2147483647 (an accepted INI value) wraps in unsigned 32-bit arithmetic like retail's imul (UBSan regression)")
{
	WallFx f("2147483647");
	Object *hub = f.hubAt(1000.0f, 1000.0f);
	std::vector<ObjectID> made;
	REQUIRE(WallSpan::build(*f.logic, *hub, *hub->getPosition(), Coord3D{ 1400.0f, 1000.0f, 0.0f }, *f.alice(), 0, &made));
	REQUIRE(made.size() >= 4); // tile index 2 and above overflowed a signed product
	for (int i = 0; i < 10; ++i)
	{
		f.logic->runLogicFrame();
	}
}

namespace
{
// lane BUILD-4: records the fade requests (and forwards nothing: the fixture has no drawables)
struct FadeHooks : ObjectClientHooks
{
	std::vector<std::pair<ObjectID, UnsignedInt>> fades;
	void objectCreated(Object &) override {}
	void objectDestroyed(Object &) override {}
	void fadeIn(Object &obj, UnsignedInt frames) override { fades.emplace_back(obj.getID(), frames); }
};
} // namespace

TEST_CASE("walls (BUILD-4): each tile is told fadeIn(138) (RW 0x7954A8); a staggered tile waits at percent 0 (RW 0x79546F after the stagger), not its 1-health percent")
{
	WallFx f; // StaggeredBuildFactor 20
	FadeHooks hooks;
	ObjectClientHooks *before = f.logic->clientHooks();
	f.logic->setClientHooks(&hooks);
	Object *hub = f.hubAt(1000.0f, 1000.0f);
	std::vector<ObjectID> made;
	REQUIRE(WallSpan::build(*f.logic, *hub, *hub->getPosition(), Coord3D{ 1400.0f, 1000.0f, 0.0f }, *f.alice(), 0, &made));
	f.logic->setClientHooks(before);
	REQUIRE(made.size() >= 4);
	REQUIRE(hooks.fades.size() == made.size());
	for (size_t i = 0; i < made.size(); ++i)
	{
		CHECK(hooks.fades[i].first == made[i]);
		CHECK(hooks.fades[i].second == 138u);
	}
	for (size_t i = 1; i < made.size(); ++i)
	{
		Object *t = f.logic->findObjectByID(made[i]);
		REQUIRE(t != nullptr);
		if (!t->isKindOfName("WALL_HUB"))
		{
			INFO("tile " << i);
			CHECK(t->getConstructionPercent() == 0.0f);
			CHECK(t->getBodyModule()->getHealth() == 1.0f);
		}
	}
}

TEST_CASE("walls: the source hub's linked-piece list holds every tile; a snapped end hub is linked too, a free end has none")
{
	{
		WallFx f; // a free end
		Object *hub = f.hubAt(1000.0f, 1000.0f);
		std::vector<ObjectID> made;
		REQUIRE(WallSpan::build(*f.logic, *hub, *hub->getPosition(), Coord3D{ 1400.0f, 1000.0f, 0.0f }, *f.alice(), 0, &made));
		GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(hub->findModule("GettingBuiltBehavior"));
		REQUIRE(gb != nullptr);
		CHECK(gb->linkedPieces().size() == made.size());
		for (ObjectID id : made)
		{
			bool found = false;
			for (const GettingBuiltBehavior::LinkedPiece &l : gb->linkedPieces())
			{
				found = found || l.id == id;
			}
			CHECK(found);
		}
	}
	{
		WallFx f; // a snapped end: a second hub within 20 of the end point
		Object *hub = f.hubAt(1000.0f, 1000.0f);
		Object *end = f.hubAt(1400.0f, 1005.0f);
		std::vector<ObjectID> made;
		REQUIRE(WallSpan::build(*f.logic, *hub, *hub->getPosition(), Coord3D{ 1400.0f, 1000.0f, 0.0f }, *f.alice(), 0, &made));
		GettingBuiltBehavior *gb = dynamic_cast<GettingBuiltBehavior *>(hub->findModule("GettingBuiltBehavior"));
		GettingBuiltBehavior *egb = dynamic_cast<GettingBuiltBehavior *>(end->findModule("GettingBuiltBehavior"));
		REQUIRE((gb && egb));
		bool hubKnowsEnd = false, endKnowsHub = false, endKnowsSelf = false;
		for (const GettingBuiltBehavior::LinkedPiece &l : gb->linkedPieces())
		{
			hubKnowsEnd = hubKnowsEnd || l.id == end->getID();
		}
		for (const GettingBuiltBehavior::LinkedPiece &l : egb->linkedPieces())
		{
			endKnowsHub = endKnowsHub || l.id == hub->getID();
			endKnowsSelf = endKnowsSelf || l.id == end->getID();
		}
		CHECK(hubKnowsEnd);
		CHECK(endKnowsHub);
		CHECK_FALSE(endKnowsSelf); // RW writes the links to the source hub; the end hub learns the source only
		for (ObjectID id : made)
		{
			bool found = false;
			for (const GettingBuiltBehavior::LinkedPiece &l : gb->linkedPieces())
			{
				found = found || l.id == id;
			}
			CHECK(found);
		}
	}
}

TEST_CASE("placement: RotWK's clear-path gate applies to a DOZER builder (RW 0x796A46..0x796A98), IMMOBILE or not; a non-DOZER builder skips it")
{
	WallFx f;
	Object *dozer = f.make("ImmDozer", f.teamOf("Alice")); // an IMMOBILE DOZER without an AI cannot reach any site
	Object *walker = f.make("Walker", f.teamOf("Alice"));  // a mobile non-DOZER: no clear-path test at all
	const ThingTemplate &seg = *f.w.get("WallSeg")->getFinalOverride();
	const Coord3D site{ 3000.0f, 3000.0f, 0.0f };
	CHECK(BuildPlacement::isLocationLegalToBuild(*f.logic, site, seg, 0.0f, LLF_CLEAR_PATH, dozer, f.alice()) == LBC_NO_CLEAR_PATH);
	CHECK(BuildPlacement::isLocationLegalToBuild(*f.logic, site, seg, 0.0f, LLF_CLEAR_PATH, walker, f.alice()) == LBC_OK);
	CHECK(BuildPlacement::isLocationLegalToBuild(*f.logic, site, seg, 0.0f, 0, dozer, f.alice()) == LBC_OK);
}

TEST_CASE("build audio (AUDIO-2 review r2): GettingBuiltBehavior's first completion from frame 5 posts one VoiceFullyCreated (0x7E2); the self-repair posts none (RW 0x857D6C .. 0x857DBA)")
{
	BuildFx f;
	std::vector<std::pair<int, std::uint32_t>> seen;
	int owner = 0;
	AudioApi::installUnitVoiceHandler([&](int e, std::uint32_t o, std::uint32_t) { seen.emplace_back(e, o); }, &owner);
	f.select(f.plot);
	f.dispatch.dispatch(f.construct("Hall", *f.plot->getPosition()));
	Object *building = f.find("Hall");
	REQUIRE(building != nullptr);
	for (int i = 0; i < 120 && building->getConstructionPercent() != -1.0f; ++i)
	{
		f.logic->runLogicFrame();
	}
	REQUIRE(building->getConstructionPercent() == -1.0f);
	REQUIRE(seen.size() == 1);
	CHECK(seen[0].first == 0x7E2);
	CHECK(seen[0].second == building->getID());
	// a repair of the completed structure runs the same completion again: m_completedOnce (retail's +0x33) keeps it silent
	hit(*building, 300.0f);
	bool repairing = false;
	for (int i = 0; i < 450; ++i)
	{
		f.logic->runLogicFrame();
		repairing |= building->isUnderConstruction();
	}
	CHECK(repairing); // the self-repair ran (UNDER_CONSTRUCTION again, RW 0x8566DF) and completed
	CHECK(building->getBodyModule()->getHealth() == 1000.0f);
	CHECK(seen.size() == 1);
	// the generic completion helper (the dozer path) no longer posts it: one announcement per construction
	Construction::completeConstruction(*building, nullptr);
	CHECK(seen.size() == 1);
	AudioApi::uninstallUnitVoiceHandler(&owner);
}
