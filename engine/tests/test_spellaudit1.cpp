// OpenBFME unit tests. GPL-3.0.
// Lane SPELLAUDIT-1: the spell book audit's instrument. For every skirmish faction's book, each power is bought through MSG_PURCHASE_SCIENCE (granted
// only when the faction cannot buy it), cast through the spell book's own message (MSG_DO_SPECIAL_POWER_AT_LOCATION / MSG_DO_SPECIAL_POWER) in a
// fresh two-player game, and what follows is recorded over 2700 logic frames (540 s; RotWK runs 5 a second): the recharge, every object the cast made (template, owner, rank, distance
// from the cast point, how long it lived), the health / attribute modifier lists / owner of probe lines (own and enemy soldiers at 0 .. 300 from the
// cast point, an own and an enemy economy building, a creep), the money, the weather and the stop lines the cast met. With SPELLAUDIT1_OUT set the
// records are written there as JSON lines (logs/spellaudit1-matrix.md is built from them); without it the sweep only checks that every power casts.

#include "HordeCompleteTestUtil.h"
#include "doctest.h"
#include "SpellTestAI.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/SpecialPower.h"
#include "Common/Team.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Economy.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/ProductionSettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/TerrainResourceBehavior.h"
#include "GameLogic/Object/AttributeModifierPool.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SpellCommands.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct AuditFaction
{
	const char *templateName;
	const char *citadel;
	const char *soldier;
	const char *economy;
};

const AuditFaction kAudit[7] = {
	{ "FactionMen", "MenFortressCitadel", "GondorFighter", "GondorFarm" },
	{ "FactionElves", "ElvenCitadel", "ElvenLorienWarrior", "ElvenMallornTree" },
	{ "FactionDwarves", "DwarvenFortressCitadel", "DwarvenGuardian", "DwarvenMineShaft" },
	{ "FactionIsengard", "IsengardFortressCitadel", "IsengardFighter", "IsengardFurnace" },
	{ "FactionMordor", "MordorFortressCitadel", "MordorFighter", "MordorSlaughterHouse" },
	{ "FactionWild", "WildFortressCitadel", "GoblinFighter", "WildMineShaft" },
	{ "FactionAngmar", "AngmarFortressCitadel", "AngmarThrallMaster", "AngmarMill" },
};

std::string jsonEscape(const std::string &s)
{
	std::string o;
	for (char c : s)
	{
		if (c == '"' || c == '\\')
		{
			o += '\\';
		}
		if ((unsigned char)c >= 0x20)
		{
			o += c;
		}
	}
	return o;
}

// one fresh game: player 0 casts with the faction under audit, player 1 is the enemy (Mordor; Men when Mordor casts)
struct AuditGame
{
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<GameLogic> logic;
	std::unique_ptr<GameLogicDispatch> dispatch;
	spelltest::GameAI ai;
	SpellCommands spell;
	starttest::Shared &s;
	Object *book = nullptr;
	int enemyFaction = 4;

	AuditGame(starttest::Shared &shared, int faction)
		: players(shared.world->nameKeys(), shared.world->playerTemplates(), teams)
		, s(shared)
	{
		enemyFaction = faction == 4 ? 0 : 4;
		SkirmishSetup setup;
		const int factions[2] = { faction, enemyFaction };
		for (int i = 0; i < 2; ++i)
		{
			SkirmishPlayer p;
			p.name = "a" + std::to_string(i);
			p.faction = kAudit[factions[i]].templateName;
			p.human = i == 0;
			p.team = i;
			p.startIndex = i + 1;
			setup.players.push_back(p);
		}
		REQUIRE(players.setupSkirmish(setup).empty());
		logic = std::make_unique<GameLogic>(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain);
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic->settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic->economy().settings(), &err), err);
		REQUIRE_MESSAGE(ProductionSettings::load(*s.mount->fs, logic->productionSettings(), &err), err); // GameData's build times (a summoned building builds)
		logic->castleTemplates().setLoader(CastleTemplateStore::fileSystemLoader(*s.mount->fs));
		logic->random().seedRandom(41);
		dispatch = std::make_unique<GameLogicDispatch>(*logic);
		ai.attach(shared, *logic);
		spell.registerHandlers(*dispatch);
		SpecialPowerModules::installScienceHooks(*logic);
		place(kAudit[faction].citadel, 0, 600.0f, 600.0f)->friend_onBuildComplete();
		place(kAudit[enemyFaction].citadel, 1, 7000.0f, 8000.0f)->friend_onBuildComplete();
		book = SpecialPowerModules::createSpellBook(*logic, *player(0));
		REQUIRE(book);
	}
	~AuditGame() { logic->reset(); }
	Player *player(int i) { return players.findPlayerWithName("a" + std::to_string(i)); }
	Object *place(const char *name, int i, float x, float y)
	{
		const ThingTemplate *t = s.world->things().findTemplate(name);
		REQUIRE_MESSAGE(t, name);
		Object *o = logic->newObject(t, player(i)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(o);
		Coord3D pos{ x, y, 0.0f };
		o->setPosition(&pos);
		return testutil::completeHorde(o); // #160: a horde makes its members where it stands at build complete (RW 0x8718ED)
	}
	bool purchase(ScienceType st)
	{
		GameMessage m(MSG_PURCHASE_SCIENCE, player(0)->getPlayerIndex());
		m.appendIntegerArgument(player(0)->getPlayerIndex());
		m.appendIntegerArgument(st);
		dispatch->dispatch(m);
		return player(0)->hasScience(st);
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
	void cast(const SpecialPowerTemplate *t)
	{
		GameMessage m(MSG_DO_SPECIAL_POWER, player(0)->getPlayerIndex());
		m.appendIntegerArgument((int)t->getID());
		m.appendIntegerArgument(0);
		m.appendObjectIDArgument(book->getID());
		dispatch->dispatch(m);
	}
};

struct AuditButton
{
	const SpecialPowerTemplate *power = nullptr;
	bool needsPosition = false;
};

std::vector<AuditButton> auditPowers(starttest::Shared &s, Object &book)
{
	std::vector<AuditButton> out;
	const CommandSet *set = s.world->commands().findCommandSet(book.getCommandSetName());
	REQUIRE_MESSAGE(set, book.getCommandSetName());
	for (const CommandButton *b : set->m_command)
	{
		if (b && !b->m_specialPowerName.empty())
		{
			const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName);
			REQUIRE_MESSAGE(t, b->m_specialPowerName);
			out.push_back(AuditButton{ t, (b->m_options & COMMAND_OPTION_NEED_TARGET_POS) != 0 });
		}
	}
	return out;
}

float healthOf(const Object *o)
{
	return (!o || o->isEffectivelyDead() || !o->getBodyModule()) ? 0.0f : o->getBodyModule()->getHealth();
}

std::string modifiersOf(const Object *o)
{
	if (!o)
	{
		return "";
	}
	const AttributeModifierPool *p = static_cast<const AttributeModifierPool *>(o->findModule("AttributeModifierPoolUpdate"));
	std::string out;
	if (p)
	{
		for (const std::string &n : p->listNames())
		{
			out += (out.empty() ? "" : "+") + n;
		}
	}
	return out;
}

struct Probe
{
	std::string role;
	ObjectID id = INVALID_ID;
	float maxHealth = 0.0f;
};

struct Spawn
{
	std::string name;
	int owner = -1;
	int rank = 0;
	unsigned firstFrame = 0, lastFrame = 0;
	float dist = 0.0f;
	bool alive = true;
};

const unsigned kSamples[] = { 1, 15, 30, 90, 150, 300, 450, 600, 900, 1350, 1800, 2700 };

// the sweep of one power: returns a JSON object (one line)
std::string auditOne(starttest::Shared &shared, int faction, size_t slot, int side, bool *cast, unsigned maxFrame)
{
	AuditGame g(shared, faction);
	const std::vector<AuditButton> powers = auditPowers(shared, *g.book);
	REQUIRE(slot < powers.size()); // tolerance: an index into the book's 12 slots (the caller walks them)
	const AuditButton &btn = powers[slot];
	const SpecialPowerTemplate *t = btn.power;
	const ScienceStore &store = shared.world->spellStores().sciences();
	PlayerScience &ps = g.player(0)->science();
	ps.addSciencePurchasePoints(1000);
	// buy the power's sciences (and their prerequisites) the way the store does: whatever is purchasable, until nothing more can be bought
	std::vector<ScienceType> wanted(t->getRequiredSciences().begin(), t->getRequiredSciences().end());
	int pointsSpent = 0, granted = 0;
	for (bool progress = true; progress;)
	{
		progress = false;
		for (ScienceType st : wanted)
		{
			const int before = ps.getSciencePurchasePoints();
			if (!ps.hasScience(st) && ps.isCapableOfPurchasingScience(st) && g.purchase(st))
			{
				pointsSpent += before - ps.getSciencePurchasePoints();
				progress = true;
			}
		}
	}
	for (ScienceType st : wanted)
	{
		if (!ps.hasScience(st))
		{
			ps.grantScience(st);
			++granted;
		}
	}
	int cost = 0;
	for (ScienceType st : wanted)
	{
		cost = std::max(cost, store.getSciencePurchaseCost(st, true));
	}
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*g.book, t);
	REQUIRE_MESSAGE(sp, t->getName());
	const bool readyAfterBuy = sp->isReady();
	// the probe lines: own soldiers along -x, enemy soldiers along +x, at 0 .. 300 from the cast point; wounded to half health so heals show
	const Coord3D where{ 3000.0f, 3000.0f, 0.0f };
	const AuditFaction &own = kAudit[faction];
	const AuditFaction &foe = kAudit[g.enemyFaction];
	std::vector<Probe> probes;
	const float dists[] = { 0.0f, 50.0f, 100.0f, 150.0f, 200.0f, 300.0f };
	auto addProbe = [&](const std::string &role, const char *name, int owner, float x, float y)
	{
		Object *o = g.place(name, owner, x, y);
		o->friend_onBuildComplete();
		ActiveBody *b = dynamic_cast<ActiveBody *>(o->getBodyModule());
		float maxH = 0.0f;
		if (b)
		{
			maxH = b->getMaxHealth();
			b->internalChangeHealth(-maxH * 0.5f);
		}
		probes.push_back(Probe{ role, o->getID(), maxH });
	};
	// side 0: only the caster's probes; side 1: only the enemy's (and a creep): probes of both sides would fight each other and hide the power's effect
	for (float d : dists)
	{
		if (side == 0)
		{
			addProbe("own_soldier@" + std::to_string((int)d), own.soldier, 0, where.x - d, where.y);
		}
		else
		{
			addProbe("foe_soldier@" + std::to_string((int)d), foe.soldier, 1, where.x + d, where.y);
		}
	}
	if (side == 0)
	{
		addProbe("own_economy@120", own.economy, 0, where.x, where.y + 120.0f);
	}
	else
	{
		addProbe("foe_economy@120", foe.economy, 1, where.x, where.y - 120.0f);
		addProbe("foe_creep@60", "NeutralWarg", 1, where.x + 30.0f, where.y + 50.0f);
	}
	// the probes before the cast (the "pre" sample)
	std::ostringstream pre;
	for (size_t i = 0; i < probes.size(); ++i)
	{
		const Object *o = g.logic->findObjectByID(probes[i].id);
		pre << (i ? "," : "") << "\"" << probes[i].role << "\":[" << (probes[i].maxHealth > 0.0f ? healthOf(o) / probes[i].maxHealth : 0.0f) << "]";
	}
	const ObjectID firstNew = g.logic->getNextObjectID();
	const std::map<std::string, unsigned, std::less<>> stopsBefore(g.logic->notedStopHits().begin(), g.logic->notedStopHits().end());
	const size_t errorsBefore = g.logic->report().errors.size();
	const unsigned long long castsBefore = g.spell.casts();
	const std::uint32_t moneyBefore = g.player(0)->getMoney()->countMoney();
	const int weatherBefore = g.logic->weather().weather();
	const unsigned castFrame = g.logic->getFrame();
	if (btn.needsPosition)
	{
		g.castAt(t, where);
	}
	else
	{
		g.cast(t);
	}
	*cast = g.spell.casts() == castsBefore + 1;
	const unsigned readyFrame = sp->getReadyFrame();
	// the recharge starts at the cast and lasts the template's ReloadTime (a passive's 0 still waits the frame, RW 0x89724B)
	CHECK_MESSAGE(readyFrame - castFrame == std::max(1u, t->getReloadTime()), t->getName());
	std::ostringstream js;
	js << "{\"faction\":\"" << own.templateName << "\",\"slot\":" << slot + 1 << ",\"power\":\"" << t->getName() << "\",\"cost\":" << cost
	   << ",\"points_spent\":" << pointsSpent << ",\"granted\":" << granted << ",\"ready_after_buy\":" << (readyAfterBuy ? "true" : "false")
	   << ",\"target\":\"" << (btn.needsPosition ? "location" : "none") << "\",\"cast\":" << (*cast ? "true" : "false")
	   << ",\"reload_frames_ini\":" << t->getReloadTime() << ",\"recharge_frames\":" << (int)(readyFrame - castFrame)
	   << ",\"radius_cursor\":" << t->getRadiusCursorRadius() << ",\"side\":" << side << ",\"pre\":{" << pre.str() << "}";
	std::map<ObjectID, Spawn> spawns;
	auto scanSpawns = [&](unsigned frame)
	{
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getID() < firstNew || o->isDestroyed())
			{
				continue;
			}
			Spawn &sp2 = spawns[o->getID()];
			if (sp2.name.empty())
			{
				sp2.name = o->getTemplate()->getName();
				sp2.firstFrame = frame;
				const Coord3D *p = o->getPosition();
				sp2.dist = std::sqrt((p->x - where.x) * (p->x - where.x) + (p->y - where.y) * (p->y - where.y));
				sp2.owner = o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerIndex() : -1;
				sp2.rank = o->getExperienceTracker() ? o->getExperienceTracker()->getRank() : -1;
			}
			sp2.lastFrame = frame;
		}
		for (auto &kv : spawns)
		{
			const Object *o = g.logic->findObjectByID(kv.first);
			kv.second.alive = o && !o->isDestroyed() && !o->isEffectivelyDead();
		}
	};
	scanSpawns(0);
	js << ",\"samples\":[";
	unsigned frame = 0;
	bool firstSample = true;
	for (unsigned target : kSamples)
	{
		if (target > maxFrame)
		{
			break;
		}
		while (frame < target)
		{
			g.logic->runLogicFrame();
			++frame;
			if (frame % 5 == 0)
			{
				scanSpawns(frame);
			}
		}
		scanSpawns(frame);
		js << (firstSample ? "" : ",") << "{\"f\":" << frame << ",\"probes\":{";
		firstSample = false;
		for (size_t i = 0; i < probes.size(); ++i)
		{
			const Object *o = g.logic->findObjectByID(probes[i].id);
			const int owner = (o && o->getControllingPlayer()) ? o->getControllingPlayer()->getPlayerIndex() : -1;
			js << (i ? "," : "") << "\"" << probes[i].role << "\":[" << (probes[i].maxHealth > 0.0f ? healthOf(o) / probes[i].maxHealth : 0.0f) << "," << owner
			   << ",\"" << jsonEscape(modifiersOf(o)) << "\"]";
		}
		std::map<std::string, int> alive;
		for (const auto &kv : spawns)
		{
			if (kv.second.alive)
			{
				++alive[kv.second.name];
			}
		}
		js << "},\"alive\":{";
		bool first = true;
		for (const auto &kv : alive)
		{
			js << (first ? "" : ",") << "\"" << kv.first << "\":" << kv.second;
			first = false;
		}
		js << "}}";
	}
	js << "]";
	// every object the cast made: name, owner, rank, first / last frame seen, distance from the cast point when first seen
	std::map<std::string, std::vector<const Spawn *>> byName;
	for (const auto &kv : spawns)
	{
		byName[kv.second.name].push_back(&kv.second);
	}
	js << ",\"spawned\":{";
	bool first = true;
	for (const auto &kv : byName)
	{
		float dmin = 1e9f, dmax = 0.0f;
		unsigned lmin = ~0u, lmax = 0, fmin = ~0u;
		std::set<int> owners, ranks;
		for (const Spawn *s2 : kv.second)
		{
			dmin = std::min(dmin, s2->dist);
			dmax = std::max(dmax, s2->dist);
			const unsigned life = s2->alive ? 999999u : s2->lastFrame - s2->firstFrame;
			lmin = std::min(lmin, life);
			lmax = std::max(lmax, life);
			fmin = std::min(fmin, s2->firstFrame);
			owners.insert(s2->owner);
			ranks.insert(s2->rank);
		}
		js << (first ? "" : ",") << "\"" << kv.first << "\":{\"n\":" << kv.second.size() << ",\"first_frame\":" << fmin << ",\"dist\":[" << dmin << "," << dmax
		   << "],\"life\":[" << lmin << "," << lmax << "],\"owners\":[";
		first = false;
		bool f2 = true;
		for (int o : owners)
		{
			js << (f2 ? "" : ",") << o;
			f2 = false;
		}
		js << "],\"ranks\":[";
		f2 = true;
		for (int r : ranks)
		{
			js << (f2 ? "" : ",") << r;
			f2 = false;
		}
		js << "]}";
	}
	js << "}";
	// the stop lines the cast met (new texts and new hits), the new errors, money, weather
	js << ",\"stops\":{";
	first = true;
	for (const auto &kv : g.logic->notedStopHits())
	{
		const auto it = stopsBefore.find(kv.first);
		const unsigned before = it == stopsBefore.end() ? 0u : it->second;
		if (kv.second > before)
		{
			js << (first ? "" : ",") << "\"" << jsonEscape(kv.first) << "\":" << kv.second - before;
			first = false;
		}
	}
	const GameLogic::Report rep = g.logic->report();
	js << "},\"errors\":[";
	for (size_t i = errorsBefore; i < rep.errors.size(); ++i)
	{
		js << (i > errorsBefore ? "," : "") << "\"" << jsonEscape(rep.errors[i]) << "\"";
	}
	js << "],\"unported_modules\":{";
	first = true;
	for (const auto &kv : rep.unportedCreated)
	{
		js << (first ? "" : ",") << "\"" << jsonEscape(kv.first) << "\":" << kv.second;
		first = false;
	}
	js << "},\"money_delta\":" << (long long)g.player(0)->getMoney()->countMoney() - (long long)moneyBefore << ",\"weather_before\":" << weatherBefore
	   << ",\"weather_after\":" << g.logic->weather().weather() << "}";
	return js.str();
}
} // namespace

TEST_CASE("SPELLAUDIT-1: every faction's spell book power is bought and cast in a fresh game; the sweep records what each cast does")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	const char *out = std::getenv("SPELLAUDIT1_OUT");
	const char *only = std::getenv("SPELLAUDIT1_ONLY"); // a power name: sweep just that one
	std::ofstream file;
	if (out)
	{
		file.open(out);
		REQUIRE_MESSAGE(file.good(), out);
	}
	int casts = 0, total = 0;
	for (int f = 0; f < 7; ++f)
	{
		size_t n = 0;
		{
			AuditGame probe(*shared, f);
			n = auditPowers(*shared, *probe.book).size();
		}
		CHECK(n == 12u);
		for (size_t k = 0; k < n; ++k)
		{
			if (only)
			{
				AuditGame probe(*shared, f);
				if (auditPowers(*shared, *probe.book)[k].power->getName() != only)
				{
					continue;
				}
			}
			bool cast = false;
			std::string line;
			try
			{
				for (int side = 0; side < ((out || only) ? 2 : 1); ++side)
				{
					line += (side ? "\n" : "") + auditOne(*shared, f, k, side, &cast, (out || only) ? 2700u : 30u); // the gate run: one side, one second
				}
			}
			catch (const std::exception &e)
			{
				// a power whose cast throws is a finding: recorded, and the sweep goes on
				line = std::string("{\"faction\":\"") + kAudit[f].templateName + "\",\"slot\":" + std::to_string(k + 1) + ",\"exception\":\"" + jsonEscape(e.what()) + "\"}";
				FAIL_CHECK(kAudit[f].templateName << " slot " << k + 1 << ": " << e.what());
			}
			++total;
			casts += cast ? 1 : 0;
			CHECK_MESSAGE(cast, kAudit[f].templateName << " slot " << k + 1);
			if (file.is_open())
			{
				file << line << "\n";
				file.flush();
			}
			else if (only)
			{
				MESSAGE(line);
			}
		}
	}
	MESSAGE("SPELLAUDIT-1 sweep: " << casts << " of " << total << " powers cast");
	CHECK(total == (only ? 1 : 84)); // 7 factions x 12 slots (SPELLAUDIT1_ONLY: the one power)
}

namespace
{
// one power cast at the audit point of a fresh game (no probes), then `frames` logic frames (5 a second)
struct OneCast
{
	AuditGame g;
	ObjectID firstNew = INVALID_ID;
	OneCast(starttest::Shared &shared, int faction, const char *power, unsigned frames)
		: g(shared, faction)
	{
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(power);
		REQUIRE_MESSAGE(t, power);
		for (ScienceType st : t->getRequiredSciences())
		{
			g.player(0)->science().grantScience(st);
		}
		bool position = false;
		for (const AuditButton &b : auditPowers(shared, *g.book))
		{
			position = b.power == t ? b.needsPosition : position;
		}
		firstNew = g.logic->getNextObjectID();
		if (position)
		{
			g.castAt(t, Coord3D{ 3000.0f, 3000.0f, 0.0f });
		}
		else
		{
			g.cast(t);
		}
		for (unsigned f = 0; f < frames; ++f)
		{
			g.logic->runLogicFrame();
		}
	}
	std::vector<Object *> made(const char *name)
	{
		std::vector<Object *> out;
		for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getID() >= firstNew && !o->isDestroyed() && !o->isEffectivelyDead() && o->getTemplate()->getName() == name)
			{
				out.push_back(o);
			}
		}
		return out;
	}
};

struct SummonRow
{
	int faction;
	const char *power;
	const char *made;
	size_t count;
	unsigned lifetimeMs; ///< the template's LifetimeUpdate MinLifetime = MaxLifetime (0: none)
};

// retail RotWK 2.01 INI: the OCL chain's Count (hordes and their contained members) and the summoned template's LifetimeUpdate; the sweep of
// logs/spellaudit1-matrix.md measured the same numbers
const SummonRow kSummons[] = {
	{ 0, "SpellBookTomBombadil", "TomBombadil_Summoned", 1, 60000 },               // OCL_SpawnTomBombadil; tombombadil.ini LifetimeUpdate 60000
	{ 0, "SpellBookHobbitAllies", "RohanHobbitFriendsHorde_Summoned", 3, 120000 }, // OCL_SpawnHobbits Count 3; hobbits.ini 120000
	{ 0, "SpellBookHobbitAllies", "RohanHobbit_Summoned", 15, 120000 },            // 3 hordes x 5
	{ 0, "SpellBookRohanAllies", "RohanRohirrimHorde_Summoned", 5, 180000 },       // OCL_SpawnRohanRohirrim Count 5
	{ 0, "SpellBookRohanAllies", "RohanRohirrim_Summoned", 25, 180000 },           // GOOD_RIDER_HORDE_SIZE 5
	{ 0, "SpellBookDunedainAllies", "GondorRangerHorde_Summoned", 4, 120000 },     // RotWK Count 4 (BFME2 1.06: 3); DUNEDAIN_ALLIES_LIFETIME
	{ 0, "SpellBookDunedainAllies", "GondorRanger_Summoned", 40, 120000 },
	{ 1, "SpellBookEntAllies", "RohanEntBirch_Summoned", 1, 120000 },              // OCL_SpawnEnts: 3 of Fir / Birch (logic random pick)
	{ 1, "SpellBookEagleAllies", "GondorGwaihir_Summoned", 2, 120000 },            // OCL_SpawnEagles: 2
	{ 2, "SpellBookMenOfDaleAllies", "DwarvenMenOfDaleHorde_Summoned", 3, 180000 },
	{ 2, "SpellBookMenOfDaleAllies", "DwarvenMenOfDale_Summoned", 30, 180000 },
	{ 3, "SpellBookWildMenAllies", "IsengardWildmanHorde_Summoned", 4, 120000 },   // ISENGARD_WILDMAN_SUMMON_DURATION
	{ 3, "SpellBookWildMenAllies", "IsengardWildman_Summoned", 48, 120000 },
	{ 3, "SpellBookWatcherAlly", "WatcherHead", 1, 30000 },                        // OCL_HatchWatcher; WATCHER_LIFETIME
	{ 3, "SpellBookDragonAlly", "SummonedDragonJH1", 1, 60000 },
	{ 3, "SpellBookCrebain", "Crebain", 1, 60000 },
	{ 4, "SpellBookEyeofSauron", "EyeOfSauron", 1, 60000 },
	{ 4, "SpellBookBalrogAlly", "MordorBalrog", 1, 90000 },                        // BALROG_LIFETIME
	{ 4, "SpellBookAwakenWyrm", "Wyrm", 1, 60000 },
	{ 5, "SpellBookSpiderlingAllies", "WildSpiderling_Summoned", 8, 180000 },      // 8 eggs, one spider each
	{ 5, "SpellBookCaveBats", "WildCaveBats", 1, 60000 },
	{ 6, "SpellBookSummonOrcs", "AngmarOrcWarriors_Summoned", 3, 90000 },          // CREATE_A_HERO_REINFORCEMENT_LIFETIME
	{ 6, "SpellBookSummonOrcs", "AngmarOrcWarrior_Summoned", 60, 90000 },
	{ 6, "SpellBookSummonGiants", "WildMountainGiant_Summoned", 2, 90000 },
	{ 6, "SpellBookSummonWights", "BarrowWight_Summoned", 5, 90000 },
	{ 6, "SpellBookSummonShadeOfWolf", "AngmarShadeWolf", 1, 90000 },
	{ 1, "SpellBookFarsight", "FarSeeingPing", 1, 90000 },                         // PalantirVisionBase LifetimeUpdate 90000
	{ 3, "SpellBookPalantirVision", "PalantirVisionPing", 1, 90000 },
	{ 1, "SpellBookEnshroudingMist", "EnshroudingMistPing", 1, 60000 },
	{ 6, "SpellBookFrozenLand", "FrozenLandPing", 1, 300000 },
};
} // namespace

TEST_CASE("SPELLAUDIT-1 retail: every spell book summon makes the INI's count of units and gives them the template's lifetime")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	for (const SummonRow &row : kSummons)
	{
		INFO(row.power << " -> " << row.made);
		OneCast c(*shared, row.faction, row.power, 60); // 12 s: every egg of these chains has hatched (the Balrog's, the longest, at 8 s)
		const std::vector<Object *> made = c.made(row.made);
		if (std::string(row.made) == "RohanEntBirch_Summoned")
		{
			CHECK(made.size() + c.made("RohanEntFir_Summoned").size() == 3u); // OCL_SpawnEnts Count 3, Fir / Birch by the logic random
			continue;
		}
		CHECK(made.size() == row.count);
		for (Object *o : made)
		{
			const LifetimeUpdate *life = dynamic_cast<const LifetimeUpdate *>(o->findModule("LifetimeUpdate"));
			REQUIRE(life);
			CHECK(life->dieFrame() - life->startFrame() == row.lifetimeMs / 200u); // 5 logic frames a second
		}
	}
}

// The BROKEN rows of logs/spellaudit1-matrix.md: each test pins what the port does TODAY next to retail's value (a gate fails on any failed assertion, so
// no should_fail). The fix that makes a power work changes the pinned line to retail's value (named in the comment beside it).
TEST_CASE("SPELLAUDIT-1 broken (pinned as it is today): Army of the Dead's six hordes are empty; retail fills each with 16 Oathbreakers (AODHordeContain)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	OneCast c(*shared, 0, "SpellBookArmyoftheDead", 60);
	CHECK(c.made("RohanOathbreakerHordeSmall").size() == 6u);
	CHECK(c.made("RohanOathbreaker").size() == 0u); // BROKEN: retail 96 (OATHBREAKER_SMALL_HORDE_SIZE 16 x 6); AODHordeContain is not bound
}

TEST_CASE("SPELLAUDIT-1 broken (pinned as it is today): Dragon Strike makes no dragon; retail's comes in from the map edge (OCL_SpellBookDragonStrike, S-931)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	OneCast c(*shared, 3, "SpellBookDragonStrike", 10);
	CHECK(c.made("SpellBookDragonStrikeDragon").size() == 0u); // BROKEN: retail 1 (CREATE_AT_EDGE_NEAR_TARGET_AND_MOVE_TO_LOCATION, S-931)
}

TEST_CASE("SPELLFIX-1 retail: a vision ping without ShroudClearingRange clears its VisionRange (RW 0x73FB7A default -1, RW 0x69990F)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	// FarSeeingPing VisionRange 250, PalantirVisionPing 300, EyeOfSauron 200, none with a ShroudClearingRange row
	const std::pair<const char *, float> pings[] = { { "FarSeeingPing", 250.0f }, { "PalantirVisionPing", 300.0f }, { "EyeOfSauron", 200.0f } };
	for (const auto &p : pings)
	{
		const ThingTemplate *t = shared->world->things().findTemplate(p.first);
		REQUIRE_MESSAGE(t, p.first);
		CHECK_MESSAGE(ShroudManager::templateVision(*t->getFinalOverride()).shroudClearingRange == p.second, p.first); // the INI's VisionRange, exact
	}
}

TEST_CASE("SPELLFIX-1 retail: Industry triples a resource building's income (SpellBookIndustry PRODUCTION 300 %)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	AuditGame g(*shared, 3);
	Object *furnace = g.place("IsengardFurnace", 0, 3000.0f, 3100.0f);
	furnace->friend_onBuildComplete();
	const float before = g.logic->economy().productionMoneyProduct(*furnace);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate("SpellBookIndustry");
	REQUIRE(t);
	for (ScienceType st : t->getRequiredSciences())
	{
		g.player(0)->science().grantScience(st);
	}
	g.castAt(t, Coord3D{ 3000.0f, 3000.0f, 0.0f });
	g.logic->runLogicFrame();
	CHECK(furnace->attributeModifierPoolModule() != nullptr);
	CHECK(before == 1.0f);
	CHECK(g.logic->economy().productionMoneyProduct(*furnace) == 3.0f); // PRODUCTION 300 %: the product of one entry, exact
}

TEST_CASE("SPELLAUDIT-1 broken (pinned as it is today): the Watcher's eight arms are gone by 10 s; retail's live as long as its head (WATCHER_LIFETIME 30 s)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	OneCast c(*shared, 3, "SpellBookWatcherAlly", 50); // 10 s: the arms are gone after ~3 s in the port
	CHECK(c.made("WatcherHead").size() == 1u);
	CHECK(c.made("WatcherHittingArm").size() == 0u); // BROKEN: retail 8
}

// Lane SPELLFIX-1 (SPELLAUDIT-1 phase 2, the first batch): the cross-cutting fixes X1, X2, X4, X5 and Heal's refill, each pinned on the powers it reaches.
namespace
{
// grant the power's sciences and cast it at the sweep's point
void grantAndCastAt(AuditGame &g, const char *power, const Coord3D &where)
{
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(power);
	REQUIRE_MESSAGE(t, power);
	for (ScienceType st : t->getRequiredSciences())
	{
		g.player(0)->science().grantScience(st);
	}
	g.castAt(t, where);
}

void steps(AuditGame &g, unsigned frames)
{
	for (unsigned f = 0; f < frames; ++f)
	{
		g.logic->runLogicFrame();
	}
}

std::vector<Object *> alive(AuditGame &g, const char *name, ObjectID firstNew)
{
	std::vector<Object *> out;
	for (Object *o = g.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getID() >= firstNew && !o->isDestroyed() && !o->isEffectivelyDead() && o->getTemplate()->getName() == name)
		{
			out.push_back(o);
		}
	}
	return out;
}
} // namespace

TEST_CASE("SPELLFIX-1 retail: Blight halves an enemy resource building's income, Dwarven Riches triples a mine's (PRODUCTION 50 % / 300 %)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	{
		AuditGame g(*shared, 6); // Angmar casts, Mordor is the enemy
		Object *house = g.place("MordorSlaughterHouse", 1, 3000.0f, 3000.0f);
		house->friend_onBuildComplete();
		CHECK(g.logic->economy().productionMoneyProduct(*house) == 1.0f);
		grantAndCastAt(g, "SpellBookBlight", Coord3D{ 3000.0f, 3000.0f, 0.0f });
		steps(g, 1);
		CHECK(g.logic->economy().productionMoneyProduct(*house) == 0.5f); // SpellBookBlight PRODUCTION 50 %: exact
	}
	{
		AuditGame g(*shared, 2);
		Object *mine = g.place("DwarvenMineShaft", 0, 3000.0f, 3050.0f); // inside the power's radius 100
		mine->friend_onBuildComplete();
		CHECK(g.logic->economy().productionMoneyProduct(*mine) == 1.0f);
		grantAndCastAt(g, "SpellBookDwarvenRiches", Coord3D{ 3000.0f, 3000.0f, 0.0f });
		steps(g, 1);
		CHECK(g.logic->economy().productionMoneyProduct(*mine) == 3.0f); // SpellBookDwarvenRiches PRODUCTION 300 %: exact
	}
}

TEST_CASE("SPELLFIX-1 retail: a furnace under Industry earns three times what it earns without (TerrainResourceBehavior RW 0x885591 with the pool's product)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	std::vector<int> pay[2];
	for (int cast = 0; cast < 2; ++cast)
	{
		AuditGame g(*shared, 3);
		Object *furnace = g.place("IsengardFurnace", 0, 3000.0f, 3100.0f);
		furnace->friend_onBuildComplete();
		TerrainResourceBehavior *trb = dynamic_cast<TerrainResourceBehavior *>(furnace->findModule("TerrainResourceBehavior"));
		REQUIRE(trb);
		const ObjectID id = furnace->getID();
		std::vector<int> &mine = pay[cast];
		g.logic->economy().setIncomeListener([&mine, id](const Economy::IncomeEvent &e) {
			if (e.object == id)
			{
				mine.push_back(e.amount);
			}
		});
		if (cast)
		{
			grantAndCastAt(g, "SpellBookIndustry", Coord3D{ 3000.0f, 3000.0f, 0.0f });
		}
		steps(g, 1);
		MESSAGE("the furnace's ground share " << trb->claimShare());
		if (trb->claimShare() == 0.0f)
		{
			trb->setClaimShare(1.0f); // the audit game has no map, so no resource grid: the whole ground share (RW 0x5F2D53's value for an uncontested claim)
		}
		steps(g, 1500); // 300 s
		g.logic->economy().setIncomeListener(nullptr);
	}
	REQUIRE(!pay[0].empty());
	REQUIRE(pay[1].size() == pay[0].size());
	std::ostringstream seq;
	for (size_t i = 0; i < pay[0].size(); ++i)
	{
		seq << " " << pay[0][i] << "/" << pay[1][i];
	}
	MESSAGE("payouts without / with Industry:" << seq.str());
	// ceil(MaxIncome * share * 3.0 * 1.0) of an integer MaxIncome at share 1: exactly three times (25 -> 75), until the income's experience levels the
	// Industry furnace up: FurnaceLevel2Production (ISENGARD_FURNACE_LVL2_PRODUCTION 1.25) and then FurnaceLevel3Production (1.5) multiply in as well, the same
	// pool query (ceil(75 * 1.25) = 94, ceil(75 * 1.25 * 1.5) = 141);
	// the furnace without Industry earns too little to level in 300 s
	std::vector<int> without(50, 25), with;
	with.insert(with.end(), 20, 75);
	with.insert(with.end(), 16, 94);
	with.insert(with.end(), 14, 141);
	CHECK(pay[0] == without);
	CHECK(pay[1] == with);
}

// still broken (SPELLFIX-1): the ping's first update comes at IdleSpawnRate (2 s), after its 1 s life. Retail's ctor RW 0x89A258 sets no wake; porting it
// alone overfilled produced Angmar hordes (HordeBanner.cpp, S-587), so it waits for the production order's read; with it the horde is back at 15.
TEST_CASE("SPELLAUDIT-1 broken (pinned as it is today): Heal's replenish ping dies before its first update and refills no horde (BannerCarrierUpdate ctor RW 0x89A258)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	AuditGame g(*shared, 0);
	Object *horde = g.place("GondorFighterHorde", 0, 3000.0f, 3000.0f);
	steps(g, 10);
	HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
	REQUIRE(hc);
	const unsigned full = hc->payloadCount();
	REQUIRE(full == 15u); // GondorFighterHorde: 15 members
	REQUIRE((int)full == hc->getSlotCapacity());
	Object *victim = nullptr;
	for (Object *m : *hc->getContainedItemsList())
	{
		victim = m->getID() != hc->bannerCarrier() ? m : victim;
	}
	REQUIRE(victim);
	DamageInfo d;
	d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	d.m_input.m_amount = 100000.0f;
	victim->attemptDamage(d);
	steps(g, 5);
	REQUIRE(hc->payloadCount() == full - 1u); // a rank-1 horde has no banner carrier: nothing else refills it
	const ObjectID firstNew = g.logic->getNextObjectID();
	grantAndCastAt(g, "SpellBookHeal", Coord3D{ 3000.0f, 3000.0f, 0.0f });
	steps(g, 1);
	CHECK(alive(g, "HealSpellHordeReplenishPing", firstNew).size() == 1u);
	steps(g, 10); // the ping lives 1 s (DeletionUpdate 1000 = 5 frames); its IdleSpawnRate (2 s) is only the period after the first update
	CHECK(alive(g, "HealSpellHordeReplenishPing", firstNew).empty());
	CHECK(hc->payloadCount() == full - 1u); // BROKEN: retail full (15), the ping's one member
	steps(g, 50);
	CHECK(hc->payloadCount() == full - 1u); // BROKEN: retail full (15)
}

// StartingBusyTime puts the object's AI in the busy state as a temporary state (RW 0x5F1D2E -> RW 0x66498A -> RW 0x74168E). A summon with a StancesBehavior
// leaves it on its first update: StancesBehavior::update (RW 0x8622E9) sets Battle from the uninitialised stance, setStance (RW 0x8620DD) asks the AI's
// isIdle (slot 0x1B8, RW 0x6643FC: the CURRENT state, the temporary one not asked) and guards in place (RW 0x662A21 -> setState(0x10)), and setState drops
// a timed temporary state (RW 0x751E3A -> RW 0x751DA9). So in retail only the summons without stances wait: the Wyrm and Gondor's Lone Tower.
TEST_CASE("SPELLFIX-1 retail: StartingBusyTime holds a summon without stances in the busy state; a stance summon's first stance ends it (RW 0x5F1D2E, RW 0x8620DD)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	struct Row
	{
		int faction;
		const char *power;
		const char *made;
		bool stances;
		unsigned busySamples; ///< logic frames sampled (after each frame) in the busy state
	};
	// the busy frames: the OCL's milliseconds at 5 logic frames a second (parseDurationUnsignedInt), sampled from the end of the frame the object was made
	// in (an egg's hatch: the Wyrm, 2100 ms = 11 frames + that frame) or from the next frame (made by the cast itself: the tower, 5000 ms = 25)
	const Row rows[] = {
		{ 0, "SpellBookRohanAllies", "RohanRohirrimHorde_Summoned", true, 0 }, // OCL_SpawnRohanRohirrim StartingBusyTime 5000
		{ 1, "SpellBookEntAllies", "RohanEntBirch_Summoned", true, 0 },        // OCL_SpawnEnts 5000
		{ 1, "SpellBookEntAllies", "RohanEntFir_Summoned", true, 0 },
		{ 4, "SpellBookBalrogAlly", "MordorBalrog", true, 0 },                 // OCL_BalrogSummoningFinish 2300
		{ 4, "SpellBookAwakenWyrm", "Wyrm", false, 12 },                       // OCL_HatchWyrm 2100
		{ 6, "SpellBookSummonShadeOfWolf", "AngmarShadeWolf", true, 0 },       // OCL_ShadeofWolfSummoningFinish 2300
		{ 3, "SpellBookDragonAlly", "SummonedDragonJH1", true, 0 },            // OCL_HatchSummonedDragon 1666 / 3500
		{ 0, "SpellBookSpawnLoneTower", "GondorSentryTower_Independant", false, 25 }, // OCL_GondorSpellBookSpawnLoneTower LONE_TOWER_BUILD_TIME 5000
	};
	for (const Row &r : rows)
	{
		INFO(std::string(r.power) << " -> " << r.made);
		AuditGame g(*shared, r.faction);
		const ObjectID firstNew = g.logic->getNextObjectID();
		grantAndCastAt(g, r.power, Coord3D{ 3000.0f, 3000.0f, 0.0f });
		std::map<ObjectID, unsigned> busy;
		std::map<ObjectID, bool> stances;
		for (unsigned f = 0; f < 120; ++f)
		{
			g.logic->runLogicFrame();
			for (Object *o : alive(g, r.made, firstNew))
			{
				const AIUpdateInterface *ai = o->getAIUpdateInterface();
				const AIStateMachine *m = ai ? ai->stateMachineOrNull() : nullptr;
				stances[o->getID()] = o->findModule("StancesBehavior") != nullptr;
				busy[o->getID()] += m && m->isBusyState() ? 1u : 0u;
			}
		}
		if (std::string(r.made).rfind("RohanEnt", 0) != 0)
		{
			REQUIRE(!busy.empty()); // the Ents: the logic random picks Fir or Birch, either list may be empty
		}
		for (const auto &e : busy)
		{
			CHECK(stances[e.first] == r.stances);
			CHECK(e.second == r.busySamples);
		}
	}
}

TEST_CASE("SPELLFIX-1 retail: a spell book structure clears the trees under it and moves own units off its footprint (ClearRemovables, RW 0x5F12D8)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	AuditGame g(*shared, 2);
	const ThingTemplate *treeT = shared->world->things().findTemplate("GenericInteractableOptimizedTree");
	REQUIRE(treeT);
	TerrainTrees &trees = g.logic->trees();
	if (!trees.hasExtent())
	{
		trees.setExtent(0.0f, 0.0f, 6000.0f, 6000.0f);
	}
	const Coord3D where{ 3000.0f, 3000.0f, 0.0f };
	REQUIRE(trees.addTree(*treeT, where, 77u));
	Object *soldier = g.place("DwarvenGuardian", 0, 3005.0f, 3000.0f);
	soldier->friend_onBuildComplete();
	steps(g, 2);
	const Coord3D start = *soldier->getPosition();
	const ObjectID firstNew = g.logic->getNextObjectID();
	grantAndCastAt(g, "SpellBookSpawnLoneTowerDwarf", where);
	steps(g, 1);
	REQUIRE(alive(g, "DwarvenSentryTower_Independent", firstNew).size() == 1u);
	bool treeLeft = false;
	for (const TerrainTrees::Record &t : trees.records())
	{
		treeLeft = treeLeft || t.clientId == 77u;
	}
	CHECK_FALSE(treeLeft); // removeTreesForConstruction RW 0x684BCA: gone at once
	steps(g, 50);
	const float dx = soldier->getPosition()->x - start.x, dy = soldier->getPosition()->y - start.y;
	MESSAGE("the soldier moved " << std::sqrt(dx * dx + dy * dy));
	CHECK(!soldier->isEffectivelyDead());
	CHECK(dx * dx + dy * dy > 20.0f * 20.0f); // tolerance: moveObjectsForConstruction RW 0x797465 sends it 0.5 .. 1.5 bounding circles away (a random spot)
}

TEST_CASE("SPELLFIX-1 retail: the templates whose shroud clearing range is their VisionRange (no ShroudClearingRange row, RW 0x69990F)")
{
	OPENBFME_REQUIRE_START(shared);
	auto ctx = shared->world->enterContext();
	std::vector<std::string> names;
	for (const ThingTemplate *t : shared->world->things().templates())
	{
		const ThingTemplate &tt = *t->getFinalOverride();
		const std::vector<std::string> *table = tt.fieldNameTable();
		bool hasRow = false;
		float vision = 0.0f;
		for (size_t i = 0; table && i < table->size(); ++i)
		{
			const FieldValue *f = tt.fieldAt(i);
			if (!f)
			{
				continue;
			}
			if ((*table)[i] == "ShroudClearingRange")
			{
				hasRow = true;
			}
			if ((*table)[i] == "VisionRange")
			{
				if (const float *x = std::get_if<float>(f))
				{
					vision = *x;
				}
				else if (const long long *n = std::get_if<long long>(f))
				{
					vision = (float)*n;
				}
			}
		}
		if (!hasRow && vision > 0.0f)
		{
			names.push_back(tt.getName());
			CHECK(ShroudManager::templateVision(tt).shroudClearingRange == vision);
		}
	}
	std::sort(names.begin(), names.end());
	std::ostringstream os;
	for (const std::string &n : names)
	{
		os << " " << n;
	}
	MESSAGE(names.size() << " templates clear their VisionRange:" << os.str());
	CHECK(std::find(names.begin(), names.end(), "FarSeeingPing") != names.end());
}
