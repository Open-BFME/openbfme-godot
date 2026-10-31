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
#include "GameLogic/SimMath.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/Economy.h"
#include "GameLogic/SkirmishAI/AIStatCollector.h"
#include "GameLogic/SkirmishAI/AITacticalAI.h"
#include "Common/Money.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSetToggle.h"

#include <algorithm>
#include <cstring>
#include <cmath>
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
	CHECK(ai[0].find("42 of the 53 decisions run") != std::string::npos);
	CHECK(ai[0].find("SOUL_FREEZE") != std::string::npos);
	CHECK(ai[1].rfind("[S-1875]", 0) == 0);
	// the decisions still unported: SOUL_FREEZE, ATTACK_HEAL_AOE and 9 spell book ones (ARMY_BREAKER, HEAL, STRUCTURE_BREAKER (+ _PREF_WALLS, BASEKILL),
	// TREE_KILLER, CITADEL, DEBUFFECONOMYBUILDING, DEBUFFPRODUCTIONBUILDING)
	int unported = 0;
	for (int t = 0; t < AI_SPECIAL_POWER_TYPE_COUNT; ++t)
	{
		if (!AISpecialPowerDecision::decisionPorted(t))
		{
			++unported;
			const bool spellBook = std::string(TheAISpecialPowerTypeNames[t]).rfind("AI_SPELLBOOK_", 0) == 0;
			CHECK_MESSAGE((spellBook || t == 44 || t == 49), TheAISpecialPowerTypeNames[t]);
		}
	}
	CHECK(unported == 11);
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
	// FBSIM-1 24 -> 22 (the computer player's phantom sites)
	CHECK(frame == 22);
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
