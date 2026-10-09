// OpenBFME unit tests (lane HERO-1): the hero list (RW Player + 0x758), the hero production entries (ProductionUpdate type 3), RespawnBody / RespawnUpdate,
// in a synthetic world: a fortress with two REVIVE buttons and a ProductionModifier that halves the revive time, a hero with RespawnBody / RespawnUpdate.
// Expected values are computed by hand from the binary's arithmetic (cited per check).

#include "doctest.h"

#include "ProdTestUtil.h"

#include "Common/PlayerHeroList.h"
#include "Common/StateHash.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

using namespace prodtest;

namespace
{
const char kHeroObjects[] =
	"Object TestHero\n"
	"  BuildCost = 1200\n"
	"  BuildTime = 30.0\n"
	"  CommandPoints = 10\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    DeathFX = None\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnAnimationTime = 2000\n"
	"    InitialSpawnAnim = EMOTION_CELEBRATING\n"
	"    InitialSpawnAnimationTime = 1000\n"
	"    RespawnRules = AutoSpawn:No Cost:1000 Time:60000 Health:100%\n"
	"    RespawnEntry = Level:2 Cost:1100 Time:90000\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object FadeHero\n"
	"  BuildCost = 1200\n"
	"  BuildTime = 30.0\n"
	"  BuildFadeInOnCreateTime = 1.0\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = RespawnBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = RespawnUpdate ModuleTag_Respawn\n"
	"    DeathAnim = DYING\n"
	"    DeathFX = None\n"
	"    RespawnAnim = LEVELED\n"
	"    RespawnAnimationTime = 2000\n"
	"    InitialSpawnAnim = EMOTION_CELEBRATING\n"
	"    InitialSpawnAnimationTime = 1000\n"
	"    RespawnRules = AutoSpawn:No Cost:1000 Time:60000 Health:100%\n"
	"  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object PlainHero\n"
	"  BuildCost = 500\n"
	"  BuildTime = 12.5\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n"
	"  End\n"
	"End\n"
	"Object Fortress\n"
	"  KindOf = STRUCTURE SELECTABLE\n"
	"  CommandSet = FortressSet\n"
	"  Behavior = ProductionUpdate ModuleTag_PU\n"
	"    MaxQueueEntries = 5\n"
	"    ProductionModifier\n"
	"      TimeMultiplier = 0.5\n"
	"      HeroRevive = Yes\n"
	"    End\n"
	"    ProductionModifier\n"
	"      CostMultiplier = 0.9\n"
	"      HeroPurchase = Yes\n"
	"      RequiredUpgrade = Upgrade_Barracks2\n"
	"    End\n"
	"  End\n"
	"  Behavior = QueueProductionExitUpdate ModuleTag_Q\n"
	"    UnitCreatePoint = X:0.0 Y:-20.0 Z:0.0\n"
	"    NaturalRallyPoint = X:30.0 Y:-50.0 Z:0.0\n"
	"    ExitDelay = 400\n"
	"  End\n"
	"End\n";
const char kHeroCommands[] =
	"CommandButton Command_Revive1\n  Command = REVIVE\n  Options = HIDE_WHILE_DISABLED CANCELABLE\nEnd\n"
	"CommandButton Command_Revive2\n  Command = REVIVE\n  Options = HIDE_WHILE_DISABLED CANCELABLE NEED_UPGRADE\n  NeededUpgrade = Upgrade_Basic\nEnd\n"
	"CommandSet FortressSet\n  1 = Command_Revive1\n  2 = Command_Revive2\nEnd\n";

struct HeroFx : ProdWorld
{
	Object *fortress = nullptr;
	CommandList list;
	std::unique_ptr<GameLogicDispatch> dispatch;
	HeroFx()
	{
		logic->settings().bodyThresholdsLoaded = true;
		logic->settings().unitDamagedThreshold = 0.7f;
		logic->settings().unitReallyDamagedThreshold = 0.35f;
		load(kHeroObjects);
		load(kHeroCommands);
		fortress = make("Fortress", teamOf("Alice"));
		REQUIRE(fortress != nullptr);
		const Coord3D at = { 1000.0f, 2000.0f, 0.0f };
		fortress->setPosition(&at);
		alice()->getMoney()->deposit(100000, false);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		dispatch->attach(list);
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, alice()->getPlayerIndex());
		sel.appendBooleanArgument(true);
		sel.appendObjectIDArgument(fortress->getID());
		list.append(sel);
	}
	PlayerHeroList &heroes() { return alice()->heroes(); }
	void queueIndex(int index)
	{
		GameMessage m(MSG_QUEUE_UNIT_CREATE, alice()->getPlayerIndex());
		m.appendBooleanArgument(true);
		m.appendIntegerArgument(index);
		m.appendIntegerArgument(-1);
		m.appendBooleanArgument(false);
		m.appendBooleanArgument(false);
		list.append(m);
	}
	void cancelIndex(int index)
	{
		GameMessage m(MSG_CANCEL_UNIT_CREATE, alice()->getPlayerIndex());
		m.appendBooleanArgument(true);
		m.appendIntegerArgument(index);
		m.appendBooleanArgument(false);
		list.append(m);
	}
	Object *find(const char *name)
	{
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == name && !o->isEffectivelyDead())
			{
				return o;
			}
		}
		return nullptr;
	}
	RespawnUpdate *respawn(Object *o) { return dynamic_cast<RespawnUpdate *>(o->findModule("RespawnUpdate")); }
	// runs frames until a live object of the template exists; returns the frame or 0
	UnsignedInt runUntil(const char *name, int maxFrames)
	{
		for (int i = 0; i < maxFrames; ++i)
		{
			frames(1);
			if (find(name))
			{
				return logic->getFrame();
			}
		}
		return 0;
	}
	void kill(Object *victim)
	{
		DamageInfo d;
		d.m_input.m_sourceID = 0;
		d.m_input.m_amount = 100000.0f;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		victim->attemptDamage(d);
	}
};

int mcBit(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("hero list: a purchase record carries BuildCost and the whole seconds of BuildTime; cost and time through the producer's hero multipliers (RW 0x780713 / 0x780614 / 0x780687)")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("PlainHero"));
	REQUIRE(f.heroes().size() == 1);
	const HeroRecord &r = f.heroes().records()[0];
	CHECK(r.cost == 500);
	CHECK(r.seconds == 12); // _ftol(12.5)
	CHECK_FALSE(r.dead);
	CHECK(r.startFrame == -1);
	CHECK(r.productionID == 0);
	CHECK(f.heroes().isAvailable(0));
	CHECK_FALSE(f.heroes().isAvailable(1));
	// purchase: the HeroPurchase modifier needs Upgrade_Barracks2 on the fortress
	CHECK(f.heroes().costAt(*f.logic, *f.alice(), 0, f.fortress) == 500);
	CHECK(f.heroes().costAt(*f.logic, *f.alice(), 0, nullptr) == 500);
	f.fortress->giveUpgrade("Upgrade_Barracks2");
	CHECK(f.heroes().costAt(*f.logic, *f.alice(), 0, f.fortress) == 450);  // trunc(500 * 0.9f)
	CHECK(f.heroes().framesAt(*f.logic, *f.alice(), 0, f.fortress) == 60); // 12 s * 5, the HeroRevive time modifier does not apply to a purchase
	CHECK(f.heroes().costAt(*f.logic, *f.alice(), 5, f.fortress) == 0);
}

TEST_CASE("hero recruit: MSG_QUEUE_UNIT_CREATE by build index pays the record's cost, starts it, and the hero leaves when the record's progress reaches 1.0")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("TestHero"));
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	CHECK(BuildAssistant::isInProducersCommandSet(*f.fortress, nullptr, 0));
	CHECK(BuildAssistant::isInProducersCommandSet(*f.fortress, nullptr, 1) == false); // the 2nd REVIVE button exists but there is no record 1
	CHECK(BuildAssistant::isInProducersCommandSet(*f.fortress, nullptr, 2) == false); // no 3rd REVIVE button
	CHECK(BuildAssistant::canMakeUnit(*f.fortress, nullptr, 0) == CANMAKE_OK);
	f.queueIndex(0);
	f.frames(1);
	ProductionUpdateInterface *pu = f.fortress->getProductionUpdate();
	REQUIRE(pu->getProductionCount() == 1);
	CHECK(pu->firstProduction()->type == PRODUCTION_BUILD_INDEX);
	CHECK(pu->firstProduction()->objectToProduce == f.w.get("TestHero"));
	CHECK(f.alice()->getMoney()->countMoney() == money - 1200);
	const HeroRecord &r = f.heroes().records()[0];
	CHECK(r.startFrame != -1);
	CHECK(r.productionID == pu->firstProduction()->productionID);
	CHECK(r.uiFactor == 0.998f);
	CHECK_FALSE(f.heroes().isAvailable(0));
	CHECK(BuildAssistant::canMakeUnit(*f.fortress, nullptr, 0) == CANMAKE_NO_PREREQUISITES); // in production
	const std::int32_t start = r.startFrame;
	const UnsignedInt made = f.runUntil("TestHero", 400);
	REQUIRE(made != 0);
	CHECK((std::int32_t)made - start == 150); // 30 s -> 150 frames; (frame - start) / 150 >= 1.0
	CHECK(f.heroes().size() == 0);            // the record left the list (RW 0x7813FF)
	CHECK(pu->getProductionCount() == 0);
	Object *hero = f.find("TestHero");
	REQUIRE(hero != nullptr);
	CHECK(hero->getProducerID() == f.fortress->getID());
	RespawnUpdate *ru = f.respawn(hero);
	REQUIRE(ru != nullptr);
	CHECK(ru->isInitialSpawn());
	CHECK(ru->state() == RespawnUpdate::STATE_SPAWN_ANIMATION);
	CHECK(hero->testModelCondition(mcBit("EMOTION_CELEBRATING")));
	CHECK(dynamic_cast<ProductionUpdate *>(pu)->heroCountdown() == 20); // 4 * LOGICFRAMES_PER_SECOND
	f.frames(5);                                                            // InitialSpawnAnimationTime 1000 ms = 5 frames
	CHECK(ru->state() == RespawnUpdate::STATE_ALIVE);
	CHECK_FALSE(hero->testModelCondition(mcBit("EMOTION_CELEBRATING")));
}

TEST_CASE("hero death and revive: a lethal hit makes a revive record with the rule's cost and time; the revive takes half the time and restores the experience")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("TestHero"));
	f.queueIndex(0);
	REQUIRE(f.runUntil("TestHero", 400) != 0);
	f.frames(30);
	Object *hero = f.find("TestHero");
	REQUIRE(hero != nullptr);
	ExperienceTracker *t = hero->getExperienceTracker();
	REQUIRE(t != nullptr);
	t->addExperiencePoints(37.0f, false, false, false);
	const float xp = t->getExperience();
	const ObjectID oldId = hero->getID();
	f.kill(hero);
	CHECK(hero->isEffectivelyDead());
	CHECK(hero->testModelCondition(mcBit("DYING")));
	CHECK(hero->testStatus(3)); // UNSELECTABLE
	CHECK((hero->getDisabledMask() & (1u << 4)) != 0); // DISABLED_PARALYZED
	REQUIRE(f.heroes().size() == 1);
	const HeroRecord &r = f.heroes().records()[0];
	CHECK(r.dead);
	CHECK(r.cost == 1000); // the RespawnRules default (rank 0 has no rule: level 1's)
	CHECK(r.seconds == 60); // 60000 ms / 1000
	CHECK(r.experience == xp);
	CHECK(r.templateName == "TestHero");
	CHECK(f.heroes().costAt(*f.logic, *f.alice(), 0, f.fortress) == 1000);
	CHECK(f.heroes().framesAt(*f.logic, *f.alice(), 0, f.fortress) == 150); // 300 frames * the HeroRevive TimeMultiplier 0.5
	CHECK(f.heroes().framesAt(*f.logic, *f.alice(), 0, nullptr) == 300);
	f.frames(30); // the countdown of the first entry has run out
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	f.queueIndex(0);
	f.frames(1);
	CHECK(f.alice()->getMoney()->countMoney() == money - 1000);
	const std::int32_t start = f.heroes().records()[0].startFrame;
	UnsignedInt made = 0;
	for (int i = 0; i < 400 && !made; ++i)
	{
		f.frames(1);
		Object *o = f.find("TestHero");
		if (o && o->getID() != oldId)
		{
			made = f.logic->getFrame();
		}
	}
	REQUIRE(made != 0);
	CHECK((std::int32_t)made - start == 150);
	Object *revived = f.find("TestHero");
	REQUIRE(revived != nullptr);
	RespawnUpdate *ru = f.respawn(revived);
	REQUIRE(ru != nullptr);
	CHECK_FALSE(ru->isInitialSpawn());
	CHECK(ru->state() == RespawnUpdate::STATE_SPAWN_ANIMATION);
	CHECK(revived->testModelCondition(mcBit("LEVELED")));
	CHECK(revived->getExperienceTracker()->getExperience() == SimMath::subf32(xp, 1.0f)); // RW 0x781563: experience - 1.0 (sic)
	CHECK(revived->getBodyModule()->getHealth() == 500.0f);
	f.frames(10); // RespawnAnimationTime 2000 ms
	CHECK(ru->state() == RespawnUpdate::STATE_ALIVE);
	CHECK_FALSE(revived->testModelCondition(mcBit("LEVELED")));
	CHECK(f.heroes().size() == 0);
}

TEST_CASE("hero production: cancel by build index refunds the stored cost and returns the record (RW 0x8A047B / 0x8A13EE / 0x780C64)")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("TestHero"));
	const std::uint32_t money = f.alice()->getMoney()->countMoney();
	f.queueIndex(0);
	f.frames(3);
	REQUIRE(f.fortress->getProductionUpdate()->getProductionCount() == 1);
	f.cancelIndex(0);
	f.frames(1);
	CHECK(f.fortress->getProductionUpdate()->getProductionCount() == 0);
	CHECK(f.alice()->getMoney()->countMoney() == money);
	const HeroRecord &r = f.heroes().records()[0];
	CHECK(r.startFrame == -1);
	CHECK(r.productionID == 0xFFFFFFFFu);
	CHECK(r.uiFactor == 1.0f);
	CHECK(f.heroes().isAvailable(0));
}

TEST_CASE("hero production: an entry whose command points the player cannot afford pushes its record's start one frame per update (RW 0x8A0669)")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("TestHero"));
	f.queueIndex(0);
	f.frames(1);
	const std::int32_t start = f.heroes().records()[0].startFrame;
	setUsage(*f.alice(), f.alice()->commandPointLimit()); // no room for the hero's 10 command points
	f.frames(7);
	CHECK(f.heroes().records()[0].startFrame == start + 7);
	setUsage(*f.alice(), 0);
	f.frames(3);
	CHECK(f.heroes().records()[0].startFrame == start + 7);
}

TEST_CASE("hero list: startProduction refuses a used production id and a record in production; findIndex by id or by nth (RW 0x7812B2 / 0x78131E)")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("TestHero"));
	f.heroes().addPurchase(*f.w.get("PlainHero"));
	f.heroes().addPurchase(*f.w.get("TestHero"));
	CHECK(f.heroes().findIndex(*f.w.get("TestHero"), 0xFFFFFFFFu, 1) == 2);
	CHECK(f.heroes().findIndex(*f.w.get("PlainHero"), 0xFFFFFFFFu, 0) == 1);
	CHECK(f.heroes().startProduction(0, 7, 100));
	CHECK_FALSE(f.heroes().startProduction(1, 7, 100)); // id 7 is taken
	CHECK_FALSE(f.heroes().startProduction(0, 8, 100)); // in production (but + 0xDC is set first, as retail)
	CHECK(f.heroes().findIndex(*f.w.get("TestHero"), 7, 0) == 0);
	CHECK(f.heroes().findIndex(*f.w.get("PlainHero"), 7, 0) == -1);
	CHECK_FALSE(f.heroes().startProduction(9, 9, 100));
	CHECK(f.heroes().cancelProduction(7));
	CHECK_FALSE(f.heroes().cancelProduction(7));
}

TEST_CASE("RespawnUpdate parse: RespawnRules and RespawnEntry accept retail's grammar and fail with retail's messages (RW 0x8B3D65 / 0x8B3F72)")
{
	HeroFx f;
	auto obj = [](const std::string &rules) {
		return "Object R" + std::to_string(std::hash<std::string>()(rules) % 100000) + "\n  Behavior = RespawnUpdate M\n" + rules + "  End\nEnd\n";
	};
	const RespawnUpdateModuleData *d = nullptr;
	{
		const std::string err = f.w.load(obj("    RespawnRules = AutoSpawn:Yes Cost:10 Time:5000 Health:50%\n    RespawnEntry = Level:3 Time:9000 Bogus:1 Cost:30\n"));
		REQUIRE_MESSAGE(err.empty(), err);
	}
	for (const ThingTemplate::Nugget &n : f.w.get("TestHero")->behaviorModules().nuggets())
	{
		if (n.name == "RespawnUpdate")
		{
			d = dynamic_cast<const RespawnUpdateModuleData *>(n.data.get());
		}
	}
	REQUIRE(d != nullptr);
	REQUIRE(d->m_rules.size() == 2);
	CHECK(d->m_rules.at(2).cost == 1100);
	CHECK(d->m_rules.at(2).timeMs == 90000);
	CHECK(d->m_rules.at(2).health == 1.0f);
	CHECK(d->ruleFor(7) == &d->m_rules.at(1));
	CHECK(d->m_respawnAnimationTime == 10);
	CHECK(d->m_initialSpawnAnimationTime == 5);
	struct Bad
	{
		const char *rules;
		const char *message;
	};
	const Bad bad[] = {
		{ "    RespawnRules = Cost:10 AutoSpawn:No Time:1 Health:1%\n", "RespawnRules entry expecting 'AutoSpawn:Yes' or 'AutoSpawn:No' entry. You specified Cost." },
		{ "    RespawnRules = autospawn:No Cost:10 Time:1 Health:1%\n", "RespawnRules entry for 'AutoSpawn:Yes' or 'AutoSpawn:No' is case sensitive. You specified autospawn." },
		{ "    RespawnRules = AutoSpawn:No Cost:10 Time:1\n", "RespawnRules entry expecting 'Health' entry. You specified (null)." },
		{ "    RespawnRules = AutoSpawn:No Cost:1 Time:1 Health:1%\n    RespawnRules = AutoSpawn:No Cost:1 Time:1 Health:1%\n", "Duplicate RespawnRules entry." },
		{ "    RespawnEntry = Level:2 Cost:5\n", "You cannot parse a 'RespawnEntry' before 'RespawnRules'." },
		{ "    RespawnRules = AutoSpawn:No Cost:1 Time:1 Health:1%\n    RespawnEntry = level:2\n", "RespawnEntry for 'Level' is case sensitive. You specified level." },
		{ "    RespawnRules = AutoSpawn:No Cost:1 Time:1 Health:1%\n    RespawnEntry = Level:1\n", "Multiple 'RespawnEntry' with the same level of 1." },
		{ "    RespawnRules = AutoSpawn:No Cost:1 Time:1 Health:1%\n    RespawnEntry = Level:2 Cost:1 Cost:2\n", "RespawnEntry Level:2 entry for 'Cost' exists multiple times." },
		{ "    RespawnRules = AutoSpawn:No Cost:1 Time:1 Health:1%\n    RespawnEntry = Level:2 AUTOSPAWN:Yes\n", "RespawnEntry Level:2 entry for 'AutoSpawn:Yes' or 'AutoSpawn:No' is case sensitive. You specified AUTOSPAWN." },
	};
	for (const Bad &b : bad)
	{
		const std::string err = f.w.load(obj(b.rules));
		CHECK_MESSAGE(err.find(b.message) != std::string::npos, b.rules << " -> " << err);
	}
	// a wrong-case key other than AutoSpawn is skipped in a RespawnEntry (case-sensitive dispatch, RW 0x8B3F72)
	CHECK(f.w.load(obj("    RespawnRules = AutoSpawn:No Cost:1 Time:1 Health:1%\n    RespawnEntry = Level:2 cost:5\n")).empty());
}

TEST_CASE("hero hash: every hero list record field, the hero countdown and RespawnUpdate's state enter the state hash")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("TestHero"));
	const std::uint32_t base = f.logic->computeStateHash();
	auto mutated = [&](auto change) {
		HeroRecord saved = f.heroes().records()[0];
		change(*f.heroes().at(0));
		const std::uint32_t h = f.logic->computeStateHash();
		*f.heroes().at(0) = saved;
		CHECK(f.logic->computeStateHash() == base);
		return h != base;
	};
	CHECK(mutated([](HeroRecord &r) { r.experience = 3.0f; }));
	CHECK(mutated([](HeroRecord &r) { r.rank = 4; }));
	CHECK(mutated([](HeroRecord &r) { r.baseRank = 4; }));
	CHECK(mutated([](HeroRecord &r) { r.upgradeMask.set(3); }));
	CHECK(mutated([](HeroRecord &r) { r.cost = 1; }));
	CHECK(mutated([](HeroRecord &r) { r.startFrame = 9; }));
	CHECK(mutated([](HeroRecord &r) { r.seconds = 9; }));
	CHECK(mutated([](HeroRecord &r) { r.dead = true; }));
	CHECK(mutated([](HeroRecord &r) { r.objectFlag = true; }));
	CHECK(mutated([](HeroRecord &r) { r.productionID = 9; }));
	CHECK(mutated([](HeroRecord &r) { r.levelCap = 9; }));
	CHECK(mutated([](HeroRecord &r) { r.uiFactor = 0.5f; }));
	CHECK(mutated([](HeroRecord &r) { r.objectName = "x"; }));
	CHECK(mutated([](HeroRecord &r) { r.templateName = "PlainHero"; }));
	f.heroes().addPurchase(*f.w.get("PlainHero"));
	CHECK(f.logic->computeStateHash() != base); // the record count
	Object *hero = f.make("TestHero", f.teamOf("Alice"));
	const std::uint32_t withHero = f.logic->computeStateHash();
	f.respawn(hero)->setState(RespawnUpdate::STATE_PERMANENTLY_DEAD);
	CHECK(f.logic->computeStateHash() != withHero);
	f.respawn(hero)->setState(RespawnUpdate::STATE_ALIVE);
	CHECK(f.logic->computeStateHash() == withHero);
	f.respawn(hero)->setInitialSpawn(true);
	CHECK(f.logic->computeStateHash() != withHero);
}

TEST_CASE("hero determinism: the same recruit / death / revive script gives the same state hash every frame")
{
	auto run = []() {
		HeroFx f;
		f.heroes().addPurchase(*f.w.get("TestHero"));
		f.heroes().addPurchase(*f.w.get("PlainHero"));
		std::vector<std::uint32_t> hashes;
		f.queueIndex(0);
		for (int i = 0; i < 200; ++i)
		{
			f.frames(1);
			hashes.push_back(f.logic->computeStateHash());
		}
		if (Object *h = f.find("TestHero"))
		{
			f.kill(h);
		}
		f.queueIndex(1);
		for (int i = 0; i < 300; ++i)
		{
			f.frames(1);
			hashes.push_back(f.logic->computeStateHash());
		}
		return hashes;
	};
	const std::vector<std::uint32_t> a = run();
	const std::vector<std::uint32_t> b = run();
	CHECK(a == b);
	CHECK(std::adjacent_find(a.begin(), a.end(), std::not_equal_to<std::uint32_t>()) != a.end());
}

TEST_CASE("hero stops: S-850 .. S-863 are reported and have a row in docs/STOPS.md")
{
	const std::vector<std::string> lines = HeroSystem::stopLines();
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE(in.good());
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string docs = ss.str();
	REQUIRE(lines.size() == 14);
	for (int i = 0; i < 14; ++i)
	{
		const std::string id = "S-8" + std::to_string(50 + i);
		CHECK(lines[(size_t)i].rfind("[" + id + "]", 0) == 0);
		CHECK_MESSAGE(docs.find("| " + id + " |") != std::string::npos, id << " has no row in docs/STOPS.md");
	}
}

// ---- the leadership aura and the level unlock ------------------------------------------------------------------------------------------------
#include "Common/SpecialPower.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "PeImage.h"

namespace
{
const char kAuraObjects[] =
	"ModifierList TestLeadership\n"
	"  Category = LEADERSHIP\n"
	"  Duration = 2500\n"
	"  Modifier = DAMAGE_MULT 150%\n"
	"End\n"
	"Upgrade Upgrade_TestLeadership\n"
	"  Type = OBJECT\n"
	"End\n"
	"Upgrade Upgrade_TestUnlock\n"
	"  Type = OBJECT\n"
	"End\n"
	"SpecialPower SpecialAbilityTestBlast\n"
	"  ReloadTime = 60000\n"
	"End\n"
	"Object AuraHero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = AttributeModifierAuraUpdate ModuleTag_Leadership\n"
	"    StartsActive = No\n"
	"    BonusName = TestLeadership\n"
	"    TriggeredBy = Upgrade_TestLeadership\n"
	"    RefreshDelay = 2000\n"
	"    Range = 200\n"
	"    AntiCategory = BUFF\n"
	"    AffectsKindOf = INFANTRY\n"
	"  End\n"
	"  Behavior = UnpauseSpecialPowerUpgrade ModuleTag_Unlock\n"
	"    SpecialPowerTemplate = SpecialAbilityTestBlast\n"
	"    TriggeredBy = Upgrade_TestUnlock\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Blast\n"
	"    SpecialPowerTemplate = SpecialAbilityTestBlast\n"
	"    StartsPaused = Yes\n"
	"  End\n"
	"End\n"
	"SpecialPower SpecialAbilityTestRally\n"
	"  ReloadTime = 30000\n"
	"End\n"
	"Object RallyHero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Rally\n"
	"    SpecialPowerTemplate = SpecialAbilityTestRally\n"
	"    AttributeModifier = TestLeadership\n"
	"    AttributeModifierAffects = ALL\n"
	"    AttributeModifierRange = 100\n"
	"    AttributeModifierAffectsSelf = Yes\n"
	"    SetModelCondition = ModelConditionState:USER_1\n"
	"    SetModelConditionTime = 2.1\n"
	"  End\n"
	"End\n"
	"Object RallyNoFilterHero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = SpecialPowerModule ModuleTag_Rally\n"
	"    SpecialPowerTemplate = SpecialAbilityTestRally\n"
	"    AttributeModifier = TestLeadership\n"
	"    AttributeModifierRange = 100\n"
	"    AttributeModifierAffectsSelf = Yes\n"
	"  End\n"
	"End\n"
	"Object HealHero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = AutoHealBehavior ModuleTag_Heal\n"
	"    StartsActive = Yes\n"
	"    HealingAmount = 10\n"
	"    HealingDelay = 1000\n"
	"    StartHealingDelay = 3000\n"
	"    HealOnlyIfNotInCombat = Yes\n"
	"  End\n"
	"End\n"
	"Object Footman\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"End\n"
	// lane HERO-1 review: a list whose application grants an upgrade at once, two targets whose CostModifierUpgrades append to the player's ordered list
	"Upgrade Upgrade_TestCost\n"
	"  Type = OBJECT\n"
	"End\n"
	"ModifierList TestCostAura\n"
	"  Category = LEADERSHIP\n"
	"  Duration = 2500\n"
	"  Modifier = DAMAGE_MULT 110%\n"
	"  Upgrade = Upgrade_TestCost\n"
	"End\n"
	"Object CostAuraHero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = AttributeModifierAuraUpdate ModuleTag_Leadership\n"
	"    StartsActive = Yes\n"
	"    BonusName = TestCostAura\n"
	"    RefreshDelay = 2000\n"
	"    Range = 200\n"
	"  End\n"
	"End\n"
	"Object CostTargetA\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = CostModifierUpgrade ModuleTag_Cost\n"
	"    TriggeredBy = Upgrade_TestCost\n"
	"    ObjectFilter = ANY +INFANTRY\n"
	"    Percentage = 20%\n"
	"  End\n"
	"End\n"
	"Object CostTargetB\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = CostModifierUpgrade ModuleTag_Cost\n"
	"    TriggeredBy = Upgrade_TestCost\n"
	"    ObjectFilter = ANY +INFANTRY\n"
	"    Percentage = 40%\n"
	"  End\n"
	"End\n"
	"Object GuardHero\n"
	"  KindOf = SELECTABLE INFANTRY HERO\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"  Behavior = AutoHealBehavior ModuleTag_Heal\n"
	"    StartsActive = Yes\n"
	"    HealingAmount = 10\n"
	"    HealingDelay = 1000\n"
	"    Radius = 200\n"
	"    HealOnlyIfNotUnderAttack = Yes\n"
	"    KindOf = INFANTRY\n"
	"  End\n"
	"End\n"
	"Object WoundedMan\n"
	"  KindOf = SELECTABLE INFANTRY\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 500\n"
	"  End\n"
	"  Behavior = AttributeModifierPoolUpdate ModuleTag_Pool\n"
	"  End\n"
	"End\n";

struct AuraFx : HeroFx
{
	AttributeModifierStore modifiers;
	AttributeModifierStore *savedModifiers = TheAttributeModifierStore;
	SpecialPowerStore powers;
	SpecialPowerStore *savedPowers = TheSpecialPowerStore;
	AuraFx()
	{
		modifiers.registerBlock(w.fx.env.blocks);
		TheAttributeModifierStore = &modifiers;
		TheSpecialPowerStore = &powers;
		w.fx.env.blocks.registerBlock("SpecialPower", [](INI *ini) { SpecialPowerStore::parseSpecialPowerDefinitionGlobal(ini); });
		load(kAuraObjects);
	}
	~AuraFx()
	{
		TheAttributeModifierStore = savedModifiers;
		TheSpecialPowerStore = savedPowers;
	}
	Object *at(const char *name, const char *owner, float x)
	{
		Object *o = make(name, teamOf(owner));
		REQUIRE(o);
		const Coord3D p = { x, 0.0f, 0.0f };
		o->setPosition(&p);
		return o;
	}
	static bool hasList(Object *o, const char *list)
	{
		const AttributeModifierPool *pool = dynamic_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate"));
		return pool && pool->hasList(list);
	}
};
} // namespace

TEST_CASE("AttributeModifierAuraUpdate: the aura waits for its upgrade, then every allied object within Range gets BonusName each RefreshDelay + id % 5 frames (RW 0x89F42D / 0x89F114)")
{
	AuraFx f;
	Object *hero = f.at("AuraHero", "Alice", 0.0f);
	Object *near = f.at("Footman", "Alice", 150.0f);
	Object *far = f.at("Footman", "Alice", 250.0f);
	Object *enemy = f.at("Footman", "Bob", 100.0f);
	AttributeModifierAuraUpdate *aura = dynamic_cast<AttributeModifierAuraUpdate *>(hero->findModule("AttributeModifierAuraUpdate"));
	REQUIRE(aura);
	f.frames(5);
	CHECK(aura->pulses() == 0); // StartsActive = No: sleeps until its upgrade
	CHECK_FALSE(AuraFx::hasList(near, "TestLeadership"));
	hero->giveUpgrade("Upgrade_TestLeadership");
	UnsignedInt firstPulse = 0;
	for (int i = 0; i < 3 && !firstPulse; ++i)
	{
		f.frames(1);
		firstPulse = aura->pulses() == 1 ? f.logic->getFrame() : 0;
	}
	CHECK(aura->pulses() == 1);
	CHECK(AuraFx::hasList(near, "TestLeadership"));
	CHECK_FALSE(AuraFx::hasList(far, "TestLeadership"));
	CHECK_FALSE(AuraFx::hasList(enemy, "TestLeadership"));
	CHECK_FALSE(AuraFx::hasList(hero, "TestLeadership")); // AllowSelf No
	float mult = 1.0f;
	CHECK(near->attributeModifierProduct(ATTRIBUTE_DAMAGE_MULT, nullptr, true, mult));
	CHECK(mult == 1.5f);
	CHECK(aura->antiCategoryRequests() == 1); // AntiCategory BUFF: the pool's category disable is S-633 (counted)
	const unsigned period = 10u + hero->getID() % 5u; // RefreshDelay 2000 ms = 10 frames
	while (aura->pulses() == 1 && f.logic->getFrame() < firstPulse + 30)
	{
		f.frames(1);
	}
	CHECK(aura->pulses() == 2);
	CHECK(f.logic->getFrame() - firstPulse == period);
}

TEST_CASE("UnpauseSpecialPowerUpgrade: its upgrade unpauses the StartsPaused power of its template and makes it ready now (RW 0x8B9676)")
{
	AuraFx f;
	Object *hero = f.at("AuraHero", "Alice", 0.0f);
	SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(hero->findModule("SpecialPowerModule"));
	REQUIRE(sp);
	REQUIRE(sp->getSpecialPowerTemplate() != nullptr);
	CHECK(sp->pauseCount() == 1);
	f.frames(3);
	hero->giveUpgrade("Upgrade_TestUnlock");
	CHECK(sp->pauseCount() == 0);
	CHECK(sp->getReadyFrame() == f.logic->getFrame());
	CHECK(sp->isReady());
}

TEST_CASE("SpecialPowerModule trigger: SetModelCondition for _ftol(5 * time) frames, the AttributeModifier on the allies in range and the caster (RW 0x897A63 / 0x8977BF / 0x89763B)")
{
	AuraFx f;
	Object *hero = f.at("RallyHero", "Alice", 0.0f);
	Object *near = f.at("Footman", "Alice", 80.0f);
	Object *far = f.at("Footman", "Alice", 150.0f);
	Object *enemy = f.at("Footman", "Bob", 50.0f);
	SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(hero->findModule("SpecialPowerModule"));
	REQUIRE(sp);
	f.frames(2);
	sp->setReadyFrame(f.logic->getFrame()); // a power starts recharging (no AvailableAtStart): ready now for the test
	REQUIRE(sp->isReady());
	sp->doSpecialPower(0);
	CHECK(sp->applied() == 2); // the footman in range and the caster itself
	CHECK(AuraFx::hasList(hero, "TestLeadership"));
	CHECK(AuraFx::hasList(near, "TestLeadership"));
	CHECK_FALSE(AuraFx::hasList(far, "TestLeadership"));
	CHECK_FALSE(AuraFx::hasList(enemy, "TestLeadership")); // allies only (AffectAllies No: also only the caster's own player)
	CHECK_FALSE(sp->isReady()); // the recharge (UpdateModuleStartsAttack No)
	const int user1 = mcBit("USER_1");
	CHECK(hero->testModelCondition(user1));
	f.frames(9);
	CHECK(hero->testModelCondition(user1));
	f.frames(2);
	CHECK_FALSE(hero->testModelCondition(user1)); // 10 frames (5 * 2.1 = 10.5 truncated)
}

TEST_CASE("SpecialPowerModule trigger (SPELL-2 review r3): an omitted AttributeModifierAffects is retail's NONE filter (RW 0x89693D) and rejects nearby allies; AffectsSelf still adds the caster")
{
	AuraFx f;
	Object *hero = f.at("RallyNoFilterHero", "Alice", 0.0f);
	Object *near = f.at("Footman", "Alice", 80.0f);
	SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(hero->findModule("SpecialPowerModule"));
	REQUIRE(sp);
	CHECK(sp->spData()->m_attributeModifierAffects == nullptr); // the constructor's final replacement: NONE, empty masks, flag 0 (RW 0x763D11)
	f.frames(2);
	sp->setReadyFrame(f.logic->getFrame());
	sp->doSpecialPower(0);
	CHECK(sp->applied() == 1); // the caster only (RW 0x897CF2)
	CHECK(AuraFx::hasList(hero, "TestLeadership"));
	CHECK_FALSE(AuraFx::hasList(near, "TestLeadership"));
}

TEST_CASE("SpecialPowerModule data: RW 0x89693A / 0x89693D replaces AttributeModifierAffects (+ 0x24) with the NONE filter (RW 0x763D11), re-read from game.dat")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		return; // set RW_GAME_DAT
	}
	std::vector<std::uint8_t> b;
	REQUIRE(pe->read(0x89693A, 8, &b));
	CHECK(b[0] == 0x8D); // lea ecx, [esi + 0x24]
	CHECK(b[1] == 0x4E);
	CHECK(b[2] == 0x24);
	CHECK(b[3] == 0xE8); // call rel32
	std::int32_t rel;
	std::memcpy(&rel, &b[4], 4);
	CHECK((std::uint32_t)(0x896942u + (std::uint32_t)rel) == 0x763D11u); // the call at 0x89693D ends at 0x896942
}

TEST_CASE("AutoHealBehavior: a hit wakes the heal after StartHealingDelay, then HealingAmount every HealingDelay until full health (RW 0x8558C0 / 0x855706 / 0x855761)")
{
	AuraFx f;
	Object *hero = f.at("HealHero", "Alice", 0.0f);
	AutoHealBehavior *heal = dynamic_cast<AutoHealBehavior *>(hero->findModule("AutoHealBehavior"));
	REQUIRE(heal);
	CHECK(heal->isAlreadyUpgraded()); // StartsActive
	f.frames(8);
	CHECK(heal->heals() == 0); // full health: nothing to heal
	DamageInfo d;
	d.m_input.m_amount = 100.0f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	hero->attemptDamage(d);
	REQUIRE(hero->getBodyModule()->getHealth() == 400.0f);
	const UnsignedInt hit = f.logic->getFrame();
	UnsignedInt firstHeal = 0;
	for (int i = 0; i < 40 && !firstHeal; ++i)
	{
		f.frames(1);
		if (heal->heals() == 1)
		{
			firstHeal = f.logic->getFrame();
		}
	}
	REQUIRE(firstHeal != 0);
	CHECK(firstHeal - hit == 15); // StartHealingDelay 3000 ms
	CHECK(hero->getBodyModule()->getHealth() == 410.0f);
	f.frames(5);
	CHECK(heal->heals() == 2); // HealingDelay 1000 ms
	CHECK(hero->getBodyModule()->getHealth() == 420.0f);
	f.frames(200);
	CHECK(hero->getBodyModule()->getHealth() == 500.0f);
	const unsigned atFull = heal->heals();
	f.frames(20);
	CHECK(heal->heals() == atFull); // asleep at full health
}

// lane HERO-1 review (Sol #1): RW 0x89F42D's scan is RW 0xA39340 with sort mode 1 (ascending distance, RW 0xA3A6B0). The far target is made first, the near one
// second: the near one's CostModifierUpgrade (granted at once by the list's Upgrade) must come first in the player's ordered cost modifier list
TEST_CASE("AttributeModifierAuraUpdate: the targets are applied nearest first, whatever their creation order (RW 0xA39340 sort mode 1), so shared player state follows distance")
{
	AuraFx f;
	Object *hero = f.at("CostAuraHero", "Alice", 0.0f);
	Object *far = f.at("CostTargetA", "Alice", 150.0f);
	Object *near = f.at("CostTargetB", "Alice", 50.0f);
	AttributeModifierAuraUpdate *aura = dynamic_cast<AttributeModifierAuraUpdate *>(hero->findModule("AttributeModifierAuraUpdate"));
	REQUIRE(aura);
	for (int i = 0; i < 5 && aura->pulses() == 0; ++i)
	{
		f.frames(1);
	}
	REQUIRE(aura->pulses() == 1);
	f.frames(1);
	const std::vector<Player::CostModifier> &mods = f.alice()->costModifiers();
	REQUIRE(mods.size() >= 2);
	CHECK(mods[mods.size() - 2].source == near->getID());
	CHECK(mods[mods.size() - 1].source == far->getID());
}

// lane HERO-1 review (Sol #2): RW 0x855533 tests the HEALER (the context's + 0x1C) for HealOnlyIfNotUnderAttack, not the target: a recently damaged target next
// to an unharmed healer is healed; a healer just hit heals nobody until LOGICFRAMES_PER_SECOND frames pass
TEST_CASE("AutoHealBehavior: the under-attack test reads the healer's last damage frame, the target's health and KindOf decide the rest (RW 0x855533)")
{
	AuraFx f;
	Object *healer = f.at("GuardHero", "Alice", 0.0f);
	Object *target = f.at("WoundedMan", "Alice", 60.0f);
	AutoHealBehavior *heal = dynamic_cast<AutoHealBehavior *>(healer->findModule("AutoHealBehavior"));
	REQUIRE(heal);
	f.frames(8);
	DamageInfo d;
	d.m_input.m_amount = 100.0f;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	target->attemptDamage(d); // the target is "under attack" now, the healer is not
	REQUIRE(target->getBodyModule()->getHealth() == 400.0f);
	const unsigned heals0 = heal->heals();
	for (int i = 0; i < 6 && heal->heals() == heals0; ++i)
	{
		f.frames(1);
	}
	CHECK(heal->heals() > heals0);
	CHECK(target->getBodyModule()->getHealth() == 410.0f); // the old reading (the target's damage frame) left it at 400
	// now the healer is hit two frames before its next pulse (HealingDelay 1000 ms = 5 frames): that pulse heals nobody (its hit is within 5 frames), a later one does
	const unsigned heals1 = heal->heals();
	f.frames(3);
	DamageInfo h;
	h.m_input.m_amount = 50.0f;
	h.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	healer->attemptDamage(h);
	const float t0 = target->getBodyModule()->getHealth();
	f.frames(3);
	CHECK(heal->heals() == heals1);
	CHECK(target->getBodyModule()->getHealth() == t0);
	f.frames(12);
	CHECK(heal->heals() > heals1);
	CHECK(target->getBodyModule()->getHealth() > t0);
}

// lane HERO-1 review (Sol #3): RW 0x8C47DE scales the body's module + 0x2C (the revive reference: the initial health, newMax after RW 0x8C1CD5 setMaxHealth), not
// the previous health internalChangeHealth keeps. A 500-health hero raised to 1000 max health revives at 100% of 1000
TEST_CASE("RespawnUpdate: the revive health scales the body's revive reference, which setMaxHealth moves to the new maximum (RW 0x8C47DE / 0x8C1CD5)")
{
	HeroFx f;
	Object *hero = f.make("TestHero", f.teamOf("Alice"));
	REQUIRE(hero != nullptr);
	ActiveBody *body = dynamic_cast<ActiveBody *>(hero->getBodyModule());
	REQUIRE(body != nullptr);
	CHECK(body->getReviveReference() == 500.0f); // the actual initial health
	body->setMaxHealth(1000.0f, 1);
	REQUIRE(body->getHealth() == 1000.0f);
	CHECK(body->getPreviousHealth() == 500.0f); // the damage accounting's previous health is a different field
	CHECK(body->getReviveReference() == 1000.0f);
	RespawnUpdate *ru = f.respawn(hero);
	REQUIRE(ru != nullptr);
	ru->onRevived(nullptr); // the RespawnRules Health:100%
	CHECK(body->getHealth() == 1000.0f); // the previous-health reading revived it at 500
}

// lane HERO-1 review (Sol #4): the two ProductionUpdate counters are independent. RW 0x8A1D60 decrements only the hero countdown (module + 0x128) at the top of
// the update; the post-exit fade delay (module + 0x118) counts in the completion pass (RW 0x8A2EEE). A hero with a 1 s fade (5 frames): the pass that makes it
// takes the first frame, and then the build-index gate (RW 0x8A1F3D: the record's progress, RW 0x780C9F answers 0 for the record erased when the hero was made)
// keeps the completion from running, so the delay stays at 4 while the hero countdown (20 frames from the hand-off) runs down at the top of each update (retail's order: such a hero would hold its producer; no retail hero has a
// BuildFadeInOnCreateTime). The old top-of-update decrement ran it out
TEST_CASE("hero production: the post-exit fade delay is only counted by the completion pass; the build-index gate holds it (RW 0x8A1D60 / 0x8A1F3D / 0x8A2EEE)")
{
	HeroFx f;
	f.heroes().addPurchase(*f.w.get("FadeHero"));
	f.queueIndex(0);
	ProductionUpdate *pu = dynamic_cast<ProductionUpdate *>(f.fortress->getProductionUpdate());
	REQUIRE(pu != nullptr);
	UnsignedInt madeAt = 0;
	for (int i = 0; i < 400 && !madeAt; ++i)
	{
		f.frames(1);
		Object *o = f.find("FadeHero");
		madeAt = o && pu->exitingObjectID() == o->getID() ? f.logic->getFrame() : 0;
	}
	REQUIRE(madeAt != 0);
	CHECK(pu->heroCountdown() == 20u); // the hero hand-off (RW 0x8A2C1E): 4 * LOGICFRAMES_PER_SECOND
	CHECK(pu->postExitDelay() == 4u); // 5 frames, one taken by the pass that made it
	f.frames(10);
	CHECK(pu->heroCountdown() == 10u); // the top of the update counts it every frame
	CHECK(pu->postExitDelay() == 4u); // the gate: the update ran, the completion did not
	CHECK(pu->exitingObjectID() == f.find("FadeHero")->getID());
}
