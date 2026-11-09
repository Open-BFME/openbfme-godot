// OpenBFME unit tests. GPL-3.0.
// Lane AIPOW-1 (stop S-1421): the AISpecialPowerUpdate decisions a computer player's heroes and units run (RW 0x992724's classes) and the ActionManager's
// location test (RW 0x75CF21 / 0x82DFB7 / 0x82D5DA). Binary facts from the RotWK binary (RW_GAME_DAT), live objects in a started skirmish against a Hard
// computer (ROTWK_INSTALL). The expectations are retail's rules (each check names its address), never this engine's frame-exact output.

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/SpecialPower.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Map/TerrainTrees.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/Economy.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "Common/Money.h"
#include "Common/Team.h"
#include "Common/StateHash.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSetToggle.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <functional>
#include <map>
#include <set>
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

// the lobby's result: slot 0 a human (`faction0`), slot 1 a Hard computer (`faction1`) on Evendim, start positions 0 and 4 (MOD-4's game)
bool startHardGame(starttest::Shared &s, const std::string &faction0, const std::string &faction1, std::uint32_t seed, buildtest::Game &out, std::string *error)
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
	message.game.slots[1].state = SLOT_HARD_AI;
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

AISpecialPowerUpdate *aiPowerOfType(Object &o, int type)
{
	for (const std::unique_ptr<BehaviorModule> &m : o.modules())
	{
		auto *u = dynamic_cast<AISpecialPowerUpdate *>(m.get());
		if (u && u->data()->m_specialPowerAIType == type)
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

// a real hit (DAMAGE_UNRESISTABLE) down to `ratio`: unlike setHealthRatio, the damage modules see it (AutoHealBehavior's onDamage, RW 0x855706, delays its
// healing by StartHealingDelay)
void woundTo(Object &o, float ratio)
{
	auto *body = dynamic_cast<ActiveBody *>(o.getBodyModule());
	REQUIRE(body);
	DamageInfo hit;
	hit.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	hit.m_input.m_amount = body->getHealth() - body->getMaxHealth() * ratio;
	o.attemptDamage(hit);
}

float distance2D(const Coord3D &a, const Coord3D &b)
{
	const float dx = a.x - b.x, dy = a.y - b.y;
	return std::sqrt(dx * dx + dy * dy);
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

// a started game with a human (Men) at start 0 and a Hard computer at start 4; `where` is a free spot between the two bases (the middle of Evendim)
struct Arena
{
	buildtest::Game g;
	GameLogic *logic = nullptr;
	const Player *human = nullptr, *ai = nullptr;
	Coord3D where{};

	bool start(starttest::Shared &s, const char *aiFaction, std::uint32_t seed, std::string *err)
	{
		if (!startHardGame(s, "FactionMen", aiFaction, seed, g, err))
		{
			return false;
		}
		logic = &g.live->logic();
		human = playerAtStart(*logic, 0);
		ai = playerAtStart(*logic, 4);
		const Object *a = human ? firstOf(*logic, human, "COMMANDCENTER") : nullptr, *b = ai ? firstOf(*logic, ai, "COMMANDCENTER") : nullptr;
		if (!a || !b)
		{
			*err = "no fortresses";
			return false;
		}
		where = Coord3D{ (a->getPosition()->x + b->getPosition()->x) * 0.5f, (a->getPosition()->y + b->getPosition()->y) * 0.5f, 0.0f };
		where.z = logic->getGroundHeight(where.x, where.y);
		// the map's other objects around the arena (creeps, trees, props) go, so the scans see only the test's objects
		for (Object *o = logic->getFirstObject(); o; o = o->getNextObject())
		{
			const float dx = o->getPosition()->x - where.x, dy = o->getPosition()->y - where.y;
			if (dx * dx + dy * dy < 700.0f * 700.0f && o->getControllingPlayer() != human && o->getControllingPlayer() != ai)
			{
				logic->destroyObject(o);
			}
		}
		advance(1);
		return true;
	}
	Object *make(const char *name, const Player *owner, float dx, float dy)
	{
		std::string err;
		Coord3D p = where;
		p.x += dx;
		p.y += dy;
		p.z = logic->getGroundHeight(p.x, p.y);
		Object *o = g.live->createObject(name, owner->getPlayerIndex(), p, 0.0f, &err);
		REQUIRE_MESSAGE(o, err);
		settle();
		return o;
	}
	// the partition takes the new objects' positions in the frame's update (RW 0xA3B4E0)
	void settle()
	{
		logic->partition().update();
	}
	void advance(int frames)
	{
		for (int i = 0; i < frames; ++i)
		{
			g.live->advance(0.2);
		}
	}
};
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("AIPOW-1: the decision functions of RW 0x992724's classes and their constants (the vtables' slot 0x1C)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("AIPOW-1 binary facts (set RW_GAME_DAT)");
		return;
	}
	struct Row
	{
		int type;
		std::uint32_t vtable, decision;
	};
	// the type -> vtable pairs are read from the constructors RW 0x992724's switch calls (`mov dword ptr [esi], vtable` after the base constructor)
	const Row rows[] = {
		{ 2, 0xC8E790, 0x9EC22C },  // ELENDIL
		{ 6, 0xC8E6EC, 0x9EBF56 },  // GANDALF_WIZARD_BLAST
		{ 7, 0xC8E6C0, 0x9EBEED },  // GIVEXP_AOE
		{ 8, 0xC8E568, 0x9EB53B },  // RANGED_AOE_ATTACK
		{ 10, 0xC8E5DC, 0x9EBCBB }, // SELFAOEHEALHEROS
		{ 11, 0xC8E590, 0x9EBBF2 }, // TARGETAOE_SUMMON
		{ 12, 0xC8E4BC, 0x9EADC8 }, // LEGOLAS_ARROWWIND
		{ 13, 0xC8E494, 0x9EAD61 }, // LEGOLAS_TRAINARCHERS
		{ 14, 0xC8E46C, 0x9EAAC1 }, // GOBLINKING_BATTLEFRENZY
		{ 15, 0xC8E444, 0x9EA7FE }, // GOBLINKING_CALLOFTHEDEEP
		{ 16, 0xC8E41C, 0x9EA553 }, // GOBLINKING_MOUNTED
		{ 17, 0xC8E3FC, 0x9EA462 }, // HEAL_AOE
		{ 18, 0xC8E3CC, 0x9EA2E1 }, // TOGGLE_MELEE_AND_RANGE
		{ 19, 0xC8E394, 0x9EA2E1 }, // TOGGLE_SIEGE (the same slot 0x1C, its own slots 0x20 / 0x24)
		{ 20, 0xC8E368, 0x9EA0A1 }, // CHARGE
		{ 43, 0xC8E518, 0x9EB0B8 }, // AOE_AND_BUFF
		{ 45, 0xC8E310, 0x9E9880 }, // DOMINATE_ENEMY
		{ 46, 0xC8E2BC, 0x9E9633 }, // DOMINATE_TROLL
		{ 47, 0xC8E310, 0x9E9880 }, // TAME_THE_BEAST (DOMINATE_ENEMY's class, constructor argument 1)
		{ 50, 0xC8E540, 0x9EB36E }, // RANGED_AOE_ATTACK_UNITS
		{ 21, 0xC8E224, 0x9B501B }, // SPELLBOOK_ALWAYS_FIRE (`mov al, 1`)
		{ 25, 0xC8E090, 0x9E86BE }, // SPELLBOOK_CAPTURE_CREEP
		{ 34, 0xC8DFB4, 0x9E7CA3 }, // SPELLBOOK_SHROUD_REVEAL
	};
	for (const Row &r : rows)
	{
		CHECK_MESSAGE(pe->u32At(r.vtable + 0x1Cu) == r.decision, TheAISpecialPowerTypeNames[r.type]);
		CHECK_MESSAGE(AISpecialPowerDecision::decisionPorted(r.type), TheAISpecialPowerTypeNames[r.type]);
	}
	CHECK(pe->u32At(0xC8E3CCu + 0x20u) == 0x9EA347u); // TOGGLE_MELEE_AND_RANGE: within 50 / at least 150
	CHECK(pe->u32At(0xC8E3CCu + 0x24u) == 0x9EA3BAu);
	CHECK(pe->u32At(0xC8E394u + 0x20u) == 0x9EA201u); // TOGGLE_SIEGE: a non-structure within 150 / a structure
	CHECK(pe->u32At(0xC8E394u + 0x24u) == 0x9EA280u);
	// the constants
	CHECK(floatAt(*pe, 0xC8E78C) == 0.7f);     // ELENDIL
	CHECK(floatAt(*pe, 0xC8E6E8) == 0.3f);     // GANDALF_WIZARD_BLAST
	CHECK(floatAt(*pe, 0xBDE8D8) == 0.8f);     // the ranged AOE's minimum distance factor
	CHECK(floatAt(*pe, 0xBD889C) == 2.0f);     // LEGOLAS_ARROWWIND's range factor
	CHECK(floatAt(*pe, 0xC8E3C4) == 2500.0f);  // TOGGLE_MELEE_AND_RANGE
	CHECK(floatAt(*pe, 0xC8E3C8) == 22500.0f);
	CHECK(floatAt(*pe, 0xC8E390) == 22500.0f); // TOGGLE_SIEGE
	CHECK(floatAt(*pe, 0xC8E2B8) == 2.0f);     // DOMINATE_TROLL
	CHECK(floatAt(*pe, 0xC8E2B4) == 0.5f);
	CHECK(floatAt(*pe, 0xC8E30C) == 2.0f);     // DOMINATE_ENEMY / TAME_THE_BEAST
	CHECK(floatAt(*pe, 0xC8E308) == 0.5f);
	CHECK(floatAt(*pe, 0xC8E514) == 2.0f);     // AOE_AND_BUFF
	CHECK(floatAt(*pe, 0xBE4170) == 200.0f);   // the GOBLINKING clusters' default range
	CHECK(floatAt(*pe, 0xC8E5B0) == 149.0f);   // TARGETAOE_SUMMON's default range
	CHECK(floatAt(*pe, 0xC1B594) == 0.0f);     // HEAL_AOE's numerator
	CHECK(floatAt(*pe, 0xBDC628) == 0.75f);
	CHECK(floatAt(*pe, 0xBDBC70) == -0.1f);    // SELFAOEHEALHEROS's roll
	CHECK(floatAt(*pe, 0xBD83D4) == 0.1f);
	CHECK(floatAt(*pe, 0xBDC6CC) == 15.0f);    // RW 0xA0229F's random walk step
	CHECK(floatAt(*pe, 0xC041F8) == 150.0f);   // .. and its end, SpellMakesAStructure's radius
	CHECK(pe->cstring(0xC8E2E8) == std::string("IsengardWargRiderHorde")); // DOMINATE's variant 1 names
	CHECK(pe->cstring(0xC8E2DC) == std::string("NeutralWarg"));
	CHECK((pe->u32At(0x9B501Bu) & 0xFFFFu) == 0x01B0u);  // RW 0x9B501B: mov al, 1
	CHECK(pe->cstring(0xBFDA18) == std::string("Player_%d_Start")); // SHROUD_REVEAL's waypoints (RW 0x9E7B8E)
	CHECK(floatAt(*pe, 0xC8DFB0) == 300.0f);   // SHROUD_REVEAL's enemy radius
	CHECK(floatAt(*pe, 0xBDAE54) == 30.0f);    // .. and its random offset
	CHECK(floatAt(*pe, 0xBE939C) == 180.0f);
	// .. and its first-frame bound RW 0xDEC148: 0 in the file, set at startup by RW 0xBCA357 (registered at RW 0xD8AB88) to LOGICFRAMES_PER_SECOND * 10:
	// mov eax, [0xD9F608]; imul eax, eax, 0xA; mov [0xDEC148], eax; ret
	CHECK(pe->u32At(0xD8AB88u) == 0xBCA357u);
	CHECK(pe->u32At(0xBCA357u) == 0xD9F608A1u);       // a1 08 f6 d9 ..
	CHECK(pe->u32At(0xBCA35Bu) == 0x0AC06B00u);      // .. 00 6b c0 0a
	CHECK(pe->u32At(0xBCA35Fu) == 0xDEC148A3u);      // a3 48 c1 de ..
	CHECK(pe->u32At(0xDEC148u) == 0u);
}

TEST_CASE("AIPOW-1: the ActionManager's per-type tables: RW 0x82D6B1 (targetless) and RW 0x82E913 (the final location test)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("AIPOW-1 ActionManager tables (set RW_GAME_DAT)");
		return;
	}
	// RW 0x82D6A0: type - 0x10 above 0x8D -> no; the byte table RW 0x82D6D1 picks RW 0x82D6C5's target; RW 0x82D6B8 answers yes
	for (int type = 0; type < 0xA0; ++type)
	{
		bool yes = false;
		if (type >= 0x10 && type - 0x10 <= 0x8D)
		{
			const std::uint32_t index = pe->u32At(0x82D6D1u + (std::uint32_t)(type - 0x10)) & 0xFFu;
			yes = pe->u32At(0x82D6C5u + 4u * index) == 0x82D6B8u;
		}
		CHECK_MESSAGE(ActionManager::targetlessTypeAccepted(type) == yes, type);
	}
	// RW 0x82E8FE: type - 10 above 0x94 -> no; the byte table RW 0x82EE0D picks RW 0x82EDE1's target
	const std::map<std::uint32_t, int> targets = { { 0x82E23Fu, 0 }, { 0x82E91Au, 1 }, { 0x82ED8Eu, 2 }, { 0x82ED95u, 3 }, { 0x82EC27u, 4 }, { 0x82ED67u, 5 },
		                                           { 0x82EDBCu, 6 }, { 0x82E93Cu, 6 }, { 0x82EA58u, 7 }, { 0x82EB27u, 8 } };
	for (int type = 0; type < 0xA0; ++type)
	{
		int expected = 0;
		if (type >= 10 && type - 10 <= 0x94)
		{
			const std::uint32_t index = pe->u32At(0x82EE0Du + (std::uint32_t)(type - 10)) & 0xFFu;
			const auto it = targets.find(pe->u32At(0x82EDE1u + 4u * index));
			REQUIRE(it != targets.end());
			expected = it->second;
		}
		CHECK_MESSAGE(ActionManager::finalTestKind(type) == expected, type);
	}
}

// ---- live objects: a Hard computer's heroes ---------------------------------------------------------------------------------------------------------------

TEST_CASE("AIPOW-1: a Hard computer's Aragorn: ELENDIL (RW 0x9EC22C) and SELFAOEHEALHEROS (RW 0x9EBCBB) answer by retail's rules")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5101, &err), err);
	Object *aragorn = a.make("GondorAragorn", a.ai, 0.0f, 0.0f);
	a.advance(2);
	AISpecialPowerUpdate *elendil = aiPowerOfType(*aragorn, 2), *athelas = aiPowerOfType(*aragorn, 10);
	REQUIRE(elendil);
	REQUIRE(athelas);
	REQUIRE(elendil->decideNow() == false); // no goal (RW 0x668303)
	REQUIRE(elendil->decision());
	CHECK(elendil->decision()->radius == 200.0f); // SpecialPowerRadius
	CHECK(elendil->decision()->kind == AISpecialPowerDecision::KIND_SELF);
	// three enemy soldiers (rank 0, not BANNER) within the radius and a goal
	Object *e1 = a.make("GondorFighter", a.human, 60.0f, 0.0f);
	a.make("GondorFighter", a.human, 70.0f, 20.0f);
	a.make("GondorFighter", a.human, 70.0f, -20.0f);
	REQUIRE(aragorn->getAIUpdateInterface()->aiAttackObject(e1, CMD_FROM_PLAYER));
	REQUIRE(aragorn->getAIUpdateInterface()->currentVictim() == e1);
	CHECK(elendil->decideNow() == false); // full health: the ratio is not below 0.7
	setHealthRatio(*aragorn, 0.6f);
	CHECK(elendil->decideNow() == true); // 3 enemies against the one other (Aragorn himself, ALLIES)
	// four allies within the radius outnumber the three enemies
	for (int i = 0; i < 4; ++i)
	{
		a.make("GondorFighter", a.ai, -60.0f, -30.0f + 20.0f * (float)i);
	}
	CHECK(elendil->decideNow() == false);
	// SELFAOEHEALHEROS: its own ratio below the roll's t (0.4 .. 0.6) -> yes; healthy and no wounded allied hero within 30 -> no; a wounded allied hero -> yes
	setHealthRatio(*aragorn, 0.3f);
	CHECK(athelas->decideNow() == true);
	setHealthRatio(*aragorn, 1.0f);
	CHECK(athelas->decideNow() == false);
	Object *boromir = a.make("GondorBoromir", a.ai, 10.0f, 10.0f);
	setHealthRatio(*boromir, 0.2f);
	CHECK(athelas->decideNow() == true);
	setHealthRatio(*boromir, 0.9f);
	CHECK(athelas->decideNow() == false);
	// a human's Aragorn never asks (RW 0x6A950B: no skirmish AI for his player); the module sleeps
	Object *theirs = a.make("GondorAragorn", a.human, 300.0f, 0.0f);
	a.advance(30);
	AISpecialPowerUpdate *theirElendil = aiPowerOfType(*theirs, 2);
	REQUIRE(theirElendil);
	CHECK(theirElendil->decision()); // initialised
	CHECK(theirElendil->decision()->counter == 0u);
}

TEST_CASE("AIPOW-1: GIVEXP_AOE (RW 0x9EBEED), RANGED_AOE_ATTACK (RW 0x9EB53B) and GANDALF_WIZARD_BLAST (RW 0x9EBF56) choose their locations")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5102, &err), err);
	Object *boromir = a.make("GondorBoromir", a.ai, 0.0f, 0.0f);
	a.advance(2);
	AISpecialPowerUpdate *captain = aiPowerOfType(*boromir, 7);
	REQUIRE(captain);
	CHECK(captain->decideNow() == true); // no goal; Boromir himself is an ally within 30 of the first quadrant point
	REQUIRE(captain->decision());
	CHECK(captain->decision()->kind == AISpecialPowerDecision::KIND_LOCATION);
	// the location is the best quadrant point: 30 * (0.70710677, 0.70710677) from Boromir (RW 0xA01D72, not validated)
	const Coord3D loc = captain->decidedLocation();
	CHECK(loc.x != boromir->getPosition()->x);
	Object *gandalf = a.make("GondorGandalf", a.ai, 0.0f, 40.0f);
	a.advance(2);
	AISpecialPowerUpdate *sword = aiPowerOfType(*gandalf, 8), *blast = aiPowerOfType(*gandalf, 6);
	REQUIRE(sword);
	REQUIRE(blast);
	CHECK(sword->decideNow() == false); // no enemy within the range
	REQUIRE(sword->decision());
	const float range = sword->decision()->range;
	MESSAGE("RANGED_AOE_ATTACK range " << range);
	REQUIRE(range > 0.0f);
	// an enemy nearer than 0.8 of the range: no
	Object *near = a.make("GondorFighter", a.human, 0.0f, 40.0f + 0.5f * range);
	CHECK(sword->decideNow() == false);
	a.logic->destroyObject(near);
	a.advance(1);
	// one between 0.8 and 1.0 of the range: the location test decides (the shroud there is CLEAR for the computer: its units stand around)
	Object *far = a.make("GondorFighter", a.human, 0.0f, 40.0f + 0.9f * range);
	const bool yes = sword->decideNow();
	MESSAGE("RANGED_AOE_ATTACK at 0.9 of the range: " << yes);
	CHECK(yes);
	CHECK(sword->decidedLocation().x == far->getPosition()->x);
	CHECK(sword->decidedLocation().y == far->getPosition()->y);
	// GANDALF_WIZARD_BLAST: a goal and the ENEMIES cluster above 20 or, wounded below 0.3, not empty
	REQUIRE(gandalf->getAIUpdateInterface()->aiAttackObject(far, CMD_FROM_PLAYER));
	setHealthRatio(*gandalf, 0.25f);
	CHECK(blast->decideNow() == false); // wounded, but no enemy within 100 of the four points 100 * (+-0.707, +-0.707) away
	setHealthRatio(*gandalf, 1.0f);
	a.make("GondorFighter", a.human, 70.0f, 40.0f + 70.0f); // at the first point
	CHECK(blast->decideNow() == false); // healthy and one enemy: not above 20
	setHealthRatio(*gandalf, 0.25f);
	CHECK(blast->decideNow() == true);
	// the location is the first point (the best count, RW 0xA01D72: not validated)
	CHECK(blast->decidedLocation().x == SimMath::addf32(SimMath::mulf32(0.70710677f, 100.0f), gandalf->getPosition()->x));
}

TEST_CASE("AIPOW-1: TOGGLE_SIEGE (RW 0x9EA2E1 / 0x9EA201 / 0x9EA280) and DOMINATE_TROLL (RW 0x9E9633) on a Hard computer's Mordor trolls")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMordor", 5103, &err), err);
	Object *troll = a.make("MordorMountainTroll", a.ai, 0.0f, 0.0f);
	a.advance(2);
	AISpecialPowerUpdate *siege = aiPowerOfType(*troll, 19);
	REQUIRE(siege);
	CHECK(siege->decideNow() == false); // no goal
	Object *soldier = a.make("GondorFighter", a.human, 100.0f, 0.0f);
	REQUIRE(troll->getAIUpdateInterface()->aiAttackObject(soldier, CMD_FROM_PLAYER));
	const ObjectWeapons *w = troll->getWeapons();
	REQUIRE(w);
	REQUIRE(w->currentWeapon());
	const bool melee = w->currentWeapon()->getTemplate()->m_meleeWeapon;
	MESSAGE("the troll's current weapon is melee: " << melee);
	// a melee weapon toggles only against a structure; a ranged one toggles against a non-structure within 150
	CHECK(siege->decideNow() == !melee);
	// RW 0x696FD2 case 0x2D: the toggle flips WEAPONSET_TOGGLE_1
	const bool before = troll->testModelCondition(0x12D);
	WeaponSetToggle::toggleFromCommandButton(*troll);
	CHECK(troll->testModelCondition(0x12D) != before);
	// DOMINATE_TROLL: two or more enemies or neutrals within the range, the most expensive healthy TROLL among the counted ones
	Object *attackTroll = a.make("MordorAttackTroll", a.ai, -200.0f, 0.0f);
	a.advance(2);
	AISpecialPowerUpdate *dominate = aiPowerOfType(*attackTroll, 46);
	REQUIRE(dominate);
	CHECK(dominate->decideNow() == false);
	REQUIRE(dominate->decision());
	CHECK(dominate->decision()->variant == 2);
	CHECK(dominate->decision()->kind == AISpecialPowerDecision::KIND_OBJECT);
}

TEST_CASE("AIPOW-1: the ActionManager's stops [S-1871] .. [S-1874] and the computer's sciences [S-1875] are reported, S-1421 names what stays unported")
{
	const std::vector<std::string> am = ActionManager::stopLines();
	REQUIRE(am.size() == 5);
	CHECK(am[0].rfind("[S-1871]", 0) == 0);
	CHECK(am[1].rfind("[S-1872]", 0) == 0);
	CHECK(am[2].rfind("[S-1873]", 0) == 0);
	CHECK(am[3].rfind("[S-1874]", 0) == 0);
	CHECK(am[4].rfind("[S-7140]", 0) == 0); // MODCOMPAT-MOD1: RW 0x82D925's unported per-type object tests
	const std::vector<std::string> ai = AISpecialPowerUpdate::stopLines();
	REQUIRE(ai.size() == 2);
	CHECK(ai[0].rfind("[S-1421]", 0) == 0);
	CHECK(ai[0].find("51 of the 53 decisions run") != std::string::npos);
	CHECK(ai[0].find("SOUL_FREEZE") == std::string::npos); // lane RAI-POW ported it
	CHECK(ai[1].rfind("[S-1875]", 0) == 0);
	// the decisions still unported: ATTACK_HEAL_AOE and the 2 spell book ones (ARMY_BREAKER, HEAL); TREE_KILLER is ported by lane RAI-POW-3
	int unported = 0;
	for (int t = 0; t < AI_SPECIAL_POWER_TYPE_COUNT; ++t)
	{
		if (!AISpecialPowerDecision::decisionPorted(t))
		{
			++unported;
			const bool spellBook = std::string(TheAISpecialPowerTypeNames[t]).rfind("AI_SPELLBOOK_", 0) == 0;
			CHECK_MESSAGE(spellBook, TheAISpecialPowerTypeNames[t]);
		}
	}
	CHECK(unported == 2); // RAI-POW-4 moved this from 3: ATTACK_HEAL_AOE is ported
	CHECK(AISpecialPowerDecision::decisionPorted(21));
	CHECK(AISpecialPowerDecision::decisionPorted(25));
	CHECK(AISpecialPowerDecision::decisionPorted(34));
}

TEST_CASE("AIPOW-1: Legolas: LEGOLAS_TRAINARCHERS (RW 0x9EAD61) needs allied archers at a location the ActionManager accepts; LEGOLAS_ARROWWIND (RW 0x9EADC8) doubles the range")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5104, &err), err);
	Object *legolas = a.make("RohanLegolas", a.ai, 0.0f, 0.0f);
	a.advance(2);
	AISpecialPowerUpdate *train = aiPowerOfType(*legolas, 13), *wind = aiPowerOfType(*legolas, 12);
	REQUIRE(train);
	REQUIRE(wind);
	CHECK(train->decideNow() == false); // no archer near any of the four points
	REQUIRE(train->decision());
	CHECK(train->decision()->kind == AISpecialPowerDecision::KIND_LOCATION);
	// two allied archers at the first point (100 * (0.707, 0.707) from Legolas)
	a.make("GondorArcher", a.ai, 70.0f, 70.0f);
	a.make("GondorArcher", a.ai, 75.0f, 65.0f);
	CHECK(train->decideNow() == true);
	const Coord3D first{ SimMath::addf32(SimMath::mulf32(0.70710677f, 100.0f), legolas->getPosition()->x),
		                 SimMath::addf32(SimMath::mulf32(0.70710677f, 100.0f), legolas->getPosition()->y), 0.0f };
	CHECK(train->decidedLocation().x == first.x);
	CHECK(train->decidedLocation().y == first.y);
	// a goal: no (RW 0x9EAD61 asks for none)
	Object *enemy = a.make("GondorFighter", a.human, -120.0f, 0.0f);
	REQUIRE(legolas->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	CHECK(train->decideNow() == false);
	// LEGOLAS_ARROWWIND: the nearest enemy within twice the range, not nearer than 0.8 of that
	REQUIRE(wind->decision());
	const float range2 = SimMath::mulf32(wind->decision()->range, 2.0f);
	MESSAGE("LEGOLAS_ARROWWIND range " << wind->decision()->range);
	CHECK(wind->decideNow() == (120.0f >= 0.8f * range2 && 120.0f <= range2));
}

TEST_CASE("AIPOW-1: a Hard computer's Dwarves: Dain's HEAL_AOE (RW 0x9EA462) needs ten allies at one of the four points, his summon (SUMMONLOC-1: the spot test runs), the Guardians CHARGE (RW 0x9EA0A1)")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionDwarves", 5105, &err), err);
	Object *dain = a.make("DwarvenDain", a.ai, 0.0f, 0.0f);
	a.advance(2);
	AISpecialPowerUpdate *rage = aiPowerOfType(*dain, 17), *guard = aiPowerOfType(*dain, 11);
	REQUIRE(rage);
	REQUIRE(guard);
	Object *enemy = a.make("GondorFighter", a.human, 0.0f, -150.0f);
	REQUIRE(dain->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	CHECK(rage->decideNow() == false); // Dain alone is one ally
	REQUIRE(rage->decision());
	CHECK(rage->decision()->radius == 200.0f);
	for (int i = 0; i < 9; ++i)
	{
		a.make("GondorFighter", a.ai, 141.0f + 5.0f * (float)(i % 3), 141.0f + 5.0f * (float)(i / 3));
	}
	CHECK(rage->decideNow() == false); // nine soldiers; Dain stands 200 from the first point, on its circle's edge (the scan keeps distances below 200)
	a.make("GondorFighter", a.ai, 150.0f, 130.0f);
	CHECK(rage->decideNow() == true); // ten
	// the summon: a goal and more than one enemy (horde or loose) near a point the ActionManager accepts; lane SUMMONLOC-1: the summons' spot test
	// (RW 0x82E586) runs, the open ground answers yes and no unported test is counted
	a.make("GondorFighter", a.human, 10.0f, -150.0f);
	a.make("GondorFighter", a.human, -10.0f, -150.0f);
	const unsigned long long before = ActionManager::stats().unportedTypeTests[0x5B];
	CHECK(guard->decideNow() == true);
	CHECK(ActionManager::stats().unportedTypeTests[0x5B] == before);
	REQUIRE(guard->decision());
	const Coord3D spot = guard->decision()->location;
	// the same spot under a structure (RW 0x82E586: an overlapping live STRUCTURE refuses) answers no
	Object *fort = a.make("DwarvenFortressCitadel", a.human, spot.x - a.where.x, spot.y - a.where.y);
	REQUIRE(fort);
	a.advance(1);
	CHECK_FALSE(ActionManager::canDoSpecialPowerAtLocation(*dain, *guard->decision()->button, spot));
	a.logic->destroyObject(fort);
	a.advance(1);
	// CHARGE (RW 0x9EA0A1) on the Guardians: the nearest enemy, or the first one whose health ratio is below the first's
	Object *guardians = a.make("DwarvenGuardianHorde", a.ai, -300.0f, 0.0f);
	a.advance(3);
	AISpecialPowerUpdate *charge = aiPowerOfType(*guardians, 20);
	REQUIRE(charge);
	CHECK(charge->decideNow() == false); // no goal and no target
	REQUIRE(charge->decision());
	CHECK(charge->decision()->kind == AISpecialPowerDecision::KIND_OBJECT);
	const float range = charge->decision()->range;
	MESSAGE("CHARGE range " << range);
	REQUIRE(range > 60.0f);
	Object *nearEnemy = a.make("GondorFighter", a.human, -260.0f, 0.0f);
	Object *farEnemy = a.make("GondorFighter", a.human, -245.0f, 0.0f);
	REQUIRE(guardians->getAIUpdateInterface()->aiAttackObject(nearEnemy, CMD_FROM_PLAYER));
	CHECK(charge->decideNow() == true);
	CHECK(charge->decision()->target == nearEnemy->getID());
	setHealthRatio(*farEnemy, 0.5f);
	CHECK(charge->decideNow() == true);
	CHECK(charge->decision()->target == farEnemy->getID());
}

TEST_CASE("SUMMONLOC-1: end to end: a Hard computer's Dain summons his Royal Guard (SPELL_BOOK_MEN_OF_DALE_ALLIES, AI type 11) into a fight on open ground")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionDwarves", 5105, &err), err);
	Object *dain = a.make("DwarvenDain", a.ai, 0.0f, 0.0f);
	dain->getExperienceTracker()->gainExpForLevel(10, true, false);
	a.advance(2);
	AISpecialPowerUpdate *guard = aiPowerOfType(*dain, 11);
	REQUIRE(guard);
	Object *enemy = a.make("GondorFighter", a.human, 0.0f, -150.0f);
	a.make("GondorFighter", a.human, 10.0f, -150.0f);
	a.make("GondorFighter", a.human, -10.0f, -150.0f);
	REQUIRE(dain->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	AISpecialPowerUpdate::stats() = AISpecialPowerUpdate::Stats{};
	int frame = -1;
	for (int i = 0; i < 300 && frame < 0; ++i)
	{
		a.advance(1);
		if (AISpecialPowerUpdate::stats().executedByType[11] != 0)
		{
			frame = i;
		}
	}
	MESSAGE("the Royal Guard summon ran after " << frame << " frames");
	// engine pin (seed 5105): the module's 25-frame counter (RW 0x99302D) from its first update and the roll above Hard's chance pass on this frame
	// lane FBSIM-1 (the scene's computer players build through their dozers, whose sites are now RotWK's phantoms until placed, RW 0x88D272 / 0x88D44F: the shared random stream and the AIs' play move), measured on the lane's tree (a CI host): 22 -> 24
	// merge queue q29 (measured once on the merged tree and at each merge point): 23; community #113 moves it 22 -> 23 (its FlagCaptureSquad's draws, RW 0x9BC1A0,
	// shift the shared random stream) and FBSIM-1 on that tree keeps 23
	// merge queue q33 (measured at each merge point): #133's straight production exits move the scene's units and the shared random stream: 23 -> 22
	CHECK(frame == 22);
	CHECK(AISpecialPowerUpdate::stats().executedByType[11] == 1);
}

TEST_CASE("AIPOW-1: end to end: a Hard computer's wounded Aragorn uses Athelas on his own (SELFAOEHEALHEROS), a human's does not")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5107, &err), err);
	Object *mine = a.make("GondorAragorn", a.ai, 0.0f, 0.0f);
	Object *theirs = a.make("GondorAragorn", a.human, 400.0f, 0.0f);
	a.advance(2);
	// real hits: ModuleTag_AragornHealing (an AutoHealBehavior, 30 every 5 frames) waits its StartHealingDelay after them, so neither regenerates here
	woundTo(*mine, 0.3f);
	woundTo(*theirs, 0.3f);
	const float wounded = AISpecialPowerUpdate::healthRatio(*theirs);
	CHECK(wounded == doctest::Approx(0.3f));
	CHECK(AISpecialPowerUpdate::healthRatio(*mine) == wounded);
	AISpecialPowerUpdate::stats() = AISpecialPowerUpdate::Stats{};
	int frame = -1;
	for (int i = 0; i < 300 && frame < 0; ++i)
	{
		a.advance(1);
		if (AISpecialPowerUpdate::stats().executedByType[10] != 0)
		{
			frame = i;
		}
	}
	MESSAGE("Athelas ran after " << frame << " frames");
	// the 25-frame counter (RW 0x99302D) counts the module's updates from its first (2 frames before the wound); the roll above 0.5 and Hard's chance (1 : 1)
	// pass on that frame here. Engine pin (seed 5107). Lane PATHPERF-1: 23 with findPath's hierarchical corridor (RW 0x6F8601); 22 with it switched off (a
	// test switch, measured): the corridor's routes move the frame on which the roll passes. Lane TARGET-1: 22: the mood scan's one-off random offset of its
	// next check (RW 0x6686F0, GameLogicRandomValue line 0x2079 after the idle entry's resetNextMoodCheckTime RW 0x662DD9) draws from the logic generator
	// before the power's roll. Merge queue q28 (the random stream moves with the merged batch; measured once on the merged tree and at each merge point): 26.
	// Merge queue q29 (measured once on the merged tree and at each merge point): 22; community #113 moves it 26 -> 24 (its FlagCaptureSquad's draws) and
	// FBSIM-1 24 -> 22 (the computer player's phantom sites). Lane PATH-REBUILD (RW 0x767B0E, the found paths bent and rounded: the routes move the frame
	// on which the roll passes, as PATHPERF-1's corridor did; measured on the lane's tree): 23
	CHECK(frame == 23);
	CHECK(AISpecialPowerUpdate::stats().executedByType[10] == 1);
	a.advance(10); // Athelas's heal lands a few frames after the cast
	CHECK(AISpecialPowerUpdate::healthRatio(*mine) > wounded);    // healed
	CHECK(AISpecialPowerUpdate::healthRatio(*theirs) == wounded); // the human's sleeps (RW 0x6A950B)
}

TEST_CASE("AIPOW-1: the computer's spell book: SHROUD_REVEAL (RW 0x9E7CA3) looks at an unseen start with enemies, CAPTURE_CREEP (RW 0x9E86BE) at a creep lair it sees")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMordor", 5108, &err), err);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	AISpecialPowerUpdate *eye = aiPowerOfType(*book, 34), *creep = aiPowerOfType(*book, 25);
	REQUIRE(eye);
	REQUIRE(creep);
	a.advance(2);
	REQUIRE(eye->decision());
	// not before logic frame 51 (RW 0xDEC148 = 50, `jbe`), and no draw while it waits
	while (a.logic->getFrame() < 50u)
	{
		a.advance(1);
	}
	REQUIRE(a.logic->getFrame() == 50u);
	a.logic->random().enableCallLog(true);
	a.logic->random().clearCallLog();
	CHECK(eye->decideNow() == false);
	CHECK(a.logic->random().callLog().empty());
	a.logic->random().enableCallLog(false);
	a.advance(1);
	REQUIRE(a.logic->getFrame() == 51u);
	const std::vector<int> &ids = eye->decision()->waypointIds;
	MESSAGE("start waypoints " << ids.size());
	REQUIRE(ids.size() >= 2); // Evendim: Player_1_Start .. Player_N_Start
	int yes = 0;
	for (int i = 0; i < 30; ++i)
	{
		if (!eye->decideNow())
		{
			continue;
		}
		++yes;
		// within 180 (+ the RandomizeTargetLocation walk, up to 150) of a start the computer does not see CLEAR
		bool nearUnseen = false;
		for (int id : ids)
		{
			const Waypoint *w = a.logic->terrain()->findWaypointById(id);
			REQUIRE(w);
			const float dx = eye->decidedLocation().x - w->location.x, dy = eye->decidedLocation().y - w->location.y;
			if (dx * dx + dy * dy <= 331.0f * 331.0f && a.logic->shroud()->getStatusAt(a.ai->getPlayerIndex(), w->location.x, w->location.y) != CELLSHROUD_CLEAR)
			{
				nearUnseen = true;
			}
		}
		CHECK(nearUnseen);
	}
	MESSAGE("SHROUD_REVEAL said yes " << yes << " times in 30");
	CHECK(yes > 0); // the human's start: fogged, with his units
	// CAPTURE_CREEP: the first non-playable player hostile to the computer, the newest STRUCTURE of its default team the computer does not see SHROUDED
	const bool creepYes = creep->decideNow();
	MESSAGE("CAPTURE_CREEP: " << creepYes);
}

TEST_CASE("AIPOW-1 r2: the spell book's ASSIST_BATTLE_BUFF (RW 0x9E896E) with its timer (RW 0x9E880B), BUFFTERRAIN (RW 0x9E77EE), ENSHROUDINGMIST (RW 0x9E7869) and REBUILD (RW 0x9E7744)")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5109, &err), err);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	AISpecialPowerUpdate *buff = aiPowerOfType(*book, 22), *terrain = aiPowerOfType(*book, 30), *mist = aiPowerOfType(*book, 29), *rebuild = aiPowerOfType(*book, 31);
	a.advance(2);
	if (buff)
	{
		REQUIRE(buff->decision());
		CHECK(buff->decision()->variant == 0);
		CHECK(buff->decideNow() == false); // no army unit has a victim
	}
	// a computer soldier fighting a human soldier, wounded: its side's average ratio is below 0.99 with an enemy within 350
	// (the hordes are the army: a horde member template is not CAN_ATTACK)
	Object *mine = a.make("GondorFighterHorde", a.ai, 0.0f, 0.0f);
	Object *theirs = a.make("GondorFighterHorde", a.human, 60.0f, 0.0f);
	a.advance(2);
	REQUIRE(mine->getAIUpdateInterface()->aiAttackObject(theirs, CMD_FROM_PLAYER));
	const std::vector<ObjectID> &aiArmy = AITacticalAI::recordOf(*a.logic, aiPlayer->getPlayerIndex()).army();
	REQUIRE(std::find(aiArmy.begin(), aiArmy.end(), mine->getID()) != aiArmy.end());
	// a horde's ratio is its members' average (the contain's slot 0x264): wound the members
	REQUIRE(mine->getContain());
	REQUIRE(mine->getContain()->getContainedItemsList());
	for (Object *m : *mine->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.5f);
	}
	if (buff)
	{
		const bool first = buff->decideNow();
		MESSAGE("ASSIST_BATTLE_BUFF on a wounded horde: " << first << " (the ActionManager's test at its position decides)");
		CHECK(buff->decision()->assistUnit == mine->getID());
		CHECK(buff->decision()->assistTarget == mine->getID());
		REQUIRE(!a.logic->skirmishAI().spellBookAssistTimers().empty());
		const std::pair<int, unsigned> t = a.logic->skirmishAI().spellBookAssistTimers().front();
		CHECK(t.first == a.ai->getPlayerIndex());
		CHECK(t.second >= a.logic->getFrame() + 15u);
		CHECK(t.second <= a.logic->getFrame() + 50u);
		CHECK(buff->decideNow() == false); // the timer is not ready again before 15 frames
	}
	else
	{
		MESSAGE("GoodSpellBook has no ASSIST_BATTLE_BUFF AISpecialPowerUpdate");
	}
	// the Men's spell book lists Elven Wood and Enshrouding Mist, its command set does not hold them: inactive (RW 0x8B714F); the Elves' test runs them
	if (terrain)
	{
		CHECK(!terrain->active());
		CHECK(terrain->decideNow() == false);
	}
	if (mist)
	{
		CHECK(!mist->active());
		CHECK(mist->decideNow() == false);
	}
	if (rebuild)
	{
		// the rebuild power's type (0x57) is one of S-1874's unported location tests: a wounded structure is chosen, the ActionManager answers no (counted)
		Object *fortress = firstOf(*a.logic, a.ai, "COMMANDCENTER");
		REQUIRE(fortress);
		CHECK(rebuild->decideNow() == false);
		setHealthRatio(*fortress, 0.4f);
		const unsigned long long before = ActionManager::stats().unportedTypeTests[0x57];
		CHECK(rebuild->decideNow() == false);
		CHECK(ActionManager::stats().unportedTypeTests[0x57] > before);
	}
}

TEST_CASE("AIPOW-1 r2: CALLTHEHORDE (RW 0x9E71B0): money above 1000 and over a quarter of the command points free (its boundaries)")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMordor", 5110, &err), err);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	AISpecialPowerUpdate *horde = aiPowerOfType(*book, 33);
	REQUIRE(horde);
	a.advance(2);
	// no retail command set holds Command_SpellBookCalltheHorde (the evil books list it, their command sets do not): RW 0x8B714F leaves the module
	// inactive in a game; the test initialises it as if the set held the button
	CHECK(!horde->active());
	CHECK(horde->decision() == nullptr);
	AISpecialPowerUpdate fresh(book, horde->data());
	fresh.initAsIfInCommandSetForTest();
	REQUIRE(fresh.active());
	REQUIRE(fresh.decision());
	REQUIRE(fresh.decision()->button);
	const int limit = a.logic->economy().commandPointLimit(*aiPlayer);
	MESSAGE("command point limit " << limit);
	REQUIRE(limit > 4);
	REQUIRE(limit % 4 == 0); // 1 - used / limit is exactly 0.25 at used = 3 / 4 of it
	auto setMoney = [&](std::uint32_t m) {
		aiPlayer->getMoney()->withdraw(aiPlayer->getMoney()->countMoney(), false);
		aiPlayer->getMoney()->deposit(m, false);
		REQUIRE(aiPlayer->getMoney()->countMoney() == m);
	};
	auto setUsage = [&](int u) {
		aiPlayer->commandPoints().removeUsage(aiPlayer->commandPoints().getUsage());
		aiPlayer->commandPoints().addUsage(u);
		REQUIRE(aiPlayer->commandPoints().getUsage() == u);
	};
	const int edge = limit / 4 * 3;
	setMoney(1001);
	setUsage(edge - 1);
	CHECK(fresh.decideNow() == true);
	setUsage(edge); // a quarter free is not above a quarter
	CHECK(fresh.decideNow() == false);
	setUsage(0);
	CHECK(fresh.decideNow() == true);
	setMoney(1000); // exactly 1000 is not above it
	CHECK(fresh.decideNow() == false);
	setMoney(0);
	CHECK(fresh.decideNow() == false);
}

TEST_CASE("AIPOW-1 r2: BUFFECONOMYBUILDING (RW 0x9E7671): Dwarven Riches chooses each economy structure once (the decision's set of ids)")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionDwarves", 5111, &err), err);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	AISpecialPowerUpdate *econ = aiPowerOfType(*book, 32);
	REQUIRE(econ);
	a.advance(2);
	CHECK(econ->data()->m_commandButtonName == "Command_SpellBookDwarvenRiches");
	REQUIRE(econ->active()); // the Dwarves' spell book's command set holds the button (RW 0x8B714F)
	REQUIRE(econ->decision());
	Object *mine1 = a.make("DwarvenMineShaft", a.ai, 0.0f, 0.0f);
	Object *mine2 = a.make("DwarvenMineShaft", a.ai, 300.0f, 0.0f);
	REQUIRE(mine1->isKindOfName("ECONOMY_STRUCTURE"));
	REQUIRE(mine2->isKindOfName("ECONOMY_STRUCTURE"));
	std::vector<ObjectID> chosen;
	for (int k = 0; k < 20; ++k)
	{
		const std::set<ObjectID> before = econ->decision()->buffedIds;
		if (!econ->decideNow())
		{
			CHECK(econ->decision()->buffedIds == before); // a no adds nothing
			break;
		}
		REQUIRE(econ->decision()->buffedIds.size() == before.size() + 1); // one new structure per yes, never one already chosen
		ObjectID added = INVALID_ID;
		for (ObjectID id : econ->decision()->buffedIds)
		{
			if (before.count(id) == 0)
			{
				added = id;
			}
		}
		const Object *o = a.logic->findObjectByID(added);
		REQUIRE(o);
		CHECK(econ->decidedLocation().x == o->getPosition()->x);
		CHECK(econ->decidedLocation().y == o->getPosition()->y);
		CHECK((chosen.empty() || chosen.back() < added)); // the economy list in id order
		chosen.push_back(added);
	}
	MESSAGE("BUFFECONOMYBUILDING chose " << chosen.size() << " structures");
	REQUIRE(!chosen.empty());
	CHECK(std::find(chosen.begin(), chosen.end(), mine1->getID()) != chosen.end());
	CHECK(std::find(chosen.begin(), chosen.end(), mine2->getID()) != chosen.end());
	CHECK(econ->decideNow() == false); // every one chosen: no duplicate
	CHECK(econ->decision()->buffedIds.size() == chosen.size());
}

TEST_CASE("AIPOW-1 r2: ASSIST_BATTLE_DEBUFF (RW 0x9E896E, variant 1): Men's Arrow Volley at the busy unit's victim")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5112, &err), err);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	AISpecialPowerUpdate *debuff = aiPowerOfType(*book, 23);
	REQUIRE(debuff);
	a.advance(2);
	CHECK(debuff->data()->m_commandButtonName == "Command_SpellBookArrowVolleyGood");
	REQUIRE(debuff->active());
	REQUIRE(debuff->decision());
	CHECK(debuff->decision()->variant == 1);
	CHECK(debuff->decideNow() == false); // no army unit has a victim
	Object *mine = a.make("GondorFighterHorde", a.ai, 0.0f, 0.0f);
	Object *theirs = a.make("GondorFighterHorde", a.human, 60.0f, 0.0f);
	a.advance(2);
	REQUIRE(mine->getAIUpdateInterface()->aiAttackObject(theirs, CMD_FROM_PLAYER));
	REQUIRE(mine->getContain());
	for (Object *m : *mine->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.5f); // its side's average below 0.99
	}
	CHECK(debuff->decideNow() == true);
	CHECK(debuff->decision()->assistUnit == mine->getID());
	CHECK(debuff->decision()->assistTarget == theirs->getID()); // DEBUFF: the victim
	CHECK(distance2D(debuff->decidedLocation(), *theirs->getPosition()) <= 150.0f);
	MESSAGE("ASSIST_BATTLE_DEBUFF at " << distance2D(debuff->decidedLocation(), *theirs->getPosition()) << " from the victim");
	CHECK(debuff->decideNow() == false); // the player's timer (RW 0x9E880B) is not ready again before 15 frames
}

TEST_CASE("AIPOW-1 r2: the Elves' BUFFTERRAIN (Elven Wood, RW 0x9E77EE) and ENSHROUDINGMIST (RW 0x9E7869) at a busy archer unit")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionElves", 5113, &err), err);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	AISpecialPowerUpdate *terrain = aiPowerOfType(*book, 30), *mist = aiPowerOfType(*book, 29);
	REQUIRE(terrain);
	REQUIRE(mist);
	a.advance(2);
	CHECK(terrain->data()->m_commandButtonName == "Command_SpellBookElvenWood");
	CHECK(mist->data()->m_commandButtonName == "Command_SpellBookEnshroudingMist");
	REQUIRE(terrain->active());
	REQUIRE(mist->active());
	CHECK(terrain->decideNow() == false); // nobody busy
	CHECK(mist->decideNow() == false);
	Object *mine = a.make("ElvenLorienArcherHorde", a.ai, 0.0f, 0.0f);
	Object *theirs = a.make("GondorFighterHorde", a.human, 150.0f, 0.0f);
	a.advance(2);
	REQUIRE(mine->getAIUpdateInterface()->aiAttackObject(theirs, CMD_FROM_PLAYER));
	CHECK(terrain->decideNow() == true);
	CHECK(distance2D(terrain->decidedLocation(), *mine->getPosition()) <= 150.0f);
	REQUIRE(AIThreatFinder::threatCategory(*mine->getTemplate()) == 1); // AIKindOf 1: ARCHER (TheAIKindOfNames)
	CHECK(mist->decideNow() == true);
	CHECK(distance2D(mist->decidedLocation(), *mine->getPosition()) <= 150.0f);
	MESSAGE("BUFFTERRAIN " << distance2D(terrain->decidedLocation(), *mine->getPosition()) << ", ENSHROUDINGMIST "
	                       << distance2D(mist->decidedLocation(), *mine->getPosition()) << " from the unit");
}

// ---- lane RAI-POW: SOUL_FREEZE (RW 0x9E9B37) and the team's special-power registry (RW 0x7A3EF5 / 0x7A5582) ----------------------------------------------

TEST_CASE("RAI-POW: SOUL_FREEZE (RW 0x9E9B37) bins the enemies into retail's 3x3 cost grid and returns a cell of the caster's range")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("RAI-POW SOUL_FREEZE binary facts (set RW_GAME_DAT)");
	}
	else
	{
		CHECK(pe->u32At(0xC8E340u + 0x1Cu) == 0x9E9B37u); // the decision (vtable RW 0xC8E340, slot 0x1C)
		CHECK(AISpecialPowerDecision::decisionPorted(44));
		CHECK(floatAt(*pe, 0xC8E338) == 2.0f);    // the second difference test's factor
		CHECK(floatAt(*pe, 0xC8E33C) == 0.65f);   // the fallback's allied-ratio bound
		CHECK(pe->u32At(0xC8E360u) == 0x55555555u); // the double 2/3: its low word .. and its high word
		CHECK(pe->u32At(0xC8E364u) == 0x3FE55555u);
	}
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5201, &err), err);
	// the target's shroud must be CLEAR (SPECIAL_ARROW_STORM's final location test, RW 0x82E913 type 0x2F): reveal the map for both players
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	Object *necro = a.make("AngmarNecromancerHorde", a.human, 0.0f, 0.0f);
	AISpecialPowerUpdate *sf = aiPowerOfType(*necro, 44);
	REQUIRE(sf);
	a.advance(1);
	REQUIRE(sf->decision());
	CHECK(sf->data()->m_commandButtonName == "Command_NecromancerSoulFreeze");
	CHECK(sf->decision()->powerTemplate == TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroSoulFreeze"));
	CHECK(sf->decision()->range == 300.0f); // ANGMAR_NECRO_SOUL_FREEZE_START_RANGE through SpecialPowerRange
	const Coord3D at = *necro->getPosition();
	// no enemy in the scan -> no
	CHECK(sf->decideNow() == false);
	// a disabled caster skips every hit's accounting (Object + 0x1C8, RW 0x9325B4), so the enemy total stays 0
	Object *far = a.make("GondorFighterHorde", a.ai, 400.0f, 0.0f);
	CHECK(sf->decideNow() == false); // outside the 300 range
	a.logic->destroyObject(far);
	Object *enemy = a.make("GondorFighterHorde", a.ai, 290.0f, 0.0f);
	necro->setDisabled(DISABLED_HELD, a.logic->getFrame() + 1000);
	CHECK(sf->decideNow() == false);
	necro->clearDisabled(DISABLED_HELD);
	// the enemy bins x = 2, y = 1 ((caster - 300 + 590) / 200, (caster - 300 + 300) / 200); the target is x * RANGE from the origin
	REQUIRE(sf->decideNow() == true);
	// the exact retail arithmetic (RW 0x9E9F68): origin = position - range (SSE), target = index * range + origin (SSE)
	const float ox = SimMath::subf32(at.x, 300.0f), oy = SimMath::subf32(at.y, 300.0f);
	CHECK(sf->decidedLocation().x == SimMath::addf32(SimMath::mulf32(2.0f, 300.0f), ox));
	CHECK(sf->decidedLocation().y == SimMath::addf32(SimMath::mulf32(1.0f, 300.0f), oy));
	CHECK(sf->decidedLocation().z == at.z);
	// a tie keeps the first cell in x-major order: an enemy at x = 0, y = 1 comes before the one at x = 2, y = 1
	a.logic->destroyObject(enemy);
	// a second caster on the computer's team (its registry is still empty) for the tie
	Object *necro2 = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 600.0f);
	AISpecialPowerUpdate *sf2 = aiPowerOfType(*necro2, 44);
	REQUIRE(sf2);
	a.advance(1);
	// both enemies in the x = 2 column (cells (2, 0) and (2, 2)); the tie keeps (2, 0), the first in x-major order
	a.make("GondorFighterHorde", a.human, 200.0f, 400.0f);
	a.make("GondorFighterHorde", a.human, 200.0f, 800.0f);
	REQUIRE(sf2->decideNow() == true);
	const float o2x = SimMath::subf32(necro2->getPosition()->x, 300.0f), o2y = SimMath::subf32(necro2->getPosition()->y, 300.0f);
	CHECK(sf2->decidedLocation().x == SimMath::addf32(SimMath::mulf32(2.0f, 300.0f), o2x));
	CHECK(sf2->decidedLocation().y == SimMath::addf32(SimMath::mulf32(0.0f, 300.0f), o2y)); // cell y = 0
}

TEST_CASE("RAI-POW: the team's special-power registry (RW 0x7A3EF5 / 0x7A5582): a computer's Necromancer registers Soul Freeze and another caster of the team is refused")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5202, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	Object *necro = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 0.0f);
	AISpecialPowerUpdate *sf = aiPowerOfType(*necro, 44);
	REQUIRE(sf);
	a.make("GondorFighterHorde", a.human, 100.0f, 0.0f);
	a.advance(1);
	REQUIRE(sf->decision());
	Team *team = necro->getTeam();
	REQUIRE(team);
	REQUIRE(team->specialPowerRecords().empty());
	REQUIRE(sf->decideNow() == true);
	REQUIRE(team->specialPowerRecords().size() == 1);
	CHECK(team->specialPowerRecords()[0].powerType == sf->decision()->powerType);
	CHECK(team->specialPowerRecords()[0].frame == a.logic->getFrame());
	CHECK(team->specialPowerRecords()[0].caster == necro->getID());
	CHECK(team->findSpecialPowerRecord(sf->decision()->powerType, nullptr, *a.logic) == necro);
	CHECK(team->findSpecialPowerRecord(sf->decision()->powerType, necro, *a.logic) == necro);
	CHECK(team->findSpecialPowerRecord(sf->decision()->powerType, nullptr, *a.logic)->getID() == necro->getID());
	// the same caster passes the gate again (retail compares the record's id with the caster), a second unit of the team does not
	CHECK(sf->decideNow() == true);
	Object *other = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 0.0f);
	AISpecialPowerUpdate *sf2 = aiPowerOfType(*other, 44);
	REQUIRE(sf2);
	a.advance(1);
	CHECK(sf2->decideNow() == false);
}

TEST_CASE("RAI-POW end to end: a Hard computer's Angmar Necromancer runs Soul Freeze on its own and writes the team's registry")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5203, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	Object *necro = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 0.0f);
	AISpecialPowerUpdate *sf = aiPowerOfType(*necro, 44);
	REQUIRE(sf);
	a.make("GondorFighterHorde", a.human, 100.0f, 0.0f);
	a.make("GondorFighterHorde", a.human, 110.0f, 0.0f);
	// the retail module's starter is StartsPaused (data\ini\object\evilfaction\hordes\angmar\angmarhordes.ini); a script's
	// unpause (pauseCountdown) is what the AI waits for, so lift it here to exercise the framework's readiness gate
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroSoulFreeze");
	REQUIRE(t);
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*necro, t);
	REQUIRE(sp);
	sp->pauseCountdown(false);
	sp->setReadyFrame(a.logic->getFrame());
	REQUIRE(sp->isReady());
	a.advance(1);
	REQUIRE(sf->decision());
	AISpecialPowerUpdate::stats() = AISpecialPowerUpdate::Stats{};
	int frame = -1;
	for (int i = 0; i < 400 && frame < 0; ++i)
	{
		a.advance(1);
		if (AISpecialPowerUpdate::stats().executedByType[44] != 0)
		{
			frame = i;
		}
	}
	MESSAGE("Soul Freeze ran after " << frame << " frames");
	// engine pin (seed 5203): the module's 25-frame counter (RW 0x99302D) from its first update and the roll above Hard's chance pass on this frame.
	// Merge queue q43 (measured once on the merged tree and at each merge point): #226's own tree said 26; on the archive (q41's DECOMPFIX-1104B and q42
	// move the random stream) the roll passes at 23 from #226's merge point on
	CHECK(frame == 23);
	CHECK(AISpecialPowerUpdate::stats().executedByType[44] == 1);
	REQUIRE(necro->getTeam());
	const std::vector<Team::SpecialPowerRecord> &records = necro->getTeam()->specialPowerRecords();
	REQUIRE(!records.empty());
	CHECK(records[0].powerType == sf->decision()->powerType);
	CHECK(records[0].caster == necro->getID());
	for (const Team::SpecialPowerRecord &r : records)
	{
		CHECK(r.caster == necro->getID());
	}
	// one record per successful decision: the next call appends exactly one
	const size_t before = records.size();
	REQUIRE(sf->decideNow() == true);
	CHECK(records.size() == before + 1);
}

TEST_CASE("RAI-POW: the team special-power registry round-trips through a snapshot and an old snapshot stays version 1")
{
	OPENBFME_REQUIRE_START(s);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroSoulFreeze");
	REQUIRE(t);
	TeamFactory f;
	TeamPrototype *proto = f.initTeam("RAI-POW registry", nullptr, false, nullptr);
	REQUIRE(proto);
	Team *saved = f.createTeam(proto);
	REQUIRE(saved);
	saved->addSpecialPowerRecord(t->getSpecialPowerType(), 7, 42);
	XferSave save;
	saved->xfer(save);
	REQUIRE(!save.data().empty());
	CHECK(save.data()[0] == 2); // the block's version
	Team *loaded = f.createTeam(proto);
	REQUIRE(loaded);
	XferLoad load(save.data());
	loaded->xfer(load);
	REQUIRE(loaded->specialPowerRecords().size() == 1);
	CHECK(loaded->specialPowerRecords()[0].powerType == t->getSpecialPowerType());
	CHECK(loaded->specialPowerRecords()[0].frame == 7);
	CHECK(loaded->specialPowerRecords()[0].caster == 42);
	// a team with no record writes version 1, and a version 1 load clears a record the target team already had
	Team *empty = f.createTeam(proto);
	REQUIRE(empty);
	XferSave save1;
	empty->xfer(save1);
	REQUIRE(!save1.data().empty());
	CHECK(save1.data()[0] == 1);
	Team *loaded1 = f.createTeam(proto);
	REQUIRE(loaded1);
	loaded1->addSpecialPowerRecord(t->getSpecialPowerType(), 9, 43);
	XferLoad load1(save1.data());
	loaded1->xfer(load1);
	CHECK(loaded1->specialPowerRecords().empty());
}

// review probe prtrkq (PR #226, Greptile P1 / Devin): an initialized Soul Freeze decision that went through a snapshot keeps deciding
TEST_CASE("RAI-POW review probe: SOUL_FREEZE still decides after its module round-trips through a snapshot")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5204, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	Object *necro = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 0.0f);
	AISpecialPowerUpdate *sf = aiPowerOfType(*necro, 44);
	REQUIRE(sf);
	a.make("GondorFighterHorde", a.human, 100.0f, 0.0f);
	a.advance(1);
	REQUIRE(sf->decision());
	REQUIRE(sf->initialized());
	XferSave save;
	sf->xfer(save);
	XferLoad load(save.data());
	sf->xfer(load);
	REQUIRE(sf->decision());
	CHECK(sf->decideNow() == true); // before the load it answers yes (the registry test above); a null template after the load answers no
}

// pin: the position sampler's membership (record + 0x10) joins the player info record's state hash, so perf1's 4-player skirmish and uber1's Uber
// battle hash differently from frame 0 (engine references, re-recorded here: not retail goldens)

// ---- lane RAI-POW-2: the structure breakers (RW 0x9E7A98 / 0x9E7942 / 0x9E6EF4) and the record's position sampler (RW 0x8E4042 / 0xA024DA) -------------

namespace
{
// the module of `type` on a spell book: the book's own active module, or (when that faction's command set does not reach the button) a module initialised
// as if it did (RW 0x8B714F's test hook, as CALLTHEHORDE's test)
AISpecialPowerUpdate *spellBookModule(Object &book, int type, std::unique_ptr<AISpecialPowerUpdate> &fresh)
{
	AISpecialPowerUpdate *found = aiPowerOfType(book, type);
	REQUIRE(found);
	if (found->active())
	{
		return found;
	}
	fresh = std::make_unique<AISpecialPowerUpdate>(&book, found->data());
	fresh->initAsIfInCommandSetForTest();
	return fresh.get();
}

// the spell book's power of the module's button made ready (RW 0x8B708B asks isReady)
void readySpellPower(Object &book, AISpecialPowerUpdate &module, std::uint32_t frame)
{
	REQUIRE(module.decision());
	const CommandButton *button = module.decision()->button;
	REQUIRE(button);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(button->m_specialPowerName);
	REQUIRE(t);
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(book, t);
	REQUIRE(sp);
	sp->pauseCountdown(false);
	sp->setReadyFrame(frame);
	REQUIRE(sp->isReady());
}

// the centre RW 0x8E4082 grows for a cluster whose first point is `first` and whose second is `second`: weight starts at the sampler's radius `r`;
// length L = (float)sqrt of the float sum of squares of first - second; newWeight = 0.5 * ((r + L) + r); the point moves along the normalised first -
// second by (r - newWeight)
Coord3D secondPointClusterCenter(const Coord3D &first, const Coord3D &second, float r)
{
	const float dz = SimMath::subf32(first.z, second.z), dy = SimMath::subf32(first.y, second.y), dx = SimMath::subf32(first.x, second.x);
	const float length = SimMath::fstpDword(SimMath::length3d(dx, dy, dz));
	const float inv = SimMath::divf32(1.0f, length);
	const float nx = SimMath::mulf32(dx, inv), ny = SimMath::mulf32(dy, inv), nz = SimMath::mulf32(dz, inv);
	const float newWeight = SimMath::mulf32(SimMath::addf32(SimMath::addf32(r, length), r), 0.5f);
	return Coord3D{ SimMath::addf32(SimMath::addf32(SimMath::mulf32(nx, r), first.x), SimMath::mulf32(newWeight, SimMath::subf32(0.0f, nx))),
		            SimMath::addf32(SimMath::addf32(SimMath::mulf32(ny, r), first.y), SimMath::mulf32(newWeight, SimMath::subf32(0.0f, ny))),
		            SimMath::addf32(SimMath::addf32(SimMath::mulf32(nz, r), first.z), SimMath::mulf32(newWeight, SimMath::subf32(0.0f, nz))) };
}

// the brain's enemy manager picks its current enemy once the manager's update has run a few frames (RW 0x6C74C2 / 0x9B2EB9)
void awaitEnemy(Arena &a)
{
	const AISkirmishPlayer *ai = a.logic->skirmishAI().findAI(a.ai->getPlayerIndex());
	for (int i = 0; i < 40 && (!ai || AITacticalAI::enemyOfGroup(*ai) < 0); ++i)
	{
		a.advance(1);
	}
	REQUIRE(ai);
	REQUIRE(AITacticalAI::enemyOfGroup(*ai) >= 0); // tolerance: the arena has at least one hostile player
}

// every unit of the player's army list goes: the test's own busy unit is then the first one RW 0xA0241D finds
void clearArmy(GameLogic &logic, const Player &player)
{
	const std::vector<ObjectID> ids = AITacticalAI::recordOf(logic, player.getPlayerIndex()).army();
	for (ObjectID id : ids)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			logic.destroyObject(o);
		}
	}
}

// the player's structures go, his citadel stays (a player without one is defeated): the sampler then holds only what the test makes
void leaveOnlyTheCitadel(GameLogic &logic, const Player *player)
{
	std::vector<ObjectID> doomed;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == player && o->isKindOfName("STRUCTURE") && !o->isKindOfName("COMMANDCENTER"))
		{
			doomed.push_back(o->getID());
		}
	}
	for (ObjectID id : doomed)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			logic.destroyObject(o);
		}
	}
}
} // namespace

TEST_CASE("RAI-POW-2: the structure breakers' decisions, their vtables and the sampler's constants (RW 0x9E7A98 / 0x9E7942 / 0x9E6EF4 / 0x8E4042)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("RAI-POW-2 binary facts (set RW_GAME_DAT)");
	}
	else
	{
		CHECK(pe->u32At(0xC8DF88u + 0x1Cu) == 0x9E7A98u); // STRUCTURE_BREAKER
		CHECK(pe->u32At(0xC8DF60u + 0x1Cu) == 0x9E7942u); // STRUCTURE_BREAKER_PREF_WALLS
		CHECK(pe->u32At(0xC8DDFCu + 0x1Cu) == 0x9E6EF4u); // STRUCTURE_BASEKILL
		CHECK(floatAt(*pe, 0xC8DF5C) == 500.0f);           // the walls' enemy radius
		CHECK(floatAt(*pe, 0xC8DDF8) == 500.0f);           // the base kill's enemy radius
		CHECK(floatAt(*pe, 0xBD889C) == 2.0f);             // RW 0x8E3DD6: the cluster reach's sqrt(2) factor
		CHECK(floatAt(*pe, 0xBD869C) == 0.5f);             // RW 0x8E4082's weight step
		CHECK(floatAt(*pe, 0xBD1908) == 1.0f);             // RW 0x403175's 1 / length
		CHECK(pe->u32At(0x8E4069u) == 0x45100FF3u); // RW 0x8E4069: movss xmm0, [ebp + 8] (the sampler ctor's radius argument)
		CHECK(pe->u32At(0x8E4071u) == 0x46110FF3u); // RW 0x8E4071: movss [esi + 0x1C], xmm0 (the sampler's radius at + 0x1C)
		CHECK(pe->u32At(0x8E4076u) == 0x5D5EC68Bu); // RW 0x8E4076: mov eax, esi / pop esi / pop ebp (the ctor returns the sampler)
	}
	// the KindOf bits the wall mask and the base kill name (TheKindOfNames RW 0xDA0E68's order)
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("WALK_ON_TOP_OF_WALL") == 60);
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("DEFENSIVE_WALL") == 61);
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("WALL_UPGRADE") == 150);
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("WALL_HUB") == 156);
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("WALL_SEGMENT") == 189);
	CHECK(ObjectTemplateInfoBuilder::kindOfIndex("DOZER_FACTORY") == 194);
	CHECK(AISpecialPowerDecision::decisionPorted(27));
	CHECK(AISpecialPowerDecision::decisionPorted(28));
	CHECK(AISpecialPowerDecision::decisionPorted(36));
}

TEST_CASE("RAI-POW-2: STRUCTURE_BREAKER (RW 0x9E7A98) picks the enemy record's densest structure cluster (RW 0xA024DA)")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5301, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	awaitEnemy(a);
	leaveOnlyTheCitadel(*a.logic, a.human);
	a.advance(1);
	const Object *citadel = firstOf(*a.logic, a.human, "COMMANDCENTER");
	REQUIRE(citadel);
	REQUIRE(citadel->isKindOfName("DOZER_FACTORY"));
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *breaker = spellBookModule(*book, 27, fresh);
	REQUIRE(breaker->decision());
	CHECK(breaker->decision()->kind == AISpecialPowerDecision::KIND_LOCATION);
	readySpellPower(*book, *breaker, a.logic->getFrame());
	// the citadel is a one-member cluster: it is the densest cluster until the two farms below (RW 0xA024DA accepts one member)
	CHECK(breaker->decideNow() == true);
	CHECK(breaker->decidedLocation().x == citadel->getPosition()->x);
	CHECK(breaker->decidedLocation().y == citadel->getPosition()->y);
	// two enemy farms 20 apart: the cluster's centre is RW 0x8E4082's grown centre of the two, and it wins over the citadel's one
	AIStatCollector *record = a.logic->skirmishAI().record(a.human->getPlayerIndex());
	REQUIRE(record);
	Object *farm1 = a.make("GondorFarm", a.human, -12.0f, 4.0f);
	Object *farm2 = a.make("GondorFarm", a.human, 8.0f, 4.0f);
	record->update(*a.logic); // the sampler add is deferred until the object's position is final (defence-records PR)
	REQUIRE(std::abs(distance2D(*farm1->getPosition(), *citadel->getPosition())) > 400.0f); // tolerance: Evendim's midpoint is far from the human base
	REQUIRE(std::abs(distance2D(*farm2->getPosition(), *farm1->getPosition())) < 30.0f);
	const Coord3D expected = secondPointClusterCenter(*farm1->getPosition(), *farm2->getPosition(), 200.0f);
	CHECK(breaker->decideNow() == true);
	CHECK(breaker->decidedLocation().x == expected.x);
	CHECK(breaker->decidedLocation().y == expected.y);
	CHECK(breaker->decidedLocation().z == expected.z);
	MESSAGE("STRUCTURE_BREAKER at " << distance2D(breaker->decidedLocation(), *farm1->getPosition()) << " from the first farm");
	// a third farm far away is a cluster of one and changes nothing
	a.make("GondorFarm", a.human, 500.0f, -300.0f);
	record->update(*a.logic);
	CHECK(breaker->decideNow() == true);
	CHECK(breaker->decidedLocation().x == expected.x);
	CHECK(breaker->decidedLocation().y == expected.y);
	// RW 0xB4D9A0: a cluster whose centre the caster's player sees SHROUDED is skipped; with every cell shrouded nothing is left
	a.logic->shroud()->undoRevealMapForPlayerPermanently(a.ai->getPlayerIndex());
	a.logic->shroud()->shroudMapForPlayer(a.ai->getPlayerIndex());
	a.advance(1); // the shroud's next update folds the lookers into the cells
	CHECK(breaker->decideNow() == false);
	a.logic->shroud()->revealMapForPlayerPermanently(a.ai->getPlayerIndex());
	a.advance(1);
	CHECK(breaker->decideNow() == true);
	// a human caster never decides (RW 0x6A950B: no skirmish AI for his player)
	Object *aragorn = a.make("GondorAragorn", a.human, 300.0f, 0.0f);
	AISpecialPowerUpdate humanCaster(aragorn, breaker->data());
	humanCaster.initAsIfInCommandSetForTest();
	REQUIRE(humanCaster.decision());
	CHECK(humanCaster.decideNow() == false);
}

TEST_CASE("RAI-POW-2: STRUCTURE_BREAKER_PREF_WALLS (RW 0x9E7942) takes the nearest enemy wall near the busy unit and falls back to RW 0x9E7A98")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5302, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	awaitEnemy(a);
	leaveOnlyTheCitadel(*a.logic, a.human);
	clearArmy(*a.logic, *a.ai);
	a.advance(1);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *walls = spellBookModule(*book, 28, fresh);
	REQUIRE(walls->decision());
	readySpellPower(*book, *walls, a.logic->getFrame());
	// two enemy walls far from the unit (the fallback's densest cluster; thin, so the summon's spot test accepts their midpoint)
	AIStatCollector *record = a.logic->skirmishAI().record(a.human->getPlayerIndex());
	REQUIRE(record);
	Object *far1 = a.make("BlueMountainWall", a.human, -700.0f, 300.0f);
	Object *far2 = a.make("BlueMountainWall", a.human, -420.0f, 300.0f);
	record->update(*a.logic); // the sampler add is deferred until the object's position is final (defence-records PR)
	const Coord3D clusterCenter = secondPointClusterCenter(*far1->getPosition(), *far2->getPosition(), 200.0f);
	// a computer soldier busy with a human one; a wall of the human 100 away from it
	Object *mine = a.make("GondorFighterHorde", a.ai, 0.0f, 0.0f);
	Object *theirs = a.make("GondorFighterHorde", a.human, 60.0f, 0.0f);
	Object *wall = a.make("BlueMountainWall", a.human, 100.0f, 0.0f);
	REQUIRE(wall->isKindOfName("WALL_SEGMENT"));
	REQUIRE(mine->getAIUpdateInterface()->aiAttackObject(theirs, CMD_FROM_PLAYER));
	CHECK(walls->decideNow() == true);
	// the wall is the target; the location base then applies the power's RandomizeTargetLocation walk (RW 0xA01C9C, 15 .. 150), because a spot under the
	// wall's own footprint fails the ActionManager's pathable test (RW 0x82D2C9): the answer stays within 150 of the wall, not at the cluster
	CHECK(distance2D(walls->decidedLocation(), *wall->getPosition()) <= 150.0f); // tolerance: RW 0xA01C9C's random walk is 15 .. 150
	CHECK(distance2D(walls->decidedLocation(), clusterCenter) > 150.0f); // tolerance: the walk never reaches the cluster, 500+ away
	MESSAGE("PREF_WALLS at " << distance2D(walls->decidedLocation(), *wall->getPosition()) << " from the wall");
	// no busy unit: RW 0x9E7A5F runs the structure-breaker fallback; its densest cluster is the far walls' grown midpoint, and Ent Allies' summon spot
	// test (RW 0x82E586, RadiusCursorRadius 100) refuses a spot inside that cluster, so the decision answers no (the near wall is gone: this is not it)
	a.logic->destroyObject(mine);
	a.logic->destroyObject(wall);
	a.advance(1);
	CHECK(walls->decideNow() == false);
	// the fallback's candidate exists: the far walls are one cluster whose centre is the grown midpoint (the STRUCTURE_BREAKER test pins its arithmetic)
	const AIPositionSampler::Group *cluster = nullptr;
	for (const AIPositionSampler::Group &c : record->positions().groups())
	{
		if (c.members.size() == 2)
		{
			cluster = &c;
		}
	}
	REQUIRE(cluster);
	CHECK(cluster->center.x == clusterCenter.x);
	CHECK(cluster->center.y == clusterCenter.y);
	// with a busy unit but no wall within 500 the same fallback runs (the wall is gone)
	Object *mine2 = a.make("GondorFighterHorde", a.ai, 0.0f, 40.0f);
	Object *theirs2 = a.make("GondorFighterHorde", a.human, 60.0f, 40.0f);
	REQUIRE(mine2->getAIUpdateInterface()->aiAttackObject(theirs2, CMD_FROM_PLAYER));
	CHECK(walls->decideNow() == false);
}

TEST_CASE("RAI-POW-2: STRUCTURE_BASEKILL (RW 0x9E6EF4) takes an enemy DOZER_FACTORY within 500, else refuses while the enemy still lists one")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5303, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	awaitEnemy(a);
	clearArmy(*a.logic, *a.ai);
	a.advance(1);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *baseKill = spellBookModule(*book, 36, fresh);
	REQUIRE(baseKill->decision());
	readySpellPower(*book, *baseKill, a.logic->getFrame());
	const Object *citadel = firstOf(*a.logic, a.human, "COMMANDCENTER");
	REQUIRE(citadel);
	REQUIRE(citadel->isKindOfName("DOZER_FACTORY"));
	// a busy computer soldier 250 from the enemy citadel: the enemy dozer factory within 500 is the target
	const Coord3D c = *citadel->getPosition();
	const Object *a0 = firstOf(*a.logic, a.ai, "COMMANDCENTER");
	REQUIRE(a0);
	const float dx = a0->getPosition()->x - c.x, dy = a0->getPosition()->y - c.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	REQUIRE(len > 400.0f); // tolerance: the two bases on Evendim are farther apart than the 500 scan
	const Coord3D near{ c.x + dx / len * 250.0f, c.y + dy / len * 250.0f, 0.0f };
	Object *mine = a.make("GondorFighterHorde", a.ai, near.x - a.where.x, near.y - a.where.y);
	Object *theirs = a.make("GondorFighterHorde", a.human, 40.0f, 40.0f);
	REQUIRE(mine->getAIUpdateInterface()->aiAttackObject(theirs, CMD_FROM_PLAYER));
	CHECK(baseKill->decideNow() == true);
	CHECK(baseKill->decidedLocation().x == citadel->getPosition()->x);
	CHECK(baseKill->decidedLocation().y == citadel->getPosition()->y);
	// no busy unit and the enemy's structure list still holds a DOZER_FACTORY (RW 0x9E6E8E): no
	a.logic->destroyObject(mine);
	a.advance(1);
	CHECK(baseKill->decideNow() == false);
}

TEST_CASE("RAI-POW-2: the position sampler's membership (record + 0x10) round-trips through a snapshot")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5304, &err), err);
	a.make("GondorFarm", a.human, 0.0f, 0.0f);
	a.make("GondorFarm", a.human, 20.0f, 0.0f);
	a.make("GondorFarm", a.human, 400.0f, 0.0f);
	AIStatCollector *record = a.logic->skirmishAI().record(a.human->getPlayerIndex());
	REQUIRE(record);
	record->update(*a.logic); // the sampler add is deferred until the object's position is final (defence-records PR)
	const std::vector<ObjectID> before = record->positions().members();
	REQUIRE(before.size() >= 3); // tolerance: the human base's own structures join the three farms
	const std::vector<AIPositionSampler::Group> groups = record->positions().groups();
	REQUIRE(!groups.empty());
	for (const AIPositionSampler::Group &g : groups)
	{
		CHECK(g.radius >= 200.0f); // tolerance: DefenseTreeNodeRadius (200) is a group's floor, a group with several members is larger (RW 0x8E4082)
	}
	XferSave save;
	record->xfer(save);
	REQUIRE(!save.data().empty());
	CHECK(save.data()[0] == 3); // the block's version: the sampler's membership
	AIStatCollector loaded(a.human->getPlayerIndex());
	XferLoad load(save.data());
	loaded.xfer(load);
	CHECK(loaded.positions().members() == before);
	REQUIRE(loaded.positions().groups().size() == groups.size());
	for (size_t i = 0; i < groups.size(); ++i)
	{
		CHECK(loaded.positions().groups()[i].members == groups[i].members);
		CHECK(loaded.positions().groups()[i].radius == groups[i].radius);
		CHECK(loaded.positions().groups()[i].center.x == groups[i].center.x);
		CHECK(loaded.positions().groups()[i].center.y == groups[i].center.y);
		CHECK(loaded.positions().groups()[i].center.z == groups[i].center.z);
	}
}

// ---- lane RAI-POW-2 (community #233): the economy debuffs (RW 0x9E74B7 / 0x9E726A) and the AIPlayer's enemy (RW 0x6AA5F5 / 0x8F47EB / 0x8F457D) ----------------------------

// (the helpers spellBookModule, readySpellPower, awaitEnemy, clearArmy and leaveOnlyTheCitadel are the structure breakers' above)

// pin: the AIPlayer's enemy cache (RW 0x8F47EB) enters the skirmish AI's hash and the save only once an AI has asked for an enemy (it holds a value);
// a game that never asks keeps every recorded perf/uber state hash and the AI block's version 4 bytes.
TEST_CASE("RAI-POW-2: the economy debuffs' decisions, vtables and the enemy acquire's constants (RW 0x9E74B7 / 0x9E726A / 0x8F457D)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("RAI-POW-2 economy debuff binary facts (set RW_GAME_DAT)");
	}
	else
	{
		CHECK(pe->u32At(0xC8DE9Cu + 0x1Cu) == 0x9E74B7u); // DEBUFFECONOMYBUILDING
		CHECK(pe->u32At(0xC8DE74u + 0x1Cu) == 0x9E726Au); // DEBUFFPRODUCTIONBUILDING
		CHECK(floatAt(*pe, 0xC8DE70) == 200.0f);   // the production debuff's enemy radius
		CHECK(floatAt(*pe, 0xBD1908) == 1.0f);     // the economy weight's start
		CHECK(floatAt(*pe, 0xC1C4AC) == 999999995904.0f);  // acquireEnemy's initial best
		CHECK(floatAt(*pe, 0xC798D4) == 499999997952.0f);  // an unreachable candidate
		CHECK(floatAt(*pe, 0xC798D0) == 250000.0f);        // a rival AI already targets it
		CHECK(floatAt(*pe, 0xC798CC) == 625.0f);           // a rival AI targets me
		CHECK(pe->cstring(0xBFD514) == std::string("PlyrCreeps")); // the player name acquireEnemy skips
	}
	CHECK(AISpecialPowerDecision::decisionPorted(41));
	CHECK(AISpecialPowerDecision::decisionPorted(42));
}

TEST_CASE("RAI-POW-2: DEBUFFECONOMYBUILDING (RW 0x9E74B7) takes the enemy record's first visible, built economy building and the enemy cache refreshes every 25 frames")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMordor", 5401, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	// no AI has asked for an enemy yet: the cache is empty, so the block keeps version 5's bytes (the new fields are not written)
	{
		XferSave empty;
		a.logic->skirmishAI().xferState(empty, true);
		REQUIRE(!empty.data().empty());
		CHECK(empty.data()[0] == 5);
	}
	awaitEnemy(a);
	leaveOnlyTheCitadel(*a.logic, a.human);
	clearArmy(*a.logic, *a.ai);
	a.advance(1);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *blight = spellBookModule(*book, 41, fresh);
	REQUIRE(blight->decision());
	CHECK(blight->data()->m_commandButtonName == "Command_SpellBookBlight");
	readySpellPower(*book, *blight, a.logic->getFrame());
	// the AIPlayer's enemy (RW 0x6AA5F5 -> 0x8F47EB): the only enemy player, cached for 25 frames
	AISkirmishPlayer *ai = a.logic->skirmishAI().findAI(a.ai->getPlayerIndex());
	REQUIRE(ai);
	const Player *enemy = AITacticalAI::enemyPlayer(*a.logic, *ai);
	REQUIRE(enemy == a.human);
	// RW 0x7A1129's ThingClass read is not ported: the acquire says so once (S-4257)
	bool classifiedStop = false;
	for (const std::string &line : a.logic->report().stops)
	{
		classifiedStop = classifiedStop || line.rfind("[S-4257]", 0) == 0;
	}
	CHECK(classifiedStop);
	CHECK(ai->enemyIndex == a.human->getPlayerIndex());
	const std::uint32_t refresh = ai->enemyRefreshFrame;
	CHECK(refresh >= a.logic->getFrame()); // tolerance: the refresh frame is this frame + 25, read at its boundary
	CHECK(refresh <= a.logic->getFrame() + 25u); // tolerance: RW 0x8F47EB sets now + 25 exactly
	a.advance(1);
	CHECK(AITacticalAI::enemyPlayer(*a.logic, *ai) == enemy); // the cache is not refreshed before its frame
	CHECK(ai->enemyRefreshFrame == refresh);
	// two enemy economy buildings: the first in the record's list wins a tie (the weight both start at 1.0)
	Object *farm1 = a.make("GondorFarm", a.human, 0.0f, 0.0f);
	Object *farm2 = a.make("GondorFarm", a.human, 300.0f, 0.0f);
	CHECK(blight->decideNow() == true);
	CHECK(blight->decidedLocation().x == farm1->getPosition()->x);
	CHECK(blight->decidedLocation().y == farm1->getPosition()->y);
	// RW 0x44DDEC(object, 2): an UNDER_CONSTRUCTION building is skipped
	const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	farm1->setStatus((unsigned)underConstruction, true);
	CHECK(blight->decideNow() == true);
	CHECK(blight->decidedLocation().x == farm2->getPosition()->x);
	farm1->setStatus((unsigned)underConstruction, false);
	// RW 0xB4D9A0: everything the caster's player sees shrouded -> no
	a.logic->shroud()->undoRevealMapForPlayerPermanently(a.ai->getPlayerIndex());
	a.logic->shroud()->shroudMapForPlayer(a.ai->getPlayerIndex());
	a.advance(1);
	CHECK(blight->decideNow() == false);
	a.logic->shroud()->revealMapForPlayerPermanently(a.ai->getPlayerIndex());
	a.advance(1);
	CHECK(blight->decideNow() == true);
	// the snapshot round-trips the two new AIPlayer fields; with a used cache the AI block is version 6
	XferSave save;
	a.logic->skirmishAI().xferState(save, true);
	REQUIRE(!save.data().empty());
	CHECK(save.data()[0] == 6);
	const int savedEnemy = ai->enemyIndex;
	const std::uint32_t savedRefresh = ai->enemyRefreshFrame;
	ai->enemyIndex = -1;
	ai->enemyRefreshFrame = 0;
	XferLoad load(save.data());
	a.logic->skirmishAI().xferState(load, true);
	AISkirmishPlayer *loaded = a.logic->skirmishAI().findAI(a.ai->getPlayerIndex());
	REQUIRE(loaded);
	CHECK(loaded->enemyIndex == savedEnemy);
	CHECK(loaded->enemyRefreshFrame == savedRefresh);
}

TEST_CASE("RAI-POW-2: DEBUFFPRODUCTIONBUILDING (RW 0x9E726A) takes the enemy FS_FACTORY with the most enemy FS_FACTORYs within 200")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMordor", 5402, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	awaitEnemy(a);
	clearArmy(*a.logic, *a.ai);
	a.advance(1);
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *snowbind = spellBookModule(*book, 42, fresh);
	REQUIRE(snowbind->decision());
	CHECK(snowbind->data()->m_commandButtonName == "Command_SpellBookSnowbind");
	readySpellPower(*book, *snowbind, a.logic->getFrame());
	// three extra enemy citadels (FS_FACTORY): two 100 apart, one alone 600 away; the human's own citadel is alone at its base
	const Object *base = firstOf(*a.logic, a.human, "COMMANDCENTER");
	REQUIRE(base);
	REQUIRE(base->isKindOfName("FS_FACTORY"));
	Object *pair1 = a.make("MenFortressCitadel", a.human, 0.0f, 0.0f);
	Object *pair2 = a.make("MenFortressCitadel", a.human, 100.0f, 0.0f);
	Object *alone = a.make("MenFortressCitadel", a.human, 600.0f, 0.0f);
	REQUIRE(pair1->isKindOfName("FS_FACTORY"));
	REQUIRE(std::abs(distance2D(*alone->getPosition(), *pair1->getPosition())) > 400.0f); // tolerance: the lone citadel is out of the pair's 200 count
	CHECK(snowbind->decideNow() == true);
	// the pair's first: it counts pair2 and itself within 200; the lone ones count one
	CHECK(snowbind->decidedLocation().x == pair1->getPosition()->x);
	CHECK(snowbind->decidedLocation().y == pair1->getPosition()->y);
	// the pair's first is shrouded: pair2 wins (still two in reach), and the lone ones stay at one
	a.logic->shroud()->undoRevealMapForPlayerPermanently(a.ai->getPlayerIndex());
	a.logic->shroud()->shroudMapForPlayer(a.ai->getPlayerIndex());
	a.advance(1);
	const bool second = snowbind->decideNow();
	CHECK(second == false);
	if (second)
	{
		CHECK(snowbind->decidedLocation().x == pair2->getPosition()->x);
	}
	a.logic->shroud()->revealMapForPlayerPermanently(a.ai->getPlayerIndex());
	a.advance(1);
}

// ---- lane RAI-POW-2 (community #234): CITADEL (RW 0x9E6CCB) ---------------------------------------------------------------------------------------------

TEST_CASE("RAI-POW-2: CITADEL (RW 0x9E6CCB) draws its point along the AI base -> enemy DOZER_FACTORY line, lines 0x49 / 0x51")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("RAI-POW-2 CITADEL binary facts (set RW_GAME_DAT)");
	}
	else
	{
		CHECK(pe->u32At(0xC8DD50u + 0x1Cu) == 0x9E6CCBu); // the decision (vtable RW 0xC8DD50, slot 0x1C)
		CHECK((pe->u32At(0x9E6D4Cu) & 0xFFFFu) == 0x496Au); // push 0x49: the first draw's line
		CHECK((pe->u32At(0x9E6D8Au) & 0xFFFFu) == 0x516Au); // push 0x51: the second draw's line
		CHECK(pe->cstring(0xC8DD70).find("AISpellBookCitadel.cpp") != std::string::npos); // the draws' source name
		// review fix: the second draw's low bound is fld [0xBD19DC] (RW 0x9E6D9D) = -1.0f, the first's fldz (RW 0x9E6D5A)
		CHECK((pe->u32At(0x9E6D9Du) & 0xFFFFu) == 0x05D9u);    // fld dword ptr [disp32]
		CHECK(pe->u32At(0x9E6D9Fu) == 0x00BD19DCu);            // .. [0xBD19DC]
		CHECK(floatAt(*pe, 0xBD19DC) == -1.0f);
	}
	CHECK(AISpecialPowerDecision::decisionPorted(37));
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5501, &err), err);
	REQUIRE(a.logic->shroud());
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	const AISkirmishPlayer *ai = a.logic->skirmishAI().findAI(a.ai->getPlayerIndex());
	for (int i = 0; i < 40 && (!ai || AITacticalAI::enemyOfGroup(*ai) < 0); ++i)
	{
		a.advance(1);
	}
	REQUIRE(ai);
	REQUIRE(AITacticalAI::enemyOfGroup(*ai) >= 0); // tolerance: the arena has at least one hostile player
	leaveOnlyTheCitadel(*a.logic, a.human);
	a.advance(1);
	const Object *citadel = firstOf(*a.logic, a.human, "COMMANDCENTER");
	REQUIRE(citadel);
	REQUIRE(citadel->isKindOfName("DOZER_FACTORY"));
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *cit = spellBookModule(*book, 37, fresh);
	REQUIRE(cit->decision());
	CHECK(cit->data()->m_commandButtonName == "Command_SpellBookCitadel");
	readySpellPower(*book, *cit, a.logic->getFrame());
	// the body: the target is the enemy record's first DOZER_FACTORY (the human's citadel); the two draws are RW 0x6D332C at "AISpellBookCitadel.cpp"
	// lines 0x49 / 0x51, both (0, 1), and the point is base + (target - base) * t1 with the perpendicular (t2 * dy, -t2 * dx) offset. The ActionManager's
	// building-power placement test for type 0x67 is stop S-1874's unported branch (with its forbidden-object filter): it refuses the point, and the
	// 15 .. 150 walk, so the decision answers no, as it does in this port today
	const Coord3D &base = ai->build.startPosition; // RW 0x8F0284 -> RW 0x90CD45
	const Coord3D &goal = *citadel->getPosition();
	const float dx = SimMath::subf32(goal.x, base.x), dy = SimMath::subf32(goal.y, base.y), dz = SimMath::subf32(goal.z, base.z);
	a.logic->random().enableCallLog(true);
	a.logic->random().clearCallLog();
	const bool yes = cit->decideNow();
	const std::vector<GameLogicRandom::Call> &log = a.logic->random().callLog();
	a.logic->random().enableCallLog(false);
	REQUIRE(log.size() >= 2); // tolerance: the walk may add draws after the two of the point
	CHECK(log[0].real);
	CHECK(log[0].rlo == 0.0f);
	CHECK(log[0].rhi == 1.0f);
	CHECK(log[0].line == 0x49);
	CHECK(log[0].file == "AISpellBookCitadel.cpp");
	CHECK(log[1].real);
	CHECK(log[1].rlo == -1.0f); // review fix: RW 0xBD19DC
	CHECK(log[1].rhi == 1.0f);
	CHECK(log[1].line == 0x51);
	CHECK(log[1].file == "AISpellBookCitadel.cpp");
	const float t1 = log[0].rresult, t2 = log[1].rresult;
	const Coord3D point{ SimMath::addf32(base.x, SimMath::mulf32(dx, t1)), SimMath::addf32(base.y, SimMath::mulf32(dy, t1)),
		                 SimMath::addf32(base.z, SimMath::mulf32(dz, t1)) };
	// RW 0x9E6DE0's SSE: the cross product with (0, 0, t2): x += t2 * dy - dz * 0, y = (dz * 0 - t2 * dx) + y, z = (dx * 0 - dy * 0) + z
	const float zdz = SimMath::mulf32(dz, 0.0f);
	const Coord3D expected{ SimMath::addf32(point.x, SimMath::subf32(SimMath::mulf32(t2, dy), zdz)),
		                    SimMath::addf32(SimMath::subf32(zdz, SimMath::mulf32(t2, dx)), point.y),
		                    SimMath::addf32(SimMath::subf32(SimMath::mulf32(dx, 0.0f), SimMath::mulf32(dy, 0.0f)), point.z) };
	// review fix (Greptile / Devin): the point the decision handed to RW 0xA0229F is the expected one
	CHECK(cit->proposedLocationForTest().x == expected.x);
	CHECK(cit->proposedLocationForTest().y == expected.y);
	CHECK(cit->proposedLocationForTest().z == expected.z);
	CHECK(ActionManager::canDoSpecialPowerAtLocation(*book, *cit->decision()->button, expected) == false); // S-1874: type 0x67 is not ported
	CHECK(yes == false);
	CHECK(log.size() == 4); // the point's two draws, then RW 0xA01C9C's random walk's two: the walk tried and was refused too
	// a human's spell book never decides (RW 0x6A950B: no skirmish AI for his player)
	Player *humanPlayer = a.logic->players().getNthPlayer(a.human->getPlayerIndex());
	REQUIRE(humanPlayer);
	Object *theirBook = SpecialPowerModules::getSpellBookObject(*a.logic, *humanPlayer);
	if (theirBook)
	{
		AISpecialPowerUpdate humanCaster(theirBook, cit->data());
		humanCaster.initAsIfInCommandSetForTest();
		REQUIRE(humanCaster.decision());
		CHECK(humanCaster.decideNow() == false);
	}
}

// ---- lane RAI-POW-3: TREE_KILLER (Devastation, RW 0x9E70B6) and the terrain's tree-cluster index (TerrainTrees + 0x584) -----------------------

namespace
{
// the spell book's module of `type`: the book's own active module, or (when the faction's command set does not reach the button) a module
// initialised as if it did (RW 0x8B714F's test hook, as CALLTHEHORDE's test)
AISpecialPowerUpdate *spellBookModuleForTest(Object &book, int type, std::unique_ptr<AISpecialPowerUpdate> &fresh)
{
	AISpecialPowerUpdate *found = aiPowerOfType(book, type);
	REQUIRE(found);
	if (found->active())
	{
		return found;
	}
	fresh = std::make_unique<AISpecialPowerUpdate>(&book, found->data());
	fresh->initAsIfInCommandSetForTest();
	return fresh.get();
}

// the spell book's power of the module's button made ready (RW 0x8B708B asks isReady)
void readySpellPowerForTest(Object &book, AISpecialPowerUpdate &module, std::uint32_t frame)
{
	REQUIRE(module.decision());
	const CommandButton *button = module.decision()->button;
	REQUIRE(button);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(button->m_specialPowerName);
	REQUIRE(t);
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(book, t);
	REQUIRE(sp);
	sp->pauseCountdown(false);
	sp->setReadyFrame(frame);
	REQUIRE(sp->isReady());
}
} // namespace

TEST_CASE("RAI-POW-3: TREE_KILLER (Devastation, RW 0x9E70B6) is ported and the computer's spell book decides it")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionIsengard", 6101, &err), err);
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i); // RW 0xB4D9A0 == 2 skips a cluster; the test sees every tree
	}
	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	REQUIRE(!a.logic->trees().records().empty()); // Evendim's map has trees
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *killer = spellBookModuleForTest(*book, 35, fresh);
	REQUIRE(killer);
	CHECK(AISpecialPowerDecision::decisionPorted(35)); // red before the port: false
	REQUIRE(killer->decision());
	CHECK(killer->decision()->kind == AISpecialPowerDecision::KIND_LOCATION);
	CHECK(killer->data()->m_commandButtonName == "Command_SpellBookDevastation");
	readySpellPowerForTest(*book, *killer, a.logic->getFrame());
	CHECK(killer->decideNow() == true); // red before the port: false (the decision answers no and counts it)
}

TEST_CASE("RAI-POW-3: the tree-cluster index (TerrainTrees + 0x584) groups the map's trees at 100 and the decision burns the largest")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionIsengard", 6102, &err), err);
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	TerrainTrees &trees = a.logic->trees();
	const std::vector<TerrainTrees::Record> &records = trees.records();
	REQUIRE(!records.empty());
	CHECK(TerrainTrees::kClusterPadding == 100.0f); // RW 0xBD88D8, RW 0x8E4042's padding argument at RW 0x684876

	// every tree record is in exactly one cluster (RW 0x683C2D adds each record as the map makes it; RW 0x67D3C2's clear does not unlink)
	std::vector<int> membership(records.size(), 0);
	for (const TerrainTrees::Cluster &c : trees.clusters())
	{
		CHECK(!c.members.empty());
		CHECK(c.radius >= TerrainTrees::kClusterPadding); // tolerance: the empty group's radius is the padding and it only grows (RW 0x8E40A4 / 0x8E4082)
		for (int m : c.members)
		{
			REQUIRE(m >= 0); // tolerance: a member is a record index the map's add wrote
			REQUIRE((size_t)m < records.size()); // tolerance: the index must name one of the map's records
			++membership[(size_t)m];
		}
	}
	for (int n : membership)
	{
		CHECK(n == 1);
	}
	// the expected pick: the cluster with the most members (strictly, in cluster order), none used and none SHROUDED (the map is revealed)
	std::vector<TerrainTrees::Cluster> &clusters = trees.clusters();
	size_t expected = clusters.size();
	for (size_t i = 0; i < clusters.size(); ++i)
	{
		if (clusters[i].used || (expected != clusters.size() && clusters[i].members.size() <= clusters[expected].members.size()))
		{
			continue;
		}
		expected = i;
	}
	REQUIRE(expected != clusters.size());
	const Coord3D expectedCenter = clusters[expected].center;

	Player *aiPlayer = a.logic->players().getNthPlayer(a.ai->getPlayerIndex());
	REQUIRE(aiPlayer);
	Object *book = SpecialPowerModules::getSpellBookObject(*a.logic, *aiPlayer);
	REQUIRE(book);
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *killer = spellBookModuleForTest(*book, 35, fresh);
	REQUIRE(killer);
	readySpellPowerForTest(*book, *killer, a.logic->getFrame());

	// a snapshot taken before the cast has version 1's tree block (no used cluster)
	std::vector<std::uint8_t> before;
	REQUIRE_MESSAGE(a.logic->saveSnapshot(before, &err), err);
	CHECK(killer->decideNow() == true);
	CHECK(killer->decidedLocation().x == expectedCenter.x);
	CHECK(killer->decidedLocation().y == expectedCenter.y);
	CHECK(killer->decidedLocation().z == expectedCenter.z);
	CHECK(clusters[expected].used); // RW 0x9E7095
	MESSAGE("TREE_KILLER picked cluster " << expected << " of " << clusters.size() << " with " << clusters[expected].members.size() << " trees");

	// a second decision skips the burnt cluster: it picks the next-largest unused one (or answers no when none is left)
	size_t second = clusters.size();
	for (size_t i = 0; i < clusters.size(); ++i)
	{
		if (clusters[i].used || (second != clusters.size() && clusters[i].members.size() <= clusters[second].members.size()))
		{
			continue;
		}
		second = i;
	}
	const bool again = killer->decideNow();
	if (second == clusters.size())
	{
		CHECK(again == false);
	}
	else
	{
		CHECK(again == true);
		CHECK(killer->decidedLocation().x == clusters[second].center.x);
		CHECK(killer->decidedLocation().y == clusters[second].center.y);
		CHECK(clusters[second].used);
	}

	// the used bytes round-trip in the TerrainTrees block (version 2, written only now that one is set)
	std::vector<std::uint8_t> used;
	REQUIRE_MESSAGE(a.logic->saveSnapshot(used, &err), err);
	REQUIRE(used != before);
	REQUIRE_MESSAGE(a.g.live->loadSnapshot(used, &err), err);
	CHECK(trees.anyClusterUsed());
	CHECK(trees.clusters()[expected].used);
	// ... and the earlier version 1 snapshot loads into a game whose clusters were never used
	REQUIRE_MESSAGE(a.g.live->loadSnapshot(before, &err), err);
	CHECK(!trees.anyClusterUsed());
}

TEST_CASE("RAI-POW-3 review: a used tree cluster enters the game hash before any tree changes")
{
	// the decision sets the used byte (RW 0x9E7095) before the location base (RW 0xA0229F) answers, so a refused cast leaves a used cluster and no
	// other trace: the byte alone must reach the hash
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionIsengard", 6103, &err), err);
	TerrainTrees &trees = a.logic->trees();
	REQUIRE(!trees.clusters().empty());
	REQUIRE(!trees.anyChanged());
	const std::uint32_t before = a.logic->computeStateHash();
	trees.clusters().back().used = true;
	REQUIRE(!trees.anyChanged()); // no tree toppled
	CHECK(a.logic->computeStateHash() != before); // red before the fix: the trees' section was hashed only once a tree changed
}

TEST_CASE("RAI-POW-3 review: the tree clusters are the connected groups of trees closer than sqrt(2) * 100 (RW 0x683807's insert and merge)")
{
	// an independent check of the grouping (Greptile's note): the greedy insert merges every group a new tree touches, so the clusters are exactly the
	// connected components of "squared 3D distance < 2 * 100^2" (RW 0x6831BE -> RW 0x8E3DD6); the test computes them with its own union-find
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionIsengard", 6104, &err), err);
	const TerrainTrees &trees = a.logic->trees();
	const std::vector<TerrainTrees::Record> &records = trees.records();
	REQUIRE(records.size() > 1); // tolerance: the map's tree count is not this test's subject; it needs two trees to pair
	std::vector<size_t> parent(records.size());
	for (size_t i = 0; i < parent.size(); ++i)
	{
		parent[i] = i;
	}
	const std::function<size_t(size_t)> root = [&](size_t i) { return parent[i] == i ? i : parent[i] = root(parent[i]); };
	const float bound = SimMath::mulf32(SimMath::mulf32(100.0f, 100.0f), 2.0f);
	for (size_t i = 0; i < records.size(); ++i)
	{
		for (size_t j = i + 1; j < records.size(); ++j)
		{
			const Coord3D &p = records[i].position, &q = records[j].position;
			const float dx = SimMath::subf32(p.x, q.x), dy = SimMath::subf32(p.y, q.y), dz = SimMath::subf32(p.z, q.z);
			const float d2 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
			if (bound > d2)
			{
				parent[root(i)] = root(j);
			}
		}
	}
	std::map<size_t, std::vector<std::uint32_t>> expected;
	for (size_t i = 0; i < records.size(); ++i)
	{
		expected[root(i)].push_back((std::uint32_t)i);
	}
	std::vector<std::vector<std::uint32_t>> want, got;
	for (auto &e : expected)
	{
		want.push_back(e.second);
	}
	for (const TerrainTrees::Cluster &c : trees.clusters())
	{
		std::vector<std::uint32_t> m(c.members.begin(), c.members.end());
		std::sort(m.begin(), m.end());
		got.push_back(m);
	}
	std::sort(want.begin(), want.end());
	std::sort(got.begin(), got.end());
	CHECK(got.size() == want.size());
	CHECK(got == want);
	CHECK(want.size() < records.size()); // tolerance: the map has trees closer than the bound, so some cluster holds several (the exact grouping is checked above)
	MESSAGE(records.size() << " trees in " << got.size() << " clusters");
}

TEST_CASE("RAI-POW-4: the team threat orders round-trip in version 3 and older snapshots clear them")
{
	OPENBFME_REQUIRE_START(s);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate("SpecialAbilityNecroSoulFreeze");
	REQUIRE(t);
	TeamFactory f;
	TeamPrototype *proto = f.initTeam("RAI-POW-4 orders", nullptr, false, nullptr);
	REQUIRE(proto);
	Team *saved = f.createTeam(proto);
	REQUIRE(saved);
	Team::Order order;
	order.command = 0x11;
	order.flags = 0xFF;
	order.flagsWord = 0x4014; // RW 0x7A5AE6: the target setter clears the constructor's 0x2000
	order.timestamp = 17;
	order.target = 42;
	saved->addOrderForTest(order);
	Team::Order positionOrder = order;
	positionOrder.command = 0x1A;
	positionOrder.flags = 0x0F;
	positionOrder.flagsWord = 0x8014; // RW 0x7A5AA3
	positionOrder.timestamp = 18;
	positionOrder.target = INVALID_ID;
	positionOrder.position = Coord3D{ 11.0f, 22.0f, 33.0f };
	saved->addOrderForTest(positionOrder);
	XferSave save;
	saved->xfer(save);
	REQUIRE(!save.data().empty());
	CHECK(save.data()[0] == 3); // the block's version
	TeamFactory loadedFactory;
	TeamPrototype *loadedProto = loadedFactory.initTeam("RAI-POW-4 orders", nullptr, false, nullptr);
	REQUIRE(loadedProto);
	Team *loaded = loadedFactory.createTeam(loadedProto);
	REQUIRE(loaded);
	XferLoad load(save.data());
	loaded->xfer(load);
	REQUIRE(loaded->orders().size() == 2);
	const Team::Order &back = loaded->orders()[0];
	CHECK(back.command == 0x11);
	CHECK(back.flags == 0xFF);
	CHECK(back.flagsWord == 0x4014);
	CHECK(back.timestamp == 17);
	CHECK(back.target == 42);
	CHECK(back.hasTarget());
	CHECK(loaded->orders()[1].flagsWord == 0x8014);
	CHECK(loaded->orders()[1].position.z == 33.0f);
	XferSave roundTrip;
	loaded->xfer(roundTrip);
	CHECK(roundTrip.data() == save.data());
	for (unsigned frame : { 3017u, 3018u, 3019u })
	{
		saved->expireOrders(frame);
		loaded->expireOrders(frame);
		CHECK(loaded->orders().size() == 3019u - frame);
		XferSave a, b;
		saved->xfer(a);
		loaded->xfer(b);
		CHECK(a.data() == b.data());
		StateHasher ha, hb;
		saved->crc(ha);
		loaded->crc(hb);
		CHECK(ha.value() == hb.value());
	}
	// a version 2 snapshot (the registry only) loads into a team whose order list is cleared
	Team *registry = f.createTeam(proto);
	REQUIRE(registry);
	registry->addSpecialPowerRecord(t->getSpecialPowerType(), 3, 7);
	XferSave save2;
	registry->xfer(save2);
	REQUIRE(!save2.data().empty());
	CHECK(save2.data()[0] == 2);
	Team *loaded2 = f.createTeam(proto);
	REQUIRE(loaded2);
	loaded2->addOrderForTest(order);
	XferLoad load2(save2.data());
	loaded2->xfer(load2);
	CHECK(loaded2->orders().empty());
	CHECK(loaded2->specialPowerRecords().size() == 1);
}

// ---- lane RAI-POW-4: the team threat order (RW 0x7A59CB) and ATTACK_HEAL_AOE (Well of Souls, RW 0x9E691D) --------------------------------

namespace
{
// the horde's module of `type`, or a module initialised as if its command button were in the command set (RW 0x8B714F's test hook)
AISpecialPowerUpdate *hordePowerForTest(Object &horde, int type, std::unique_ptr<AISpecialPowerUpdate> &fresh)
{
	AISpecialPowerUpdate *found = aiPowerOfType(horde, type);
	REQUIRE(found);
	if (found->active())
	{
		return found;
	}
	fresh = std::make_unique<AISpecialPowerUpdate>(&horde, found->data());
	fresh->initAsIfInCommandSetForTest();
	return fresh.get();
}

void makePowerReady(Object &horde, AISpecialPowerUpdate &module)
{
	REQUIRE(module.decision());
	const CommandButton *button = module.decision()->button;
	REQUIRE(button);
	const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(button->m_specialPowerName);
	REQUIRE(t);
	SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(horde, t);
	REQUIRE(sp);
	sp->pauseCountdown(false);
	sp->setReadyFrame(horde.logic().getFrame());
	REQUIRE(sp->isReady());
}
} // namespace

TEST_CASE("RAI-POW-4: the team threat order (RW 0x7A59CB) records the most damaged member and the engine updates it every frame")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5301, &err), err);
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	Object *horde = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 0.0f);
	Object *ally = a.make("AngmarNecromancerHorde", a.ai, -60.0f, 0.0f);
	Object *enemy1 = a.make("GondorFighterHorde", a.human, 100.0f, 0.0f);
	Object *enemy2 = a.make("GondorFighterHorde", a.human, -60.0f, 100.0f);
	a.advance(2);
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(enemy1, CMD_FROM_PLAYER));
	REQUIRE(ally->getAIUpdateInterface()->aiAttackObject(enemy2, CMD_FROM_PLAYER));
	// the caster horde's members are barely hurt (0.99), the ally horde's badly (0.8): the ally is the most damaged member (RW 0x7A0141's
	// smallest deficit)
	REQUIRE(horde->getContain());
	REQUIRE(ally->getContain());
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.99f);
	}
	for (Object *m : *ally->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.8f);
	}
	Team *team = horde->getTeam();
	REQUIRE(team);
	REQUIRE(team == ally->getTeam());
	REQUIRE(team->orders().empty());
	team->updateThreatOrder(*a.logic);
	REQUIRE(team->orders().size() == 1);
	const Team::Order &order = team->orders()[0];
	CHECK(order.command == 0x11); // the 50 .. 1500 deficit order, the one ATTACK_HEAL_AOE consumes
	CHECK(order.hasTarget());
	CHECK(order.target == ally->getID());
	// an existing matching order is not added again (RW 0x7A159B)
	team->updateThreatOrder(*a.logic);
	CHECK(team->orders().size() == 1);
	// Team::updateState (RW 0x7A208C) calls the vslot at its end: expire the direct order, advance one frame through ScriptEngine::updateTeamState,
	// and the team has a fresh one
	team->expireOrders(a.logic->getFrame() + 3001);
	CHECK(team->orders().empty());
	a.advance(1);
	CHECK(!team->orders().empty());
	// RW 0x7A2004: an order older than 3000 frames goes
	team->expireOrders(a.logic->getFrame() + 3001);
	CHECK(team->orders().empty());
}

TEST_CASE("RAI-POW-4: ATTACK_HEAL_AOE (Well of Souls, RW 0x9E691D) consumes the team's 0x11 order after the distance and location gates")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5302, &err), err);
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	// the battle sits on the AI fortress's plateau (dry land: Well of Souls has no WATER_OK and its radius cursor is 75 wide)
	const Object *aiFort = firstOf(*a.logic, a.ai, "COMMANDCENTER");
	REQUIRE(aiFort);
	a.where = *aiFort->getPosition();
	Object *horde = a.make("AngmarNecromancerHorde", a.ai, -200.0f, 0.0f);
	Object *ally = a.make("AngmarNecromancerHorde", a.ai, 150.0f, 0.0f);
	Object *enemy1 = a.make("GondorFighterHorde", a.human, -100.0f, 0.0f);
	Object *enemy2 = a.make("GondorFighterHorde", a.human, 250.0f, 0.0f);
	a.advance(2);
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(enemy1, CMD_FROM_PLAYER));
	REQUIRE(ally->getAIUpdateInterface()->aiAttackObject(enemy2, CMD_FROM_PLAYER));
	REQUIRE(horde->getContain());
	REQUIRE(ally->getContain());
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.99f);
	}
	for (Object *m : *ally->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.8f);
	}
	std::unique_ptr<AISpecialPowerUpdate> fresh;
	AISpecialPowerUpdate *heal = hordePowerForTest(*horde, 49, fresh);
	REQUIRE(heal->decision());
	CHECK(heal->decision()->kind == AISpecialPowerDecision::KIND_LOCATION);
	CHECK(heal->data()->m_commandButtonName == "Command_NecromancerWellOfSouls");
	makePowerReady(*horde, *heal);
	Team *team = horde->getTeam();
	REQUIRE(team);
	REQUIRE(team == ally->getTeam());
	team->updateThreatOrder(*a.logic);
	REQUIRE(team->orders().size() == 1);
	CHECK(team->orders()[0].target == ally->getID());
	// the order's member is 600 away: 600 is not below distanceLimit (400) * 0.9, so the decision answers no and keeps the order
	CHECK(heal->decideNow() == false);
	CHECK(team->orders().size() == 1);
	// the caster joins the ally: the estimated length gate and the first enemy within the range (250) now pass
	Coord3D near = *ally->getPosition();
	near.x += 30.0f;
	horde->teleportTo(&near, true);
	a.logic->partition().update();
	CHECK(distance2D(*horde->getPosition(), *ally->getPosition()) < 60.0f); // tolerance: the teleport puts the caster 30 from the ally
	{
		const CommandButton *b = heal->decision()->button;
		const SpecialPowerTemplate *t = TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName);
		REQUIRE(t);
		SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*horde, t);
		REQUIRE(sp);
		CHECK(sp->requirementsMet());
		CHECK(horde->getContain()->getContainCount() >= 4u); // tolerance: SpecialAbilityNecroWellOfSouls UnitCost (4) is a floor; the horde has its members
	}
	CHECK(heal->decideNow() == true);
	// the location is the nearest enemy the scan found (a horde member, not the horde's own centre) and the order is erased (RW 0x7A1C5E)
	CHECK(distance2D(heal->decidedLocation(), *horde->getPosition()) <= heal->decision()->range); // tolerance: the scan's own radius bounds a hit
	bool onEnemy = false;
	for (Object *o = a.logic->getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == a.human && !o->isEffectivelyDead() && heal->decidedLocation().x == o->getPosition()->x &&
		    heal->decidedLocation().y == o->getPosition()->y)
		{
			onEnemy = true;
		}
	}
	CHECK(onEnemy);
	CHECK(team->orders().empty());
	// a target id that resolves to nothing does not fall back to the payload position: the order stays and the decision answers no
	Team::Order dead;
	dead.command = 0x11;
	dead.flags = 0xFF;
	dead.flagsWord = 0xC014; // both payload bits: a nonzero unresolved target still has no fallback
	dead.position = *horde->getPosition();
	dead.target = 0x7FFFFFF0u;
	team->addOrderForTest(dead); // tests only
	CHECK(heal->decideNow() == false);
	CHECK(team->orders().size() == 1);
	CHECK(team->orders()[0].target == dead.target);
	team->eraseOrder(0);
	dead.target = INVALID_ID;
	team->addOrderForTest(dead);
	CHECK(heal->decideNow() == true); // RW 0x9E6991: zero target uses the valid position payload
	CHECK(team->orders().empty());
	// any team registry record of the power rejects, even the caster's own (RW 0x9E6941)
	team->addSpecialPowerRecord(heal->decision()->powerType, a.logic->getFrame(), horde->getID());
	CHECK(heal->decideNow() == false);
}


namespace
{
void setDeficit(Object &object, float deficit)
{
	auto *body = dynamic_cast<ActiveBody *>(object.getBodyModule());
	REQUIRE(body);
	REQUIRE(body->getMaxHealth() > deficit); // tolerance: keep the fixture alive while setting an exact deficit
	body->friend_setHealthRaw(body->getMaxHealth() - deficit);
	REQUIRE(body->getHealth() - body->getMaxHealth() == -deficit);
}

Team *threatTeam(Arena &a)
{
	TeamFactory &factory = a.logic->players().teams();
	TeamPrototype *proto = factory.initTeam("Threat review", const_cast<Player *>(a.ai), false, nullptr);
	REQUIRE(proto);
	Team *team = factory.createTeam(proto);
	REQUIRE(team);
	return team;
}
} // namespace

TEST_CASE("RAI-POW-4 review: summed threat thresholds retain the first minimum target and exact payload flags")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionMen", 5310, &err), err);
	Object *one = a.make("GondorAragorn", a.ai, 0.0f, 0.0f);
	Object *two = a.make("GondorAragorn", a.ai, -60.0f, 0.0f);
	Object *enemy = a.make("GondorFighter", a.human, 100.0f, 0.0f);
	Team *team = threatTeam(a);
	one->setTeam(team);
	two->setTeam(team);
	team->updateThreatOrder(*a.logic);
	CHECK(team->orders().empty()); // no eligible members returns zero (RW 0x7A0291)
	REQUIRE(one->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	REQUIRE(two->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	struct Row { float one, two; int command, flags; };
	const Row rows[] = {
		{ 30.0f, 30.0f, 0x11, 0xFF }, { 800.0f, 800.0f, 0x1A, 0x0F },
		{ 25.0f, 25.0f, 0, 0 }, { 750.0f, 750.0f, 0x11, 0xFF },
		{ 1250.0f, 1250.0f, 0x1A, 0x0F }, { 1251.0f, 1251.0f, 0x1A, 0 },
		{ 20.0f, 40.0f, 0x11, 0xFF }
	}; // RW 0x7A5A16/2A/3E: strict -2500/-1500/-50 comparisons of the TOTAL
	a.logic->random().enableCallLog(true);
	a.logic->random().clearCallLog();
	const auto seed = a.logic->random().seedArray();
	for (const Row &row : rows)
	{
		CAPTURE(row.one);
		CAPTURE(row.two);
		while (!team->orders().empty()) { team->eraseOrder(0); }
		setDeficit(*one, row.one);
		setDeficit(*two, row.two);
		team->updateThreatOrder(*a.logic);
		if (!row.command) { CHECK(team->orders().empty()); continue; }
		REQUIRE(team->orders().size() == 1);
		const Team::Order &order = team->orders()[0];
		CHECK(order.command == row.command);
		CHECK(order.flags == row.flags);
		CHECK(order.flagsWord == 0x4014);
		CHECK(order.timestamp == 0);
		// a tie keeps the first member of retail's walk, the newest (RW 0x66362D: the member list is newest first, the port's getLastMember)
		CHECK(order.target == two->getID());
	}
	CHECK(a.logic->random().callLog().empty());
	CHECK(a.logic->random().seedArray() == seed);
}

TEST_CASE("RAI-POW-4 review: contained and entering deficits join ordinary members before threat selection")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5311, &err), err);
	Object *horde = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 0.0f);
	Object *hero = a.make("GondorAragorn", a.ai, -60.0f, 0.0f);
	Object *enemy = a.make("GondorFighter", a.human, 100.0f, 0.0f);
	a.advance(2);
	Team *team = threatTeam(a);
	horde->setTeam(team);
	hero->setTeam(team);
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	REQUIRE(horde->getContain());
	auto *contain = horde->getContain();
	auto *hi = contain->getHordeContainInterface();
	REQUIRE(hi);
	std::vector<Object *> members(contain->getContainedItemsList()->begin(), contain->getContainedItemsList()->end());
	REQUIRE(members.size() >= 2); // tolerance: the retail horde supplies at least the two members needed for this case
	for (Object *member : members) { setDeficit(*member, 0.0f); }
	const auto count = hi->countWithEntering();
	setDeficit(*members[0], 40.0f);
	setDeficit(*members[1], 20.0f);
	hi->releaseMemberForGarrison(members[1]); // RW 0x86EBF4: real transition into the entering-ID tree
	REQUIRE(hi->membersEntering() == std::vector<ObjectID>{ members[1]->getID() });
	CHECK(hi->countWithEntering() == count);
	team->updateThreatOrder(*a.logic);
	REQUIRE(team->orders().size() == 1); // -40 + -20 crosses -50 (RW 0x86EB1D / 0x7A5A3E)
	CHECK(team->orders()[0].command == 0x11);
	CHECK(team->orders()[0].flags == 0xFF);
	CHECK(team->orders()[0].flagsWord == 0x4014);
	CHECK(team->orders()[0].target == horde->getID());
	team->eraseOrder(0);
	// The horde and ordinary-body paths both contribute to the total (RW 0x7A01E8 / 0x7A0249).
	setDeficit(*members[0], 20.0f);
	setDeficit(*members[1], 0.0f);
	setDeficit(*hero, 40.0f);
	REQUIRE(hero->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	team->updateThreatOrder(*a.logic);
	REQUIRE(team->orders().size() == 1);
	CHECK(team->orders()[0].command == 0x11);
	CHECK(team->orders()[0].flags == 0xFF);
	CHECK(team->orders()[0].target == hero->getID());
}

TEST_CASE("RAI-POW-4 review (prtrlp): a soldier inside a horde adds nothing of its own to the team's threat total (RW 0x693A1A(0) / 0x68C866)")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5313, &err), err);
	Object *horde = a.make("AngmarNecromancerHorde", a.ai, 0.0f, 0.0f);
	Object *enemy = a.make("GondorFighter", a.human, 100.0f, 0.0f);
	a.advance(2);
	Team *team = threatTeam(a);
	horde->setTeam(team);
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
	auto *contain = horde->getContain();
	REQUIRE(contain);
	auto *hi = contain->getHordeContainInterface();
	REQUIRE(hi);
	for (Object *member : *contain->getContainedItemsList()) { setDeficit(*member, 0.0f); }
	Object *soldier = contain->getContainedItemsList()->front();
	REQUIRE(soldier->getAIUpdateInterface());
	soldier->getAIUpdateInterface()->setCurrentVictim(enemy); // a soldier in melee holds its own victim (AIAttackMelee.cpp's RW 0x6682B1 write)
	soldier->setTeam(team); // a horde's soldiers can share its team (as the factory makes them)
	REQUIRE(soldier->getHordeObject(false) == horde);
	setDeficit(*soldier, 30.0f);
	team->updateThreatOrder(*a.logic);
	// retail: the horde's -30 only (above -50: no order); counting the soldier as well would make -60 and an order on the soldier
	CHECK(team->orders().empty());
	setDeficit(*soldier, 60.0f);
	team->updateThreatOrder(*a.logic);
	REQUIRE(team->orders().size() == 1);
	CHECK(team->orders()[0].command == 0x11);
	CHECK(team->orders()[0].target == horde->getID()); // the soldier itself never wins
}

TEST_CASE("RAI-POW-4 review: healthy hordes with free slots produce the exclusive position payload")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5312, &err), err);
	Object *enemy = a.make("GondorFighter", a.human, 100.0f, 0.0f);
	std::vector<Object *> hordes;
	for (int i = 0; i < 4; ++i) { hordes.push_back(a.make("AngmarNecromancerHorde", a.ai, -60.0f * i, 0.0f)); }
	a.advance(2);
	Team *team = threatTeam(a);
	for (Object *horde : hordes)
	{
		horde->setTeam(team);
		REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(enemy, CMD_FROM_PLAYER));
		auto *contain = horde->getContain();
		REQUIRE(contain);
		REQUIRE(!contain->getContainedItemsList()->empty());
		// The initial list includes a banner carrier in addition to the seven capacity slots.
		for (int i = 0; i < 2; ++i)
		{
			Object *removed = contain->getContainedItemsList()->front();
			contain->removeFromContain(removed);
			removed->setTeam(a.ai->getDefaultTeam());
		}
		for (Object *member : *contain->getContainedItemsList()) { setDeficit(*member, 0.0f); }
		auto *hi = contain->getHordeContainInterface();
		REQUIRE(hi);
		REQUIRE((int)hi->countWithEntering() < hi->getSlotCapacity()); // tolerance: each horde contributes one free-slot count
	}
	team->updateThreatOrder(*a.logic);
	REQUIRE(team->orders().size() == 1);
	CHECK(team->orders()[0].command == 0x1A);
	CHECK(team->orders()[0].flags == 0x0F);
	CHECK(team->orders()[0].flagsWord == 0x8014); // RW 0x7A5AA3: clear 0x2000 before setting 0x8000
	CHECK(team->orders()[0].target == INVALID_ID);
	CHECK(team->orders()[0].position.x == 0.0f);
	CHECK(team->orders()[0].position.y == 0.0f);
	CHECK(team->orders()[0].position.z == 0.0f);
}


TEST_CASE("RAI-POW-4 review: Attack Heal AoE snapshot resumes the decision reservation and full RNG state")
{
	OPENBFME_REQUIRE_START(s);
	Arena a;
	std::string err;
	REQUIRE_MESSAGE(a.start(*s, "FactionAngmar", 5313, &err), err);
	for (int i = 0; i < a.logic->players().getPlayerCount(); ++i)
	{
		a.logic->shroud()->revealMapForPlayerPermanently(i);
	}
	// the battle sits on the AI fortress's plateau (dry land: Well of Souls has no WATER_OK and its radius cursor is 75 wide)
	const Object *aiFort = firstOf(*a.logic, a.ai, "COMMANDCENTER");
	REQUIRE(aiFort);
	a.where = *aiFort->getPosition();
	Object *horde = a.make("AngmarNecromancerHorde", a.ai, -200.0f, 0.0f);
	Object *ally = a.make("AngmarNecromancerHorde", a.ai, 150.0f, 0.0f);
	Object *enemy1 = a.make("GondorFighterHorde", a.human, -100.0f, 0.0f);
	Object *enemy2 = a.make("GondorFighterHorde", a.human, 250.0f, 0.0f);
	AISpecialPowerUpdate *heal = aiPowerOfType(*horde, 49);
	REQUIRE(heal);
	heal->initAsIfInCommandSetForTest(); // initialize the attached module before its first scheduled update
	REQUIRE(heal->active());
	a.advance(2);
	REQUIRE(horde->getAIUpdateInterface()->aiAttackObject(enemy1, CMD_FROM_PLAYER));
	REQUIRE(ally->getAIUpdateInterface()->aiAttackObject(enemy2, CMD_FROM_PLAYER));
	REQUIRE(horde->getContain());
	REQUIRE(ally->getContain());
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.99f);
	}
	for (Object *m : *ally->getContain()->getContainedItemsList())
	{
		setHealthRatio(*m, 0.8f);
	}
	makePowerReady(*horde, *heal);
	Team *team = horde->getTeam();
	REQUIRE(team);
	team->updateThreatOrder(*a.logic);
	REQUIRE(team->orders().size() == 1);
	CHECK_FALSE(heal->decideNow()); // RW 0x9E69E9: the pre-distance goal reservation is part of the saved state
	const ObjectID casterID = horde->getID(), allyID = ally->getID();
	const auto savedHash = a.logic->computeStateHash();
	StateHasher moduleHash;
	heal->crc(moduleHash);
	std::vector<std::uint8_t> snapshot;
	REQUIRE_MESSAGE(a.logic->saveSnapshot(snapshot, &err), err);
	std::vector<std::uint32_t> expectedHashes;
	std::vector<std::uint8_t> expectedAfterDecision, expectedAfterFrames;
	std::vector<GameLogicRandom::Call> expectedCalls;
	GameLogicRandom::Seed expectedSeed{};
	Coord3D expectedLocation{};
	for (int pass = 0; pass < 2; ++pass)
	{
		CAPTURE(pass);
		if (pass)
		{
			REQUIRE_MESSAGE(a.g.live->loadSnapshot(snapshot, &err), err);
			a.logic = &a.g.live->logic();
			horde = a.logic->findObjectByID(casterID);
			ally = a.logic->findObjectByID(allyID);
			REQUIRE(horde);
			REQUIRE(ally);
			heal = aiPowerOfType(*horde, 49);
			REQUIRE(heal);
			team = horde->getTeam();
			REQUIRE(team);
			StateHasher loadedHash;
			heal->crc(loadedHash);
			CHECK(loadedHash.value() == moduleHash.value());
			CHECK(a.logic->computeStateHash() == savedHash);
			std::vector<std::uint8_t> roundTrip;
			REQUIRE_MESSAGE(a.logic->saveSnapshot(roundTrip, &err), err);
			CHECK(roundTrip == snapshot);
		}
		a.logic->random().enableCallLog(true);
		a.logic->random().clearCallLog();
		Coord3D near = *ally->getPosition();
		near.x += 30.0f;
		horde->teleportTo(&near, true);
		a.logic->partition().update();
		const auto decisionSeed = a.logic->random().seedArray();
		const auto decisionCalls = a.logic->random().callLog().size();
		CHECK(heal->decideNow()); // RW 0x9E691D: restored powerType/button suffice; no unsaved template cache
		CHECK(team->orders().empty());
		CHECK(a.logic->random().callLog().size() == decisionCalls);
		CHECK(a.logic->random().seedArray() == decisionSeed);
		if (!pass) { expectedLocation = heal->decidedLocation(); }
		CHECK(heal->decidedLocation().x == expectedLocation.x);
		CHECK(heal->decidedLocation().y == expectedLocation.y);
		CHECK(heal->decidedLocation().z == expectedLocation.z);
		std::vector<std::uint8_t> afterDecision;
		REQUIRE_MESSAGE(a.logic->saveSnapshot(afterDecision, &err), err);
		if (!pass) { expectedAfterDecision = afterDecision; }
		CHECK(afterDecision == expectedAfterDecision); // includes both pathfinder goal reservations and consumed orders
		for (int i = 0; i < 5; ++i)
		{
			a.advance(1);
			const auto hash = a.logic->computeStateHash();
			if (!pass) { expectedHashes.push_back(hash); }
			CHECK(hash == expectedHashes[(size_t)i]);
		}
		std::vector<std::uint8_t> afterFrames;
		REQUIRE_MESSAGE(a.logic->saveSnapshot(afterFrames, &err), err);
		if (!pass)
		{
			expectedAfterFrames = afterFrames;
			expectedCalls = a.logic->random().callLog();
			expectedSeed = a.logic->random().seedArray();
		}
		CHECK(afterFrames == expectedAfterFrames);
		CHECK(a.logic->random().seedArray() == expectedSeed);
		const auto &calls = a.logic->random().callLog();
		REQUIRE(calls.size() == expectedCalls.size());
		for (size_t i = 0; i < calls.size(); ++i)
		{
			const auto &got = calls[i], &want = expectedCalls[i];
			CHECK(got.real == want.real);
			CHECK(got.lo == want.lo);
			CHECK(got.hi == want.hi);
			CHECK(got.rlo == want.rlo);
			CHECK(got.rhi == want.rhi);
			CHECK(got.result == want.result);
			CHECK(got.rresult == want.rresult);
			CHECK(got.file == want.file);
			CHECK(got.line == want.line);
			CHECK(got.drewNumber == want.drewNumber);
		}
	}
}
