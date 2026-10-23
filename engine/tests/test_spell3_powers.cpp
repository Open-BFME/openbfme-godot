// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-3, retail: the spell book powers a player casts, one focused test per power kind: the terrain-moving Flood (FloodUpdate's members run their
// Bezier paths and die at the end, the water horses' weapon hurts what stands in the way), an area-damage power (Earthquake: its pieces, its five weapons),
// a summon, a buff, a global power and Devastation; the casts go through the retail GameMessages and SpellCommands.

#include "doctest.h"
#include "SpellTestAI.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameClient/TreeTopple.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace
{
// two players: 0 casts (its faction is the test's), 1 is the enemy (Mordor)
struct Spell3Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	std::unique_ptr<GameLogicDispatch> dispatch;
	spelltest::GameAI ai;
	SpellCommands spell;
	starttest::Shared &s;
	Object *book = nullptr;

	Spell3Game(starttest::Shared &shared, const char *faction, const char *citadel)
		: players(shared.world->nameKeys(), shared.world->playerTemplates(), teams)
		, s(shared)
	{
		SkirmishSetup setup;
		const char *factions[2] = { faction, "FactionMordor" };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishPlayer p;
			p.name = "s" + std::to_string(i);
			p.faction = factions[i];
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic->economy().settings(), &err), err); // GameData's money values (as LiveGame loads them)
		logic->castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(*s.mount->fs)); // the castle layouts (Bases.big), as LiveGame installs them
		logic->random().seedRandom(31);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		ai.attach(shared, *logic);
		spell.registerHandlers(*dispatch);
		SpecialPowerModules::installScienceHooks(*logic);
		place(citadel, 0, 500.0f, 300.0f)->friend_onBuildComplete();
		place("MordorFortressCitadel", 1, 6000.0f, 300.0f)->friend_onBuildComplete();
		book = SpecialPowerModules::createSpellBook(*logic, *player(0));
		REQUIRE(book);
	}
	~Spell3Game() { logic->reset(); }
	Player *player(int i) { return players.findPlayerWithName("s" + std::to_string(i)); }
	Object *place(const char *name, int i, float x, float y)
	{
		const ThingTemplate *t = s.world->things().findTemplate(name);
		REQUIRE_MESSAGE(t, name);
		Object *o = logic->newObject(t, player(i)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		return o;
	}
	const SpecialPowerTemplate *grant(const char *power)
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(power);
		REQUIRE_MESSAGE(t, power);
		for (ScienceType st : t->getRequiredSciences())
		{
			player(0)->science().grantScience(st);
		}
		return t;
	}
	void castAt(const SpecialPowerTemplate *t, const Coord3D &where)
	{
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, player(0)->getPlayerIndex());
		m.appendIntegerArgument((int)t->getID());
		m.appendLocationArgument(where);
		m.appendObjectIDArgument(INVALID_ID);
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(book->getID());
		dispatch->dispatch(m);
	}
	std::vector<Object *> objectsOf(const char *templateName)
	{
		std::vector<Object *> out;
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == templateName && !o->isDestroyed())
			{
				out.push_back(o);
			}
		}
		return out;
	}
	float health(const std::vector<Object *> &objs)
	{
		float h = 0.0f;
		for (Object *o : objs)
		{
			h += (o->isEffectivelyDead() || !o->getBodyModule()) ? 0.0f : o->getBodyModule()->getHealth();
		}
		return h;
	}
};

const OCLSpecialPower *oclOf(Spell3Game &g, const SpecialPowerTemplate *t)
{
	return dynamic_cast<const OCLSpecialPower *>(SpecialPowerModules::findModule(*g.book, t));
}
} // namespace

TEST_CASE("SPELL-3 retail: Flood (terrain-moving): FloodUpdate makes its seven water horses, runs them along their Bezier paths and kills them at the end")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionElves", "ElvenCitadel");
	const SpecialPowerTemplate *flood = g.grant("SpellBookFlood");
	const Coord3D where{ 3000.0f, 3000.0f, 0.0f };
	// enemies in the water's way: AngleOfFlow 25 degrees, the members' paths run from about -200 to +200 along the flow
	std::vector<Object *> foes;
	for (int i = 0; i < 6; ++i)
	{
		foes.push_back(g.place("MordorFighter", 1, where.x - 60.0f + 25.0f * (float)i, where.y + 10.0f * (float)(i % 2)));
	}
	const float before = g.health(foes);
	g.castAt(flood, where);
	const OCLSpecialPower *ocl = oclOf(g, flood);
	REQUIRE(ocl);
	REQUIRE(ocl->created().size() == 1u); // OCL_SpellBookFlood: SpellBookFlood
	Object *fo = g.logic->findObjectByID(ocl->created().front());
	REQUIRE(fo);
	CHECK(fo->getTemplate()->getName() == "SpellBookFlood");
	FloodUpdate *fu = dynamic_cast<FloodUpdate *>(fo->findModule("FloodUpdate"));
	REQUIRE(fu);
	CHECK_FALSE(fu->membersCreated());
	const ObjectID floodId = fo->getID();
	g.logic->runLogicFrame();
	fo = g.logic->findObjectByID(floodId);
	REQUIRE(fo);
	fu = dynamic_cast<FloodUpdate *>(fo->findModule("FloodUpdate"));
	REQUIRE(fu->membersCreated());
	REQUIRE(fu->records().size() == 7u); // seven FloodMember blocks of FloodHorse
	size_t longest = 0;
	for (const FloodUpdate::Record &r : fu->records())
	{
		Object *m = g.logic->findObjectByID(r.id);
		REQUIRE(m);
		CHECK(m->getTemplate()->getName() == "FloodHorse");
		CHECK(r.path.size() >= 2u);
		CHECK(r.index == 1); // stepped once in the update that made it
		longest = r.path.size() > longest ? r.path.size() : longest;
	}
	// the first member's first control point: (-200, 0) turned by 25 degrees about the flood's position (z = 0)
	const FloodUpdate::Record &r0 = fu->records().front();
	const float a = 25.0f * 0.017453292f;
	CHECK(r0.path.front().x == doctest::Approx(where.x - 200.0f * std::cos(a)).epsilon(1e-4));
	CHECK(r0.path.front().y == doctest::Approx(where.y - 200.0f * std::sin(a)).epsilon(1e-4));
	CHECK(r0.path.back().x == doctest::Approx(where.x + 200.0f * std::cos(a)).epsilon(1e-4));
	// the members move each frame, then die at their path's end
	Object *m0 = g.logic->findObjectByID(r0.id);
	const Coord3D p1 = *m0->getPosition();
	g.logic->runLogicFrame();
	const Coord3D p2 = *g.logic->findObjectByID(r0.id)->getPosition();
	CHECK((p1.x != p2.x || p1.y != p2.y));
	for (size_t f = 0; f < longest + 4; ++f)
	{
		g.logic->runLogicFrame();
	}
	for (const FloodUpdate::Record &r : dynamic_cast<FloodUpdate *>(g.logic->findObjectByID(floodId)->findModule("FloodUpdate"))->records())
	{
		const Object *m = g.logic->findObjectByID(r.id);
		CHECK((m == nullptr || m->isEffectivelyDead()));
	}
	const float after = g.health(foes);
	MESSAGE("SPELL-3 flood: 7 members, longest path " << longest << " points, enemy health " << before << " -> " << after);
	CHECK(after < before);
	// DeletionUpdate removes the flood object after 10 s
	for (int f = 0; f < 60; ++f)
	{
		g.logic->runLogicFrame();
	}
	CHECK(g.logic->findObjectByID(floodId) == nullptr);
}

TEST_CASE("SPELL-3 retail: two identical Flood casts hash alike, frame by frame")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	std::vector<std::uint32_t> hashes[2];
	for (int run = 0; run < 2; ++run)
	{
		Spell3Game g(*shared, "FactionElves", "ElvenCitadel");
		const SpecialPowerTemplate *flood = g.grant("SpellBookFlood");
		g.place("MordorFighter", 1, 3000.0f, 3000.0f);
		g.castAt(flood, Coord3D{ 3000.0f, 3000.0f, 0.0f });
		for (int f = 0; f < 30; ++f)
		{
			g.logic->runLogicFrame();
			hashes[run].push_back(g.logic->computeStateHash());
		}
	}
	CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("SPELL-3 retail: Devastation (terrain-changing): the harvestable trees within its radius topple, give their lumber and pay up to the cap; others stand")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionIsengard", "IsengardFortressCitadel");
	const SpecialPowerTemplate *dev = g.grant("SpellBookDevastation");
	TerrainTrees &trees = g.logic->trees();
	trees.setExtent(-300.0f, -300.0f, 7000.0f, 7000.0f); // the test logic has no map: the grid of a 730 x 730 map with a 30-cell border
	const ThingTemplate *harvestable = shared->world->things().findTemplate("GenericInteractableOptimizedTree");
	const ThingTemplate *plain = shared->world->things().findTemplate("GenericNONInteractableOptimizedTree");
	REQUIRE(harvestable);
	REQUIRE(plain);
	const Coord3D where{ 3000.0f, 3000.0f, 0.0f };
	// three harvestable trees inside SPECIALPOWER_DEVASTATION_RADIUS (180), one outside, one inside that is not harvestable
	REQUIRE(trees.addTree(*harvestable, Coord3D{ where.x + 40.0f, where.y, 0.0f }, 1));
	REQUIRE(trees.addTree(*harvestable, Coord3D{ where.x - 100.0f, where.y + 60.0f, 0.0f }, 2));
	REQUIRE(trees.addTree(*harvestable, Coord3D{ where.x, where.y - 170.0f, 0.0f }, 3));
	REQUIRE(trees.addTree(*harvestable, Coord3D{ where.x + 400.0f, where.y, 0.0f }, 4));
	REQUIRE(trees.addTree(*plain, Coord3D{ where.x + 10.0f, where.y + 10.0f, 0.0f }, 5));
	const int perTree = g.logic->economy().settings().supplyBoxesPerTree;
	CHECK(perTree == 90); // gamedata.ini SupplyBoxesPerTree
	CHECK(g.logic->economy().settings().valuePerSupplyBox == 10);
	CHECK(trees.records()[0].value == perTree);
	const std::uint32_t before = g.player(0)->getMoney()->countMoney();
	const std::uint32_t hashBefore = g.logic->computeStateHash();
	g.castAt(dev, where);
	const std::uint32_t after = g.player(0)->getMoney()->countMoney();
	// the three inside: toppled away from the cast point, their lumber gone (the record cleared), a topple and a removal event each
	int toppled = 0, removed = 0;
	for (const TerrainTrees::Event &e : trees.events())
	{
		toppled += e.kind == TerrainTrees::Event::TOPPLE ? 1 : 0;
		removed += e.kind == TerrainTrees::Event::REMOVE ? 1 : 0;
		if (e.kind == TerrainTrees::Event::TOPPLE)
		{
			CHECK(e.speed == doctest::Approx(0.1f));
		}
	}
	CHECK(toppled == 3);
	CHECK(removed == 3);
	CHECK(trees.events().front().clientId == 1u); // the closest first
	CHECK(trees.events().front().direction.x == doctest::Approx(40.0f));
	CHECK(trees.records()[0].clientId == 0u);
	CHECK(trees.records()[1].clientId == 0u);
	CHECK(trees.records()[2].clientId == 0u);
	CHECK(trees.records()[3].clientId == 4u); // outside the radius
	CHECK_FALSE(trees.records()[3].toppled);
	CHECK(trees.records()[4].clientId == 5u); // not harvestable
	CHECK_FALSE(trees.records()[4].toppled);
	// the money: each tree's 900 through the multiplayer multiplier and the handicap, times TreeValueMultiplier 50 %, the sum capped at TreeValueTotalCap 1500
	const int added = g.logic->economy().applyIncomeMultipliers(*g.player(0), (float)(perTree * 10));
	const float total = 0.5f * (float)added * 3.0f;
	const std::uint32_t expected = (std::uint32_t)(total < 1500.0f ? total : 1500.0f);
	MESSAGE("SPELL-3 devastation: 3 trees, money " << before << " -> " << after << " (expected +" << expected << ")");
	CHECK(after - before == expected);
	CHECK(g.logic->computeStateHash() != hashBefore);
	// a second cast finds nothing there any more
	TerrainTrees::Record *left = trees.findClosestTree(where, 180.0f, false, 2);
	CHECK(left == nullptr);
}

TEST_CASE("SPELL-3: the terrain tree grid: buckets of the map extent, the closest standing tree, a game without a changed tree keeps its hash")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionIsengard", "IsengardFortressCitadel");
	TerrainTrees &trees = g.logic->trees();
	const std::uint32_t h0 = g.logic->computeStateHash();
	trees.setExtent(0.0f, 0.0f, 5000.0f, 5000.0f);
	const ThingTemplate *harvestable = shared->world->things().findTemplate("GenericInteractableOptimizedTree");
	REQUIRE(harvestable);
	// a template without W3DTreeDraw adds nothing (RW 0x683DF6)
	CHECK_FALSE(trees.addTree(*shared->world->things().findTemplate("MordorFighter"), Coord3D{ 10.0f, 10.0f, 0.0f }, 9));
	// trees in neighbouring buckets (a bucket is 5000 / 49.9 = 100.2 wide): the query walks every bucket its square touches
	REQUIRE(trees.addTree(*harvestable, Coord3D{ 1001.0f, 1001.0f, 0.0f }, 1));
	REQUIRE(trees.addTree(*harvestable, Coord3D{ 1099.0f, 1001.0f, 0.0f }, 2));
	REQUIRE(trees.addTree(*harvestable, Coord3D{ 899.0f, 950.0f, 0.0f }, 3));
	CHECK(g.logic->computeStateHash() == h0); // added, not changed: not hashed
	TerrainTrees::Record *t = trees.findClosestTree(Coord3D{ 1090.0f, 1000.0f, 0.0f }, 50.0f, false, 0);
	REQUIRE(t);
	CHECK(t->clientId == 2u);
	t = trees.findClosestTree(Coord3D{ 905.0f, 955.0f, 0.0f }, 20.0f, false, 2);
	REQUIRE(t);
	CHECK(t->clientId == 3u);
	CHECK(trees.findClosestTree(Coord3D{ 3000.0f, 3000.0f, 0.0f }, 50.0f, false, 0) == nullptr);
	// the radius gets 7.0 (RW 0xBE4AD4): a tree 55 away is found with radius 50
	CHECK(trees.findClosestTree(Coord3D{ 1001.0f, 1056.0f, 0.0f }, 50.0f, false, 0) != nullptr);
	trees.toppleTree(*t, Coord3D{ 900.0f, 900.0f, 0.0f }, nullptr);
	CHECK(t->toppled);
	CHECK(trees.findClosestTree(Coord3D{ 905.0f, 955.0f, 0.0f }, 20.0f, true, 0) == nullptr); // ignoring toppled trees
	CHECK(g.logic->computeStateHash() != h0);
}

TEST_CASE("SPELL-3 retail: a felled tree falls towards its direction, bounces and lies (KillWhenFinishedToppling = No); its lumber gone, it sinks away")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ThingTemplate *tt = shared->world->things().findTemplate("GenericInteractableOptimizedTree");
	REQUIRE(tt);
	const W3DTreeDrawModuleData *td = nullptr;
	for (const ThingTemplate::Nugget &n : tt->drawModules().nuggets())
	{
		td = td ? td : dynamic_cast<const W3DTreeDrawModuleData *>(n.data.get());
	}
	REQUIRE(td);
	CHECK(td->m_doTopple);
	CHECK_FALSE(td->m_killWhenToppled);
	CHECK(td->m_toppleFX == "FX_ToppleTree");
	TreeTopple tp;
	tp.add(7, Coord3D{ 100.0f, 200.0f, 5.0f }, td);
	tp.add(8, Coord3D{ 300.0f, 200.0f, 5.0f }, td);
	CHECK_FALSE(tp.topple(99, Coord3D{ 1.0f, 0.0f, 0.0f }, 0.1f));
	REQUIRE(tp.topple(7, Coord3D{ 0.0f, 30.0f, 0.0f }, 0.1f));
	CHECK_FALSE(tp.topple(7, Coord3D{ 0.0f, 30.0f, 0.0f }, 0.1f)); // only an upright tree
	const std::vector<TreeTopple::FX> fx = tp.takeFX();
	REQUIRE(fx.size() == 1u);
	CHECK(fx[0].name == "FX_ToppleTree");
	// lane TREES1: the logic's speed (Devastation's 0.1) is used as it is when above 0 (RW 0x4E9E4E; MinimumToppleSpeed only stands in for 0)
	CHECK(tp.trees()[0].angularVelocity == 0.1f * td->m_initialVelocityPercent);
	int updates = 0;
	while (tp.trees()[0].state == TreeTopple::FALLING && updates < 400)
	{
		tp.advance(TreeTopple::kUpdateSeconds);
		++updates;
	}
	MESSAGE("SPELL-3 tree: down after " << updates << " updates (" << updates * TreeTopple::kUpdateSeconds << " s)");
	CHECK(tp.trees()[0].state == TreeTopple::DOWN);
	CHECK(tp.trees()[0].angle == doctest::Approx(TreeTopple::kAngularLimit).epsilon(0.05));
	CHECK(tp.trees()[1].state == TreeTopple::UPRIGHT);
	// the trunk (+Z) now points along the fall direction (+Y)
	float r[9];
	Coord3D off;
	tp.pose(0, r, off);
	CHECK(r[5] > 0.95f);  // the Y component of the rotated Z axis
	CHECK(r[8] < 0.1f);
	for (int i = 0; i < 200; ++i)
	{
		tp.advance(TreeTopple::kUpdateSeconds);
	}
	CHECK(tp.trees()[0].state == TreeTopple::DOWN); // it lies: KillWhenFinishedToppling = No
	tp.remove(7);
	// lane TREES1: RW 0x4EA068 / 0x4ED508: SinkTime (10 s: 50 logic frames) x 6 client frames per logic frame = 300 updates, each lowering the tree by
	// SinkDistance / 50; the next update removes it
	for (int i = 0; i < 300; ++i)
	{
		tp.advance(TreeTopple::kUpdateSeconds);
	}
	CHECK(tp.trees()[0].state == TreeTopple::DOWN);
	tp.advance(TreeTopple::kUpdateSeconds);
	CHECK(tp.trees()[0].state == TreeTopple::GONE);
	float sunk = 0.0f;
	for (int i = 0; i < 300; ++i)
	{
		sunk += td->m_sinkDistance / (float)td->m_sinkFrames;
	}
	CHECK(td->m_sinkFrames == 50u);
	CHECK(tp.trees()[0].sink == sunk); // about 6 x SinkDistance: the tree sinks out of sight in the first sixth
}

TEST_CASE("SPELL-3 retail: summon: the Dwarves' Citadel egg hatches into the summoned citadel")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionDwarves", "DwarvenFortressCitadel");
	const SpecialPowerTemplate *cit = g.grant("SpellBookCitadel");
	const Coord3D where{ 2500.0f, 2500.0f, 0.0f };
	g.castAt(cit, where);
	const OCLSpecialPower *ocl = oclOf(g, cit);
	REQUIRE(ocl);
	MESSAGE("SPELL-3 citadel: the OCL made " << ocl->created().size() << " object(s), unported uses " << ocl->unportedEffects());
	std::vector<std::string> seen;
	bool citadel = false;
	int frames = 0;
	for (; frames < 150 && !citadel; ++frames)
	{
		g.logic->runLogicFrame();
		citadel = !g.objectsOf("DwarvenSummonedCitadel").empty();
	}
	MESSAGE("SPELL-3 citadel: hatched after " << frames << " logic frames");
	for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == g.player(0))
		{
			seen.push_back(o->getTemplate()->getName() + (o->isEffectivelyDead() ? "(dead)" : ""));
		}
	}
	std::string all;
	for (const std::string &n : seen)
	{
		all += n + " ";
	}
	MESSAGE("SPELL-3 citadel: player objects after the cast: " << all);
	REQUIRE(citadel);
	// the summoned castle (CastleBehavior Summoned = Yes) unpacks its layout SummonedCitadel_Dwarven by the timed unpack: FadeTime, then BuildTime with BASE_BUILD
	Object *flag = g.objectsOf("DwarvenSummonedCitadel").front();
	CastleBehavior *cb = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
	REQUIRE(cb);
	int states[5] = { 0, 0, 0, 0, 0 };
	const int justBuilt = CombatNames::modelCondition("JUST_BUILT"), baseBuild = CombatNames::modelCondition("BASE_BUILD");
	const unsigned noAttack = (unsigned)CombatNames::status("NO_ATTACK");
	// review r1: the members' conditions through the timed unpack (RW 0x7987EE non-instant: JUST_BUILT and NO_ATTACK from creation; RW 0x799F0F at the
	// ends of the fade and the build)
	auto members = [&]() {
		std::vector<Object *> out;
		if (Object *k = g.logic->findObjectByID(cb->keepId()))
		{
			out.push_back(k);
		}
		for (ObjectID id : cb->members())
		{
			if (Object *o = g.logic->findObjectByID(id))
			{
				out.push_back(o);
			}
		}
		return out;
	};
	int checkedFade = 0, checkedBuild = 0;
	// review r2: an enemy soldier beside the citadel: the keep must not fire on it while it fades in (JUST_BUILT) or builds (NO_ATTACK): RW 0x66844A's
	// mood scan refuses then (RW 0x6684D0 and isAbleToAttack RW 0x668A43); once unpacked it fires
	Object *foe = g.place("MordorFighter", 1, where.x + 140.0f, where.y);
	const float foeHealth = foe->getBodyModule()->getHealth();
	const ObjectID foeId = foe->getID();
	int firstHitState = -1, firstHitFrame = -1;
	for (int f = 0; f < 60; ++f)
	{
		if (cb->state() >= 0 && cb->state() < 5)
		{
			++states[cb->state()];
		}
		const Object *fo = g.logic->findObjectByID(foeId);
		const float hp = fo && !fo->isEffectivelyDead() ? fo->getBodyModule()->getHealth() : 0.0f;
		if (firstHitState < 0 && hp < foeHealth)
		{
			firstHitState = cb->state();
			firstHitFrame = f;
		}
		for (Object *m : members())
		{
			INFO("state " << cb->state() << " member " << m->getTemplate()->getName());
			if (cb->state() == CastleBehavior::STATE_UNPACKING)
			{
				CHECK(m->testModelCondition(justBuilt));
				CHECK_FALSE(m->testModelCondition(baseBuild));
				CHECK(m->testStatus(noAttack));
				++checkedFade;
			}
			else if (cb->state() == CastleBehavior::STATE_BUILDING)
			{
				CHECK_FALSE(m->testModelCondition(justBuilt));
				CHECK(m->testModelCondition(baseBuild));
				CHECK(m->testStatus(noAttack));
				++checkedBuild;
			}
			else if (cb->state() == CastleBehavior::STATE_UNPACKED)
			{
				CHECK_FALSE(m->testModelCondition(baseBuild));
				CHECK_FALSE(m->testStatus(noAttack));
			}
		}
		g.logic->runLogicFrame();
	}
	CHECK(checkedFade > 0);
	CHECK(checkedBuild > 0);
	MESSAGE("SPELL-3 citadel: frames in the castle states 0..4: " << states[0] << " " << states[1] << " " << states[2] << " " << states[3] << " " << states[4]
			<< "; owned " << cb->ownedObjects().size() << ", keep " << cb->keepId() << ", error '" << cb->lastError() << "'");
	CHECK(cb->state() == CastleBehavior::STATE_UNPACKED);
	CHECK(cb->keepId() != INVALID_ID);
	CHECK(cb->ownedObjects().size() > 1u);
	CHECK(states[2] == 10); // FadeTime 2.0 s
	CHECK(states[3] == 26); // BuildTime 5.0 s: the timer reaches 0 after 25 decrements (an SSE single sum of 0.2) and expires on the 26th
	MESSAGE("SPELL-3 citadel: the enemy soldier beside it was first hit in castle state " << firstHitState << " (frame " << firstHitFrame << ")");
	CHECK(firstHitState == CastleBehavior::STATE_UNPACKED); // nothing fired during the fade (2) or the build (3); the unpacked keep fires
}

TEST_CASE("SPELL-3 retail (review r3): the summoned Citadel's members ordered to attack while they fade in and build fire nothing; unpacked, they fire")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionDwarves", "DwarvenFortressCitadel");
	const SpecialPowerTemplate *cit = g.grant("SpellBookCitadel");
	const Coord3D where{ 2500.0f, 2500.0f, 0.0f };
	g.castAt(cit, where);
	bool citadel = false;
	for (int f = 0; f < 150 && !citadel; ++f)
	{
		g.logic->runLogicFrame();
		citadel = !g.objectsOf("DwarvenSummonedCitadel").empty();
	}
	REQUIRE(citadel);
	CastleBehavior *cb = dynamic_cast<CastleBehavior *>(g.objectsOf("DwarvenSummonedCitadel").front()->findModule("CastleBehavior"));
	REQUIRE(cb);
	Object *foe = g.place("MordorFighter", 1, where.x + 140.0f, where.y);
	const ObjectID foeId = foe->getID();
	const float foeHealth = foe->getBodyModule()->getHealth();
	// the attack sub machine's Aim / Fire leave the machine when the owner cannot possibly attack (RW 0x741798 -> isAbleToAttack RW 0x691269: NO_ATTACK)
	int ordersFade = 0, ordersBuild = 0, ordersUnpacked = 0, firstHitState = -1, attackingWhileBuilding = 0;
	for (int f = 0; f < 70; ++f)
	{
		const int state = cb->state();
		Object *fo = g.logic->findObjectByID(foeId);
		const float hp = fo && !fo->isEffectivelyDead() ? fo->getBodyModule()->getHealth() : 0.0f;
		if (firstHitState < 0 && hp < foeHealth)
		{
			firstHitState = state;
		}
		std::vector<Object *> members;
		if (Object *k = g.logic->findObjectByID(cb->keepId()))
		{
			members.push_back(k);
		}
		for (ObjectID id : cb->members())
		{
			if (Object *o = g.logic->findObjectByID(id))
			{
				members.push_back(o);
			}
		}
		for (Object *m : members)
		{
			AIUpdateInterface *ai = m->getAIUpdateInterface();
			if (!ai || !m->getWeapons() || !fo)
			{
				continue;
			}
			if ((state == CastleBehavior::STATE_UNPACKING || state == CastleBehavior::STATE_BUILDING) && ai->currentStateId() == 50u)
			{
				++attackingWhileBuilding; // the order was taken, but the attack state must not last
			}
			if (f % 3 == 0 && ai->aiAttackObject(fo, CMD_FROM_PLAYER))
			{
				(state == CastleBehavior::STATE_UNPACKING ? ordersFade : state == CastleBehavior::STATE_BUILDING ? ordersBuild : ordersUnpacked) += 1;
			}
		}
		g.logic->runLogicFrame();
	}
	MESSAGE("SPELL-3 citadel orders: fading " << ordersFade << ", building " << ordersBuild << ", unpacked " << ordersUnpacked << "; frames in the attack state while not "
			<< "unpacked " << attackingWhileBuilding << "; the soldier first hit in castle state " << firstHitState);
	CHECK(ordersFade > 0);
	CHECK(ordersBuild > 0);
	CHECK(ordersUnpacked > 0);
	CHECK(cb->state() == CastleBehavior::STATE_UNPACKED);
	CHECK(firstHitState == CastleBehavior::STATE_UNPACKED); // no shot during the fade (2) or the build (3), ordered or not
}

TEST_CASE("SPELL-3 retail: area damage: Earthquake makes its six ground pieces, fires its five weapons and wrecks the enemy around it")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionMen", "MenFortressCitadel");
	const SpecialPowerTemplate *quake = g.grant("SpellBookEarthquake");
	const Coord3D where{ 3000.0f, 3000.0f, 0.0f };
	std::vector<Object *> foes;
	Object *farm = g.place("MordorSlaughterHouse", 1, where.x + 40.0f, where.y);
	farm->friend_onBuildComplete();
	foes.push_back(farm);
	for (int i = 0; i < 4; ++i)
	{
		foes.push_back(g.place("MordorFighter", 1, where.x - 50.0f + 25.0f * (float)i, where.y - 60.0f));
	}
	const float before = g.health(foes);
	// lane BACKLOG4: the foes by id. The quake kills them and the dead are deleted within the 45 frames; the health of the freed objects was read before
	std::vector<ObjectID> foeIds;
	for (const Object *o : foes)
	{
		foeIds.push_back(o->getID());
	}
	g.castAt(quake, where);
	const OCLSpecialPower *ocl = oclOf(g, quake);
	REQUIRE(ocl);
	int pieces = 0;
	ObjectID controller = INVALID_ID;
	for (ObjectID id : ocl->created())
	{
		const Object *o = g.logic->findObjectByID(id);
		REQUIRE(o);
		pieces += o->getTemplate()->getName() == "SpellBookEarthquakePiece" ? 1 : 0;
		controller = o->getTemplate()->getName() == "SpellBookEarthquake" ? id : controller;
	}
	CHECK(pieces == 6); // OCL_SpellBookEarthquake: the controller and six pieces at their offsets
	REQUIRE(controller != INVALID_ID);
	unsigned long long shots = 0;
	for (int f = 0; f < 45; ++f) // the last nugget fires after 3400 ms (17 frames); the controller lives 10 s
	{
		g.logic->runLogicFrame();
		if (Object *c = g.logic->findObjectByID(controller))
		{
			shots = dynamic_cast<FireWeaponUpdate *>(c->findModule("FireWeaponUpdate"))->shots();
		}
	}
	foes.clear();
	for (ObjectID id : foeIds)
	{
		if (Object *o = g.logic->findObjectByID(id))
		{
			foes.push_back(o);
		}
	}
	const float after = g.health(foes);
	MESSAGE("SPELL-3 earthquake: " << pieces << " pieces, " << shots << " weapon shots, enemy health " << before << " -> " << after);
	CHECK(shots == 5u);
	CHECK(after < before);
}

TEST_CASE("SPELL-3 retail: Dragon Strike (S-931): the map-edge strafe run is not ported; the cast takes its recharge, makes nothing and notes the stop")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionIsengard", "IsengardFortressCitadel");
	const SpecialPowerTemplate *strike = g.grant("SpellBookDragonStrike");
	const OCLSpecialPower *ocl = oclOf(g, strike);
	REQUIRE(ocl);
	const unsigned long long before = ocl->unportedEffects();
	g.castAt(strike, Coord3D{ 3000.0f, 3000.0f, 0.0f });
	CHECK(ocl->created().empty());
	CHECK(ocl->unportedEffects() == before + 1u);
	CHECK(ocl->getPercentReady() < 1.0f); // the recharge started
	bool noted = false;
	for (const std::string &l : g.logic->report().stops)
	{
		noted = noted || l.rfind("[S-931]", 0) == 0;
	}
	CHECK(noted);
}

TEST_CASE("SPELL-3 retail (review r1): a camouflaged unit loses its cover when Devastation fells its grove (the stealth tree query reads the live tree list)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionIsengard", "IsengardFortressCitadel");
	const SpecialPowerTemplate *dev = g.grant("SpellBookDevastation");
	TerrainTrees &trees = g.logic->trees();
	trees.setExtent(-300.0f, -300.0f, 7000.0f, 7000.0f);
	const ThingTemplate *harvestable = shared->world->things().findTemplate("GenericInteractableOptimizedTree");
	REQUIRE(harvestable);
	const Coord3D spot{ 3000.0f, 3000.0f, 0.0f };
	REQUIRE(trees.addTree(*harvestable, Coord3D{ spot.x + 30.0f, spot.y, 0.0f }, 1));
	REQUIRE(trees.addTree(*harvestable, Coord3D{ spot.x - 25.0f, spot.y + 20.0f, 0.0f }, 2));
	// a camouflage unit (DefaultInvisibilityUpdate.inc: CAMOUFLAGE, ForbiddenConditions AWAY_FROM_TREES MOVING FIRING_ANY, UpdatePeriod 2000 ms)
	Object *legolas = g.place("RohanLegolas", 0, spot.x, spot.y);
	CHECK(g.logic->invisibility().treesNear(spot, legolas));
	g.logic->runLogicFrame();
	for (int f = 0; f < 25; ++f)
	{
		g.logic->runLogicFrame();
	}
	const bool hiddenInGrove = InvisibilityManager::isInvisible(*legolas);
	g.castAt(dev, spot);
	CHECK(trees.records()[0].clientId == 0u); // felled and harvested
	CHECK(trees.records()[1].clientId == 0u);
	CHECK_FALSE(g.logic->invisibility().treesNear(spot, legolas)); // RW 0x67F52B(.., 50.0, ignoreToppled 1, 0): no standing tree left
	for (int f = 0; f < 25; ++f)
	{
		g.logic->runLogicFrame();
	}
	const bool hiddenAfter = InvisibilityManager::isInvisible(*legolas);
	MESSAGE("SPELL-3 camouflage: in the grove " << hiddenInGrove << ", after Devastation " << hiddenAfter);
	CHECK(hiddenInGrove);
	CHECK_FALSE(hiddenAfter);
	// a toppled tree no longer counts even before its lumber is taken (the flag alone)
	REQUIRE(trees.addTree(*harvestable, Coord3D{ spot.x + 400.0f, spot.y, 0.0f }, 3));
	const Coord3D far{ spot.x + 400.0f, spot.y + 10.0f, 0.0f };
	CHECK(g.logic->invisibility().treesNear(far, nullptr));
	TerrainTrees::Record *third = trees.findClosestTree(far, 50.0f, true, 0);
	REQUIRE(third);
	trees.toppleTree(*third, spot, nullptr);
	CHECK_FALSE(g.logic->invisibility().treesNear(far, nullptr));
}

TEST_CASE("SPELL-3 (review r1): failures reach the game report: a Flood member of an unknown template, a summoned castle whose side has no layout")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionAngmar", "AngmarFortressCitadel");
	// a FloodUpdate whose member names a template no INI defines (a mod's typo): retail skips it silently, the port reports it
	Object *host = g.place("SpellBookFlood", 0, 3000.0f, 3000.0f);
	FloodUpdateModuleData data;
	FloodMemberData m;
	m.templateName = "NoSuchFloodHorse";
	m.controlPointOffset[3] = Coord3D{ 200.0f, 0.0f, 0.0f };
	m.speed = 20.0f;
	data.m_members.push_back(m);
	FloodUpdate fu(host, &data);
	fu.update();
	CHECK(fu.membersCreated());
	CHECK(fu.records().empty());
	auto reported = [&](const char *what) {
		for (const std::string &e : g.logic->report().errors)
		{
			if (e.find(what) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	};
	CHECK(reported("NoSuchFloodHorse"));
	// the Dwarves' summoned citadel made for an Angmar player: CastleToUnpackForFaction names no Angmar layout, the unpack fails, the report says so and the
	// castle stays packed instead of advancing
	Object *flag = g.place("DwarvenSummonedCitadel", 0, 3500.0f, 3500.0f);
	flag->friend_onBuildComplete(); // as the OCL makes it (RW 0x5F1623)
	CastleBehavior *cb = dynamic_cast<CastleBehavior *>(flag->findModule("CastleBehavior"));
	REQUIRE(cb);
	cb->onCreate();
	CHECK(cb->state() == CastleBehavior::STATE_PACKED);
	for (int f = 0; f < 5; ++f)
	{
		g.logic->runLogicFrame();
	}
	CHECK(cb->state() == CastleBehavior::STATE_IDLE);
	CHECK(cb->ownedObjects().empty());
	CHECK(reported("the timed unpack failed"));
}

TEST_CASE("SPELL-3 retail: granting Scavenger science alone does not execute the purchase button CommandTrigger")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	Spell3Game g(*shared, "FactionWild", "WildFortressCitadel");
	const SpecialPowerTemplate *scav = g.grant("SpellBookScavenger");
	const ScavengerSpecialPower *sp = dynamic_cast<const ScavengerSpecialPower *>(SpecialPowerModules::findModule(*g.book, scav));
	REQUIRE(sp);
	for (int f = 0; f < 20; ++f)
	{
		g.logic->runLogicFrame();
	}
	CHECK_FALSE(sp->active());
	CHECK(sp->triggers() == 0u);
}
