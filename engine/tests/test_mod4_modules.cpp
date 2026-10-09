// OpenBFME unit tests. GPL-3.0.
// Lane MOD-4 (QA-1 U2): the modules nobody had ported. Binary facts read from the RotWK binary (RW_GAME_DAT), the retail data (ROTWK_INSTALL) and live objects
// carrying each module in a started skirmish. The expectations are retail's rules (each check names its address), never this engine's frame-exact output.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/Module/Mod4Stops.h"
#include "GameLogic/Module/RepairSpecialPower.h"
#include "GameLogic/Module/SlavedUpdate.h"
#include "GameLogic/Module/SpawnBehavior.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
float floatAt(const retailtest::PeImage &pe, std::uint32_t va)
{
	const std::uint32_t u = pe.u32At(va);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}

// the lobby's result: slot 0 a human (`faction0`), slot 1 a computer of `aiState` (`faction1`) on Evendim, start positions 0 and 4 (BUILD-1's game)
bool startMod4Game(starttest::Shared &s, const std::string &faction0, const std::string &faction1, SlotState aiState, std::uint32_t seed, buildtest::Game &out,
                   std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, faction0);
	b.tmpl = buildtest::templateIndex(s, faction1);
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = aiState;
	message.game.mapName = "maps/map mp evendim/map mp evendim.map";
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

const Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

AISpecialPowerUpdate *aiPower(Object &o, const std::string &button)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		auto *u = dynamic_cast<AISpecialPowerUpdate *>(m.get());
		if (u && u->data()->m_commandButtonName == button)
		{
			return u;
		}
	}
	return nullptr;
}

void setHealthRatio(Object &o, float ratio)
{
	auto *body = dynamic_cast<ActiveBody *>(o.getBodyModule());
	REQUIRE(body);
	body->internalChangeHealth(body->getMaxHealth() * ratio - body->getHealth());
}

Coord3D nearObject(const Object &o, float dx, float dy)
{
	return Coord3D{ o.getPosition()->x + dx, o.getPosition()->y + dy, o.getPosition()->z };
}

Object *firstOf(GameLogic &logic, const Player *owner, const char *kindOfName)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName(kindOfName) && !x->isEffectivelyDead())
		{
			return x;
		}
	}
	return nullptr;
}
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MOD-4: AISpecialPowerUpdate's binary facts: the 53 SpecialPowerAIType names (RW 0xDB84F8), the field table RW 0xC6DAD0 and the decision constants")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("MOD-4 binary facts (set RW_GAME_DAT)");
		return;
	}
	for (int i = 0; i < AI_SPECIAL_POWER_TYPE_COUNT; ++i)
	{
		CHECK(pe->cstring(pe->u32At(0xDB84F8u + 4u * (std::uint32_t)i)) == TheAISpecialPowerTypeNames[i]);
	}
	CHECK(pe->u32At(0xDB84F8u + 4u * AI_SPECIAL_POWER_TYPE_COUNT) == 0u);
	const char *const fields[] = { "CommandButtonName", "SpecialPowerAIType", "SpecialPowerRadius", "SpecialPowerRange", "RandomizeTargetLocation", "SpellMakesAStructure" };
	const std::uint32_t offsets[] = { 8, 12, 16, 20, 24, 25 };
	for (int i = 0; i < 6; ++i)
	{
		CHECK(pe->cstring(pe->u32At(0xC6DAD0u + 16u * (std::uint32_t)i)) == fields[i]);
		CHECK(pe->u32At(0xC6DAD0u + 16u * (std::uint32_t)i + 12u) == offsets[i]);
	}
	CHECK(pe->u32At(0xC6DAD0u + 16u * 6u) == 0u);
	CHECK(floatAt(*pe, 0xBD869C) == 0.5f);    // RW 0x993071: the roll
	CHECK(floatAt(*pe, 0xBD1908) == 1.0f);    // RW 0x99333F: the difficulty chance
	CHECK(floatAt(*pe, 0xC8E250) == 0.5f);    // BASIC_SELF_BUFF
	CHECK(floatAt(*pe, 0xC8E24C) == 0.8f);
	CHECK(floatAt(*pe, 0xC8E254) == 50.0f);
	CHECK(floatAt(*pe, 0xC8E258) == 10000.0f); // GOBLIN_POISON
	CHECK(floatAt(*pe, 0xC71EC8) == 0.35f);    // the stances
	CHECK(floatAt(*pe, 0xBDE8B8) == 10000.0f); // ENEMY_TYPE_KILLER_RANGED
	// RW 0x992724's kinds: each type's vtable slot 6 (0x18) is RW 0x993006 (self), RW 0xA02806 (object) or RW 0xA01C54 (location); the vtables are read from the
	// constructors the switch calls (the type -> constructor pairs of the switch are pinned by the decompile, see AISpecialPowerUpdate.h)
	const std::uint32_t vtables[AI_SPECIAL_POWER_TYPE_COUNT] = { 0xC8E25C, 0xC8E7B8, 0xC8E790, 0xC8E734, 0xC8E70C, 0xC8E5B4, 0xC8E6EC, 0xC8E6C0, 0xC8E568, 0xC8E698,
		0xC8E5DC, 0xC8E590, 0xC8E4BC, 0xC8E494, 0xC8E46C, 0xC8E444, 0xC8E41C, 0xC8E3FC, 0xC8E3CC, 0xC8E394, 0xC8E368, 0xC8E224, 0xC8E0BC, 0xC8E0BC, 0xC8E178,
		0xC8E090, 0xC8E070, 0xC8DF88, 0xC8DF60, 0xC8DF3C, 0xC8DF14, 0xC8DEEC, 0xC8DEC4, 0xC8DE48, 0xC8DFB4, 0xC8DE20, 0xC8DDFC, 0xC8DD50, 0xC8DD28, 0xC8DD00,
		0xC8DCD8, 0xC8DE9C, 0xC8DE74, 0xC8E518, 0xC8E340, 0xC8E310, 0xC8E2BC, 0xC8E310, 0xC8E4E4, 0xC8DCB0, 0xC8E540, 0xC8E75C, 0xC8E284 };
	for (int t = 0; t < AI_SPECIAL_POWER_TYPE_COUNT; ++t)
	{
		const std::uint32_t slot = pe->u32At(vtables[t] + 0x18u);
		const int kind = slot == 0x993006u ? AISpecialPowerDecision::KIND_SELF : slot == 0xA02806u ? AISpecialPowerDecision::KIND_OBJECT : slot == 0xA01C54u ? AISpecialPowerDecision::KIND_LOCATION : -1;
		CHECK_MESSAGE(kind == AISpecialPowerDecision::kindOf(t), TheAISpecialPowerTypeNames[t]);
		// a ported decision names its function; RW 0x7FEAC1 (`xor al, al; ret 4`) is "never"
		const std::uint32_t decision = pe->u32At(vtables[t] + 0x1Cu);
		if (t == 40 || t == 1)
		{
			CHECK(decision == 0x7FEAC1u);
		}
	}
	CHECK(pe->u32At(0xC8DD28u + 0x1Cu) == 0x9E6C47u); // STANCEBATTLE
	CHECK(pe->u32At(0xC8DD00u + 0x1Cu) == 0x9E6BA3u); // STANCEAGGRESSIVE
	CHECK(pe->u32At(0xC8E25Cu + 0x1Cu) == 0x9E9488u); // BASIC_SELF_BUFF
	CHECK(pe->u32At(0xC8E734u + 0x1Cu) == 0x9EC0A7u); // the killers share RW 0x9EC0A7
	CHECK(pe->u32At(0xC8E70Cu + 0x1Cu) == 0x9EC0A7u);
	CHECK(pe->u32At(0xC8E5B4u + 0x1Cu) == 0x9EC0A7u);
	CHECK(pe->u32At(0xC8E75Cu + 0x1Cu) == 0x9EC0A7u);
}

TEST_CASE("MOD-4: RepairSpecialPower's binary facts: SpecialPowerModule's special power interface but the three do* slots (RW 0xC752F0 against RW 0xC64FF0)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("MOD-4 binary facts (set RW_GAME_DAT)");
		return;
	}
	for (std::uint32_t slot = 0; slot < 0x60u; slot += 4u)
	{
		const std::uint32_t a = pe->u32At(0xC752F0u + slot), b = pe->u32At(0xC64FF0u + slot);
		if (slot == 0x28u || slot == 0x2Cu || slot == 0x30u)
		{
			CHECK(a != b);
		}
		else
		{
			CHECK_MESSAGE(a == b, slot);
		}
	}
	CHECK(pe->u32At(0xC752F0u + 0x2Cu) == 0x8CCB85u);
	std::vector<std::uint8_t> ret;
	REQUIRE(pe->read(pe->u32At(0xC752F0u + 0x28u), 3, &ret));
	CHECK(ret[0] == 0xC2); // ret 4
	REQUIRE(pe->read(pe->u32At(0xC752F0u + 0x30u), 3, &ret));
	CHECK(ret[0] == 0xC2); // ret 8
	CHECK(pe->u32At(0xC84858u) == 0u); // the class's own field table is empty
}

// ---- the retail data ---------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MOD-4: every retail AISpecialPowerUpdate names a known SpecialPowerAIType and RepairSpecialPower parses as a SpecialPowerModule (RW 0x992693)")
{
	OPENBFME_REQUIRE_START(s);
	std::map<int, int> byType;
	int declarations = 0, repairs = 0;
	for (const ThingTemplate *tt : s->world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
		{
			if (n.name == "AISpecialPowerUpdate")
			{
				const auto *d = dynamic_cast<const AISpecialPowerUpdateModuleData *>(n.data.get());
				REQUIRE(d);
				const std::string where = tt->getName() + " " + n.tag;
				CHECK_MESSAGE(d->m_specialPowerAIType >= 0, where);
				CHECK_MESSAGE(!d->m_commandButtonName.empty(), where);
				++byType[d->m_specialPowerAIType];
				++declarations;
			}
			if (n.name == "RepairSpecialPower")
			{
				const auto *d = dynamic_cast<const SpecialPowerModuleData *>(n.data.get());
				REQUIRE(d);
				REQUIRE(d->m_specialPowerTemplate);
				CHECK(d->m_specialPowerTemplate->getName() == "SpecialRepairStructure");
				++repairs;
			}
		}
	}
	MESSAGE("AISpecialPowerUpdate nuggets " << declarations << ", RepairSpecialPower " << repairs);
	CHECK(declarations > 300);
	CHECK(repairs > 0);
	// every type the retail data uses most is one this lane ports
	CHECK(byType[38] > 0);
	CHECK(byType[39] > 0);
	CHECK(byType[40] > 0);
	CHECK(byType[1] > 0);
	CHECK(byType[0] > 0);
}

// ---- live objects --------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MOD-4: a Hard computer's Boromir changes his stance on his own (AISpecialPowerUpdate STANCEAGGRESSIVE / STANCEBATTLE, RW 0x9E6BA3 / 0x9E6C47); a human's does not")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startMod4Game(*s, "FactionMen", "FactionMen", SLOT_HARD_AI, 4401, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0), *ai = playerAtStart(logic, 4);
	REQUIRE(human);
	REQUIRE(ai);
	REQUIRE(logic.skirmishAI().findAI(ai->getPlayerIndex()) != nullptr);
	REQUIRE(logic.skirmishAI().findAI(human->getPlayerIndex()) == nullptr);
	const Object *aiFortress = firstOf(logic, ai, "COMMANDCENTER"), *humanFortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(aiFortress);
	REQUIRE(humanFortress);
	Object *mine = g.live->createObject("GondorBoromir", ai->getPlayerIndex(), nearObject(*aiFortress, 250.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(mine, err);
	Object *theirs = g.live->createObject("GondorBoromir", human->getPlayerIndex(), nearObject(*humanFortress, 250.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(theirs, err);
	for (int i = 0; i < 3; ++i)
	{
		g.live->advance(0.2);
	}
	AISpecialPowerUpdate *aggressive = aiPower(*mine, "Command_SetStanceAggressive"), *battle = aiPower(*mine, "Command_SetStanceBattle");
	REQUIRE(aggressive);
	REQUIRE(battle);
	CHECK(aggressive->initialized());
	CHECK(aggressive->active());
	REQUIRE(aggressive->decision());
	CHECK(aggressive->decision()->type == 39);
	CHECK(aggressive->decision()->kind == AISpecialPowerDecision::KIND_SELF);
	REQUIRE(aggressive->decision()->button);
	CHECK(aggressive->decision()->button->m_name == "Command_SetStanceAggressive");
	StancesBehavior *mineStance = StancesBehavior::of(*mine), *theirStance = StancesBehavior::of(*theirs);
	REQUIRE(mineStance);
	REQUIRE(theirStance);
	// full health: Aggressive (the class is not 2, the ratio above 0.35); Hard's SpecialPowerActivationProbability is the constructor's 1 : 1, so the button runs
	// once the counter reaches 25 frames (RW 0x99302D) and the roll (above 0.5) passes
	int frames = 0;
	while (mineStance->getStance() != STANCE_AGGRESSIVE && frames < 400)
	{
		g.live->advance(0.2);
		++frames;
	}
	CHECK(mineStance->getStance() == STANCE_AGGRESSIVE);
	CHECK(frames >= 20);
	CHECK(theirStance->getStance() != STANCE_AGGRESSIVE); // the human's: no skirmish AI (RW 0x6A950B), the module sleeps
	// wounded below 0.35: Battle
	setHealthRatio(*mine, 0.3f);
	CHECK(AISpecialPowerUpdate::healthRatio(*mine) <= 0.35f);
	frames = 0;
	while (mineStance->getStance() != STANCE_BATTLE && frames < 400)
	{
		g.live->advance(0.2);
		++frames;
	}
	CHECK(mineStance->getStance() == STANCE_BATTLE);
}

TEST_CASE("MOD-4: a Rohan peasant's RepairSpecialPower sends it to repair a damaged structure (RW 0x8CCB85 -> aiRepair 0x13); the power does nothing on a unit or for a non-dozer")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startMod4Game(*s, "FactionMen", "FactionMordor", SLOT_EASY_AI, 4402, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	Object *peasant = g.live->createObject("RohanPeasant1", human->getPlayerIndex(), nearObject(*fortress, 300.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(peasant, err);
	Object *soldier = g.live->createObject("GondorBoromir", human->getPlayerIndex(), nearObject(*fortress, 300.0f, 60.0f), 0.0f, &err);
	REQUIRE_MESSAGE(soldier, err);
	g.live->advance(0.2);
	auto *power = dynamic_cast<RepairSpecialPower *>(peasant->findModule("RepairSpecialPower"));
	REQUIRE(power);
	auto *dozer = dynamic_cast<DozerAIUpdate *>(peasant->getAIUpdateInterface());
	REQUIRE(dozer);
	power->doSpecialPowerAtObject(soldier, 0); // not a STRUCTURE
	CHECK(power->repairs() == 0);
	CHECK(!dozer->repairing());
	setHealthRatio(*fortress, 0.5f);
	const float before = fortress->getBodyModule()->getHealth();
	power->doSpecialPowerAtObject(fortress, 0);
	CHECK(power->repairs() == 1);
	CHECK(dozer->repairing());
	CHECK(power->triggers() == 0); // SpecialPowerModule's base do* (the recharge, the trigger) is not run
	for (int i = 0; i < 300 && fortress->getBodyModule()->getHealth() <= before; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(fortress->getBodyModule()->getHealth() > before);
	// doSpecialPower / doSpecialPowerAtLocation do nothing (RW 0x9F3A3C / 0x8851E4)
	power->doSpecialPower(0);
	power->doSpecialPowerAtLocation(*fortress->getPosition(), 0);
	CHECK(power->repairs() == 1);
}

// ---- the registered stops ------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MOD-4: PickupStuffUpdate, GeometryUpgrade, ThreatFinderUpdate and the rebuild holes stay UnportedModules, each reported as its stop [S-1422] .. [S-1424], [S-1427]")
{
	OPENBFME_REQUIRE_START(s);
	const std::vector<std::string> unported = s->world->modules().unportedModuleClassNames();
	const std::vector<std::string> stops = s->world->acceptanceStops();
	const char *const ids[] = { "[S-1422] PickupStuffUpdate", "[S-1423] GeometryUpgrade", "[S-1424] ThreatFinderUpdate", "[S-1427] RebuildHoleExposeDie",
		"[S-1427] RebuildHoleExposeDie" };
	REQUIRE(Mod4Stops::classes().size() == 5);
	for (size_t i = 0; i < Mod4Stops::classes().size(); ++i)
	{
		const std::string &name = Mod4Stops::classes()[i];
		CHECK_MESSAGE(std::find(unported.begin(), unported.end(), name) != unported.end(), name);
		int found = 0;
		for (const std::string &l : stops)
		{
			found += l.rfind(ids[i], 0) == 0 ? 1 : 0;
		}
		CHECK_MESSAGE(found == 1, ids[i]); // the two rebuild classes share S-1427
		// the retail data uses each class
		int uses = 0;
		for (const ThingTemplate *tt : s->world->things().templates())
		{
			for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
			{
				uses += n.name == name ? 1 : 0;
			}
		}
		CHECK_MESSAGE(uses > 0, name);
	}
	// the lane's ported classes are bound
	CHECK(std::find(unported.begin(), unported.end(), "AISpecialPowerUpdate") == unported.end());
	CHECK(std::find(unported.begin(), unported.end(), "RepairSpecialPower") == unported.end());
	int s1421 = 0;
	for (const std::string &l : stops)
	{
		s1421 += l.rfind("[S-1421] AISpecialPowerUpdate", 0) == 0 ? 1 : 0;
	}
	CHECK(s1421 == 1);
}

TEST_CASE("MOD-4: the binary facts behind [S-1422] .. [S-1424]: the three field tables (RW 0xC64600 / 0xC6F578 / 0xC4C320)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("MOD-4 binary facts (set RW_GAME_DAT)");
		return;
	}
	struct Row
	{
		std::uint32_t table;
		std::vector<std::pair<const char *, std::uint32_t>> rows;
	};
	const Row tables[] = {
		{ 0xC64600, { { "SkirmishAIOnly", 8 }, { "ScanRange", 12 }, { "StuffToPickUp", 16 }, { "ScanIntervalSeconds", 20 } } },
		{ 0xC6F578, { { "ShowGeometry", 312 }, { "HideGeometry", 324 }, { "WallBoundsMesh", 336 }, { "RampMesh1", 340 }, { "RampMesh2", 344 } } },
		{ 0xC4C320, { { "DefaultRadius", 8 } } },
	};
	for (const Row &t : tables)
	{
		for (size_t i = 0; i < t.rows.size(); ++i)
		{
			CHECK(pe->cstring(pe->u32At(t.table + 16u * (std::uint32_t)i)) == t.rows[i].first);
			CHECK(pe->u32At(t.table + 16u * (std::uint32_t)i + 12u) == t.rows[i].second);
		}
		CHECK(pe->u32At(t.table + 16u * (std::uint32_t)t.rows.size()) == 0u);
	}
}

// ---- SpawnBehavior / SlavedUpdate ----------------------------------------------------------------------------------------------------------------------------

TEST_CASE("MOD-4: a Cave Troll Lair spawns its troll, which guards the lair, and replaces it SpawnReplaceDelay after its death (SpawnBehavior / SlavedUpdate)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startMod4Game(*s, "FactionMen", "FactionMordor", SLOT_EASY_AI, 4403, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	Object *lair = g.live->createObject("CaveTrollLair", human->getPlayerIndex(), nearObject(*fortress, 600.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(lair, err);
	auto *spawner = dynamic_cast<SpawnBehavior *>(lair->findModule("SpawnBehavior"));
	REQUIRE(spawner);
	CHECK(spawner->data()->m_spawnNumber == 1);
	CHECK(spawner->data()->m_canReclaimOrphans);
	REQUIRE(spawner->data()->m_spawnTemplateNames.size() == 1);
	CHECK(spawner->data()->m_spawnTemplateNames[0] == "CaveTroll_Slaved");
	int frames = 0;
	while (spawner->spawns().empty() && frames < 100)
	{
		g.live->advance(0.2);
		++frames;
	}
	MESSAGE("the first troll came after " << frames << " frames");
	REQUIRE(spawner->spawns().size() == 1);
	CHECK(spawner->spawnCount() == 1);
	Object *troll = logic.findObjectByID(spawner->spawns().front());
	REQUIRE(troll);
	CHECK(troll->getTemplate()->getName() == "CaveTroll_Slaved");
	CHECK(troll->getProducerID() == lair->getID());
	CHECK(troll->getTeam() == lair->getTeam());
	auto *slaved = dynamic_cast<SlavedUpdate *>(troll->findModule("SlavedUpdate"));
	REQUIRE(slaved);
	CHECK(slaved->getSlaverID() == lair->getID());
	CHECK(slaved->data()->m_guardMaxRange == 250);
	CHECK(slaved->data()->m_guardWanderRange == 80);
	CHECK(slaved->data()->m_markUnselectable); // the constructor's default (RW 0x6550E6)
	CHECK(troll->testStatus((unsigned)ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE")));
	// the troll is sent far away; once idle the guard brings it back within the lair's GuardMaxRange
	for (int i = 0; i < 30; ++i)
	{
		g.live->advance(0.2);
	}
	const Coord3D away = nearObject(*lair, 900.0f, 0.0f);
	troll->setPosition(&away);
	if (AIUpdateInterface *ai = troll->getAIUpdateInterface())
	{
		ai->aiIdle(CMD_FROM_AI);
	}
	auto distToLair = [&]() {
		const float dx = troll->getPosition()->x - lair->getPosition()->x, dy = troll->getPosition()->y - lair->getPosition()->y;
		return dx * dx + dy * dy;
	};
	const float before = distToLair();
	const unsigned long long movesBefore = SlavedUpdate::stats().guardMoves;
	for (int i = 0; i < 400 && distToLair() > 400.0f * 400.0f; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(SlavedUpdate::stats().guardMoves > movesBefore);
	CHECK(distToLair() < before);
	CHECK(distToLair() <= 400.0f * 400.0f);
	// the troll dies: a replacement frame now + SpawnReplaceDelay (120 s = 600 frames) is queued, and a new troll comes after it
	const unsigned diedAt = logic.getFrame();
	troll->kill(0);
	g.live->advance(0.2);
	CHECK(spawner->spawns().empty());
	REQUIRE(spawner->replacementFrames().size() == 1);
	CHECK(spawner->replacementFrames().front() >= diedAt + 600u);
	CHECK(spawner->replacementFrames().front() <= diedAt + 602u);
	frames = 0;
	while (spawner->spawns().empty() && frames < 700)
	{
		g.live->advance(0.2);
		++frames;
	}
	CHECK(spawner->spawns().size() == 1);
	CHECK(frames >= 595);
	// the lair dies: its troll loses its slaver (DieOnMastersDeath is not set for the cave troll) and lives on
	Object *second = logic.findObjectByID(spawner->spawns().front());
	REQUIRE(second);
	lair->kill(0);
	for (int i = 0; i < 5; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(!second->isEffectivelyDead());
	CHECK(second->getProducerID() == INVALID_ID);
	auto *slaved2 = dynamic_cast<SlavedUpdate *>(second->findModule("SlavedUpdate"));
	REQUIRE(slaved2);
	CHECK(slaved2->getSlaverID() == INVALID_ID);
}

TEST_CASE("MOD-4: SpawnBehavior / SlavedUpdate binary facts (field tables RW 0xC06498 / 0xC08420, the update rates RW 0xBC5FF9 / 0xBC6E11)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("MOD-4 binary facts (set RW_GAME_DAT)");
		return;
	}
	const std::pair<const char *, std::uint32_t> spawn[] = { { "SpawnNumber", 8 }, { "SpawnReplaceDelay", 12 }, { "OneShot", 20 }, { "CanReclaimOrphans", 21 },
		{ "AggregateHealth", 22 }, { "ExitByBudding", 23 }, { "SpawnTemplateName", 32 }, { "SpawnedRequireSpawner", 24 },
		{ "PropagateDamageTypesToSlavesWhenExisting", 28 }, { "InitialBurst", 16 }, { "RespectCommandLimit", 25 }, { "FadeInTime", 396 },
		{ "KillSpawnsBasedOnModelConditionState", 400 }, { "ShareUpgrades", 401 }, { "SpawnInsideBuilding", 402 } };
	for (size_t i = 0; i < sizeof(spawn) / sizeof(spawn[0]); ++i)
	{
		CHECK(pe->cstring(pe->u32At(0xC06498u + 16u * (std::uint32_t)i)) == spawn[i].first);
		CHECK(pe->u32At(0xC06498u + 16u * (std::uint32_t)i + 12u) == spawn[i].second);
	}
	const char *const slaved[] = { "LeashRange", "GuardMaxRange", "GuardWanderRange", "AttackRange", "AttackWanderRange", "ScoutRange", "ScoutWanderRange",
		"RepairRange", "RepairMinAltitude", "RepairMaxAltitude", "DistToTargetToGrantRangeBonus", "RepairRatePerSecond", "RepairWhenBelowHealth%",
		"RepairMinReadyTime", "RepairMaxReadyTime", "RepairMinWeldTime", "RepairMaxWeldTime", "RepairWeldingSys", "RepairWeldingFXBone", "StayOnSameLayerAsMaster",
		"UseSlaverAsControlForEvaObjectSightedEvents", "DieOnMastersDeath", "GuardPositionOffset", "FadeOutRange", "FadeTime", "MarkUnselectable" };
	for (size_t i = 0; i < sizeof(slaved) / sizeof(slaved[0]); ++i)
	{
		CHECK(pe->cstring(pe->u32At(0xC08420u + 16u * (std::uint32_t)i)) == slaved[i]);
	}
	// the rates: [0xD9F608] (LOGICFRAMES_PER_SECOND) / 2 into RW 0xDE9008 and / 4 into RW 0xDE95FC
	std::vector<std::uint8_t> b;
	REQUIRE(pe->read(0xBC6E11, 16, &b));
	CHECK(b[0] == 0xA1);  // mov eax, [0xD9F608]
	CHECK(b[5] == 0x6A);  // push 4
	CHECK(b[6] == 0x04);
	REQUIRE(pe->read(0xBC6003, 6, &b));
	CHECK(b[0] == 0xA3); // mov [0xDE9008], eax
	CHECK(pe->u32At(0xBC6004) == 0xDE9008u);
	// MarkUnselectable's default is true (RW 0x6550E6: mov byte [eax + 0x6C], 1)
	REQUIRE(pe->read(0x6550E6, 4, &b));
	CHECK(b[0] == 0xC6);
	CHECK(b[2] == 0x6C);
	CHECK(b[3] == 0x01);
}

TEST_CASE("MOD-4 r2: a slave leaves its master when the master's TEAM is no ally of the slave's team, a team override included (RW 0x7A3DAD on the team)")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startMod4Game(*s, "FactionMen", "FactionMordor", SLOT_EASY_AI, 4404, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	REQUIRE(human);
	Object *fortress = firstOf(logic, human, "COMMANDCENTER");
	REQUIRE(fortress);
	Object *lair = g.live->createObject("CaveTrollLair", human->getPlayerIndex(), nearObject(*fortress, 600.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(lair, err);
	auto *spawner = dynamic_cast<SpawnBehavior *>(lair->findModule("SpawnBehavior"));
	REQUIRE(spawner);
	for (int i = 0; i < 100 && spawner->spawns().empty(); ++i)
	{
		g.live->advance(0.2);
	}
	REQUIRE(spawner->spawns().size() == 1);
	Object *troll = logic.findObjectByID(spawner->spawns().front());
	REQUIRE(troll);
	auto *slaved = dynamic_cast<SlavedUpdate *>(troll->findModule("SlavedUpdate"));
	REQUIRE(slaved);
	for (int i = 0; i < 5; ++i)
	{
		g.live->advance(0.2);
	}
	CHECK(slaved->getSlaverID() == lair->getID()); // allies: it stays
	// the master's team (its player's default team) is made the slave's enemy by a TEAM override: the player-level relationship stays allied
	Team *masterTeam = lair->getTeam();
	Team *slaveTeam = troll->getTeam();
	REQUIRE(masterTeam);
	REQUIRE(slaveTeam);
	masterTeam->setTeamRelationship(slaveTeam, ENEMIES);
	CHECK(masterTeam->getRelationship(slaveTeam) == ENEMIES);
	for (int i = 0; i < 5; ++i)
	{
		g.live->advance(0.2);
	}
	// retail asks the master's team: the slave takes the master's team and ends its slavery (the old code asked the player and kept it)
	CHECK(slaved->getSlaverID() == INVALID_ID);
}
