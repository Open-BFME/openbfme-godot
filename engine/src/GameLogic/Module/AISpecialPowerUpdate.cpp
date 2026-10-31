// OpenBFME. GPL-3.0.
// See GameLogic/Module/AISpecialPowerUpdate.h for the sources and what is inference.

#include "GameLogic/Module/AISpecialPowerUpdate.h"
#include "GameLogic/SnapshotXfer.h"

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
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AISpecialPowerInternal.h"
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
#include "GameLogic/WeaponSetToggle.h"

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

using namespace AISpecialPowerDetail;

namespace
{
const char *const kStop =
	"[S-1421] AISpecialPowerUpdate (lanes MOD-4, AIPOW-1): the module (RW 0x8B73B7 / 0x8B714F / 0x8B708B), the decision framework (RW 0x993055 / 0x993307 / "
	"0x99302D), the location base (RW 0xA021D8 / 0xA0229F / 0xA01C9C with the ActionManager's test RW 0x75CF21) and 42 of the 53 decisions run; not ported "
	"(they answer no, counted): SOUL_FREEZE (RW 0x9E9B37) and ATTACK_HEAL_AOE (RW 0x9E691D: the team's special power registry Team + 0x140 and its order "
	"list Team + 0x13C, RW 0x7A3EF5 / 0x7A5582 / 0x7A1C5E, not ported); spell book HEAL (RW 0x9E85C6: the ZH AIPlayer's list, Player + 0x2FC -> + 0x4, "
	"entries of type 0x1A); ARMY_BREAKER (RW 0x9E92CA: the brain's army trackers, AI + 0x164 -> + 0x20); STRUCTURE_BREAKER, its _PREF_WALLS and "
	"_BASEKILL, CITADEL (RW 0x9E7A98 / 0x9E7942 / 0x9E6EF4 / 0x9E6CCB: the enemy record's structure cluster table, record + 0x10, RW 0x8E4042 / 0xA024DA); "
	"DEBUFFECONOMYBUILDING / DEBUFFPRODUCTIONBUILDING (RW 0x9E74B7 / 0x9E726A: the ZH AISkirmishPlayer's enemy, Player + 0x2FC vslot 0x30 -> RW "
	"0x8F47EB acquireEnemy); TREE_KILLER (RW 0x9E70B6: TheTerrainLogic + 0x584's list); the per-frame steering of a summoned helper (slot 1 of "
	"TARGETAOE_SUMMON RW 0x9EB961, ASSIST_BATTLE_* RW 0x9E8A42, SHROUD_REVEAL RW 0x9E7F0F: the helper is found through the record's army and hero lists, "
	"RW 0x99316A, and steered by Object + 0xA4's goal position, RW 0x68B411; the counters run); INFERENCE: an unset SpecialPowerAIType is -1, the map "
	"object re-init (behaviour slot 0xB4, RW 0x8B7554) is the first update's, the AI's goal object (AIUpdate + 0x40) is the AI's current victim (ZH donor), "
	"only SpecialAbilityUpdate answers RW 0x68C461's vslot 4, Object + 0xA0 bit 6 (the disabled exception of RW 0x696FD2) is clear, the player info "
	"record lists are the live objects in id order (S-418), ASSIST_BATTLE's timer list (RW 0xDEC154) belongs to the game, the battle stats sum in the "
	"partition's unsorted order";

const char *const kStopSpellBook =
	"[S-1875] computer players' science purchases (lane AI5): retail RotWK has no skirmish AI science buyer. Player::attemptToPurchaseScience RW 0x6AE36F is "
	"called only by the script action PLAYER_PURCHASE_SCIENCE (RW 0x7BD210), MSG_PURCHASE_SCIENCE (RW 0x779A3D) and ZH's AIPlayer::doUpgradesAndSkills "
	"(RW 0x8F7B3B, AIData SideInfo SkillSet1..5), which RW 0x8F9705 runs only for a computer player without a skirmish AI; the AIPlayer + 0x12C class "
	"(RW 0x99FB84, AIUpgradeScienceBuilder) buys structure upgrades and expansions (ported, AIUpgrade.h). So a skirmish AI's sciences come from its "
	"scripts as in retail. Not ported: doUpgradesAndSkills for a computer player without a skirmish AI (retail AIData defines no SkillSet: it buys and "
	"draws nothing; a mod's SkillSets would make such players buy sciences)";

const unsigned kExecuteFrames = 5u * LOGICFRAMES_PER_SECOND; // RW 0x993024: [0xD9F608] * 5

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
	case AISP_ELENDIL: case AISP_SELFAOEHEALHEROS: case AISP_GOBLINKING_MOUNTED: case AISP_TOGGLE_MELEE_AND_RANGE: case AISP_TOGGLE_SIEGE: case AISP_CHARGE:
	case AISP_DOMINATE_TROLL: case AISP_DOMINATE_ENEMY: case AISP_TAME_THE_BEAST: case AISP_GANDALF_WIZARD_BLAST: case AISP_GIVEXP_AOE: case AISP_RANGED_AOE_ATTACK:
	case AISP_LEGOLAS_ARROWWIND: case AISP_RANGED_AOE_ATTACK_UNITS: case AISP_AOE_AND_BUFF: case AISP_LEGOLAS_TRAINARCHERS: case AISP_GOBLINKING_BATTLEFRENZY:
	case AISP_GOBLINKING_CALLOFTHEDEEP: case AISP_TARGETAOE_SUMMON: case AISP_HEAL_AOE: case AISP_SPELLBOOK_ALWAYS_FIRE: case AISP_SPELLBOOK_CAPTURE_CREEP:
	case AISP_SPELLBOOK_SHROUD_REVEAL: case AISP_SPELLBOOK_ASSIST_BATTLE_BUFF: case AISP_SPELLBOOK_ASSIST_BATTLE_DEBUFF: case AISP_SPELLBOOK_BUFFTERRAIN:
	case AISP_SPELLBOOK_REBUILD: case AISP_SPELLBOOK_ENSHROUDINGMIST: case AISP_SPELLBOOK_CALLTHEHORDE: case AISP_SPELLBOOK_BUFFECONOMYBUILDING:
	case AISP_BASIC_SELF_BUFF: case AISP_CAPTURE_BUILDING: case AISP_ENEMY_TYPE_KILLER: case AISP_ENEMY_TYPE_KILLER_RANGED: case AISP_ENEMY_TYPE_KILLER_STRUCTURES: case AISP_TOGGLE_MOUNTED:
	case AISP_STANCEBATTLE: case AISP_STANCEAGGRESSIVE: case AISP_STANCEHOLDGROUND: case AISP_BASIC_SELF_DEBUFF: case AISP_MORGUL_BLADE: case AISP_GOBLIN_POISON:
		return true;
	default:
		return false;
	}
}

bool &AISpecialPowerUpdate::mod4DecisionsOnlyForTest()
{
	static thread_local bool on = false;
	return on;
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
	if (!found && m_assumeButtonForTest && TheCommandStore)
	{
		found = TheCommandStore->findCommandButton(m_data->m_commandButtonName); // tests only: the button by name, as if the set held it
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
				// the constructor's argument (RW 0x992724: 0x9E95F1(2) for DOMINATE_TROLL, 0x9E983E(0 / 1) for DOMINATE_ENEMY / TAME_THE_BEAST)
				m_decision->variant = m_decision->type == AISP_DOMINATE_TROLL ? 2 : ((m_decision->type == AISP_TAME_THE_BEAST || m_decision->type == AISP_SPELLBOOK_ASSIST_BATTLE_DEBUFF) ? 1 : 0); // RW 0x9E88B3(0 / 1) for ASSIST_BATTLE_BUFF / _DEBUFF
				if (m_decision->type == AISP_SPELLBOOK_SHROUD_REVEAL && obj->logic().terrain())
				{
					// RW 0x9E7B8E: "Player_%d_Start" (RW 0xBFDA18) for d = 1 .. 8 in TheTerrainLogic's waypoint list (vslot 0x84, compared by name); the
					// first name the map lacks ends the list
					for (int d = 1; d <= 8; ++d)
					{
						const Waypoint *w = obj->logic().terrain()->findWaypointByName("Player_" + std::to_string(d) + "_Start");
						if (!w)
						{
							break;
						}
						m_decision->waypointIds.push_back(w->id);
					}
				}
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

void AISpecialPowerUpdate::initAsIfInCommandSetForTest()
{
	if (!m_initialized)
	{
		m_assumeButtonForTest = true;
		init();
		m_assumeButtonForTest = false;
	}
}

bool AISpecialPowerUpdate::decideNow()
{
	if (!m_initialized)
	{
		init();
	}
	if (!m_decision)
	{
		return false;
	}
	m_decision->randomize = m_data->m_randomizeTargetLocation;
	m_decision->makesStructure = m_data->m_spellMakesAStructure;
	return decide(*getObject());
}

bool AISpecialPowerUpdate::decide(Object &obj)
{
	if (mod4DecisionsOnlyForTest())
	{
		switch (m_decision->type)
		{
		case AISP_BASIC_SELF_BUFF: case AISP_CAPTURE_BUILDING: case AISP_ENEMY_TYPE_KILLER: case AISP_ENEMY_TYPE_KILLER_RANGED: case AISP_ENEMY_TYPE_KILLER_STRUCTURES:
		case AISP_TOGGLE_MOUNTED: case AISP_STANCEBATTLE: case AISP_STANCEAGGRESSIVE: case AISP_STANCEHOLDGROUND: case AISP_BASIC_SELF_DEBUFF: case AISP_MORGUL_BLADE:
		case AISP_GOBLIN_POISON:
			break;
		default:
			++stats().unportedDecisions;
			return false;
		}
	}
	switch (m_decision->type)
	{
	case AISP_BASIC_SELF_BUFF:
		return decideSelfBuff(obj);
	case AISP_BASIC_SELF_DEBUFF:
		return decideSelfDebuff(obj);
	case AISP_GOBLIN_POISON:
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
	case AISP_STANCEBATTLE:
		return healthRatio(obj) <= 0.35f; // RW 0x9E6C47, RW 0xC71EC8
	case AISP_STANCEAGGRESSIVE:
	{
		// RW 0x9E6BA3
		const StancesBehavior *s = StancesBehavior::of(obj); // RW 0x861E6E / 0x68BDA5
		if (!s || StancesBehavior::stanceClass(s->getStance()) == 2)
		{
			return false;
		}
		return healthRatio(obj) > 0.35f;
	}
	case AISP_STANCEHOLDGROUND:
	case AISP_CAPTURE_BUILDING:
		return false; // RW 0x7FEAC1
	case AISP_ENEMY_TYPE_KILLER:
	case AISP_ENEMY_TYPE_KILLER_RANGED:
	case AISP_ENEMY_TYPE_KILLER_STRUCTURES:
	case AISP_MORGUL_BLADE:
		return decideKiller(obj);
	case AISP_TOGGLE_MOUNTED:
		return decideToggleMounted(obj);
	// ---- lane AIPOW-1 (AISpecialPowerDecisions.cpp) ----
	case AISP_ELENDIL:
		return decideElendil(obj);
	case AISP_SELFAOEHEALHEROS:
		return decideSelfAoeHealHeros(obj);
	case AISP_GOBLINKING_MOUNTED:
		return !obj.testModelCondition(Construction::modelConditionIndex("MOUNTED")); // RW 0x9EA553: not MOUNTED (model condition 0xD6)
	case AISP_TOGGLE_MELEE_AND_RANGE:
		return decideToggleWeaponSet(obj, false);
	case AISP_TOGGLE_SIEGE:
		return decideToggleWeaponSet(obj, true);
	case AISP_CHARGE:
		return decideCharge(obj);
	case AISP_DOMINATE_TROLL:
		return decideDominate(obj, false);
	case AISP_DOMINATE_ENEMY:
	case AISP_TAME_THE_BEAST:
		return decideDominate(obj, true);
	case AISP_GANDALF_WIZARD_BLAST:
	{
		// RW 0x9EBF56: a goal; the ENEMIES cluster (RW 0xA01D72(obj, 0)) above 20, or not empty while the body ratio (slot 0x14, x87) is below 0.3 (RW 0xC8E6E8)
		if (!goalObject(obj))
		{
			return false;
		}
		const unsigned n = clusterScan(obj, m_decision->radius, 4u, COUNT_ALL, false);
		return n > 20u || (n != 0u && bodyRatioWide(obj) < (double)0.3f);
	}
	case AISP_GIVEXP_AOE:
		// RW 0x9EBEED: no goal and an ALLIES cluster (RW 0xA01D72(obj, 1)) that is not empty
		return !goalObject(obj) && clusterScan(obj, m_decision->radius, 2u, COUNT_ALL, false) != 0u;
	case AISP_RANGED_AOE_ATTACK:
		return decideRangedAoe(obj, m_decision->range, false);
	case AISP_LEGOLAS_ARROWWIND:
		return decideRangedAoe(obj, SimMath::mulf32(m_decision->range, 2.0f), false); // RW 0x9EADDD: mulss by 2.0 (RW 0xBD889C)
	case AISP_RANGED_AOE_ATTACK_UNITS:
		return decideRangedAoe(obj, m_decision->range, true);
	case AISP_AOE_AND_BUFF:
		return decideAoeAndBuff(obj);
	case AISP_LEGOLAS_TRAINARCHERS:
		// RW 0x9EAD61: no goal and an ALLIES cluster counting ARCHERs that a location accepts (RW 0x9EAB29)
		return !goalObject(obj) && clusterScan(obj, m_decision->radius, 2u, COUNT_ARCHERS, true) != 0u;
	case AISP_GOBLINKING_BATTLEFRENZY:
	case AISP_GOBLINKING_CALLOFTHEDEEP:
	case AISP_TARGETAOE_SUMMON:
	{
		// RW 0x9EAAC1 / 0x9EA7FE / 0x9EBBF2: a goal and more than one HORDE-or-loose object in the cluster a location accepts (RW 0x9EA866: ALLIES, RW 0x9EA5A3 /
		// 0x9EB706: ENEMIES), offset the range + 0x14, or 200 (RW 0xBE4170) / 149 (RW 0xC8E5B0) when it is not above 0
		if (!goalObject(obj))
		{
			return false;
		}
		const unsigned mask = m_decision->type == AISP_GOBLINKING_BATTLEFRENZY ? 2u : 4u;
		const float range = m_decision->range > 0.0f ? m_decision->range : (m_decision->type == AISP_TARGETAOE_SUMMON ? 149.0f : 200.0f);
		return clusterScan(obj, range, mask, COUNT_HORDES_OR_LOOSE, true) > 1u;
	}
	case AISP_SPELLBOOK_ALWAYS_FIRE:
		return true; // RW 0x9B501B (the location stays where it is)
	case AISP_SPELLBOOK_CAPTURE_CREEP:
		return decideCaptureCreep(obj);
	case AISP_SPELLBOOK_SHROUD_REVEAL:
		return decideShroudReveal(obj);
	case AISP_SPELLBOOK_ASSIST_BATTLE_BUFF:
	case AISP_SPELLBOOK_ASSIST_BATTLE_DEBUFF:
		return decideAssistBattle(obj);
	case AISP_SPELLBOOK_BUFFTERRAIN:
		return decideBuffTerrain(obj);
	case AISP_SPELLBOOK_REBUILD:
		return decideRebuild(obj);
	case AISP_SPELLBOOK_ENSHROUDINGMIST:
		return decideEnshroudingMist(obj);
	case AISP_SPELLBOOK_CALLTHEHORDE:
		return decideCallTheHorde(obj);
	case AISP_SPELLBOOK_BUFFECONOMYBUILDING:
		return decideBuffEconomy(obj);
	case AISP_HEAL_AOE:
	{
		// RW 0x9EA462: a goal and an ALLIES cluster (RW 0xA01F99(obj, 1)) of at least 10; then 0.0 / (the hits of the four points) <= 0.75 (RW 0xC1B594 /
		// 0xBDC628: the numerator is the constant 0.0, so this holds whenever there were hits)
		if (!goalObject(obj))
		{
			return false;
		}
		size_t hits = 0;
		if (clusterScan(obj, m_decision->radius, 2u, COUNT_ALL, false, &hits) < 10u)
		{
			return false;
		}
		return SimMath::pc24DivW(0.0, (double)hits) <= (double)0.75f;
	}
	default:
		++stats().unportedDecisions;
		if (m_decision->type >= 0 && m_decision->type < AI_SPECIAL_POWER_TYPE_COUNT)
		{
			++stats().unportedByType[(size_t)m_decision->type];
		}
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
		if (type == AISP_ENEMY_TYPE_KILLER_STRUCTURES)
		{
			return hasKind(c, kStructure);
		}
		if (type == AISP_MORGUL_BLADE)
		{
			return hasKind(c, kHero) || hasKind(c, kInfantry) || hasKind(c, kCreateAHero);
		}
		return hasKind(c, kHero) || hasKind(c, kMonster) || hasKind(c, kBigMonster) || hasKind(c, kCreateAHero);
	};
	auto accepts = [&](const Object &c) { // slot 0x24
		if (type == AISP_ENEMY_TYPE_KILLER_RANGED)
		{
			// RW 0x9EBFF7 (SSE): (c - obj) y^2 + x^2 above 10000.0 (RW 0xBDE8B8)
			const Coord3D *p = obj.getPosition(), *q = c.getPosition();
			const float dy = SimMath::subf32(q->y, p->y), dx = SimMath::subf32(q->x, p->x);
			return SimMath::addf32(SimMath::mulf32(dy, dy), SimMath::mulf32(dx, dx)) > 10000.0f;
		}
		if (type == AISP_ENEMY_TYPE_KILLER_STRUCTURES)
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
	// RW 0x696FD2 / 0x697890 / 0x6979D9 with source 1 (CMD_FROM_SCRIPT) and no auto flag (lane MODS-3: Object::doCommandButton and its target forms; their
	// disabled gates and every command type are theirs)
	const CommandButton *b = m_decision->button;
	if (loc)
	{
		obj.doCommandButtonAtPosition(b, *loc, CMD_FROM_SCRIPT, false); // RW 0xA01C54
	}
	else if (target)
	{
		obj.doCommandButtonAtObject(b, target, CMD_FROM_SCRIPT, false); // RW 0xA02806
	}
	else if (m_decision->kind != AISpecialPowerDecision::KIND_OBJECT)
	{
		obj.doCommandButton(b, CMD_FROM_SCRIPT, false); // RW 0x993006
	}
	// INFERENCE (S-1421, as before): an object decision whose target is gone reaches RW 0x697890 with a null target; the port's power modules are not
	// handed a null target, so nothing happens
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
		h.addI32(d.variant);
		h.addU32((std::uint32_t)d.assistUnit);
		h.addU32((std::uint32_t)d.assistTarget);
		h.addU32((std::uint32_t)d.buffedIds.size());
		for (ObjectID id : d.buffedIds)
		{
			h.addU32((std::uint32_t)id);
		}
		h.addU32((std::uint32_t)d.waypointIds.size());
		for (int id : d.waypointIds)
		{
			h.addI32(id);
		}
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
	return { kStop, kStopSpellBook };
}

// ---- lane SAVE-4b: the snapshot (OpenBFME native, S-4190; retail's AISpecialPowerUpdate::xfer RW 0x8B74A6 (1, 4) holds two bools after UpdateModule::xfer
// and no decision) -----------------------------------------------------------------------------------------------------------------------------------------
// The decision whole when it exists, its command button by name (TheCommandStore's: the button the object's command set holds). The test switch
// m_assumeButtonForTest is a test harness setting, not state.
void AISpecialPowerUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferBool(&m_active);
	x.xferBool(&m_initialized);
	bool has = !x.isLoading() && m_decision != nullptr;
	x.xferBool(&has);
	if (x.isLoading())
	{
		m_decision.reset();
		if (has)
		{
			m_decision = std::make_unique<AISpecialPowerDecision>();
		}
	}
	if (!m_decision)
	{
		return;
	}
	AISpecialPowerDecision &d = *m_decision;
	x.xferInt(&d.type);
	x.xferInt(&d.kind);
	if (x.isLoading() && (AISpecialPowerDecision::kindOf(d.type) < 0 || AISpecialPowerDecision::kindOf(d.type) != d.kind))
	{
		throw XferException("an AI special power decision of type " + std::to_string(d.type) + " and kind " + std::to_string(d.kind)
			+ " is not one this build makes");
	}
	x.xferInt(&d.powerType);
	std::string button = (!x.isLoading() && d.button) ? d.button->m_name : std::string();
	x.xferAsciiString(&button);
	if (x.isLoading())
	{
		d.button = button.empty() ? nullptr : (TheCommandStore ? TheCommandStore->findCommandButton(button) : nullptr);
		if (!button.empty() && !d.button)
		{
			throw XferException("the snapshot's AI special power decision names the command button '" + button + "', which this game's data lacks");
		}
	}
	x.xferUnsignedInt(&d.counter);
	x.xferReal(&d.range);
	x.xferReal(&d.radius);
	x.xferBool(&d.fired);
	x.xferBool(&d.randomize);
	x.xferBool(&d.makesStructure);
	x.xferObjectID(&d.target);
	SnapshotXfer::coord3D(x, d.location);
	SnapshotXfer::ints(x, d.waypointIds);
	for (int id : d.waypointIds) // lane SAVE-4e review r2: each a waypoint of this map (the shroud reveal looks it up, RW 0x9E7CA3)
	{
		const TerrainLogic *terrain = getObject()->logic().terrain();
		if (x.isLoading() && (!terrain || !terrain->findWaypointById(id)))
		{
			throw XferException("an AI special power decision's waypoint " + std::to_string(id) + " is not a waypoint of this map");
		}
	}
	SnapshotXfer::enumIn(x, d.variant, 0, 2, "an AI special power decision's variant");
	x.xferObjectID(&d.assistUnit);
	x.xferObjectID(&d.assistTarget);
	SnapshotXfer::ids(x, d.buffedIds);
}
