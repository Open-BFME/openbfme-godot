// OpenBFME. GPL-3.0.
// See GameLogic/Module/AISpecialPowerUpdate.h for the sources and what is inference.

#include "GameLogic/Module/AISpecialPowerUpdate.h"

#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Construction.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

// RW 0xDB84F8
const char *const TheAISpecialPowerTypeNames[AI_SPECIAL_POWER_TYPE_COUNT + 1] = {
	"AI_SPECIAL_POWER_BASIC_SELF_BUFF", "AI_SPECIAL_POWER_CAPTURE_BUILDING", "AI_SPECIAL_POWER_ELENDIL", "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER",
	"AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_RANGED", "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_STRUCTURES", "AI_SPECIAL_POWER_GANDALF_WIZARD_BLAST",
	"AI_SPECIAL_POWER_GIVEXP_AOE", "AI_SPECIAL_POWER_RANGED_AOE_ATTACK", "AI_SPECIAL_POWER_TOGGLE_MOUNTED", "AI_SPECIAL_POWER_SELFAOEHEALHEROS",
	"AI_SPECIAL_POWER_TARGETAOE_SUMMON", "AI_SPECIAL_POWER_LEGOLAS_ARROWWIND", "AI_SPECIAL_POWER_LEGOLAS_TRAINARCHERS", "AI_SPECIAL_POWER_GOBLINKING_BATTLEFRENZY",
	"AI_SPECIAL_POWER_GOBLINKING_CALLOFTHEDEEP", "AI_SPECIAL_POWER_GOBLINKING_MOUNTED", "AI_SPECIAL_POWER_HEAL_AOE", "AI_SPECIAL_POWER_TOGGLE_MELEE_AND_RANGE",
	"AI_SPECIAL_POWER_TOGGLE_SIEGE", "AI_SPECIAL_POWER_CHARGE", "AI_SPELLBOOK_ALWAYS_FIRE", "AI_SPELLBOOK_ASSIST_BATTLE_BUFF", "AI_SPELLBOOK_ASSIST_BATTLE_DEBUFF",
	"AI_SPELLBOOK_ARMY_BREAKER", "AI_SPELLBOOK_CAPTURE_CREEP", "AI_SPELLBOOK_HEAL", "AI_SPELLBOOK_STRUCTURE_BREAKER", "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS",
	"AI_SPELLBOOK_ENSHROUDINGMIST", "AI_SPELLBOOK_BUFFTERRAIN", "AI_SPELLBOOK_REBUILD", "AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_CALLTHEHORDE",
	"AI_SPELLBOOK_SHROUD_REVEAL", "AI_SPELLBOOK_TREE_KILLER", "AI_SPELLBOOK_STRUCTURE_BASEKILL", "AI_SPELLBOOK_CITADEL", "AI_SPECIAL_POWER_STANCEBATTLE",
	"AI_SPECIAL_POWER_STANCEAGGRESSIVE", "AI_SPECIAL_POWER_STANCEHOLDGROUND", "AI_SPELLBOOK_DEBUFFECONOMYBUILDING", "AI_SPELLBOOK_DEBUFFPRODUCTIONBUILDING",
	"AI_SPECIAL_POWER_AOE_AND_BUFF", "AI_SPECIAL_POWER_SOUL_FREEZE", "AI_SPECIAL_POWER_DOMINATE_ENEMY", "AI_SPECIAL_POWER_DOMINATE_TROLL",
	"AI_SPECIAL_POWER_TAME_THE_BEAST", "AI_SPECIAL_POWER_BASIC_SELF_DEBUFF", "AI_SPECIAL_POWER_ATTACK_HEAL_AOE", "AI_SPECIAL_POWER_RANGED_AOE_ATTACK_UNITS",
	"AI_SPECIAL_POWER_MORGUL_BLADE", "AI_SPECIAL_POWER_GOBLIN_POISON", nullptr
};

namespace
{
enum Type
{
	BASIC_SELF_BUFF = 0,
	CAPTURE_BUILDING = 1,
	ENEMY_TYPE_KILLER = 3,
	ENEMY_TYPE_KILLER_RANGED = 4,
	ENEMY_TYPE_KILLER_STRUCTURES = 5,
	TOGGLE_MOUNTED = 9,
	STANCEBATTLE = 38,
	STANCEAGGRESSIVE = 39,
	STANCEHOLDGROUND = 40,
	BASIC_SELF_DEBUFF = 48,
	MORGUL_BLADE = 51,
	GOBLIN_POISON = 52
};

const char *const kStop =
	"[S-1421] AISpecialPowerUpdate (lane MOD-4): the module (RW 0x8B73B7 / 0x8B714F / 0x8B708B), the decision framework (RW 0x993055 / 0x993307 / 0x99302D) and "
	"the decisions BASIC_SELF_BUFF, BASIC_SELF_DEBUFF, GOBLIN_POISON, ENEMY_TYPE_KILLER (+ _RANGED, _STRUCTURES, MORGUL_BLADE), TOGGLE_MOUNTED, the three "
	"STANCE types and CAPTURE_BUILDING (never) run; the other 39 decisions (ELENDIL, GANDALF_WIZARD_BLAST, GIVEXP_AOE, RANGED_AOE_ATTACK(_UNITS), "
	"SELFAOEHEALHEROS, TARGETAOE_SUMMON, the LEGOLAS / GOBLINKING types, HEAL_AOE, the TOGGLE_MELEE_AND_RANGE / _SIEGE, CHARGE, every AI_SPELLBOOK_* type, "
	"AOE_AND_BUFF, SOUL_FREEZE, DOMINATE_*, TAME_THE_BEAST, ATTACK_HEAL_AOE) answer no (counted); the location decisions need the ActionManager's "
	"canDoSpecialPowerAtLocation (RW 0x75CF21 -> 0x82D925 / 0x82C3D7 / 0x82D5DA), not ported; INFERENCE: an unset SpecialPowerAIType is -1, the map object "
	"re-init (behaviour slot 0xB4, RW 0x8B7554) is the first update's, the AI's goal object (AIUpdate + 0x40) is the AI's current victim (ZH donor), only "
	"SpecialAbilityUpdate answers RW 0x68C461's vslot 4, Object + 0xA0 bit 6 (the disabled exception of RW 0x696FD2) is clear";

const unsigned kExecuteFrames = 5u * LOGICFRAMES_PER_SECOND; // RW 0x993024: [0xD9F608] * 5

int kind(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

bool hasKind(const Object &o, int bit)
{
	return bit >= 0 && o.isKindOf((unsigned)bit);
}

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

// body slot 0x14 (RW 0x8C1D75): health / max health in the FPU (PC24), 0 without a positive max health
double bodyRatioWide(const Object &o)
{
	const BodyModuleInterface *body = o.getBodyModule();
	if (!body)
	{
		return 0.0;
	}
	const float maxHealth = body->getMaxHealth();
	return maxHealth > 0.0f ? SimMath::pc24DivW((double)body->getHealth(), (double)maxHealth) : 0.0;
}

// RW 0x668303 on the object's AI (+ 0x260): AIUpdate + 0x40 found by id. DONOR: ZH SlavedUpdate::update reads getCurrentVictim at the call site RotWK's
// SlavedUpdate makes with RW 0x668303, so it is the AI's current victim (AI-3 reads it so too, S-891)
Object *goalObject(Object &obj)
{
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	return ai ? ai->currentVictim() : nullptr;
}

// RW 0xC1676C (the controlling player's relationship to the candidate's team in `mask`: ENEMIES 4, NEUTRAL 8, ALLIES 2) and RW 0xC10E20 (alive) around the
// object within `radius`, distance type 0, near to far (RW 0xA39340 with order 1)
PartitionHits scan(Object &obj, float radius, unsigned mask)
{
	const Player *player = obj.getControllingPlayer();
	PartitionFilterFn relation([&](Object &o) {
		if (!player)
		{
			return false;
		}
		switch (player->getRelationship(o.getTeam()))
		{
		case ENEMIES:
			return (mask & 4u) != 0;
		case NEUTRAL:
			return (mask & 8u) != 0;
		case ALLIES:
			return (mask & 2u) != 0;
		}
		return false;
	});
	PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); });
	return obj.logic().partition().iterateObjectsInRange(*obj.getPosition(), radius, FROM_CENTER_2D, { &relation, &alive }, ITER_SORTED_NEAR_TO_FAR);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#define AS_OFF(m) (int)offsetof(AISpecialPowerUpdateModuleData, m)
const FieldParse kAISpecialPowerUpdate[] = { // RW 0xC6DAD0
	{ "CommandButtonName", INI::parseAsciiString, nullptr, AS_OFF(m_commandButtonName) },
	{ "SpecialPowerAIType", AISpecialPowerUpdateModuleData::parseSpecialPowerAIType, nullptr, AS_OFF(m_specialPowerAIType) },
	{ "SpecialPowerRadius", INI::parseReal, nullptr, AS_OFF(m_specialPowerRadius) },
	{ "SpecialPowerRange", INI::parseReal, nullptr, AS_OFF(m_specialPowerRange) },
	{ "RandomizeTargetLocation", INI::parseBool, nullptr, AS_OFF(m_randomizeTargetLocation) },
	{ "SpellMakesAStructure", INI::parseBool, nullptr, AS_OFF(m_spellMakesAStructure) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef AS_OFF
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace

void AISpecialPowerUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAISpecialPowerUpdate);
}

void AISpecialPowerUpdateModuleData::parseSpecialPowerAIType(INI *ini, void *, void *store, const void *)
{
	// RW 0x992693: the next token compared (StringBase::compare) with each name in order; no match stores -1 (no error)
	const std::string token = ini->getNextToken();
	int index = -1;
	for (int i = 0; i < AI_SPECIAL_POWER_TYPE_COUNT; ++i)
	{
		if (token == TheAISpecialPowerTypeNames[i])
		{
			index = i;
			break;
		}
	}
	*static_cast<int *>(store) = index;
}

int AISpecialPowerDecision::kindOf(int type)
{
	// the base class of each type's constructor (RW 0x992724's table): slot 0x18 RW 0x993006 (self), RW 0xA02806 (object) or RW 0xA01C54 (location)
	switch (type)
	{
	case 0: case 2: case 9: case 10: case 16: case 18: case 19: case 38: case 39: case 40: case 48: case 52:
		return KIND_SELF;
	case 1: case 3: case 4: case 5: case 20: case 46: case 51:
		return KIND_OBJECT;
	default:
		return type >= 0 && type < AI_SPECIAL_POWER_TYPE_COUNT ? KIND_LOCATION : -1;
	}
}

bool AISpecialPowerDecision::decisionPorted(int type)
{
	switch (type)
	{
	case BASIC_SELF_BUFF: case CAPTURE_BUILDING: case ENEMY_TYPE_KILLER: case ENEMY_TYPE_KILLER_RANGED: case ENEMY_TYPE_KILLER_STRUCTURES: case TOGGLE_MOUNTED:
	case STANCEBATTLE: case STANCEAGGRESSIVE: case STANCEHOLDGROUND: case BASIC_SELF_DEBUFF: case MORGUL_BLADE: case GOBLIN_POISON:
		return true;
	default:
		return false;
	}
}

AISpecialPowerUpdate::Stats &AISpecialPowerUpdate::stats()
{
	static thread_local Stats s;
	return s;
}

AISpecialPowerUpdate::AISpecialPowerUpdate(Thing *thing, const AISpecialPowerUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	// RW 0x8B703A: + 0x20 / + 0x21 / + 0x24 cleared; the UpdateModule constructor's wake frame (the next frame)
}

float AISpecialPowerUpdate::healthRatio(const Object &obj)
{
	static const int kHorde = kind("HORDE");
	if (hasKind(obj, kHorde))
	{
		// RW 0x68C866: the contain's horde interface (Object + 0x258, vslot 0x7C), its slot 0x264
		if (const HordeContain *hc = dynamic_cast<const HordeContain *>(obj.getContain()))
		{
			return hc->averageMemberHealthRatio();
		}
		return 0.0f; // INFERENCE: a HORDE without a horde contain (retail would call through a null interface)
	}
	return SimMath::fstpDword(bodyRatioWide(obj)); // fstp dword
}

void AISpecialPowerUpdate::init()
{
	// RW 0x8B714F
	Object *obj = getObject();
	const CommandButton *found = nullptr;
	if (const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(obj->getCommandSetName()) : nullptr) // RW 0x69156B / 0x71EFA2
	{
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i) // 0x21 slots, RW 0x80C837
		{
			const CommandButton *b = set->getCommandButton(i);
			if (b && b->m_name == m_data->m_commandButtonName) // RW 0x4065AA
			{
				found = b;
				break;
			}
		}
	}
	int powerType = 0;
	float range = 0.0f;
	if (found)
	{
		m_active = true;
		const SpecialPowerTemplate *t = TheSpecialPowerStore && !found->m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(found->m_specialPowerName) : nullptr;
		if (t)
		{
			powerType = t->getSpecialPowerType(); // RW 0x688D3C -> + 0x1C
			for (const std::unique_ptr<BehaviorModule> &m : obj->modules()) // RW 0x68C461: behaviour slot 0x64, its vslot 4, the power's type (RW 0x851D10)
			{
				const SpecialAbilityUpdate *u = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
				const SpecialPowerTemplate *ut = u ? u->abilityData()->m_specialPowerTemplate : nullptr;
				if (ut && ut->getSpecialPowerType() == powerType)
				{
					range = u->abilityData()->m_startAbilityRange; // RW 0x8513DD: module data + 0x4C
					break;
				}
			}
		}
	}
	// not found: a debug message only (RW 0x8B7213 ..)
	if (m_active)
	{
		if (!m_decision)
		{
			const int k = AISpecialPowerDecision::kindOf(m_data->m_specialPowerAIType); // RW 0x992724
			if (k >= 0)
			{
				m_decision = std::make_unique<AISpecialPowerDecision>();
				m_decision->type = m_data->m_specialPowerAIType;
				m_decision->kind = k;
			}
		}
		if (!m_decision)
		{
			m_active = false; // "Special power type is invalid" (a debug message)
		}
		else
		{
			m_decision->button = found;
			if (0.0f < range)
			{
				m_decision->range = range;
			}
			if (0.0f < m_data->m_specialPowerRadius)
			{
				m_decision->radius = m_data->m_specialPowerRadius;
			}
			if (0.0f < m_data->m_specialPowerRange)
			{
				m_decision->range = m_data->m_specialPowerRange;
			}
			m_decision->powerType = powerType;
		}
	}
	m_initialized = true;
}

bool AISpecialPowerUpdate::powerReady(const SpecialPowerTemplate *t)
{
	// RW 0x8B708B
	Object *obj = getObject();
	static const int kLeavingFactory = statusBit("IS_LEAVING_FACTORY"); // status 90
	if (kLeavingFactory >= 0 && obj->testStatus((unsigned)kLeavingFactory))
	{
		return false;
	}
	if (!SpecialPowerModules::canUseSpecialPower(*obj, t)) // RW 0x7B1D79
	{
		return false;
	}
	SpecialPowerModuleInterface *m = SpecialPowerModules::findModule(*obj, t); // RW 0x68C26D
	if (!m)
	{
		m_active = false;
		return false;
	}
	return m->isReady(); // interface vslot 4
}

UpdateSleepTime AISpecialPowerUpdate::update()
{
	// RW 0x8B73B7
	if (!m_initialized)
	{
		init();
	}
	if (!m_active)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *player = obj->getControllingPlayer(); // RW 0x68B678
	if (!player)
	{
		return UPDATE_SLEEP_FOREVER; // INFERENCE: retail reads the player's + 0x754 (every live object has a controlling player)
	}
	if (player->isDefeated()) // RW 0x6AAC4B: Player + 0x754
	{
		m_active = false;
		return UPDATE_SLEEP_NONE;
	}
	if (!m_decision || !logic.skirmishAI().findAI(player->getPlayerIndex())) // RW 0x6A950B
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (!m_decision->button)
	{
		init();
	}
	++m_decision->counter; // slot 1, RW 0x993019 (slot 0x14 is empty for every type ported here)
	const CommandButton *b = m_decision->button;
	if (!b)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore && !b->m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName) : nullptr;
	if (!t || powerReady(t))
	{
		// RW 0x993055: GameLogicRandomValueReal(0, 1) above 0.5 (RW 0xBD869C), then the data flags and the decision
		const float roll = logic.random().getValueReal(0.0f, 1.0f, "AISPecialPower.cpp", 0x71);
		if (!(roll <= 0.5f))
		{
			m_decision->randomize = m_data->m_randomizeTargetLocation;
			m_decision->makesStructure = m_data->m_spellMakesAStructure;
			++stats().asked;
			if (decide(*obj))
			{
				++stats().yes;
				// RW 0x993307: SpecialPowerActivationProbability of the player's difficulty (RW 0x6AA61B)
				const SkirmishAIStore *store = logic.skirmishAI().store();
				const int d = player->getSkirmishDifficulty() < 0 ? 0 : player->getSkirmishDifficulty();
				bool chance = false;
				if (store)
				{
					const AIProbability &p = store->data().difficultyTuning[d < SkirmishAI::DIFFICULTY_COUNT ? d : SkirmishAI::DIFFICULTY_COUNT - 1].specialPower;
					if (1.0f <= SimMath::divf32((float)p.numerator, (float)p.denominator)) // RW 0xBD1908
					{
						chance = true;
					}
					else
					{
						chance = logic.random().getValue(0, p.denominator - 1, "AIDifficulty.cpp", 0x40) < p.numerator;
					}
				}
				if (chance && kExecuteFrames <= m_decision->counter) // RW 0x99302D
				{
					m_decision->counter = 0;
					m_decision->fired = true;
					execute(*obj);
				}
			}
		}
	}
	return UPDATE_SLEEP_NONE; // slot 0x10, RW 0x490AC4
}

bool AISpecialPowerUpdate::decide(Object &obj)
{
	switch (m_decision->type)
	{
	case BASIC_SELF_BUFF:
		return decideSelfBuff(obj);
	case BASIC_SELF_DEBUFF:
		return decideSelfDebuff(obj);
	case GOBLIN_POISON:
	{
		// RW 0x9E9438
		static const int kStructure = kind("STRUCTURE"), kMachine = kind("MACHINE"), kShip = kind("SHIP");
		Object *goal = goalObject(obj);
		if (!goal || hasKind(*goal, kStructure) || hasKind(*goal, kMachine) || hasKind(*goal, kShip))
		{
			return false;
		}
		// RW 0x66137C (x87): (x - gx)^2 ... as fld / fsub / fmul / faddp: dy * dy + dx * dx, compared wide with 10000.0 (RW 0xC8E258)
		const Coord3D *p = obj.getPosition(), *q = goal->getPosition();
		const double dx = SimMath::pc24SubW((double)p->x, (double)q->x), dy = SimMath::pc24SubW((double)p->y, (double)q->y);
		return SimMath::pc24AddW(SimMath::pc24MulW(dy, dy), SimMath::pc24MulW(dx, dx)) < 10000.0;
	}
	case STANCEBATTLE:
		return healthRatio(obj) <= 0.35f; // RW 0x9E6C47, RW 0xC71EC8
	case STANCEAGGRESSIVE:
	{
		// RW 0x9E6BA3
		const StancesBehavior *s = StancesBehavior::of(obj); // RW 0x861E6E / 0x68BDA5
		if (!s || StancesBehavior::stanceClass(s->getStance()) == 2)
		{
			return false;
		}
		return healthRatio(obj) > 0.35f;
	}
	case STANCEHOLDGROUND:
	case CAPTURE_BUILDING:
		return false; // RW 0x7FEAC1
	case ENEMY_TYPE_KILLER:
	case ENEMY_TYPE_KILLER_RANGED:
	case ENEMY_TYPE_KILLER_STRUCTURES:
	case MORGUL_BLADE:
		return decideKiller(obj);
	case TOGGLE_MOUNTED:
		return decideToggleMounted(obj);
	default:
		++stats().unportedDecisions;
		obj.logic().noteStop(kStop);
		return false;
	}
}

bool AISpecialPowerUpdate::decideSelfBuff(Object &obj)
{
	// RW 0x9E9488
	if (!goalObject(obj))
	{
		return false;
	}
	const float r = healthRatio(obj);
	if (0.5f > r) // RW 0xC8E250
	{
		return true;
	}
	if (0.8f <= r) // RW 0xC8E24C
	{
		return false;
	}
	unsigned enemies = 0, others = 0;
	for (const PartitionHit &hit : scan(obj, 50.0f, 6u)) // RW 0xC8E254
	{
		if (obj.getRelationship(*hit.object) == ENEMIES) // RW 0x68D7AB; slots 0x20 / 0x24 answer yes (RW 0x9B501B)
		{
			++enemies;
		}
		else
		{
			++others;
		}
	}
	return !(enemies < others);
}

bool AISpecialPowerUpdate::decideSelfDebuff(Object &obj)
{
	// RW 0x9EAF26: the threat sums, each step fild (unsigned) + the threat value in the FPU, _ftol2's low word
	if (!goalObject(obj))
	{
		return false;
	}
	std::uint32_t enemies = 0, others = 0;
	for (const PartitionHit &hit : scan(obj, m_decision->range, 6u))
	{
		const double v = AIThreatFinder::threatValueWide(*hit.object); // RW 0x68F0EC
		if (obj.getRelationship(*hit.object) == ENEMIES)
		{
			enemies = SimMath::ftol2Low32(SimMath::pc24AddW(SimMath::fildU32(enemies), v));
		}
		else
		{
			others = SimMath::ftol2Low32(SimMath::pc24AddW(SimMath::fildU32(others), v));
		}
	}
	return others < enemies;
}

bool AISpecialPowerUpdate::decideKiller(Object &obj)
{
	// RW 0x9EC0A7
	if (!goalObject(obj)) // RW 0x9930A3
	{
		return m_decision->target != INVALID_ID && obj.logic().findObjectByID(m_decision->target);
	}
	GameLogic &logic = obj.logic();
	m_decision->target = INVALID_ID; // RW 0xA027D5(0)
	static const int kHero = kind("HERO"), kMonster = kind("MONSTER"), kBigMonster = kind("BIG_MONSTER"), kCreateAHero = kind("CREATE_A_HERO"),
					 kStructure = kind("STRUCTURE"), kInfantry = kind("INFANTRY"), kIgnoreForVictory = kind("IGNORE_FOR_VICTORY");
	const int type = m_decision->type;
	auto inMask = [&](const Object &c) { // slot 0x20 (RW 0x9EC08A / 0x9EBC5A / 0x9EC09A) and RW 0x70C548
		if (type == ENEMY_TYPE_KILLER_STRUCTURES)
		{
			return hasKind(c, kStructure);
		}
		if (type == MORGUL_BLADE)
		{
			return hasKind(c, kHero) || hasKind(c, kInfantry) || hasKind(c, kCreateAHero);
		}
		return hasKind(c, kHero) || hasKind(c, kMonster) || hasKind(c, kBigMonster) || hasKind(c, kCreateAHero);
	};
	auto accepts = [&](const Object &c) { // slot 0x24
		if (type == ENEMY_TYPE_KILLER_RANGED)
		{
			// RW 0x9EBFF7 (SSE): (c - obj) y^2 + x^2 above 10000.0 (RW 0xBDE8B8)
			const Coord3D *p = obj.getPosition(), *q = c.getPosition();
			const float dy = SimMath::subf32(q->y, p->y), dx = SimMath::subf32(q->x, p->x);
			return SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)) > 10000.0f;
		}
		if (type == ENEMY_TYPE_KILLER_STRUCTURES)
		{
			return !hasKind(c, kIgnoreForVictory) && c.getProductionUpdate() != nullptr; // RW 0x9EBC5F, RW 0x68C363
		}
		return true; // RW 0x8470FA
	};
	for (const PartitionHit &hit : scan(obj, m_decision->range, 4u))
	{
		Object *c = hit.object;
		if (!inMask(*c) || !accepts(*c))
		{
			continue;
		}
		if (Object *current = logic.findObjectByID(m_decision->target)) // RW 0x8DBACE
		{
			const float candRatio = SimMath::fstpDword(bodyRatioWide(*c)); // fstp dword
			const float targetRatio = SimMath::fstpDword(bodyRatioWide(*current)); // INFERENCE: the FPU value (PC24) is the float's
			if (targetRatio <= candRatio)
			{
				continue;
			}
		}
		m_decision->target = c->getID(); // RW 0xA027D5: + 0x74
	}
	return logic.findObjectByID(m_decision->target) != nullptr;
}

bool AISpecialPowerUpdate::decideToggleMounted(Object &obj)
{
	// RW 0x9EBE42
	const Player *player = obj.getControllingPlayer();
	Team *team = obj.getTeam();
	if (!player || team == player->getDefaultTeam()) // + 0x31C against Player + 0x30C
	{
		return false;
	}
	static const int kHorde = kind("HORDE");
	static const int kMounted = Construction::modelConditionIndex("MOUNTED"); // 0xD6
	int cavalry = 0, others = 0;
	for (Object *m = team ? team->getFirstMember() : nullptr; m; m = m->friend_teamNext()) // RW 0x66362D / 0x6632C9
	{
		const Object *container = m->getContainedBy(); // + 0x27C
		if (container && hasKind(*container, kHorde))
		{
			continue;
		}
		if (AIThreatFinder::threatCategory(*m->getTemplate()) == 3) // template + 0x530: AIKindOf CAVALRY
		{
			++cavalry;
		}
		else
		{
			++others;
		}
	}
	if (obj.testModelCondition(kMounted))
	{
		return others > cavalry;
	}
	return cavalry > others;
}

void AISpecialPowerUpdate::execute(Object &obj)
{
	// slot 0x18
	if (m_decision->type >= 0 && m_decision->type < AI_SPECIAL_POWER_TYPE_COUNT)
	{
		++stats().executedByType[(size_t)m_decision->type];
	}
	++stats().executed;
	switch (m_decision->kind)
	{
	case AISpecialPowerDecision::KIND_OBJECT:
	{
		// RW 0xA02806: RW 0x697890(button, the target found by id, 1, 0), then + 0x20 = 0
		Object *target = obj.logic().findObjectByID(m_decision->target);
		runButton(obj, target, nullptr);
		m_decision->target = INVALID_ID;
		break;
	}
	case AISpecialPowerDecision::KIND_LOCATION:
		runButton(obj, nullptr, &m_decision->location); // RW 0xA01C54
		break;
	default:
		runButton(obj, nullptr, nullptr); // RW 0x993006
		break;
	}
}

void AISpecialPowerUpdate::runButton(Object &obj, Object *target, const Coord3D *loc)
{
	// RW 0x696FD2 / 0x697890 / 0x6979D9 with source 1 (CMD_FROM_SCRIPT) and no auto flag
	const CommandButton *b = m_decision->button;
	if (!b || obj.getDisabledMask() != 0)
	{
		return; // RW 0x9325B4: a disabled object does nothing (Object + 0xA0 bit 6 taken as clear)
	}
	if (b->m_command == GUI_COMMAND_SET_STANCE && !target && !loc)
	{
		// RW 0x696FD2 case 0x3A: the object's StancesBehavior (RW 0x861E6E / 0x68BDA5) takes the button's first stance (RW 0x75D324(0)); the debug checks aside
		if (StancesBehavior *s = StancesBehavior::of(obj))
		{
			if (!b->m_stances.empty() && b->m_stances[0] >= 0 && b->m_stances[0] < STANCE_COUNT)
			{
				s->setStance((StanceType)b->m_stances[0]);
			}
		}
		return;
	}
	if (b->m_command != GUI_COMMAND_SPECIAL_POWER && b->m_command != GUI_COMMAND_SPELL_BOOK)
	{
		++stats().unportedCommands;
		obj.logic().noteStop(kStop);
		return;
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore && !b->m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b->m_specialPowerName) : nullptr;
	if (!t)
	{
		return; // a button without a power (+ 0x44 null) does nothing
	}
	const unsigned options = b->m_options | 0x40000u;
	if (loc)
	{
		SpecialPowerModules::doSpecialPowerAtLocation(obj, t, *loc, options, true); // force: source == 1
	}
	else if (target)
	{
		SpecialPowerModules::doSpecialPowerAtObject(obj, t, target, options, true);
	}
	else if (m_decision->kind != AISpecialPowerDecision::KIND_OBJECT)
	{
		SpecialPowerModules::doSpecialPower(obj, t, options, true);
	}
}

void AISpecialPowerUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_active);
	h.addBool(m_initialized);
	h.addBool(m_decision != nullptr);
	if (m_decision)
	{
		const AISpecialPowerDecision &d = *m_decision;
		h.addI32(d.type);
		h.addI32(d.kind);
		h.addI32(d.powerType);
		h.addString(d.button ? d.button->m_name : std::string());
		h.addU32(d.counter);
		h.addFloat(d.range);
		h.addFloat(d.radius);
		h.addBool(d.fired);
		h.addBool(d.randomize);
		h.addBool(d.makesStructure);
		h.addU32((std::uint32_t)d.target);
		h.addFloat(d.location.x);
		h.addFloat(d.location.y);
		h.addFloat(d.location.z);
	}
}

void AISpecialPowerUpdate::registerClass(ModuleFactory &modules)
{
	const char *name = "AISpecialPowerUpdate";
	modules.bindTypedData<AISpecialPowerUpdateModuleData>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const AISpecialPowerUpdateModuleData *typed = dynamic_cast<const AISpecialPowerUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<AISpecialPowerUpdate>(thing, typed);
	});
}

std::vector<std::string> AISpecialPowerUpdate::stopLines()
{
	return { kStop };
}
