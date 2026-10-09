// OpenBFME retail tests (lane HERO-1): the heroes of the 7 skirmish factions with the retail data. They run only when ROTWK_INSTALL and BFME2_INSTALL are set
// (otherwise SKIP).
//
// Per faction: the player's hero list at the game start is its PlayerTemplate's BuildableHeroesMP (RW 0x6B16EC), the ring heroes join through the spell book's
// BuildableHeroListUpgrade (RW 0x8BC4C6); then EVERY hero of the list except Create-A-Hero (S-852) is recruited from the faction's fortress by
// MSG_QUEUE_UNIT_CREATE with the build-index flag: the cost is BuildCost through calcCostToBuild, the time five frames per whole second of BuildTime (both
// through the fortress's hero ProductionModifiers), the hero appears with the fortress as producer and plays its initial spawn; it gains a level by kills of an
// enemy hero; it dies to a killing hit (a revive record with the RespawnUpdate rule of its level), is revived the same way and comes back with its experience.
// Independent expectations: the workspace census, committed as tests/data/hero/faction_heroes.json (each faction's BuildableHeroesMP + BuildableRingHeroesMP and
// its structures).

#include "doctest.h"

#include "Common/BuildAssistant.h"
#include "Common/MiniJson.h"
#include "Common/PlayerHeroList.h"
#include "Common/PlayerList.h"
#include "Common/NumericState.h"
#include "Common/Team.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/Damage.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/LiveScripting.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/SpellCommands.h"
#include "Common/SpecialPower.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameLogic/Module/HeroAbilityModules.h"
#include "GameLogic/Module/StealthAbilityModules.h"
#include "GameLogic/System/EmotionSystem.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "PathfindTestUtil.h"
#include "RetailTestMount.h"
#include "GameLogic/AI/AIWorld.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <typeinfo>
#include <vector>

namespace
{
struct Shared
{
	retailtest::Mount *mount = nullptr;
	std::unique_ptr<RetailObjectWorld> world;
	std::string error;
	JsonValue census;
};

Shared &shared()
{
	static Shared s;
	static bool built = false;
	if (!built)
	{
		built = true;
		s.mount = retailtest::pureMount();
		if (s.mount && s.mount->fs)
		{
			s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
			if (!s.world->load(&s.error))
			{
				s.world.reset();
			}
			std::vector<unsigned char> bytes;
			std::string err;
			if (!retailtest::readLocalFile(retailtest::dataDir() + "/hero/faction_heroes.json", bytes, &err) ||
				!JsonValue::parse(std::string(bytes.begin(), bytes.end()), s.census, &err))
			{
				s.error = "faction_heroes.json: " + err;
				s.world.reset();
			}
		}
	}
	return s;
}

#define REQUIRE_RETAIL_WORLD(sh)                                                  \
	Shared &sh = shared();                                                        \
	if (!sh.mount || !sh.mount->fs)                                               \
	{                                                                             \
		MESSAGE("SKIP: ROTWK_INSTALL / BFME2_INSTALL not set (retail hero test)"); \
		return;                                                                   \
	}                                                                             \
	REQUIRE_MESSAGE(sh.world, sh.error);                                          \
	auto contextScope = sh.world->enterContext()

const std::string *stringField(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.getFinalOverride()->findField(name);
	return v ? std::get_if<std::string>(v) : nullptr;
}

bool hasNugget(const ThingTemplate &tt, const char *cls)
{
	for (const ThingTemplate::Nugget &n : tt.getFinalOverride()->behaviorModules().nuggets())
	{
		if (n.name == cls)
		{
			return true;
		}
	}
	return false;
}

int reviveButtons(const CommandSet &set)
{
	int n = 0;
	for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set.getCommandButton(i);
		n += b && b->m_command == GUI_COMMAND_REVIVE ? 1 : 0;
	}
	return n;
}

struct Game
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	CommandList list;
	std::unique_ptr<GameLogicDispatch> dispatch;
	SpellCommands spells;
	Player *me = nullptr;
	Player *enemy = nullptr;
	Game(Shared &sh, const std::string &faction, const std::string &enemyFaction, bool withAI = false)
		: players(sh.world->nameKeys(), sh.world->playerTemplates(), teams)
	{
		SkirmishSetup setup;
		setup.players.push_back({ "Tester", faction, true, 0, 0, 0 });
		setup.players.push_back({ "Enemy", enemyFaction, false, 1, 0, 1 });
		setup.startingMoney = 100000000;
		setup.defaultStartingCash = 100000000;
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(sh.world->things(), sh.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		logic->random().seedRandom(1);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*sh.mount->fs, logic->settings(), &err), err);
		logic->productionSettings() = sh.world->productionSettings();
		logic->setUpgradeTypes(&sh.world->upgradeTypes());
		REQUIRE_MESSAGE(EconomySettings::load(*sh.mount->fs, logic->economy().settings(), &err), err);
		logic->economy().initAllCommandPoints();
		me = players.findPlayerWithName("Tester");
		enemy = players.findPlayerWithName("Enemy");
		REQUIRE(me);
		REQUIRE(enemy);
		me->commandPoints().setFromScript(100000000, 100000000);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		dispatch->attach(list);
		spells.registerHandlers(*dispatch);
		if (!withAI)
		{
			return;
		}
		// lane HERO-1 part 2: an AIWorld on flat terrain (the abilities move, idle and busy their hero through the AI)
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*sh.mount->fs, aiConfig, &err), err);
		terrain = std::make_unique<pathtest::SyntheticTerrain>(400, 400);
		ai = std::make_unique<AIWorld>(*logic, aiConfig, sh.world->iniMacros());
		ai->attach();
		ai->newMap(*terrain);
		// lane HERO-2: the partition's region is the terrain's extent (RW 0x62FCCD at a map's start): 400 cells of 10 (getClosestObject's range needs one)
		logic->partition().setRegion(0.0f, 0.0f, 4000.0f, 4000.0f);
		// lane HERO-2: the script engine of the live game (Scripts.lua / ScriptEvents.xml): the abilities that act through script events (Screech's BeScary)
		scripting = std::make_unique<LiveScripting>(*logic, *sh.mount->fs, sh.world->nameKeys());
		REQUIRE_MESSAGE(scripting->start("", &err), err);
	}
	~Game()
	{
		if (scripting)
		{
			scripting->detach();
		}
		if (logic)
		{
			logic->reset(); // the objects (and their AI modules) go while the AIWorld exists
		}
	}
	AIWorldConfig aiConfig;
	std::unique_ptr<pathtest::SyntheticTerrain> terrain;
	std::unique_ptr<AIWorld> ai;
	std::unique_ptr<LiveScripting> scripting;
	Object *make(const ThingTemplate *tt, Player *owner, float x, float y)
	{
		Object *o = logic->newObject(tt, owner->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		const Coord3D at = { x, y, 0.0f };
		o->setPosition(&at);
		return o;
	}
	void select(Object *o)
	{
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, me->getPlayerIndex());
		sel.appendBooleanArgument(true);
		sel.appendObjectIDArgument(o->getID());
		list.append(sel);
	}
	void queueIndex(int index)
	{
		GameMessage m(MSG_QUEUE_UNIT_CREATE, me->getPlayerIndex());
		m.appendBooleanArgument(true);
		m.appendIntegerArgument(index);
		m.appendIntegerArgument(-1);
		m.appendBooleanArgument(false);
		m.appendBooleanArgument(false);
		list.append(m);
	}
	void kill(Object &victim, Object *killer)
	{
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_sourceID = killer ? killer->getID() : 0;
		info.m_input.m_amount = 100000.0f;
		info.m_input.m_kill = true;
		victim.attemptDamage(info);
	}
	Object *liveOf(const ThingTemplate *tt, ObjectID notThis)
	{
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate() == tt && !o->isEffectivelyDead() && o->getID() != notThis)
			{
				return o;
			}
		}
		return nullptr;
	}
};

struct HeroRun
{
	std::string hero;
	int cost = 0, frames = 0, reviveCost = 0, reviveFrames = 0, rankAtDeath = 0;
	bool leveled = false, revived = false;
	int unlockedBefore = 0, unlockedAfter = 0; ///< UnpauseSpecialPowerUpgrade modules executed before / after the level
	int auras = 0, aurasMeasured = 0;          ///< AttributeModifierAuraUpdate modules / those whose ally got the bonus and whose enemy did not
	std::vector<std::string> abilitiesUsed;    ///< SpecialPowerModule powers cast through MSG_DO_SPECIAL_POWER whose effect was measured
	std::vector<std::string> abilitiesOther;   ///< update-driven powers not cast or without a measured effect (reported)
	std::vector<std::string> unportedModules;  ///< lane HERO-2: the hero's modules that are still the UnportedModule placeholder (S-140)
};

// the executed UnpauseSpecialPowerUpgrade modules of a hero; each one's special power must be unpaused
int unlockedPowers(Object &hero, std::vector<std::string> &problems, const std::string &tag)
{
	int n = 0;
	for (const std::unique_ptr<BehaviorModule> &m : hero.modules())
	{
		const UnpauseSpecialPowerUpgrade *u = dynamic_cast<const UnpauseSpecialPowerUpgrade *>(m.get());
		if (!u || !u->isAlreadyUpgraded())
		{
			continue;
		}
		++n;
		const UnpauseSpecialPowerUpgradeModuleData *d = dynamic_cast<const UnpauseSpecialPowerUpgradeModuleData *>(u->muxData());
		for (const std::unique_ptr<BehaviorModule> &m2 : hero.modules())
		{
			const SpecialPowerModule *sp = dynamic_cast<const SpecialPowerModule *>(m2.get());
			if (sp && d && sp->getSpecialPowerTemplate() == d->m_specialPowerTemplate && sp->pauseCount() != 0)
			{
				problems.push_back(tag + ": " + d->m_specialPowerTemplateName + " is unlocked but its power is still paused");
			}
		}
	}
	return n;
}

std::vector<std::string> jsonStrings(const JsonValue *v)
{
	std::vector<std::string> out;
	if (v)
	{
		for (const JsonValue &e : v->array)
		{
			out.push_back(e.string);
		}
	}
	return out;
}

void checkFaction(const std::string &faction)
{
	REQUIRE_RETAIL_WORLD(sh);
	const JsonValue *fj = sh.census.get(faction);
	REQUIRE(fj != nullptr);
	const std::string enemyFaction = faction == "FactionMordor" ? "FactionMen" : "FactionMordor";
	Game g(sh, faction, enemyFaction);
	const PlayerTemplate *pt = g.me->getPlayerTemplate();
	REQUIRE(pt != nullptr);

	// the census lists BuildableHeroesMP then BuildableRingHeroesMP
	std::vector<std::string> both = pt->m_buildableHeroesMP;
	both.insert(both.end(), pt->m_buildableRingHeroesMP.begin(), pt->m_buildableRingHeroesMP.end());
	CHECK(both == jsonStrings(fj->get("buildable_heroes_mp")));

	// the game start: one purchase record per BuildableHeroesMP template, in order
	HeroSystem::initPlayer(*g.logic, *g.me);
	std::vector<std::string> listed;
	for (const HeroRecord &r : g.me->heroes().records())
	{
		listed.push_back(r.templateName);
		CHECK_FALSE(r.dead);
		CHECK(r.startFrame == -1);
		const ThingTemplate *tt = sh.world->things().findTemplate(r.templateName);
		REQUIRE(tt);
		CHECK(r.cost == BuildAssistant::buildCost(*tt));
	}
	CHECK(listed == pt->m_buildableHeroesMP);

	// the ring heroes: the spell book's BuildableHeroListUpgrade, triggered by Upgrade_RingHero
	if (Object *book = SpecialPowerModules::createSpellBook(*g.logic, *g.me))
	{
		const size_t before = g.me->heroes().size();
		book->giveUpgrade("Upgrade_RingHero");
		CHECK(g.me->heroes().size() == before + pt->m_buildableRingHeroesMP.size());
		CHECK(g.me->heroes().records().back().templateName == pt->m_buildableRingHeroesMP.back());
	}
	else
	{
		FAIL_CHECK("no spell book for " << faction);
	}

	// the fortress: the faction's structure with ProductionUpdate whose own CommandSet has the most REVIVE buttons
	const ThingTemplate *fortress = nullptr;
	int fortressRevives = 0;
	for (const std::string &s : jsonStrings(fj->get("structures")))
	{
		const ThingTemplate *tt = sh.world->things().findTemplate(s);
		const std::string *setName = tt ? stringField(*tt, "CommandSet") : nullptr;
		const CommandSet *set = setName ? sh.world->commands().findCommandSet(*setName) : nullptr;
		if (!set || !hasNugget(*tt, "ProductionUpdate"))
		{
			continue;
		}
		const int n = reviveButtons(*set);
		if (n > fortressRevives)
		{
			fortress = tt;
			fortressRevives = n;
		}
	}
	REQUIRE_MESSAGE(fortress != nullptr, faction << " has no structure with REVIVE buttons");
	MESSAGE(faction << ": fortress " << fortress->getName() << " with " << fortressRevives << " REVIVE buttons, " << g.me->heroes().size() << " records");
	Object *keep = g.make(fortress, g.me, 2000.0f, 2000.0f);
	g.select(keep);
	g.logic->runLogicFrame();
	ProductionUpdateInterface *pu = keep->getProductionUpdate();
	REQUIRE(pu != nullptr);
	// with door animations the hero is made once the door waits open: DoorOpeningTime + 1 frames after the progress completes (RW 0x8A0B2F, `>` strict)
	const ProductionUpdate *puImpl = dynamic_cast<const ProductionUpdate *>(pu);
	REQUIRE(puImpl);
	const int doorDelay = puImpl->data()->m_numDoorAnimations > 0 ? (int)puImpl->data()->m_doorOpeningTime + 1 : 0;

	// a victim worth experience: the enemy's first hero
	const ThingTemplate *victimTemplate = nullptr;
	for (const std::string &n : g.enemy->getPlayerTemplate()->m_buildableHeroesMP)
	{
		const ThingTemplate *tt = sh.world->things().findTemplate(n);
		if (tt && n != "CreateAHero")
		{
			victimTemplate = tt;
			break;
		}
	}
	REQUIRE(victimTemplate);

	std::vector<HeroRun> runs;
	std::vector<std::string> problems;
	std::vector<std::string> heroes = pt->m_buildableHeroesMP;
	heroes.insert(heroes.end(), pt->m_buildableRingHeroesMP.begin(), pt->m_buildableRingHeroesMP.end());
	for (const std::string &name : heroes)
	{
		if (name == "CreateAHero")
		{
			continue; // S-852
		}
		const ThingTemplate *tt = sh.world->things().findTemplate(name);
		REQUIRE(tt);
		HeroRun run;
		run.hero = name;
		const std::string tag = faction + "/" + name;
		const int index = g.me->heroes().findIndex(*tt, 0xFFFFFFFFu, 0);
		if (index < 0 || index >= fortressRevives)
		{
			problems.push_back(tag + ": record index " + std::to_string(index) + " not reachable by the fortress's REVIVE buttons");
			continue;
		}
		const int expectedCost = SimMath::truncToInt32(NumericState::pc24Mul((float)BuildAssistant::calcCostToBuild(*tt, g.me, nullptr, (int)BuildAssistant::buildCost(*tt)), pu->heroCostMultiplier(false)));
		const int seconds = SimMath::truncToInt32(BuildAssistant::buildTime(*tt));
		const int expectedFrames = SimMath::truncToInt32(NumericState::pc24Mul((float)BuildAssistant::calcTimeToBuild(*tt, g.me, nullptr, seconds, g.logic->productionSettings(), *g.logic), pu->heroTimeMultiplier(false)));
		const std::uint32_t money = g.me->getMoney()->countMoney();
		g.queueIndex(index);
		g.logic->runLogicFrame();
		if (pu->getProductionCount() != 1)
		{
			problems.push_back(tag + ": not queued (canMakeUnit " + std::to_string((int)BuildAssistant::canMakeUnit(*keep, nullptr, index)) + ")");
			continue;
		}
		run.cost = pu->firstProduction()->cost; // the charge the entry stores (other income can reach the player in the same frame)
		(void)money;
		if (run.cost != expectedCost)
		{
			problems.push_back(tag + ": paid " + std::to_string(run.cost) + " expected " + std::to_string(expectedCost) + " (cost change now " + std::to_string(g.me->getProductionCostChangeBasedOnKindOf(*tt, false)) +
				", modifiers " + std::to_string(g.me->costModifiers().size()) + ", hero multiplier " + std::to_string(pu->heroCostMultiplier(false)) + ")");
		}
		const std::int32_t start = g.me->heroes().at(index)->startFrame;
		Object *hero = nullptr;
		for (int i = 0; i < 3000 && !hero; ++i)
		{
			g.logic->runLogicFrame();
			hero = g.liveOf(tt, INVALID_ID);
		}
		if (!hero)
		{
			problems.push_back(tag + ": no hero was made");
			continue;
		}
		const int heroDoorDelay = hero->isKindOfName("GIANT_BIRD") ? 0 : doorDelay; // a GIANT_BIRD needs no door (RW 0x8A1FC7: DOOR_NONE_NEEDED)
		run.frames = (int)g.logic->getFrame() - start - heroDoorDelay;
		if (run.frames != expectedFrames)
		{
			problems.push_back(tag + ": made after " + std::to_string(run.frames) + " frames, expected " + std::to_string(expectedFrames));
		}
		if (hero->getProducerID() != keep->getID() || hero->getControllingPlayer() != g.me)
		{
			problems.push_back(tag + ": wrong producer / owner");
		}
		RespawnUpdate *ru = dynamic_cast<RespawnUpdate *>(hero->findModule("RespawnUpdate"));
		if (ru && !ru->isInitialSpawn())
		{
			problems.push_back(tag + ": the first spawn is not the initial spawn");
		}
		for (int i = 0; i < 40; ++i)
		{
			g.logic->runLogicFrame(); // the spawn animation and the hero countdown
		}
		// a level by kills
		ExperienceTracker *t = hero->getExperienceTracker();
		REQUIRE(t);
		const int rank0 = t->getRank();
		run.unlockedBefore = unlockedPowers(*hero, problems, tag);
		for (int k = 0; k < 12 && t->getRank() == rank0; ++k)
		{
			Object *victim = g.make(victimTemplate, g.enemy, 2400.0f, 2400.0f);
			g.logic->runLogicFrame(); // the victim's level 1 (its ExperienceAward) is granted in phase 5 (the delayed level grant)
			g.logic->runLogicFrame();
			g.kill(*victim, hero);
			g.logic->runLogicFrame();
		}
		run.leveled = t->getRank() > rank0;
		if (!run.leveled && t->experienceForNextLevel(nullptr) > 0) // a ring hero starts at its last level
		{
			problems.push_back(tag + ": no level gained by 12 kills of " + victimTemplate->getName() + " (experience " + std::to_string(t->getExperience()) + ", rank " + std::to_string(t->getRank()) + ", victim value " +
				std::to_string(g.make(victimTemplate, g.enemy, 2600.0f, 2600.0f)->getExperienceTracker()->getExperienceValue(*hero, false)) + ", next " + std::to_string(t->experienceForNextLevel(nullptr)) + ")");
		}
		for (int i = 0; i < 20; ++i)
		{
			g.logic->runLogicFrame(); // the delayed level grant
		}
		run.unlockedAfter = unlockedPowers(*hero, problems, tag);
		// the rest of the levels (the kills above showed the kill path): every level's abilities are unlocked for the checks below
		t->gainExpForLevel(20, true, false);
		for (int i = 0; i < 20; ++i)
		{
			g.logic->runLogicFrame();
		}
		unlockedPowers(*hero, problems, tag);
		// its abilities: every ready power whose effect is the SpecialPowerModule trigger's own (an AttributeModifier or GiveLevels; not one an update module
		// starts) is cast through MSG_DO_SPECIAL_POWER; the measured effect: the targets it reached (RW 0x89763B) and the attribute modifier on the hero or an ally
		for (const std::unique_ptr<BehaviorModule> &m : hero->modules())
		{
			SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
			if (!sp || !sp->getSpecialPowerTemplate() || sp->spData()->m_updateModuleStartsAttack || (sp->spData()->m_attributeModifier.empty() && sp->spData()->m_giveLevels == 0))
			{
				continue;
			}
			if (sp->pauseCount() != 0 || !sp->isReady() || !sp->requirementsMet() || sp->spData()->m_attributeModifierWeatherBased)
			{
				continue;
			}
			const SpecialPowerModuleData *spd = sp->spData();
			Object *ally = nullptr;
			for (const std::string &n : jsonStrings(fj->get("horde_member_units")))
			{
				const ThingTemplate *mt = sh.world->things().findTemplate(n);
				if (mt && (!spd->m_attributeModifierAffects || ObjectFilterMatch::allows(*g.logic, *spd->m_attributeModifierAffects, mt, spd->m_targetEnemy ? g.enemy : g.me, g.me)))
				{
					const Coord3D hp = *hero->getPosition();
					ally = g.make(mt, spd->m_targetEnemy ? g.enemy : g.me, SimMath::addf32(hp.x, 20.0f), hp.y);
					break;
				}
			}
			const unsigned applied0 = sp->applied();
			const unsigned triggers0 = sp->triggers();
			GameMessage cast(MSG_DO_SPECIAL_POWER, g.me->getPlayerIndex());
			cast.appendIntegerArgument((int)sp->getSpecialPowerTemplate()->getID());
			cast.appendIntegerArgument(0);
			cast.appendObjectIDArgument(hero->getID());
			g.list.append(cast);
			g.logic->runLogicFrame();
			bool measured = sp->applied() > applied0;
			if (measured && !spd->m_attributeModifier.empty())
			{
				const AttributeModifierPool *heroPool = dynamic_cast<const AttributeModifierPool *>(hero->findModule("AttributeModifierPoolUpdate"));
				const AttributeModifierPool *allyPool = ally ? dynamic_cast<const AttributeModifierPool *>(ally->findModule("AttributeModifierPoolUpdate")) : nullptr;
				measured = (heroPool && heroPool->hasList(spd->m_attributeModifier)) || (allyPool && allyPool->hasList(spd->m_attributeModifier));
			}
			if (measured)
			{
				run.abilitiesUsed.push_back(sp->getSpecialPowerTemplate()->getName());
			}
			if (sp->triggers() > triggers0 && sp->isReady() && sp->getSpecialPowerTemplate()->getReloadTime() > 0)
			{
				problems.push_back(tag + ": " + sp->getSpecialPowerTemplate()->getName() + " is still ready after its cast (no recharge)");
			}
			if (ally)
			{
				g.logic->destroyObject(ally);
			}
		}
		// the leadership auras: an allied horde member and an enemy one next to the hero; after a pulse the ally carries BonusName, the enemy does not
		for (const std::unique_ptr<BehaviorModule> &m : hero->modules())
		{
			AttributeModifierAuraUpdate *aura = dynamic_cast<AttributeModifierAuraUpdate *>(m.get());
			if (!aura)
			{
				continue;
			}
			++run.auras;
			const AttributeModifierAuraUpdateModuleData *d = dynamic_cast<const AttributeModifierAuraUpdateModuleData *>(aura->muxData());
			REQUIRE(d);
			if (!aura->isAlreadyUpgraded() && !d->m_triggeredBy.empty())
			{
				hero->giveUpgrade(d->m_triggeredBy.front());
			}
			const ThingTemplate *allyTemplate = nullptr;
			for (const std::string &n : jsonStrings(fj->get("horde_member_units")))
			{
				const ThingTemplate *mt = sh.world->things().findTemplate(n);
				if (mt && (!d->m_objectFilter || ObjectFilterMatch::allows(*g.logic, *d->m_objectFilter, mt, g.me, g.me)))
				{
					allyTemplate = mt;
					break;
				}
			}
			if (!allyTemplate || d->m_affectContainedOnly || d->m_bonusName.empty())
			{
				continue;
			}
			const Coord3D hp = *hero->getPosition();
			const float step = SimMath::mulf32(d->m_range, 0.5f);
			// the side the aura reaches gets the bonus, the other does not (TargetEnemy: the enemy)
			Object *ally = g.make(allyTemplate, d->m_targetEnemy ? g.enemy : g.me, SimMath::addf32(hp.x, step), hp.y);
			Object *foe = g.make(allyTemplate, d->m_targetEnemy ? g.me : g.enemy, SimMath::subf32(hp.x, step), hp.y);
			const unsigned before = aura->pulses();
			for (int i = 0; i < 60 && aura->pulses() == before; ++i)
			{
				g.logic->runLogicFrame();
			}
			const AttributeModifierPool *allyPool = dynamic_cast<const AttributeModifierPool *>(ally->findModule("AttributeModifierPoolUpdate"));
			const AttributeModifierPool *foePool = dynamic_cast<const AttributeModifierPool *>(foe->findModule("AttributeModifierPoolUpdate"));
			const bool ok = aura->pulses() > before && allyPool && allyPool->hasList(d->m_bonusName) && foePool && !foePool->hasList(d->m_bonusName);
			if (ok)
			{
				++run.aurasMeasured;
			}
			else
			{
				problems.push_back(tag + ": the aura " + d->m_bonusName + " did not reach " + allyTemplate->getName() + " (pulses " + std::to_string(aura->pulses() - before) + ")");
			}
			g.logic->destroyObject(ally);
			g.logic->destroyObject(foe);
		}
		if (!ru)
		{
			// a ring hero without RespawnBody / RespawnUpdate (galadriel.ini, sauron.ini: "we do not want to put them back in the respawnable hero list")
			const size_t before = g.me->heroes().size();
			g.kill(*hero, nullptr);
			g.logic->runLogicFrame();
			if (g.me->heroes().size() != before)
			{
				problems.push_back(tag + ": a hero without RespawnUpdate made a revive record");
			}
			runs.push_back(run);
			continue;
		}
		// death: a revive record with the rule of the hero's rank
		run.rankAtDeath = t->getRank();
		const float xp = t->getExperience();
		const unsigned ruleCost = ru->ruleCost();
		const int ruleSeconds = ru->ruleSeconds();
		const ObjectID oldId = hero->getID();
		g.kill(*hero, nullptr);
		g.logic->runLogicFrame();
		const int dead = g.me->heroes().findIndex(*tt, 0xFFFFFFFFu, 0);
		const HeroRecord *r = g.me->heroes().at(dead);
		if (!r || !r->dead || r->cost != ruleCost || r->seconds != ruleSeconds || r->experience != xp || r->rank != run.rankAtDeath)
		{
			problems.push_back(tag + ": the revive record is not as expected");
			runs.push_back(run);
			continue;
		}
		if (dead >= fortressRevives)
		{
			problems.push_back(tag + ": the revive record's index is beyond the REVIVE buttons");
			continue;
		}
		const int reviveCost = g.me->heroes().costAt(*g.logic, *g.me, dead, keep);
		const int reviveFrames = g.me->heroes().framesAt(*g.logic, *g.me, dead, keep);
		const std::uint32_t money2 = g.me->getMoney()->countMoney();
		g.queueIndex(dead);
		g.logic->runLogicFrame();
		run.reviveCost = pu->firstProduction() ? pu->firstProduction()->cost : -1;
		(void)money2;
		if (run.reviveCost != reviveCost)
		{
			problems.push_back(tag + ": revive paid " + std::to_string(run.reviveCost) + " expected " + std::to_string(reviveCost));
		}
		const std::int32_t start2 = g.me->heroes().at(dead) ? g.me->heroes().at(dead)->startFrame : -1;
		Object *back = nullptr;
		for (int i = 0; i < 4000 && !back; ++i)
		{
			g.logic->runLogicFrame();
			back = g.liveOf(tt, oldId);
		}
		if (!back)
		{
			problems.push_back(tag + ": no revived hero");
			runs.push_back(run);
			continue;
		}
		run.reviveFrames = (int)g.logic->getFrame() - start2 - heroDoorDelay;
		if (run.reviveFrames != reviveFrames)
		{
			problems.push_back(tag + ": revived after " + std::to_string(run.reviveFrames) + " frames, expected " + std::to_string(reviveFrames));
		}
		RespawnUpdate *ru2 = dynamic_cast<RespawnUpdate *>(back->findModule("RespawnUpdate"));
		// a new hero starts at its level 1 experience (RequiredExperience 1); the revive adds the record's experience - 1.0 (RW 0x781563)
		run.revived = ru2 && !ru2->isInitialSpawn() && back->getExperienceTracker()->getExperience() == SimMath::addf32(1.0f, SimMath::subf32(xp, 1.0f)) &&
			back->getExperienceTracker()->getRank() == run.rankAtDeath;
		if (!run.revived)
		{
			problems.push_back(tag + ": the revived hero did not keep its experience or played the initial spawn (xp " + std::to_string(xp) + " -> " + std::to_string(back->getExperienceTracker()->getExperience()) + ")");
		}
		for (int i = 0; i < 40; ++i)
		{
			g.logic->runLogicFrame();
		}
		runs.push_back(run);
	}
	std::ostringstream summary;
	for (const HeroRun &r : runs)
	{
		summary << r.hero << " $" << r.cost << "/" << r.frames << "f rank@death " << r.rankAtDeath << " revive $" << r.reviveCost << "/" << r.reviveFrames << "f unlocked "
				<< r.unlockedBefore << "->" << r.unlockedAfter << " auras " << r.aurasMeasured << "/" << r.auras << " used";
		for (const std::string &a : r.abilitiesUsed)
		{
			summary << " " << a;
		}
		if (!r.abilitiesOther.empty())
		{
			summary << " NOT";
			for (const std::string &a : r.abilitiesOther)
			{
				summary << " " << a;
			}
		}
		summary << "; ";
	}
	MESSAGE(faction << ": " << summary.str());
	for (const std::string &p : problems)
	{
		FAIL_CHECK(p);
	}
	CHECK(runs.size() + 1 == heroes.size()); // every hero but Create-A-Hero
	int levelUnlocks = 0;
	for (const HeroRun &r : runs)
	{
		levelUnlocks += r.unlockedAfter > r.unlockedBefore ? 1 : 0;
	}
	CHECK_MESSAGE(levelUnlocks > 0, faction << ": no hero unlocked an ability by its level");
	size_t used = 0, aurasMeasured = 0;
	for (const HeroRun &r : runs)
	{
		used += r.abilitiesUsed.size();
		aurasMeasured += (size_t)r.aurasMeasured;
	}
	CHECK_MESSAGE(used + aurasMeasured >= 2, faction << ": fewer than two hero abilities with a measured effect");
	for (const std::string &e : g.logic->report().errors)
	{
		FAIL_CHECK("logic error: " << e);
	}
}
// lane HERO-1 part 2: the update-driven abilities of one hero (see the comment inside); the cast results go to `used` / `other`
int unlockByUpgrade(Object &hero, const SpecialPowerTemplate *t);
void castUpdateAbilities(Shared &sh, Game &g, const JsonValue *fj, Object *&hero, HeroRun &run)
{
	// lane HERO-1 part 2: the abilities a SpecialAbilityUpdate drives (UpdateModuleStartsAttack): each ready one is cast through the message path
	// (MSG_DO_SPECIAL_POWER_AT_OBJECT at an enemy unit 40 in front of the hero, an ally for LevelGrantSpecialPower), the update runs it to its end, and
	// its effect is measured by its class: WeaponFire damage to the enemy, ToggleMounted the MOUNTED toggle, HeroMode the hero modifier / HERO model
	// condition, LevelGrant experience on the ally, ModelCondition the power's targets / emotion requests, SpecialAbility its attribute modifier or heal
	// lane HERO-2: the powers in module order, the hide toggles (ToggleHiddenSpecialAbilityUpdate) last: a HIDDEN hero cannot start a power whose
	// PreventActivationConditions name HIDDEN (RW 0x75CDC4), so a hide cast first would block the rest
	// lane HERO-2: a rousing speech never ends its ability (RousingSpeechUpdate's update RW 0x8B068D calls no finish: S-1224), so the hero keeps
	// SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING and no other power is ready after it (RW 0x896C83): speeches go last
	std::vector<SpecialPowerModule *> powers, hides, speeches;
	for (const std::unique_ptr<BehaviorModule> &m : hero->modules())
	{
		SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
		// lane HERO-2: also a power whose update module runs alongside its own trigger (UpdateModuleStartsAttack No: RW 0x897E87 initiates the update anyway)
		if (!sp || !sp->getSpecialPowerTemplate() || (!sp->spData()->m_updateModuleStartsAttack && !SpecialAbilityModules::findUpdate(*hero, sp->getSpecialPowerTemplate())))
		{
			continue;
		}
		SpecialPowerUpdateInterface *u = SpecialAbilityModules::findUpdate(*hero, sp->getSpecialPowerTemplate());
		const ToggleMountedSpecialAbilityUpdate *tmu = dynamic_cast<ToggleMountedSpecialAbilityUpdate *>(u);
		const ToggleMountedSpecialAbilityUpdateModuleData *tmud = tmu ? dynamic_cast<const ToggleMountedSpecialAbilityUpdateModuleData *>(tmu->abilityData()) : nullptr;
		if (dynamic_cast<RousingSpeechUpdate *>(u) || (tmud && !tmud->m_mountedTemplate.empty()))
		{
			speeches.push_back(sp); // the speeches and the landings (the hero is replaced) last
			continue;
		}
		const bool hide = dynamic_cast<ToggleHiddenSpecialAbilityUpdate *>(u) != nullptr;
		(hide ? hides : powers).push_back(sp);
	}
	powers.insert(powers.end(), hides.begin(), hides.end());
	powers.insert(powers.end(), speeches.begin(), speeches.end());
	for (SpecialPowerModule *sp : powers)
	{
		const std::string pname = sp->getSpecialPowerTemplate()->getName();
		if (sp->getSpecialPowerTemplate()->m_type == 0x87)
		{
			run.abilitiesOther.push_back(pname + "(fake leadership button)"); // SPECIAL_FAKE_LEADERSHIP_BUTTON: a NONPRESSABLE display button, the leadership is an aura
			continue;
		}
		SpecialAbilityUpdate *su = dynamic_cast<SpecialAbilityUpdate *>(SpecialAbilityModules::findUpdate(*hero, sp->getSpecialPowerTemplate()));
		if (!su)
		{
			run.abilitiesOther.push_back(pname + "(no SpecialAbilityUpdate)");
			continue;
		}
		for (int i = 0; i < 200 && hero->getDisabledMask() != DISABLEDMASK_NONE; ++i)
		{
			g.logic->runLogicFrame(); // a previous ability's FreezeAfterTriggerDuration holds the hero; the gate (RW 0x897987) refuses a held caster
		}
		if (sp->pauseCount() != 0 && unlockByUpgrade(*hero, sp->getSpecialPowerTemplate()) > 0)
		{
			g.logic->runLogicFrame(); // lane HERO-2: a power a purchase / script unlocks (not a level)
		}
		if (sp->pauseCount() == 0 && !sp->isReady())
		{
			sp->setReadyFrame(g.logic->getFrame()); // the recharge since the hero was made (e.g. Athelas' 90 s) is not what is measured
		}
		if (sp->pauseCount() != 0 || !sp->isReady() || !sp->requirementsMet())
		{
			run.abilitiesOther.push_back(pname + "(not ready: pause " + std::to_string(sp->pauseCount()) + " ready " + std::to_string(sp->isReady()) + " req " + std::to_string(sp->requirementsMet()) + " disabled " + std::to_string((unsigned)hero->getDisabledMask()) + ")");
			continue;
		}
		const bool levelGrant = dynamic_cast<LevelGrantSpecialPower *>(su) != nullptr;
		const ThingTemplate *unitTemplate = nullptr;
		for (const std::string &n : jsonStrings(fj->get("horde_member_units")))
		{
			if ((unitTemplate = sh.world->things().findTemplate(n)) != nullptr)
			{
				break;
			}
		}
		REQUIRE(unitTemplate);
		const Coord3D hp = *hero->getPosition();
		// the target: an ally unit for LevelGrant, else the enemy faction's first hero (it survives a hero's hits until the ability lands), held by NO_AUTO_ACQUIRE
		const ThingTemplate *enemyHero = g.enemy->getPlayerTemplate()->m_buildableHeroesMP.empty() ? nullptr : sh.world->things().findTemplate(g.enemy->getPlayerTemplate()->m_buildableHeroesMP.front());
		// a mount toggle (and the disguise, RW 0x8B4760) needs its hero idle (RW 0x8B1690: AI vslot 0x1B8): the target is kept away from it (a touching hero pushes it around)
		float away = dynamic_cast<ToggleMountedSpecialAbilityUpdate *>(su) || dynamic_cast<SpecialDisguiseUpdate *>(su) ? 400.0f : 40.0f;
		if (dynamic_cast<RousingSpeechUpdate *>(su))
		{
			away = 120.0f; // the speech's first ring is (WaveWidth, 2 * WaveWidth]: the update widens the wave before its first trigger (RW 0x8B06F4)
		}
		// lane HERO-2: a Screech (SPECIAL_SCREECH, the BeScary script event) terrifies the enemy's soldiers: its target is the enemy faction's first horde member unit
		const int ptype0 = sp->getSpecialPowerTemplate()->m_type;
		const ThingTemplate *enemyUnit = nullptr;
		if (const JsonValue *ej = sh.census.get(g.enemy->getPlayerTemplate()->getName()))
		{
			for (const std::string &n : jsonStrings(ej->get("horde_member_units")))
			{
				if (!enemyUnit)
				{
					enemyUnit = sh.world->things().findTemplate(n + "Horde"); // the horde: its members' emotions are its EmotionTrackerUpdate's (RW 0x68F383)
				}
			}
		}
		const bool speech = dynamic_cast<RousingSpeechUpdate *>(su) != nullptr; // lane HERO-2: the speech inspires allies (RW 0x8B07EA)
		const ThingTemplate *otherTemplate = levelGrant || speech || !enemyHero ? unitTemplate : enemyHero;
		// lane HERO-2: a ModelConditionSpecialAbilityUpdate (Boromir's horn) acts through the Lua ModelCondition events (UsingSpecialOne ->
		// RadiateUncontrollableFear): its target is the enemy horde too
		if ((ptype0 == 0x89 || dynamic_cast<ModelConditionSpecialAbilityUpdate *>(su)) && enemyUnit)
		{
			otherTemplate = enemyUnit;
		}
		// lane HERO-2: a Dominate's target is the first enemy hero, horde or unit its AttributeModifierAffects allows
		if (const DominateEnemySpecialPower *de = dynamic_cast<const DominateEnemySpecialPower *>(su))
		{
			const DominateEnemySpecialPowerModuleData *dd = dynamic_cast<const DominateEnemySpecialPowerModuleData *>(de->abilityData());
			std::vector<const ThingTemplate *> candidates;
			for (const std::string &n : g.enemy->getPlayerTemplate()->m_buildableHeroesMP)
			{
				candidates.push_back(sh.world->things().findTemplate(n));
			}
			if (const JsonValue *ej = sh.census.get(g.enemy->getPlayerTemplate()->getName()))
			{
				for (const std::string &n : jsonStrings(ej->get("horde_member_units")))
				{
					candidates.push_back(sh.world->things().findTemplate(n + "Horde"));
					candidates.push_back(sh.world->things().findTemplate(n));
				}
			}
			if (dd && dd->m_affects)
			{
				for (const std::string &n : dd->m_affects->includeNames) // a filter of named templates (Rogash's trolls)
				{
					candidates.push_back(sh.world->things().findTemplate(n));
				}
			}
			for (const ThingTemplate *c : candidates)
			{
				if (c && c->getName() != "CreateAHero" && dd && dd->m_affects && ObjectFilterMatch::allows(*g.logic, *dd->m_affects, c, g.enemy, g.me))
				{
					otherTemplate = c;
					break;
				}
			}
		}
		// lane HERO-2: a capture (SPECIAL_INFANTRY_CAPTURE_BUILDING) targets a neutral capture flag with its linked building beside it (the closest
		// LINKED_TO_FLAG object within 150 of the flag, RW 0x854573)
		const bool capture = ptype0 == 0x1D;
		Object *linked = nullptr;
		Player *neutral = g.players.getNeutralPlayer();
		if (capture)
		{
			otherTemplate = sh.world->things().findTemplate("CaptureFlag");
			REQUIRE(otherTemplate);
			const ThingTemplate *outpost = sh.world->things().findTemplate("Outpost");
			REQUIRE(outpost);
			linked = g.make(outpost, neutral, SimMath::addf32(hp.x, 140.0f), hp.y);
		}
		Object *other = g.make(otherTemplate, capture ? neutral : levelGrant || speech ? g.me : g.enemy, SimMath::addf32(hp.x, away), hp.y);
		const ObjectID linkedId = linked ? linked->getID() : INVALID_ID;
		other->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE"), true);
		hero->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE"), true);
		const ObjectID otherId = other->getID();
		if (levelGrant)
		{
			const LevelGrantSpecialPowerModuleData *ld = dynamic_cast<const LevelGrantSpecialPowerModuleData *>(su->abilityData());
			if (ld && ld->m_acceptanceFilter && !ObjectFilterMatch::allows(*g.logic, *ld->m_acceptanceFilter, *other, g.me))
			{
				g.logic->destroyObject(other);
				other = nullptr;
				for (Object *o = g.logic->getFirstObject(); o && !other; o = o->getNextObject())
				{
					if (o != hero && o->getControllingPlayer() == g.me && !o->isEffectivelyDead() && ObjectFilterMatch::allows(*g.logic, *ld->m_acceptanceFilter, *o, g.me))
					{
						other = o;
						const Coord3D at = { SimMath::addf32(hp.x, 40.0f), hp.y, 0.0f };
						other->setPosition(&at);
					}
				}
			}
		}
		BodyModuleInterface *heroBody = hero->getBodyModule();
		if (heroBody && heroBody->getHealth() >= heroBody->getMaxHealth())
		{
			DamageInfo hurt; // a wound for the heals to show
			hurt.m_input.m_damageType = DAMAGE_UNRESISTABLE;
			hurt.m_input.m_amount = SimMath::mulf32(heroBody->getMaxHealth(), 0.4f);
			hero->attemptDamage(hurt);
		}
		if (su->abilityData()->m_instant)
		{
			g.logic->runLogicFrame(); // lane HERO-2: an instant ability acts within the cast's frame: the partition links the new target first (its update phase)
		}
		const float heroHealth0 = heroBody ? heroBody->getHealth() : 0.0f;
		const float otherHealth0 = other && other->getBodyModule() ? other->getBodyModule()->getHealth() : 0.0f;
		const float otherXP0 = other && other->getExperienceTracker() ? other->getExperienceTracker()->getExperience() : 0.0f;
		const bool mounted0 = hero->testModelCondition(CombatNames::modelCondition("MOUNTED"));
		const bool disguised0 = hero->testModelCondition(300); // DISGUISED
		const bool hidden0 = hero->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("HIDDEN"));
		const unsigned applied0 = sp->applied();
		const unsigned triggered0 = su->abilitiesTriggered();
		const AutoHealBehavior *heal = dynamic_cast<const AutoHealBehavior *>(hero->findModule("AutoHealBehavior"));
		const unsigned heals0 = heal ? heal->heals() : 0;
		const int ptype = sp->getSpecialPowerTemplate()->m_type;
		const ToggleMountedSpecialAbilityUpdateModuleData *tmd = dynamic_cast<const ToggleMountedSpecialAbilityUpdateModuleData *>(su->abilityData());
		if (tmd && !tmd->m_mountedTemplate.empty())
		{
			// lane HERO-2: a landing (MountedTemplate: the fell beasts) is cast at a spot next to the hero (StartAbilityRange 50)
			GameMessage cast(MSG_DO_SPECIAL_POWER_AT_LOCATION, g.me->getPlayerIndex());
			cast.appendIntegerArgument((int)sp->getSpecialPowerTemplate()->getID());
			const Coord3D spot = { SimMath::addf32(hp.x, 20.0f), hp.y, 0.0f };
			cast.appendLocationArgument(spot);
			cast.appendObjectIDArgument(INVALID_ID);
			cast.appendIntegerArgument(0);
			cast.appendObjectIDArgument(hero->getID());
			g.list.append(cast);
		}
		else if (other && !levelGrant && su->abilityData()->m_startAbilityRange < 10000000.0f && ptype != 0x80 && ptype != 0x8F) // the Athelas / Elven Grace heals: no target
		{
			GameMessage cast(MSG_DO_SPECIAL_POWER_AT_OBJECT, g.me->getPlayerIndex());
			cast.appendIntegerArgument((int)sp->getSpecialPowerTemplate()->getID());
			cast.appendObjectIDArgument(other->getID());
			cast.appendIntegerArgument(0);
			cast.appendObjectIDArgument(hero->getID());
			g.list.append(cast);
		}
		else if (other && levelGrant)
		{
			GameMessage cast(MSG_DO_SPECIAL_POWER_AT_OBJECT, g.me->getPlayerIndex());
			cast.appendIntegerArgument((int)sp->getSpecialPowerTemplate()->getID());
			cast.appendObjectIDArgument(other->getID());
			cast.appendIntegerArgument(0);
			cast.appendObjectIDArgument(hero->getID());
			g.list.append(cast);
		}
		else
		{
			GameMessage cast(MSG_DO_SPECIAL_POWER, g.me->getPlayerIndex());
			cast.appendIntegerArgument((int)sp->getSpecialPowerTemplate()->getID());
			cast.appendIntegerArgument(0);
			cast.appendObjectIDArgument(hero->getID());
			g.list.append(cast);
		}
		bool terrified = false; // the target's EmotionTrackerUpdate got a TERROR request (RadiateTerrorEx -> BeTerrified -> ObjectEnterRunAwayPanicState)
		bool afraid = false;    // an UNCONTROLLABLE_FEAR request (RadiateUncontrollableFear -> BeUncontrollablyAfraid)
		auto sampleTerror = [&]() {
			Object *o = g.logic->findObjectByID(otherId);
			EmotionTrackerUpdate *tr = o ? EmotionTrackerUpdate::of(*o) : nullptr;
			// the request, or the nugget it started (an instant screech's request can be taken within its own frame)
			terrified = terrified || (tr && (tr->requested(EMOTION_TERROR) || (tr->current() && tr->current()->type() == EMOTION_TERROR)));
			afraid = afraid || (tr && (tr->requested(EMOTION_UNCONTROLLABLE_FEAR) || (tr->current() && tr->current()->type() == EMOTION_UNCONTROLLABLE_FEAR)));
		};
		// lane HERO-2: a landing replaces the hero and its modules go with it: the loops look the hero up by id and remember the replacement
		const ObjectID heroId = hero->getID();
		ObjectID replacementId = INVALID_ID;
		const ToggleMountedSpecialAbilityUpdate *tmLanding = tmd && !tmd->m_mountedTemplate.empty() ? dynamic_cast<const ToggleMountedSpecialAbilityUpdate *>(su) : nullptr;
		const std::string landingTemplate = tmLanding ? tmd->m_mountedTemplate : std::string();
		auto heroAlive = [&]() {
			Object *h = g.logic->findObjectByID(heroId);
			if (!landingTemplate.empty() && replacementId == INVALID_ID)
			{
				for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
				{
					if (o->getProducerID() == heroId && o->getTemplate()->getName() == landingTemplate) // Construction::buildObjectNow: the builder is the producer
					{
						replacementId = o->getID();
					}
				}
			}
			return h != nullptr && !h->isDestroyed();
		};
		g.logic->runLogicFrame();
		sampleTerror();
		bool started = heroAlive() && (su->isActive() || su->abilitiesTriggered() > triggered0);
		for (int i = 0; i < 600 && heroAlive() && su->isActive(); ++i)
		{
			g.logic->runLogicFrame();
			sampleTerror();
		}
		for (int i = 0; i < 10; ++i)
		{
			g.logic->runLogicFrame(); // the shot's flight, the heal pulse
			sampleTerror();
		}
		if (!g.logic->findObjectByID(heroId))
		{
			// the hero was replaced (its landing): measured here, its modules are gone
			Object *rep = replacementId != INVALID_ID ? g.logic->findObjectByID(replacementId) : nullptr;
			if (rep)
			{
				run.abilitiesUsed.push_back(pname + "[replaced by " + rep->getTemplate()->getName() + "]");
			}
			else
			{
				run.abilitiesOther.push_back(pname + "(the hero is gone, no replacement)");
			}
			if (Object *o = g.logic->findObjectByID(otherId))
			{
				g.logic->destroyObject(o);
			}
			hero = rep;
			REQUIRE(hero);
			return;
		}
		Object *otherNow = g.logic->findObjectByID(otherId);
		const AttributeModifierPool *heroPool = dynamic_cast<const AttributeModifierPool *>(hero->findModule("AttributeModifierPoolUpdate"));
		std::string effect;
		if (const WeaponFireSpecialAbilityUpdate *wf = dynamic_cast<const WeaponFireSpecialAbilityUpdate *>(su))
		{
			const bool hurt = !otherNow || otherNow->isEffectivelyDead() || (otherNow->getBodyModule() && otherNow->getBodyModule()->getHealth() < otherHealth0);
			if (wf->shotsFired() > 0 && hurt)
			{
				effect = "damage";
			}
			else if (wf->shotsFired() > 0)
			{
				effect = "fired";
			}
		}
		else if (const ToggleMountedSpecialAbilityUpdate *tm = dynamic_cast<const ToggleMountedSpecialAbilityUpdate *>(su))
		{
			(void)tm; // a landing that replaced the hero was measured above
			if (hero->testModelCondition(CombatNames::modelCondition("MOUNTED")) != mounted0)
			{
				effect = mounted0 ? "dismounted" : "mounted";
			}
		}
		else if (dynamic_cast<const ToggleHiddenSpecialAbilityUpdate *>(su))
		{
			// lane HERO-2: the hide toggle (lane STEALTH-2) toggles HIDDEN
			if (hero->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("HIDDEN")) != hidden0)
			{
				effect = hidden0 ? "unhidden" : "hidden";
			}
		}
		else if (const DominateEnemySpecialPower *de = dynamic_cast<const DominateEnemySpecialPower *>(su))
		{
			// lane HERO-2: the target (or its horde) went over to the hero's side
			if (de->dominated() > 0 && otherNow && otherNow->getControllingPlayer() == g.me)
			{
				effect = "dominated";
			}
		}
		else if (capture)
		{
			Object *lk = g.logic->findObjectByID(linkedId);
			if (su->captures() > 0 && lk && lk->getControllingPlayer() == g.me && otherNow && otherNow->getControllingPlayer() == g.me)
			{
				effect = "captured";
			}
			if (lk)
			{
				g.logic->destroyObject(lk);
			}
			if (otherNow)
			{
				g.logic->destroyObject(otherNow);
				otherNow = nullptr;
			}
		}
		else if (const TeleportSpecialAbilityUpdate *tp = dynamic_cast<const TeleportSpecialAbilityUpdate *>(su))
		{
			// lane HERO-2: the hero stands at the target location (RW 0x8964A0)
			const Coord3D &now = *hero->getPosition();
			if (tp->teleports() > 0 && (now.x != hp.x || now.y != hp.y))
			{
				effect = "teleported";
			}
		}
		else if (const RousingSpeechUpdate *rs = dynamic_cast<const RousingSpeechUpdate *>(su))
		{
			// lane HERO-2: the allies within the speech's waves got its ModifierName (RW 0x8B0A0D)
			if (rs->followersInspired() > 0)
			{
				effect = "inspired " + std::to_string(rs->followersInspired());
			}
		}
		else if (const ArrowStormUpdate *as = dynamic_cast<const ArrowStormUpdate *>(su))
		{
			// lane HERO-2: the volley's temporary weapons (RW 0x893EC8) hurt the target
			const bool hurt = !otherNow || otherNow->isEffectivelyDead() || (otherNow->getBodyModule() && otherNow->getBodyModule()->getHealth() < otherHealth0);
			if (as->shots() > 0)
			{
				effect = (hurt ? "damage " : "shots ") + std::to_string(as->shots());
			}
		}
		else if (const CurseSpecialPower *cs = dynamic_cast<const CurseSpecialPower *>(su))
		{
			// lane HERO-2: the enemy hero's powers restart their recharge (RW 0x8D12E3)
			if (cs->cursed() > 0)
			{
				effect = "cursed";
			}
		}
		else if (const ActivateModuleSpecialPower *am = dynamic_cast<const ActivateModuleSpecialPower *>(su))
		{
			// lane HERO-2: the triggered power modules ran at the location (RW 0x8D222A)
			if (am->activations() > 0)
			{
				effect = "activated " + std::to_string(am->activations());
			}
		}
		else if (dynamic_cast<const SpecialDisguiseUpdate *>(su))
		{
			// lane HERO-2: the disguise toggles DISGUISED (model condition 300); ForceMountedWhenDisguising mounts (RW 0x8B4760 / 0x8B45BF)
			if (hero->testModelCondition(300) != disguised0)
			{
				effect = disguised0 ? "undisguised" : (hero->testModelCondition(CombatNames::modelCondition("MOUNTED")) ? "disguised mounted" : "disguised");
			}
		}
		else if (const HeroModeSpecialAbilityUpdate *hm = dynamic_cast<const HeroModeSpecialAbilityUpdate *>(su))
		{
			const HeroModeSpecialAbilityUpdateModuleData *hd = dynamic_cast<const HeroModeSpecialAbilityUpdateModuleData *>(hm->abilityData());
			if (hd && !hd->m_heroAttributeModifier.empty() && heroPool && heroPool->hasList(hd->m_heroAttributeModifier))
			{
				effect = "modifier " + hd->m_heroAttributeModifier;
			}
			else if (hero->testModelCondition(CombatNames::modelCondition(hd && hd->m_useUserModelCondition ? "USER_3" : "HERO")))
			{
				effect = "hero mode";
			}
		}
		else if (levelGrant)
		{
			if (otherNow && otherNow->getExperienceTracker() && otherNow->getExperienceTracker()->getExperience() > otherXP0)
			{
				effect = "experience";
			}
		}
		else if (const ModelConditionSpecialAbilityUpdate *mc = dynamic_cast<const ModelConditionSpecialAbilityUpdate *>(su))
		{
			if (sp->applied() > applied0 || mc->emotionRequests() > 0)
			{
				effect = "power targets " + std::to_string(sp->applied() - applied0);
			}
			else if (afraid || terrified)
			{
				effect = afraid ? "uncontrollable fear" : "terror"; // lane HERO-2: the Lua ModelCondition event's broadcast
			}
		}
		if (effect.empty() && ptype == 0x89 && terrified)
		{
			effect = "terror";
		}
		if (effect.empty())
		{
			// SpecialAbilityUpdate itself (or a subclass whose own effect did not show): the trigger's attribute modifier, the AutoHeal button, the power's targets
			const std::string &tam = su->abilityData()->m_triggerAttributeModifier;
			if (!tam.empty() && heroPool && heroPool->hasList(tam))
			{
				effect = "modifier " + tam;
			}
			else if ((sp->getSpecialPowerTemplate()->m_type == 0x80 || sp->getSpecialPowerTemplate()->m_type == 0x8F) && heal && heal->heals() > heals0 && heroBody && heroBody->getHealth() > heroHealth0)
			{
				effect = "heal";
			}
			else if (sp->applied() > applied0)
			{
				effect = "power targets " + std::to_string(sp->applied() - applied0);
			}
		}
		if (!effect.empty())
		{
			run.abilitiesUsed.push_back(pname + "[" + effect + "]");
		}
		else
		{
			run.abilitiesOther.push_back(pname + (started ? "(triggered " + std::to_string(su->abilitiesTriggered() - triggered0) + ", no effect measured)" : "(not started)"));
		}
		if (otherNow && (otherNow->getControllingPlayer() == g.enemy || effect == "dominated"))
		{
			g.logic->destroyObject(otherNow);
		}
		for (int i = 0; i < 5; ++i)
		{
			g.logic->runLogicFrame();
		}
	}
}

// lane HERO-2: a paused power whose UnpauseSpecialPowerUpgrade the levels did not trigger is unlocked through that module's TriggeredBy (the upgrade
// a purchase or a script gives in a game); returns the number of upgrades given
int unlockByUpgrade(Object &hero, const SpecialPowerTemplate *t)
{
	int n = 0;
	for (const std::unique_ptr<BehaviorModule> &m : hero.modules())
	{
		const UnpauseSpecialPowerUpgrade *u = dynamic_cast<const UnpauseSpecialPowerUpgrade *>(m.get());
		const UnpauseSpecialPowerUpgradeModuleData *d = u ? dynamic_cast<const UnpauseSpecialPowerUpgradeModuleData *>(u->muxData()) : nullptr;
		if (!d || d->m_specialPowerTemplate != t || u->isAlreadyUpgraded())
		{
			continue;
		}
		for (const std::string &up : d->m_triggeredBy)
		{
			hero.giveUpgrade(up);
			++n;
		}
	}
	return n;
}

// lane HERO-2: the powers no SpecialAbilityUpdate drives (UpdateModuleStartsAttack: No): each ready one is cast at the enemy's location through
// MSG_DO_SPECIAL_POWER_AT_LOCATION and measured by its class: an attribute modifier / level power its targets (RW 0x89763B), OCLSpecialPower the objects
// it made, PlayerHealSpecialPower the objects it healed, SpecialPowerTimerRefreshSpecialPower the refreshes, the rest the targets
void castOtherPowers(Shared &sh, Game &g, const JsonValue *fj, Object *hero, HeroRun &run)
{
	(void)fj;
	for (const std::unique_ptr<BehaviorModule> &m : hero->modules())
	{
		SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get());
		if (!sp || !sp->getSpecialPowerTemplate() || sp->spData()->m_updateModuleStartsAttack || SpecialAbilityModules::findUpdate(*hero, sp->getSpecialPowerTemplate()))
		{
			continue;
		}
		const std::string pname = sp->getSpecialPowerTemplate()->getName();
		const SpecialPowerModuleData *spd = sp->spData();
		if (typeid(*sp) == typeid(SpecialPowerModule) && spd->m_attributeModifier.empty() && spd->m_giveLevels == 0 && spd->m_setModelCondition.empty())
		{
			// a base SpecialPowerModule with no effect of its own: the button of a passive (its unlock upgrade starts an aura / a modifier), e.g. Dain's Stubborn
			// Pride, Sharku's Blood Hunt
			run.abilitiesOther.push_back(pname + "(passive button)");
			continue;
		}
		if (sp->getSpecialPowerTemplate()->m_type == 0x87)
		{
			run.abilitiesOther.push_back(pname + "(fake leadership button)");
			continue;
		}
		for (int i = 0; i < 200 && hero->getDisabledMask() != DISABLEDMASK_NONE; ++i)
		{
			g.logic->runLogicFrame();
		}
		if (sp->pauseCount() != 0 && unlockByUpgrade(*hero, sp->getSpecialPowerTemplate()) > 0)
		{
			g.logic->runLogicFrame();
		}
		if (sp->pauseCount() == 0 && !sp->isReady())
		{
			sp->setReadyFrame(g.logic->getFrame());
		}
		if (sp->pauseCount() != 0 || !sp->isReady() || !sp->requirementsMet())
		{
			run.abilitiesOther.push_back(pname + "(other power not ready: pause " + std::to_string(sp->pauseCount()) + ")");
			continue;
		}
		const Coord3D hp = *hero->getPosition();
		const ThingTemplate *enemyHero = g.enemy->getPlayerTemplate()->m_buildableHeroesMP.empty() ? nullptr : sh.world->things().findTemplate(g.enemy->getPlayerTemplate()->m_buildableHeroesMP.back());
		Object *foe = enemyHero ? g.make(enemyHero, g.enemy, SimMath::addf32(hp.x, 40.0f), hp.y) : nullptr;
		if (foe)
		{
			foe->setStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("NO_AUTO_ACQUIRE"), true);
		}
		const ObjectID foeId = foe ? foe->getID() : INVALID_ID;
		const unsigned applied0 = sp->applied();
		const unsigned triggers0 = sp->triggers();
		const SpecialPowerTimerRefreshSpecialPower *refresh = dynamic_cast<const SpecialPowerTimerRefreshSpecialPower *>(sp);
		const unsigned refreshed0 = refresh ? refresh->refreshed() : 0;
		const size_t objects0 = g.logic->getObjectCount();
		const Coord3D at = foe ? *foe->getPosition() : hp;
		GameMessage cast(MSG_DO_SPECIAL_POWER_AT_LOCATION, g.me->getPlayerIndex());
		cast.appendIntegerArgument((int)sp->getSpecialPowerTemplate()->getID());
		cast.appendLocationArgument(at);
		cast.appendObjectIDArgument(INVALID_ID);
		cast.appendIntegerArgument(0);
		cast.appendObjectIDArgument(hero->getID());
		g.list.append(cast);
		g.logic->runLogicFrame();
		std::string effect;
		if (const OCLSpecialPower *ocl = dynamic_cast<const OCLSpecialPower *>(sp))
		{
			if (!ocl->created().empty() || g.logic->getObjectCount() > objects0)
			{
				effect = "objects " + std::to_string(ocl->created().size());
			}
		}
		else if (const PlayerHealSpecialPower *ph = dynamic_cast<const PlayerHealSpecialPower *>(sp))
		{
			if (ph->lastHealed() > 0 || sp->applied() > applied0)
			{
				effect = "healed " + std::to_string(ph->lastHealed());
			}
		}
		else if (refresh)
		{
			if (refresh->refreshed() > refreshed0)
			{
				effect = "refreshed " + std::to_string(refresh->refreshed() - refreshed0);
			}
		}
		if (effect.empty() && sp->applied() > applied0)
		{
			effect = "power targets " + std::to_string(sp->applied() - applied0);
		}
		if (!effect.empty())
		{
			run.abilitiesUsed.push_back(pname + "[" + effect + "]");
		}
		else
		{
			run.abilitiesOther.push_back(pname + (sp->triggers() > triggers0 ? "(other power triggered, no effect measured)" : "(other power not started)"));
		}
		if (Object *f = g.logic->findObjectByID(foeId))
		{
			g.logic->destroyObject(f);
		}
		for (int i = 0; i < 5; ++i)
		{
			g.logic->runLogicFrame();
		}
	}
}

// lane HERO-1 part 2: every hero of the faction's lists (Create-A-Hero aside), made next to an enemy in a game with an AIWorld, reaches its last level and casts each
// of its SpecialAbilityUpdate-driven abilities through the message path; each must show its effect (the stops S-860 / S-861 name the unported ability kinds)
void checkAbilities(const std::string &faction)
{
	REQUIRE_RETAIL_WORLD(sh);
	const JsonValue *fj = sh.census.get(faction);
	REQUIRE(fj != nullptr);
	const std::string enemyFaction = faction == "FactionMordor" ? "FactionMen" : "FactionMordor";
	Game g(sh, faction, enemyFaction, true);
	const PlayerTemplate *pt = g.me->getPlayerTemplate();
	std::vector<std::string> both = pt->m_buildableHeroesMP;
	both.insert(both.end(), pt->m_buildableRingHeroesMP.begin(), pt->m_buildableRingHeroesMP.end());
	std::vector<HeroRun> runs;
	float x = 600.0f;
	for (const std::string &name : both)
	{
		const ThingTemplate *tt = sh.world->things().findTemplate(name);
		if (!tt || name == "CreateAHero")
		{
			continue;
		}
		HeroRun run;
		run.hero = name;
		Object *hero = g.make(tt, g.me, x, 1500.0f);
		x = SimMath::addf32(x, 400.0f);
		for (int i = 0; i < 10; ++i)
		{
			g.logic->runLogicFrame();
		}
		if (ExperienceTracker *t = hero->getExperienceTracker())
		{
			t->gainExpForLevel(20, true, false);
		}
		for (int i = 0; i < 30; ++i)
		{
			g.logic->runLogicFrame();
		}
		for (const std::unique_ptr<BehaviorModule> &m : hero->modules())
		{
			if (m->isUnported() && !m->isHelper())
			{
				run.unportedModules.push_back(m->getModuleClassName());
			}
		}
		castUpdateAbilities(sh, g, fj, hero, run);
		castOtherPowers(sh, g, fj, hero, run); // lane HERO-2
		runs.push_back(run);
		g.logic->destroyObject(hero);
		g.logic->runLogicFrame();
	}
	std::ostringstream summary;
	size_t measured = 0, other = 0;
	for (const HeroRun &r : runs)
	{
		summary << r.hero << ":";
		for (const std::string &a : r.abilitiesUsed)
		{
			summary << " " << a;
		}
		for (const std::string &a : r.abilitiesOther)
		{
			summary << " NOT " << a;
		}
		for (const std::string &a : r.unportedModules)
		{
			summary << " UNPORTED " << a;
		}
		summary << "; ";
		measured += r.abilitiesUsed.size();
		other += r.abilitiesOther.size();
	}
	MESSAGE(faction << " abilities (" << measured << " measured, " << other << " not): " << summary.str());
	// the abilities whose effect must show (the lane's acceptance list); the rest that show none are S-863's (reported above, counted below)
	static const std::map<std::string, std::vector<std::string>> kRequired = {
		{ "FactionMen", { "SpecialAbilityAragornElendil", "SpecialAbilityAragornAthelas", "SpecialAbilityWizardBlast", "SpecialAbilityIstariLight", "SpecialAbilityToggleMounted",
							"SpecialAbilityKingsFavor", "SpecialAbilityTheodenGloriousCharge", "SpecialAbilitySmite", "SpecialAbilitySpearThrow", "SpecialAbilityWoundArrow",
							"SpecialAbilityWordOfPower", "SpecialAbilityScreech", "SpecialAbilityDisguise" } },
		{ "FactionElves", { "SpecialAbilityHaldirGoldenArrow", "SpecialAbilityTrainArchers", "SpecialAbilityThranduilDeadeye", "SpecialAbilityToggleMounted",
							  "SpecialAbilityHawkStrike", "SpecialAbilityThornVengeance", "SpecialAbilityElrondElvenGrace", "SpecialAbilityScreech" } },
		{ "FactionDwarves", { "SpecialAbilityDwarvenGloinSlam", "SpecialAbilityGimliLeap", "SpecialAbilityDaleSlamArrow", "SpecialAbilityScreech" } },
		{ "FactionIsengard", { "SpecialAbilitySarumanFireball", "SpecialAbilityLightingBolt", "SpecialAbilityWormtongueBackstab", "SpecialAbilityLurtzCripple",
								 "SpecialPowerTelekeneticPush", "SpecialAbilityScreech" } },
		{ "FactionMordor", { "SpecialAbilitySarumanFireball", "SpecialAbilityToggleMounted", "SpecialAbilityScreech" } },
		{ "FactionWild", { "SpecialAbilityWildShelobPoisonedStinger", "SpecialAbilityDrogothFireflight", "SpecialAbilityToggleMounted", "SpecialAbilityScreech" } },
		{ "FactionAngmar", { "SpecialAbilityWitchkingMorgulBlade", "SpecialAbilityToggleMounted", "SpecialAbilityRogashLeap", "SpecialAbilityAngmarMorgramirRuin",
							   "SpecialAbilityBlackRiderMorgulBlade", "SpecialAbilityWhisperofDeath", "SpecialAbilityScreech" } },
	};
	auto it = kRequired.find(faction);
	REQUIRE(it != kRequired.end());
	std::vector<std::string> needs = it->second;
	needs.push_back("SpecialAbilityCaptureBuilding"); // lane HERO-2: every hero captures (S-1225)
	for (const std::string &need : needs)
	{
		bool found = false;
		for (const HeroRun &r : runs)
		{
			for (const std::string &a : r.abilitiesUsed)
			{
				found = found || a.rfind(need + "[", 0) == 0;
			}
		}
		CHECK_MESSAGE(found, faction << ": " << need << " showed no effect");
	}
	size_t unexplained = 0;
	for (const HeroRun &r : runs)
	{
		for (const std::string &a : r.abilitiesOther)
		{
			// explained (S-863): a passive / fake button, a power another update class drives, the run's own conditions (no listed archer near Train Archers, the
			// MOUNTED RequiredConditions of Wind Rider and Poisoned Stinger, no ally in Move Unseen's radius, the OCL of Part the Heavens), the level attack
			// (S-1223), ActivateModuleSpecialPower's dummy target power; the rest (Faramir's mount toggle in the faction run) are S-863's open entries
			static const char *const kExplained[] = { "CaptureBuilding", "(no SpecialAbilityUpdate)", "FromFellBeast", "(passive button)", "(fake leadership button)",
				"TrainArchers", "WindRider", "PoisonedStinger", "MoveUnseen", "PartTheHeavens", "LevelAttack", "ActivateeDummy" };
			bool explained = false;
			for (const char *e : kExplained)
			{
				explained = explained || a.find(e) != std::string::npos;
			}
			// Faramir attacks his target at both triggers of his mount toggle: the retail idle gate (RW 0x8B1690, the AI's isIdle) refuses the mount
			explained = explained || (r.hero == "GondorFaramir" && a.find("SpecialAbilityToggleMounted") != std::string::npos);
			unexplained += explained ? 0 : 1;
		}
	}
	MESSAGE(faction << ": " << unexplained << " update-driven abilities without a measured effect (S-863)");
	CHECK(unexplained == 0u); // lane HERO-2: every unmeasured entry is explained (S-863)
	for (const std::string &e : g.logic->report().errors)
	{
		FAIL_CHECK("logic error: " << e);
	}
}
} // namespace

TEST_CASE("hero retail: FactionMen") { checkFaction("FactionMen"); }
TEST_CASE("hero retail: FactionElves") { checkFaction("FactionElves"); }
TEST_CASE("hero retail: FactionDwarves") { checkFaction("FactionDwarves"); }
TEST_CASE("hero retail: FactionIsengard") { checkFaction("FactionIsengard"); }
TEST_CASE("hero retail: FactionMordor") { checkFaction("FactionMordor"); }
TEST_CASE("hero retail: FactionWild") { checkFaction("FactionWild"); }
TEST_CASE("hero retail: FactionAngmar") { checkFaction("FactionAngmar"); }

TEST_CASE("hero abilities retail: FactionMen") { checkAbilities("FactionMen"); }
TEST_CASE("hero abilities retail: FactionElves") { checkAbilities("FactionElves"); }
TEST_CASE("hero abilities retail: FactionDwarves") { checkAbilities("FactionDwarves"); }
TEST_CASE("hero abilities retail: FactionIsengard") { checkAbilities("FactionIsengard"); }
TEST_CASE("hero abilities retail: FactionMordor") { checkAbilities("FactionMordor"); }
TEST_CASE("hero abilities retail: FactionWild") { checkAbilities("FactionWild"); }
TEST_CASE("hero abilities retail: FactionAngmar") { checkAbilities("FactionAngmar"); }



// lane HERO-2: DamageFilteredCreateObjectDie (RW 0x8892B0): a hero killed by a BECOME_UNDEAD hit (the Black Rider's Morgul blade poison, DamageSubType) becomes
// the killer's Barrow Wight (OCL_BecomeUndead with the killer as primary) and its body is destroyed; a normal kill leaves it alone
TEST_CASE("hero retail: a BECOME_UNDEAD kill turns a hero into the killer's Barrow Wight (DamageFilteredCreateObjectDie, RW 0x8892B0)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(sh, "FactionMen", "FactionMordor");
	const ThingTemplate *eowyn = sh.world->things().findTemplate("RohanEowyn");
	const ThingTemplate *rider = sh.world->things().findTemplate("EvilMenBlackRider");
	const ThingTemplate *wight = sh.world->things().findTemplate("BarrowWight");
	REQUIRE(eowyn);
	REQUIRE(rider);
	REQUIRE(wight);
	REQUIRE(hasNugget(*eowyn, "DamageFilteredCreateObjectDie"));
	Object *killer = g.make(rider, g.enemy, 600.0f, 600.0f);
	auto wights = [&]() {
		int n = 0;
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			n += o->getTemplate() == wight && o->getControllingPlayer() == g.enemy && !o->isEffectivelyDead() ? 1 : 0;
		}
		return n;
	};
	auto hit = [&](Object &victim, int subType) {
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_damageSubType = subType;
		info.m_input.m_sourceID = killer->getID();
		info.m_input.m_amount = 100000.0f;
		info.m_input.m_kill = true;
		victim.attemptDamage(info);
	};
	g.logic->runLogicFrame();
	Object *undead = g.make(eowyn, g.me, 620.0f, 600.0f);
	const ObjectID undeadId = undead->getID();
	g.logic->runLogicFrame();
	const int before = wights();
	hit(*undead, 1); // BECOME_UNDEAD (RW 0xDAF5CC index 1)
	g.logic->runLogicFrame();
	g.logic->runLogicFrame();
	CHECK(wights() == before + 1);
	CHECK(g.logic->findObjectByID(undeadId) == nullptr); // destroyed (RW 0x889336)
	// a NORMAL kill: no wight
	Object *plain = g.make(eowyn, g.me, 640.0f, 600.0f);
	g.logic->runLogicFrame();
	hit(*plain, 0);
	g.logic->runLogicFrame();
	g.logic->runLogicFrame();
	CHECK(wights() == before + 1);
}

// lane HERO-2: AutoAbilityBehavior (RW 0x85D9D1): MSG_DO_AUTO_ABILITY (RW 0x77B9BA) toggles Eowyn's Smite (AutoAbility = Yes) on; with an enemy hero in sight
// the module casts it by itself (Object::doSpecialPowerAtObject with AUTO_ABILITY_TRIGGERED); a second message toggles it off
TEST_CASE("hero retail: MSG_DO_AUTO_ABILITY makes Eowyn cast Smite by herself at an enemy (AutoAbilityBehavior, RW 0x85D9D1)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(sh, "FactionMen", "FactionMordor", true);
	HeroAbilityModules::registerHandlers(*g.dispatch);
	const ThingTemplate *eowyn = sh.world->things().findTemplate("RohanEowyn");
	const ThingTemplate *victimT = sh.world->things().findTemplate("MordorGothmog");
	REQUIRE(eowyn);
	REQUIRE(victimT);
	Object *hero = g.make(eowyn, g.me, 1000.0f, 1000.0f);
	for (int i = 0; i < 40; ++i)
	{
		g.logic->runLogicFrame();
	}
	const SpecialPowerTemplate *smite = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilitySmite");
	REQUIRE(smite);
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*hero, smite);
	REQUIRE(sp);
	if (dynamic_cast<SpecialPowerModule *>(sp)->pauseCount() != 0)
	{
		unlockByUpgrade(*hero, smite);
	}
	AutoAbilityBehavior *aa = AutoAbilityBehavior::forPower(*hero, smite->getName());
	REQUIRE(aa);
	CHECK(aa->autoButton().empty());
	GameMessage on(MSG_DO_AUTO_ABILITY, g.me->getPlayerIndex());
	on.appendIntegerArgument((int)smite->getID());
	on.appendObjectIDArgument(hero->getID());
	g.list.append(on);
	g.logic->runLogicFrame();
	CHECK(aa->autoButton() == "Command_EowynSmite");
	const unsigned long long casts = AutoAbilityBehavior::stats().casts;
	Object *victim = g.make(victimT, g.enemy, 1060.0f, 1000.0f);
	const float before = victim->getBodyModule()->getHealth();
	bool cast = false;
	for (int i = 0; i < 300 && !cast; ++i)
	{
		g.logic->runLogicFrame();
		cast = AutoAbilityBehavior::stats().casts > casts;
	}
	CHECK(cast);
	for (int i = 0; i < 100; ++i)
	{
		g.logic->runLogicFrame();
	}
	CHECK(victim->getBodyModule()->getHealth() < before);
	// the same button again: off (RW 0x85D76C)
	GameMessage off(MSG_DO_AUTO_ABILITY, g.me->getPlayerIndex());
	off.appendIntegerArgument((int)smite->getID());
	off.appendObjectIDArgument(hero->getID());
	g.list.append(off);
	g.logic->runLogicFrame();
	CHECK(aa->autoButton().empty());
}

// lane HERO-2: WeaponModeSpecialPowerUpdate (RW 0x89841E / 0x8983B5): Legolas's Knife Fighter locks his SECONDARY weapon (LOCKED_PERMANENTLY) with the
// LegolasKnifeFighterBonus modifier for Duration, Gimli's hero mode sets WEAPONSET_TOGGLE_1 with Slayer; both end after Duration (30 s = 150 frames)
namespace
{
bool hasModifierList(Object *o, const char *list)
{
	const AttributeModifierPool *pool = dynamic_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate"));
	return pool && pool->hasList(list);
}
} // namespace

TEST_CASE("hero retail: Knife Fighter and Gimli's hero mode switch the weapon for their Duration (WeaponModeSpecialPowerUpdate, RW 0x89841E)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(sh, "FactionElves", "FactionMordor", true);
	struct Case
	{
		const char *hero, *power, *modifier;
		bool lock;
	};
	const Case cases[] = { { "ElvenLegolas", "SpecialAbilityKnifeFighter", "LegolasKnifeFighterBonus", true }, { "DwarvenGimli", "SpecialAbilityGimliHeroMode", "Slayer", false } };
	static const int kToggle1 = [] {
		for (int i = 0; TheWeaponConditionNames[i]; ++i)
		{
			if (std::string(TheWeaponConditionNames[i]) == "WEAPONSET_TOGGLE_1")
			{
				return i;
			}
		}
		return -1;
	}();
	REQUIRE(kToggle1 >= 0);
	float x = 800.0f;
	for (const Case &c : cases)
	{
		const ThingTemplate *tt = sh.world->things().findTemplate(c.hero);
		REQUIRE_MESSAGE(tt, c.hero);
		Object *hero = g.make(tt, g.me, x, 800.0f);
		x += 200.0f;
		for (int i = 0; i < 40; ++i)
		{
			g.logic->runLogicFrame();
		}
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(c.power);
		REQUIRE_MESSAGE(t, c.power);
		WeaponModeSpecialPowerUpdate *wm = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : hero->modules())
		{
			auto *w = dynamic_cast<WeaponModeSpecialPowerUpdate *>(m.get());
			if (w && w->getSpecialPowerTemplate() == t)
			{
				wm = w;
			}
		}
		REQUIRE_MESSAGE(wm, c.hero);
		unlockByUpgrade(*hero, t); // StartsPaused: the UnpauseSpecialPowerUpgrade of the level
		wm->setReadyFrame(g.logic->getFrame());
		REQUIRE_MESSAGE(wm->isReady(), c.hero);
		GameMessage cast(MSG_DO_SPECIAL_POWER, g.me->getPlayerIndex());
		cast.appendIntegerArgument((int)t->getID());
		cast.appendIntegerArgument(0);
		cast.appendObjectIDArgument(hero->getID());
		g.list.append(cast);
		g.logic->runLogicFrame();
		CHECK_MESSAGE(wm->active(), c.hero);
		CHECK_MESSAGE(hasModifierList(hero, c.modifier), c.hero);
		ObjectWeapons *weapons = hero->getWeapons();
		REQUIRE(weapons);
		if (c.lock)
		{
			CHECK(weapons->isCurWeaponLocked());
			CHECK(weapons->curSlot() == 1); // SECONDARY
		}
		else
		{
			CHECK(MaskTest(weapons->weaponSetFlags(), (unsigned)kToggle1));
		}
		CHECK_FALSE(wm->isReady()); // the recharge started
		for (int i = 0; i < 151; ++i)
		{
			g.logic->runLogicFrame();
		}
		CHECK_FALSE(wm->active());
		CHECK_FALSE(hasModifierList(hero, c.modifier));
		if (c.lock)
		{
			CHECK_FALSE(weapons->isCurWeaponLocked());
		}
		else
		{
			CHECK_FALSE(MaskTest(weapons->weaponSetFlags(), (unsigned)kToggle1));
		}
	}
}

// lane HERO-2: DualWeaponBehavior (RW 0x85DF88): Boromir takes his close range weapon set (CLOSE_RANGE, model condition WEAPONSTATE_CLOSE_RANGE) when an enemy
// is within SwitchWeaponOnCloseRangeDistance, and goes back once it is gone and MinimumSwitchTime has passed
TEST_CASE("hero retail: Boromir switches to his close range weapon with an enemy near (DualWeaponBehavior, RW 0x85DF88)")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(sh, "FactionMen", "FactionMordor", true);
	const ThingTemplate *faramirT = sh.world->things().findTemplate("GondorBoromir");
	const ThingTemplate *orcT = sh.world->things().findTemplate("MordorGothmog");
	REQUIRE(faramirT);
	REQUIRE(orcT);
	Object *faramir = g.make(faramirT, g.me, 1000.0f, 1000.0f);
	DualWeaponBehavior *dw = dynamic_cast<DualWeaponBehavior *>(faramir->findModule("DualWeaponBehavior"));
	REQUIRE(dw);
	static const int kClose = [] {
		for (int i = 0; TheWeaponConditionNames[i]; ++i)
		{
			if (std::string(TheWeaponConditionNames[i]) == "CLOSE_RANGE")
			{
				return i;
			}
		}
		return -1;
	}();
	REQUIRE(kClose == 7);
	auto isClose = [&]() { return MaskTest(faramir->getWeapons()->weaponSetFlags(), (unsigned)kClose); };
	for (int i = 0; i < 20; ++i)
	{
		g.logic->runLogicFrame();
	}
	CHECK_FALSE(isClose());
	Object *orc = g.make(orcT, g.enemy, 1040.0f, 1000.0f);
	bool closeSeen = false;
	for (int i = 0; i < 60 && !closeSeen; ++i)
	{
		g.logic->runLogicFrame();
		closeSeen = isClose();
	}
	CHECK(closeSeen);
	g.logic->destroyObject(orc);
	bool farAgain = false;
	for (int i = 0; i < 200 && !farAgain; ++i)
	{
		g.logic->runLogicFrame();
		farAgain = !isClose();
	}
	CHECK(farAgain);
	CHECK(dw->switches() >= 2);
}

// MOD-4 r2 (Sol): RW 0x668303 is the AI's current victim (AIUpdate + 0x40 by id), not the state machine's goal object. With UseRealVictimRange (no retail object sets it:
// the data is switched on for this test and restored) the current victim alone, with no goal object in the machine, brings the close range weapon
TEST_CASE("hero retail: DualWeaponBehavior's UseRealVictimRange reads the AI's current victim (RW 0x668303), not the state machine's goal object")
{
	REQUIRE_RETAIL_WORLD(sh);
	Game g(sh, "FactionMen", "FactionMordor", true);
	const ThingTemplate *heroT = sh.world->things().findTemplate("GondorBoromir");
	const ThingTemplate *orcT = sh.world->things().findTemplate("MordorGothmog");
	REQUIRE(heroT);
	REQUIRE(orcT);
	(void)orcT;
	Object *hero = g.make(heroT, g.me, 1000.0f, 1000.0f);
	DualWeaponBehavior *dw = dynamic_cast<DualWeaponBehavior *>(hero->findModule("DualWeaponBehavior"));
	REQUIRE(dw);
	DualWeaponBehaviorModuleData *data = const_cast<DualWeaponBehaviorModuleData *>(dynamic_cast<const DualWeaponBehaviorModuleData *>(dw->getModuleData()));
	REQUIRE(data);
	const bool saved = data->m_useRealVictimRange;
	data->m_useRealVictimRange = true;
	AIUpdateInterface *ai = hero->getAIUpdateInterface();
	REQUIRE(ai);
	static const unsigned kClose = 7; // CLOSE_RANGE (TheWeaponConditionNames)
	for (int i = 0; i < 20; ++i)
	{
		g.logic->runLogicFrame();
	}
	CHECK_FALSE(MaskTest(hero->getWeapons()->weaponSetFlags(), kClose));
	// an ALLIED unit as the victim: the AI never acquires a goal object of its own against it
	Object *orc = g.make(heroT, g.me, 1030.0f, 1000.0f);
	REQUIRE(ai->stateMachine().goalObject() == nullptr);
	bool closeSeen = false;
	for (int i = 0; i < 80 && !closeSeen; ++i)
	{
		ai->setCurrentVictim(orc); // the AI's victim, with no goal object in the machine
		g.logic->runLogicFrame();
		CHECK(ai->stateMachine().goalObject() == nullptr);
		closeSeen = MaskTest(hero->getWeapons()->weaponSetFlags(), kClose);
	}
	data->m_useRealVictimRange = saved;
	CHECK(closeSeen);
}
