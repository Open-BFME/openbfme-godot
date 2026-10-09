// OpenBFME unit tests. GPL-3.0.
// Lane SPELL-2, retail: every skirmish faction's spell book command set is walked; each power's science is bought through MSG_PURCHASE_SCIENCE (or,
// when the faction cannot buy it, granted), the power is cast through MSG_DO_SPECIAL_POWER_AT_LOCATION / MSG_DO_SPECIAL_POWER and its effect is
// measured on fresh probe objects (own soldier and economy building, an enemy soldier and economy building, an enemy creep) by the module class:
// a heal, created objects, an attribute modifier on a probe, the weather, a player upgrade, an area object, a bounty, a production bonus, a
// defection. Tier 1 / 2 powers and the tier-4 summons must show their effect; the others are reported. Then determinism, the weather hash
// mutations and the stop lines.

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
#include "Common/Upgrade.h"
#include "GameClient/ControlBarCommands.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Module/ExtraUpdateModules.h"
#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SpellCommands.h"

#include <algorithm>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct BookFaction
{
	const char *templateName;
	const char *citadel;
	const char *soldier;
	const char *economy;
};

const BookFaction kBook[7] = {
	{ "FactionMen", "MenFortressCitadel", "GondorFighter", "GondorFarm" },
	{ "FactionElves", "ElvenCitadel", "ElvenLorienWarrior", "ElvenMallornTree" },
	{ "FactionDwarves", "DwarvenFortressCitadel", "DwarvenGuardian", "DwarvenMineShaft" },
	{ "FactionIsengard", "IsengardFortressCitadel", "IsengardFighter", "IsengardFurnace" },
	{ "FactionMordor", "MordorFortressCitadel", "MordorFighter", "MordorSlaughterHouse" },
	{ "FactionWild", "WildFortressCitadel", "GoblinFighter", "WildMineShaft" },
	{ "FactionAngmar", "AngmarFortressCitadel", "AngmarThrallMaster", "AngmarMill" },
};

struct BookGame
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	std::unique_ptr<GameLogicDispatch> dispatch;
	spelltest::GameAI ai; // lane SPELL-2: the effect objects fire weapons (combat geometry)
	SpellCommands spell;
	starttest::Shared &s;
	std::vector<Object *> books;

	explicit BookGame(starttest::Shared &shared)
		: players(shared.world->nameKeys(), shared.world->playerTemplates(), teams)
		, s(shared)
	{
		SkirmishSetup setup;
		for (int i = 0; i < 7; ++i)
		{
			SkirmishPlayer p;
			p.name = "b" + std::to_string(i);
			p.faction = kBook[i].templateName;
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
		logic->random().seedRandom(23);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		ai.attach(shared, *logic);
		spell.registerHandlers(*dispatch);
		SpecialPowerModules::installScienceHooks(*logic);
		for (int i = 0; i < 7; ++i)
		{
			place(kBook[i].citadel, i, 1100.0f * (float)i + 500.0f, 300.0f)->friend_onBuildComplete();
			books.push_back(SpecialPowerModules::createSpellBook(*logic, *player(i)));
			REQUIRE(books.back());
		}
	}
	~BookGame() { logic->reset(); } // the objects leave the pathfinder while the AIWorld exists
	Player *player(int i) { return players.findPlayerWithName("b" + std::to_string(i)); }
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
	bool purchase(int i, ScienceType st)
	{
		GameMessage m(MSG_PURCHASE_SCIENCE, player(i)->getPlayerIndex());
		m.appendIntegerArgument(player(i)->getPlayerIndex());
		m.appendIntegerArgument(st);
		dispatch->dispatch(m);
		return player(i)->hasScience(st);
	}
	void castAt(int i, const SpecialPowerTemplate *t, const Coord3D &where)
	{
		GameMessage m(MSG_DO_SPECIAL_POWER_AT_LOCATION, player(i)->getPlayerIndex());
		m.appendIntegerArgument((int)t->getID());
		m.appendLocationArgument(where);
		m.appendObjectIDArgument(INVALID_ID);
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(books[(size_t)i]->getID());
		dispatch->dispatch(m);
	}
	void cast(int i, const SpecialPowerTemplate *t)
	{
		GameMessage m(MSG_DO_SPECIAL_POWER, player(i)->getPlayerIndex());
		m.appendIntegerArgument((int)t->getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(books[(size_t)i]->getID());
		dispatch->dispatch(m);
	}
};

AttributeModifierPool *pool(Object &o)
{
	return static_cast<AttributeModifierPool *>(o.findModule("AttributeModifierPoolUpdate"));
}

void wound(Object &o)
{
	ActiveBody *b = dynamic_cast<ActiveBody *>(o.getBodyModule());
	if (b)
	{
		b->internalChangeHealth(-b->getMaxHealth() * 0.5f);
	}
}

struct PowerRow
{
	std::string power, science, moduleClass, effect;
	int tier = 0;
	bool affected = false;
};

int tierOfCost(int cost)
{
	return cost == 5 ? 1 : cost == 10 ? 2 : cost == 15 ? 3 : cost == 25 ? 4 : 0;
}

// the faction's spell book buttons in button order: the power and whether the button wants a target position (else MSG_DO_SPECIAL_POWER)
struct BookButton
{
	const SpecialPowerTemplate *power = nullptr;
	bool needsPosition = false;
};
std::vector<BookButton> bookPowers(starttest::Shared &s, Object &book)
{
	std::vector<BookButton> out;
	const CommandSet *set = s.world->commands().findCommandSet(book.getCommandSetName());
	REQUIRE_MESSAGE(set, book.getCommandSetName());
	for (const CommandButton *b : set->m_command)
	{
		if (b && !b->m_specialPowerName.empty())
		{
			const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName);
			REQUIRE_MESSAGE(t, b->m_specialPowerName);
			out.push_back(BookButton{ t, (b->m_options & COMMAND_OPTION_NEED_TARGET_POS) != 0 });
		}
	}
	return out;
}

std::string moduleClassOf(SpecialPowerModuleInterface *sp)
{
	if (dynamic_cast<PlayerHealSpecialPower *>(sp)) return "PlayerHealSpecialPower";
	if (dynamic_cast<OCLSpecialPower *>(sp)) return "OCLSpecialPower";
	if (dynamic_cast<PlayerUpgradeSpecialPower *>(sp)) return "PlayerUpgradeSpecialPower";
	if (dynamic_cast<DarknessSpecialPower *>(sp)) return "DarknessSpecialPower";
	if (dynamic_cast<FreezingRainSpecialPower *>(sp)) return "FreezingRainSpecialPower";
	if (dynamic_cast<CloudBreakSpecialPower *>(sp)) return "CloudBreakSpecialPower";
	if (dynamic_cast<TaintSpecialPower *>(sp)) return "TaintSpecialPower";
	if (dynamic_cast<ElvenWoodSpecialPower *>(sp)) return "ElvenWoodSpecialPower";
	if (dynamic_cast<ProductionSpeedBonus *>(sp)) return "ProductionSpeedBonus";
	if (dynamic_cast<ScavengerSpecialPower *>(sp)) return "ScavengerSpecialPower";
	if (dynamic_cast<UntamedAllegianceSpecialPower *>(sp)) return "UntamedAllegianceSpecialPower";
	if (dynamic_cast<DevastateSpecialPower *>(sp)) return "DevastateSpecialPower";
	return "SpecialPowerModule";
}

// buys (or grants) every science of the faction's book, cheapest tiers first; returns the sciences that had to be granted
std::vector<std::string> buyTree(BookGame &g, int i, const std::vector<BookButton> &powers, const ScienceStore &store)
{
	PlayerScience &ps = g.player(i)->science();
	ps.addSciencePurchasePoints(1000);
	std::vector<ScienceType> wanted;
	for (const BookButton &b : powers)
	{
		for (ScienceType st : b.power->getRequiredSciences())
		{
			if (std::find(wanted.begin(), wanted.end(), st) == wanted.end())
			{
				wanted.push_back(st);
			}
		}
	}
	for (bool progress = true; progress;)
	{
		progress = false;
		for (ScienceType st : wanted)
		{
			if (!ps.hasScience(st) && ps.isCapableOfPurchasingScience(st) && g.purchase(i, st))
			{
				progress = true;
			}
		}
	}
	std::vector<std::string> granted;
	for (ScienceType st : wanted)
	{
		if (!ps.hasScience(st))
		{
			ps.grantScience(st);
			granted.push_back(store.getInternalNameForScience(st));
		}
	}
	return granted;
}
} // namespace

TEST_CASE("SPELL-2 retail: every faction buys and casts its spell book powers; tier 1, tier 2 and the tier-4 summons show their effect")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ScienceStore &store = shared->world->spellStores().sciences();
	BookGame g(*shared);
	int checked = 0, tier4Summons = 0, stopped = 0;
	std::map<std::string, int> classes;
	for (int i = 0; i < 7; ++i)
	{
		const BookFaction &f = kBook[i];
		const int enemy = (i + 1) % 7;
		Object &book = *g.books[(size_t)i];
		const std::vector<BookButton> powers = bookPowers(*shared, book);
		REQUIRE(!powers.empty());
		const std::vector<std::string> granted = buyTree(g, i, powers, store);
		std::ostringstream grantedText;
		for (const std::string &n : granted)
		{
			grantedText << n << " ";
		}
		MESSAGE(std::string(f.templateName) << ": " << powers.size() << " spell book powers; sciences the faction cannot buy (granted): " << grantedText.str());
		for (size_t k = 0; k < powers.size(); ++k)
		{
			const SpecialPowerTemplate *t = powers[k].power;
			SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(book, t);
			REQUIRE_MESSAGE(sp, t->getName());
			PowerRow row;
			row.power = t->getName();
			row.moduleClass = moduleClassOf(sp);
			int cost = 0;
			for (ScienceType st : t->getRequiredSciences())
			{
				cost = std::max(cost, store.getSciencePurchaseCost(st, true));
				row.science = store.getInternalNameForScience(st);
			}
			row.tier = tierOfCost(cost);
			INFO(std::string(f.templateName) << " " << row.power << " (" << row.moduleClass << ", tier " << row.tier << ")");
			CHECK(sp->isReady());
			CHECK(SpecialPowerModules::canUseSpecialPower(book, t));
			// the probes at this power's own cast point
			const Coord3D where{ 1100.0f * (float)i + 500.0f, 1100.0f + 600.0f * (float)k, 0.0f };
			std::vector<Object *> probes = { g.place(f.soldier, i, where.x + 10.0f, where.y), g.place(f.economy, i, where.x - 30.0f, where.y + 20.0f),
				g.place(kBook[enemy].soldier, enemy, where.x, where.y + 15.0f), g.place(kBook[enemy].economy, enemy, where.x + 30.0f, where.y - 25.0f),
				g.place("NeutralWarg", enemy, where.x - 15.0f, where.y - 10.0f), g.place(f.citadel, i, where.x + 60.0f, where.y + 60.0f) };
			for (Object *p : probes)
			{
				p->friend_onBuildComplete();
				wound(*p);
			}
			std::vector<float> health;
			for (Object *p : probes)
			{
				health.push_back(p->getBodyModule() ? p->getBodyModule()->getHealth() : 0.0f);
			}
			const Team *creepTeam = probes[4]->getTeam();
			const int weatherBefore = g.logic->weather().weather();
			const unsigned long long castsBefore = g.spell.casts();
			const SpecialPowerModule *module = dynamic_cast<const SpecialPowerModule *>(sp);
			REQUIRE(module);
			const unsigned triggersBefore = module->triggers();
			const unsigned long long unportedBefore = module->unportedEffects();
			if (powers[k].needsPosition)
			{
				g.castAt(i, t, where);
			}
			else
			{
				g.cast(i, t);
			}
			CHECK(g.spell.casts() == castsBefore + 1);
			// the effect, by class
			const SpecialPowerModuleData *d = module->spData();
			bool affected = false;
			std::string effect;
			if (row.moduleClass == "PlayerHealSpecialPower")
			{
				for (size_t p = 0; p < probes.size(); ++p)
				{
					if (probes[p]->getBodyModule() && probes[p]->getBodyModule()->getHealth() > health[p])
					{
						affected = true;
						effect = "healed " + probes[p]->getTemplate()->getName();
					}
				}
				if (!affected)
				{
					for (size_t p = 0; p < probes.size(); ++p)
					{
						effect += probes[p]->getTemplate()->getName() + " " + std::to_string(health[p]) + "/" +
							std::to_string(probes[p]->getBodyModule() ? probes[p]->getBodyModule()->getMaxHealth() : -1.0f) + " cp " +
							std::to_string(probes[p]->getConstructionPercent()) + (probes[p]->isKindOfName("STRUCTURE") ? " S" : "") + (probes[p]->isKindOfName("WEBBED") ? " W" : "") + " team " + std::to_string(probes[p]->getTeam() == g.books[(size_t)i]->getTeam()) + "; ";
					}
				}
			}
			else if (const OCLSpecialPower *ocl = dynamic_cast<const OCLSpecialPower *>(sp))
			{
				const int where0 = ocl->data()->m_createLocation;
				if (where0 == 0 || where0 == 1 || where0 == 2 || where0 == 6 || where0 == 7)
				{
					// the map edge / spawn point cases are counted, not run (S-530): the stop must count this cast
					affected = module->unportedEffects() > unportedBefore;
					effect = "CreateLocation " + std::to_string(where0) + " counted (S-530), nothing made";
					++stopped;
				}
				else
				{
				affected = !ocl->created().empty();
				effect = "created " + std::to_string(ocl->created().size());
				if (affected)
				{
					Object *first = g.logic->findObjectByID(ocl->created().front());
					effect += first ? " (" + first->getTemplate()->getName() + ")" : "";
				}
				}
			}
			else if (const PlayerUpgradeSpecialPower *pu = dynamic_cast<const PlayerUpgradeSpecialPower *>(sp))
			{
				(void)pu;
				const PlayerUpgradeSpecialPowerModuleData *ud = dynamic_cast<const PlayerUpgradeSpecialPowerModuleData *>(d);
				REQUIRE(ud);
				REQUIRE(!ud->m_upgradeNames.empty());
				affected = g.player(i)->hasUpgradeComplete(ud->m_upgradeNames.front());
				effect = "player upgrade " + ud->m_upgradeNames.front();
			}
			else if (const TaintSpecialPower *ts = dynamic_cast<const TaintSpecialPower *>(sp))
			{
				Object *a = g.logic->findObjectByID(ts->lastAreaObject());
				affected = a != nullptr;
				effect = a ? "area object " + a->getTemplate()->getName() : "no area object";
			}
			else if (const ElvenWoodSpecialPower *ew = dynamic_cast<const ElvenWoodSpecialPower *>(sp))
			{
				Object *a = g.logic->findObjectByID(ew->lastAreaObject());
				affected = a != nullptr;
				effect = a ? "area object " + a->getTemplate()->getName() : "no area object";
			}
			else if (const ProductionSpeedBonus *pb = dynamic_cast<const ProductionSpeedBonus *>(sp))
			{
				affected = !pb->bonuses().empty();
				effect = "production bonuses " + std::to_string(pb->bonuses().size()) + (affected ? " factor " + std::to_string(pb->bonuses().front().second.second) : "");
			}
			else if (const ScavengerSpecialPower *sc = dynamic_cast<const ScavengerSpecialPower *>(sp))
			{
				affected = sc->active() && g.player(i)->getBountyPercent() > 0.0f;
				effect = "bounty percent " + std::to_string(g.player(i)->getBountyPercent());
			}
			else if (const UntamedAllegianceSpecialPower *ua = dynamic_cast<const UntamedAllegianceSpecialPower *>(sp))
			{
				affected = ua->defected() > 0 && probes[4]->getTeam() != creepTeam && probes[4]->getControllingPlayer() == g.player(i);
				effect = "defected " + std::to_string(ua->defected());
			}
			else if (d->m_attributeModifierWeatherBased)
			{
				const GlobalWeatherSystem &w = g.logic->weather();
				int withList = 0;
				for (Object *p : probes)
				{
					AttributeModifierPool *pl = pool(*p);
					if (pl && !d->m_attributeModifier.empty() && pl->hasList(d->m_attributeModifier))
					{
						++withList;
					}
					if (pl && d->m_antiCategory != 0)
					{
						for (int c = 0; c < 15; ++c)
						{
							if (((d->m_antiCategory >> c) & 1u) && pl->categoryDisabledUntil(c) == 999999999u)
							{
								++withList;
							}
						}
					}
				}
				const bool weatherSet = d->m_changeWeather == GlobalWeatherSystem::WEATHER_NO_CHANGE || w.weather() == d->m_changeWeather;
				affected = weatherSet && (withList > 0 || d->m_reEnableAntiCategory) && (w.weather() != weatherBefore || d->m_changeWeather == GlobalWeatherSystem::WEATHER_NO_CHANGE);
				effect = "weather " + std::to_string(weatherBefore) + " -> " + std::to_string(w.weather()) + ", probes affected " + std::to_string(withList);
			}
			else if (!d->m_attributeModifier.empty())
			{
				int withList = 0;
				std::string names;
				for (Object *p : probes)
				{
					AttributeModifierPool *pl = pool(*p);
					if (pl && pl->hasList(d->m_attributeModifier))
					{
						++withList;
						names += p->getTemplate()->getName() + " ";
					}
				}
				affected = withList > 0;
				effect = d->m_attributeModifier + " on " + std::to_string(withList) + " probes (" + names + ")";
			}
			else
			{
				affected = module->triggers() > triggersBefore;
				effect = "triggered (no logic effect data on the module: " + row.moduleClass + ")";
			}
			if (row.moduleClass == "DevastateSpecialPower")
			{
				affected = module->triggers() > triggersBefore;
				effect = "base part only (S-921)";
			}
			row.affected = affected;
			row.effect = effect;
			++classes[row.moduleClass];
			MESSAGE(std::string(f.templateName) << " tier " << row.tier << " " << row.power << " [" << row.moduleClass << "] " << row.science << ": " << effect << (affected ? "" : "  ** NO EFFECT **"));
			const bool summon = row.tier == 4 && row.moduleClass == "OCLSpecialPower";
			if (row.tier == 1 || row.tier == 2 || summon)
			{
				CHECK_MESSAGE(affected, std::string(f.templateName) << " " << row.power << ": " << effect);
				++checked;
			}
			tier4Summons += summon && affected ? 1 : 0;
			// the recharge started (or the power is shared with another button already cast)
			CHECK_FALSE(sp->isReady());
		}
	}
	std::ostringstream cls;
	for (const auto &c : classes)
	{
		cls << c.first << " " << c.second << ", ";
	}
	MESSAGE("SPELL-2: powers checked (tier 1 / 2 / 4 summons): " << checked << "; tier-4 summons with an effect: " << tier4Summons << "; casts stopped at S-530: " << stopped << "; classes: " << cls.str());
	CHECK(stopped == 1); // SpellBookDragonStrike (CREATE_AT_EDGE_NEAR_TARGET_AND_MOVE_TO_LOCATION)
	CHECK(tier4Summons >= 7);
	CHECK(g.spell.malformed() == 0u);
}

TEST_CASE("SPELL-2 retail: weather-based powers set the map-wide modifier, new objects get it, it ends after WeatherDuration")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const ScienceStore &store = shared->world->spellStores().sciences();
	BookGame g(*shared);
	const int mordor = 4, men = 0;
	const SpecialPowerTemplate *darkness = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookDarkness");
	REQUIRE(darkness);
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*g.books[(size_t)mordor], darkness);
	REQUIRE(sp);
	const SpecialPowerModuleData *d = dynamic_cast<SpecialPowerModule *>(sp)->spData();
	REQUIRE(d->m_attributeModifierWeatherBased);
	REQUIRE(d->m_changeWeather == 1); // CLOUDY
	for (ScienceType st : darkness->getRequiredSciences())
	{
		g.player(mordor)->science().grantScience(st);
	}
	Object *own = g.place("MordorFighter", mordor, 6000.0f, 8500.0f);
	Object *foe = g.place("GondorFighter", men, 6100.0f, 8500.0f);
	g.castAt(mordor, darkness, Coord3D{ 100.0f, 100.0f, 0.0f });
	const GlobalWeatherSystem &w = g.logic->weather();
	CHECK(w.weather() == 1);
	CHECK(w.modifierName() == d->m_attributeModifier);
	CHECK(w.modifierFramesLeft() == d->m_weatherDuration);
	// the filter names ALLIES: the caster's own soldier, far from the target, has the list; the enemy does not
	CHECK(pool(*own)->hasList(d->m_attributeModifier));
	CHECK_FALSE(pool(*foe)->hasList(d->m_attributeModifier));
	// an object made while the weather lasts gets it (Object::initObject RW 0x694073)
	Object *late = g.place("MordorFighter", mordor, 6200.0f, 8500.0f);
	CHECK(pool(*late)->hasList(d->m_attributeModifier));
	// the countdown: one frame before the end it is still on; at the end it is removed and the weather goes back to NONE
	for (unsigned f = 1; f < d->m_weatherDuration; ++f)
	{
		g.logic->runLogicFrame();
	}
	CHECK(w.modifierName() == d->m_attributeModifier);
	CHECK(pool(*own)->hasList(d->m_attributeModifier));
	g.logic->runLogicFrame();
	CHECK(w.modifierName().empty());
	CHECK(w.affectKind() == GlobalWeatherSystem::AFFECT_NONE);
	CHECK_FALSE(pool(*own)->hasList(d->m_attributeModifier));
	CHECK(w.weather() == 0);
	MESSAGE("SPELL-2 Darkness: " << d->m_attributeModifier << " for " << d->m_weatherDuration << " frames, weather changes " << w.weatherChanges());
	(void)store;
}

TEST_CASE("SPELL-2 retail: Rallying Call buffs allied infantry in range, not enemies; TargetEnemy Blight hits the enemy farm, not its own")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	BookGame g(*shared);
	const SpecialPowerTemplate *rally = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookRallyingCall");
	const SpecialPowerTemplate *blight = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookBlight");
	REQUIRE(rally);
	REQUIRE(blight);
	for (ScienceType st : rally->getRequiredSciences())
	{
		g.player(0)->science().grantScience(st);
	}
	for (ScienceType st : blight->getRequiredSciences())
	{
		g.player(4)->science().grantScience(st);
	}
	Object *mine = g.place("GondorFighter", 0, 2000.0f, 2000.0f);
	Object *far = g.place("GondorFighter", 0, 2300.0f, 2000.0f); // outside AttributeModifierRange 100
	Object *ally = g.place("ElvenLorienWarrior", 1, 2010.0f, 2000.0f); // another player (an enemy in this setup)
	g.castAt(0, rally, Coord3D{ 2000.0f, 2000.0f, 0.0f });
	CHECK(pool(*mine)->hasList("SpellBookRallyingCallModifier"));
	CHECK_FALSE(pool(*far)->hasList("SpellBookRallyingCallModifier"));
	CHECK_FALSE(pool(*ally)->hasList("SpellBookRallyingCallModifier"));
	Object *farm = g.place("GondorFarm", 0, 6000.0f, 6000.0f);
	farm->friend_onBuildComplete();
	Object *ownMill = g.place("MordorSlaughterHouse", 4, 6020.0f, 6000.0f);
	ownMill->friend_onBuildComplete();
	g.castAt(4, blight, Coord3D{ 6000.0f, 6000.0f, 0.0f });
	const SpecialPowerModule *bm = dynamic_cast<SpecialPowerModule *>(SpecialPowerModules::findModule(*g.books[4], blight));
	REQUIRE(bm);
	// TargetEnemy: the relationship mask is ENEMIES only (RW 0x897B76); AffectAllies keeps its default 1 (RW 0x8968F0), so other players' objects pass
	CHECK(pool(*farm)->hasList(bm->spData()->m_attributeModifier));
	CHECK_FALSE(pool(*ownMill)->hasList(bm->spData()->m_attributeModifier));
	MESSAGE("SPELL-2 Blight: victims " << bm->victimsAffected() << " (AffectAllies " << bm->spData()->m_affectAllies << ")");
}

TEST_CASE("SPELL-2 retail: two identical cast sequences hash alike; another target or a weather field changes the hash")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	auto run = [&](float x, int mutate) {
		BookGame g(*shared);
		const char *names[] = { "SpellBookDarkness", "SpellBookRallyingCall", "SpellBookTaint" };
		const int casters[] = { 4, 0, 3 };
		for (int k = 0; k < 3; ++k)
		{
			const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(names[k]);
			REQUIRE(t);
			for (ScienceType st : t->getRequiredSciences())
			{
				g.player(casters[k])->science().grantScience(st);
			}
			g.place(kBook[casters[k]].soldier, casters[k], x + 10.0f * (float)k, 1500.0f);
			g.castAt(casters[k], t, Coord3D{ x, 1500.0f, 0.0f });
		}
		for (int f = 0; f < 10; ++f)
		{
			g.logic->runLogicFrame();
		}
		GlobalWeatherSystem &w = g.logic->weather();
		if (mutate == 1)
		{
			w.setBurnDecay(w.burnDecay() + 1);
		}
		else if (mutate == 2)
		{
			w.setWeather(3, w.burnDecay(), 7);
		}
		else if (mutate == 3)
		{
			w.clear();
		}
		return g.logic->computeStateHash();
	};
	const std::uint32_t a = run(3500.0f, 0), b = run(3500.0f, 0), c = run(3600.0f, 0);
	CHECK(a == b);
	CHECK(a != c);
	CHECK(run(3500.0f, 1) != a);
	CHECK(run(3500.0f, 2) != a);
	CHECK(run(3500.0f, 3) != a);
}

TEST_CASE("SPELL-2 stops: S-920 .. S-924 are reported once each by the lane and by the retail world")
{
	const std::vector<std::string> lines = SpellBookPowers::stopLines();
	REQUIRE(lines.size() == 5);
	for (int i = 0; i < 5; ++i)
	{
		CHECK(lines[(size_t)i].rfind("[S-92" + std::to_string(i) + "]", 0) == 0);
	}
	OPENBFME_REQUIRE_START(shared);
	const std::vector<std::string> all = shared->world->acceptanceStops();
	for (const std::string &l : lines)
	{
		CHECK(std::count(all.begin(), all.end(), l) == 1);
	}
}

TEST_CASE("SPELL-2 retail: the tier-4 summons hatch: their eggs die and the SlowDeathBehavior OCL makes the summoned units")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	BookGame g(*shared);
	struct Summon
	{
		int faction;
		const char *power;
	};
	const Summon summons[] = { { 4, "SpellBookBalrogAlly" }, { 0, "SpellBookArmyoftheDead" }, { 3, "SpellBookDragonAlly" }, { 1, "SpellBookEagleAllies" } };
	int hatched = 0;
	for (const Summon &s : summons)
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(s.power);
		REQUIRE(t);
		for (ScienceType st : t->getRequiredSciences())
		{
			g.player(s.faction)->science().grantScience(st);
		}
		std::vector<ObjectID> before;
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			before.push_back(o->getID());
		}
		g.castAt(s.faction, t, Coord3D{ 1100.0f * (float)s.faction + 900.0f, 5000.0f, 0.0f });
		for (int f = 0; f < 150; ++f)
		{
			g.logic->runLogicFrame();
		}
		std::string made;
		int units = 0;
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			const std::string &n = o->getTemplate()->getName();
			if (std::find(before.begin(), before.end(), o->getID()) != before.end() || o->getControllingPlayer() != g.player(s.faction))
			{
				continue;
			}
			made += n + " ";
			if (n.find("Egg") == std::string::npos && n.find("PlaceHolder") == std::string::npos && !o->isEffectivelyDead())
			{
				++units;
			}
		}
		MESSAGE("SPELL-2 " << std::string(s.power) << " after 150 frames: " << made);
		hatched += units > 0 ? 1 : 0;
	}
	CHECK(hatched >= 3);
}

TEST_CASE("SPELL-2 retail: the arrow volley's effect object fires its weapons through FireWeaponUpdate and is deleted by DeletionUpdate; enemies lose health")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	BookGame g(*shared);
	const SpecialPowerTemplate *volley = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookArrowVolleyEvil");
	REQUIRE(volley);
	for (ScienceType st : volley->getRequiredSciences())
	{
		g.player(4)->science().grantScience(st);
	}
	const Coord3D where{ 7000.0f, 7000.0f, 0.0f };
	std::vector<Object *> foes;
	for (int i = 0; i < 6; ++i)
	{
		foes.push_back(g.place("GondorFighter", 0, where.x + 12.0f * (float)(i % 3) - 12.0f, where.y + 12.0f * (float)(i / 3)));
	}
	float before = 0.0f;
	for (Object *o : foes)
	{
		before += o->getBodyModule()->getHealth();
	}
	g.castAt(4, volley, where);
	const OCLSpecialPower *ocl = dynamic_cast<const OCLSpecialPower *>(SpecialPowerModules::findModule(*g.books[4], volley));
	REQUIRE(ocl);
	REQUIRE(ocl->created().size() == 1u);
	const ObjectID effect = ocl->created().front();
	Object *eo = g.logic->findObjectByID(effect);
	REQUIRE(eo);
	FireWeaponUpdate *fw = dynamic_cast<FireWeaponUpdate *>(eo->findModule("FireWeaponUpdate"));
	DeletionUpdate *del = dynamic_cast<DeletionUpdate *>(eo->findModule("DeletionUpdate"));
	REQUIRE(fw);
	REQUIRE(del);
	const unsigned dieFrame = del->dieFrame();
	CHECK(dieFrame == g.logic->getFrame() + 50u); // MaxLifetime = MinLifetime = 10000 ms
	unsigned long long shots = 0;
	while (g.logic->getFrame() < dieFrame + 2)
	{
		g.logic->runLogicFrame();
		if (Object *e = g.logic->findObjectByID(effect))
		{
			shots = dynamic_cast<FireWeaponUpdate *>(e->findModule("FireWeaponUpdate"))->shots();
		}
	}
	CHECK(shots == 2u); // ArrowVolleyPhaseInitialWeapon at once, ArrowVolleyOneWeapon after 4700 ms, both OneShot
	CHECK(g.logic->findObjectByID(effect) == nullptr);
	float after = 0.0f;
	for (Object *o : foes)
	{
		after += (o->isEffectivelyDead() || !o->getBodyModule()) ? 0.0f : o->getBodyModule()->getHealth();
	}
	MESSAGE("SPELL-2 arrow volley: shots " << shots << ", enemy health " << before << " -> " << after);
	CHECK(after < before);
}

TEST_CASE("SPELL-2 retail (review r1): the reveal object and Cloud Break's sunbeams draw the logic random numbers retail draws; the stop lines say so")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	// the disclosures (S-920 / S-921) state the draw counts
	const std::vector<std::string> lines = SpellBookPowers::stopLines();
	CHECK(lines[0].find("THREE logic random draws for the retail SuperweaponPing") != std::string::npos);
	CHECK(lines[0].find("ViewObjectRange != 0, ViewObjectDuration != 0, a GlobalData SpecialPowerViewObject name and its template") != std::string::npos);
	CHECK(lines[1].find("TWO logic random draws each for the retail CloudBreakSunbeam") != std::string::npos);
	CHECK(lines[1].find("2 * N per cast for N grid points") != std::string::npos);
	BookGame g(*shared);
	REQUIRE(g.logic->settings().specialPowerViewObject == "SuperweaponPing");
	// the reveal object: Arrow Volley (ViewObjectDuration 10000 ms) leaves a SuperweaponPing at its target for 50 frames
	const SpecialPowerTemplate *volley = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookArrowVolleyGood");
	REQUIRE(volley);
	REQUIRE(volley->m_viewObjectDuration == 50u);
	REQUIRE(volley->m_viewObjectRange != 0.0f);
	for (ScienceType st : volley->getRequiredSciences())
	{
		g.player(0)->science().grantScience(st);
	}
	auto pings = [&]() {
		std::vector<Object *> out;
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getTemplate()->getName() == "SuperweaponPing" && !o->isDestroyed())
			{
				out.push_back(o);
			}
		}
		return out;
	};
	REQUIRE(pings().empty());
	g.logic->random().enableCallLog(true);
	g.logic->random().clearCallLog();
	const Coord3D where{ 3000.0f, 3000.0f, 0.0f };
	g.castAt(0, volley, where);
	const std::vector<Object *> made = pings();
	REQUIRE(made.size() == 1u);
	CHECK(made[0]->getPosition()->x == where.x);
	CHECK(made[0]->getControllingPlayer() == g.player(0));
	int deletion = 0;
	for (const auto &c : g.logic->random().callLog())
	{
		if (c.file == "DeletionUpdate.cpp" && c.line == 0x37)
		{
			++deletion;
		}
	}
	// the reveal: its constructor's delay and the [50, 50] override (the arrow volley object itself also has a DeletionUpdate: 3 in all)
	CHECK(deletion == 3);
	DeletionUpdate *life = dynamic_cast<DeletionUpdate *>(made[0]->findModule("DeletionUpdate"));
	REQUIRE(life);
	CHECK(life->dieFrame() == g.logic->getFrame() + 50u);
	// Cloud Break: a SunbeamObject every 300 units over the terrain's extent (TheAI's 8000 x 9000 here), each with LifetimeUpdate's draw and the seed
	const SpecialPowerTemplate *cloud = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookCloudBreak");
	REQUIRE(cloud);
	for (ScienceType st : cloud->getRequiredSciences())
	{
		g.player(0)->science().grantScience(st);
	}
	g.logic->random().clearCallLog();
	g.cast(0, cloud);
	CloudBreakSpecialPower *cb = dynamic_cast<CloudBreakSpecialPower *>(SpecialPowerModules::findModule(*g.books[0], cloud));
	REQUIRE(cb);
	// x and y from (int)(300 + 0) while < extent - 300: 300 .. 7500 (25) by 300 .. 8400 (28)
	CHECK(cb->sunbeamsMade() == 25u * 28u);
	int lifetimes = 0, seeds = 0;
	for (const auto &c : g.logic->random().callLog())
	{
		lifetimes += c.file == "LifetimeUpdate.cpp" ? 1 : 0;
		seeds += (c.file == "GameLogic.cpp" && c.line == 0x19A7) ? 1 : 0;
	}
	MESSAGE("SPELL-2 Cloud Break: " << cb->sunbeamsMade() << " sunbeams, lifetime draws " << lifetimes << ", seed draws " << seeds);
	CHECK(lifetimes == (int)cb->sunbeamsMade());
	CHECK(seeds == (int)cb->sunbeamsMade());
	g.logic->random().enableCallLog(false);
}
