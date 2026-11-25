// OpenBFME. GPL-3.0.
// See GameLogic/Module/ActiveBody.h.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Contain/HordeFlank.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/Object/Object.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/SnapshotXfer.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Armor.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/Damage.h"
#include "GameLogic/DamageFX.h"
#include "GameLogic/Module/DamageModule.h"
#include "Common/PlayerList.h"
#include "GameLogic/Module/BloodthirstyUpdate.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Weapon.h"
#include "Common/AsciiString.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>
#include <stdexcept>

namespace
{
// RW 0x73A302 (parseFXList): the token is stored; retail looks it up and rejects an unknown name other than None (the FXList store is
// not loaded: stop S-149)
void parseFXName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

// RW 0xDB2B04 (theDamageSideNames, the stage of a DamageCreationList entry and of the body's + 0x34)
const char *const kDamageSideNames[] = { "NONE", "FRONT_DESTROYED", "RIGHT_DESTROYED", "BACK_DESTROYED", "LEFT_DESTROYED", nullptr };

// lane BLDGDEBRIS-1: RW 0x8C3F1F (BFME2 0x4BFD83, tier A; decomp ActiveBody.cpp:2477-2496 Rva004BFD83Parse): the OCL (RW 0x73A368: a name, "None" is
// none), then two optional tokens scanned (RW 0x42B999, an unknown name is an INI error) in the rock names (RW 0xDB2AF4 = TheFXTriggerNames) and the
// side names (RW 0xDB2B04); an absent token is 0; the proc reads no further token (the rest of the line is left unread, as every field proc leaves it). The
// entry is appended (RW 0x6E0A42 on data + 0x58)
void parseDamageCreation(INI *ini, void *, void *store, const void *)
{
	ActiveBodyModuleData::DamageCreation entry;
	const std::string name = ini->getNextToken();
	entry.ocl = AsciiStringUtil::compareNoCase(name, "None") == 0 ? std::string() : name;
	if (const char *t = ini->getNextTokenOrNull())
	{
		entry.rock = INI::scanIndexList(t, TheFXTriggerNames);
	}
	if (const char *t = ini->getNextTokenOrNull())
	{
		entry.stage = INI::scanIndexList(t, kDamageSideNames);
	}
	static_cast<std::vector<ActiveBodyModuleData::DamageCreation> *>(store)->push_back(std::move(entry));
}

// lane OWNERFIX-1: HealingBuffFx (RW 0x73A302, + 0x48) replaces the GameData default the constructor took (RW 0x8C373F); "None" stores none
void parseHealingBuffFx(INI *ini, void *data, void *store, const void *)
{
	parseFXName(ini, data, store, nullptr);
	static_cast<ActiveBodyModuleData *>(data)->m_healingBuffFxSet = true;
}

#define AB_OFF(member) (int)offsetof(ActiveBodyModuleData, member)
const FieldParse kActiveBodyFieldParse[] = {
	{ "MaxHealth", INI::parseReal, nullptr, AB_OFF(m_maxHealth) },
	{ "MaxHealthDamaged", INI::parseReal, nullptr, AB_OFF(m_maxHealthDamaged) },
	{ "MaxHealthReallyDamaged", INI::parseReal, nullptr, AB_OFF(m_maxHealthReallyDamaged) },
	{ "InitialHealth", INI::parseReal, nullptr, AB_OFF(m_initialHealth) },
	{ "RecoveryTime", INI::parseDurationUnsignedInt, nullptr, AB_OFF(m_recoveryTime) },
	{ "DodgePercent", INI::parsePercentToReal, nullptr, AB_OFF(m_dodgePercent) },
	{ "EnteringDamagedTransitionTime", INI::parseDurationUnsignedInt, nullptr, AB_OFF(m_enteringDamagedTransitionTime) },
	{ "EnteringReallyDamagedTransitionTime", INI::parseDurationUnsignedInt, nullptr, AB_OFF(m_enteringReallyDamagedTransitionTime) },
	{ "GrabObject", INI::parseAsciiString, nullptr, AB_OFF(m_grabObject) },
	{ "GrabFX", parseFXName, nullptr, AB_OFF(m_grabFX) },
	{ "GrabDamage", INI::parseReal, nullptr, AB_OFF(m_grabDamage) },
	{ "GrabOffset", INI::parseCoord2D, nullptr, AB_OFF(m_grabOffset) },
	{ "UseDefaultDamageSettings", INI::parseBool, nullptr, AB_OFF(m_useDefaultDamageSettings) },
	{ "DamageCreationList", parseDamageCreation, nullptr, AB_OFF(m_damageCreationList) },
	{ "HealingBuffFx", parseHealingBuffFx, nullptr, AB_OFF(m_healingBuffFx) },
	{ "DamagedAttributeModifier", INI::parseAsciiString, nullptr, AB_OFF(m_damagedAttributeModifier) },
	{ "ReallyDamagedAttributeModifier", INI::parseAsciiString, nullptr, AB_OFF(m_reallyDamagedAttributeModifier) },
	{ "CheerRadius", INI::parseReal, nullptr, AB_OFF(m_cheerRadius) },
	{ "RemoveUpgradesOnDeath", INI::parseBool, nullptr, AB_OFF(m_removeUpgradesOnDeath) },
	{ "BurningDeathBehavior", INI::parseBool, nullptr, AB_OFF(m_burningDeathBehavior) },
	{ "BurningDeathFX", parseFXName, nullptr, AB_OFF(m_burningDeathFX) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AB_OFF
} // namespace

void ActiveBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kActiveBodyFieldParse);
}

// RW 0x8C3841
ActiveBody::ActiveBody(Thing *thing, const ActiveBodyModuleData *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
	m_initialHealth = data->m_initialHealth; // +0x1C = data + 0xC
	m_maxHealth = data->m_maxHealth;         // +0x20 = data + 0x8
	const float inverse = SimMath::divf32(1.0f, m_maxHealth); // RW 0x8C392B divss
	m_damagedFraction = SimMath::mulf32(data->m_maxHealthDamaged, inverse); // RW 0x8C3930 mulss
	m_reallyDamagedFraction = SimMath::mulf32(data->m_maxHealthReallyDamaged, inverse); // RW 0x8C393E mulss
	m_currentHealth = data->m_initialHealth == -1.0f ? data->m_maxHealth : data->m_initialHealth;
	m_previousHealth = m_currentHealth;
	m_reviveReference = m_currentHealth; // lane HERO-1: module + 0x2C starts at the actual initial health
	m_curDamageState = BODY_PRISTINE; // RW 0x8C3841 ends with setCorrectDamageState: its effects need the finished object, so applyInitialDamageState runs after Object::buildModules
	if (data->m_useDefaultDamageSettings)
	{
		const GameLogicSettings &settings = getObject()->logic().settings();
		if ((m_damagedFraction == 0.0f || m_reallyDamagedFraction == 0.0f) && !settings.bodyThresholdsLoaded)
		{
			throw std::logic_error("ActiveBody: the template uses the default damage thresholds but GameLogicSettings has none (GameData UnitDamagedThreshold / UnitReallyDamagedThreshold not loaded)");
		}
		if (m_damagedFraction == 0.0f)
		{
			m_damagedFraction = settings.unitDamagedThreshold;
		}
		if (m_reallyDamagedFraction == 0.0f)
		{
			m_reallyDamagedFraction = settings.unitReallyDamagedThreshold;
		}
	}
}

// RW 0x8C1B79
BodyDamageType ActiveBody::calcDamageState() const
{
	if (m_currentHealth == 0.0f)
	{
		return BODY_RUBBLE;
	}
	if (SimMath::mulf32(m_reallyDamagedFraction, m_maxHealth) >= m_currentHealth)
	{
		return BODY_REALLYDAMAGED;
	}
	if (SimMath::mulf32(m_damagedFraction, m_maxHealth) >= m_currentHealth)
	{
		return BODY_DAMAGED;
	}
	return BODY_PRISTINE;
}

// RW 0x008C1BF9 (DONOR BFME2 0x004BDAA9): body interface slot 10 (+0x28) ActiveBody::setDamageState
void ActiveBody::resetDamageState(int state)
{
	float target = m_currentHealth;
	bool valid = true;
	switch (state)
	{
	case BODY_PRISTINE:
		target = m_maxHealth;
		break;
	case BODY_DAMAGED:
		target = SimMath::mulf32(m_damagedFraction, m_maxHealth);
		break;
	case BODY_REALLYDAMAGED:
		target = SimMath::mulf32(m_reallyDamagedFraction, m_maxHealth);
		break;
	case BODY_RUBBLE:
		target = 0.0f;
		break;
	default:
		valid = false;
		break;
	}
	if (valid)
	{
		internalChangeHealth(SimMath::subf32(target, m_currentHealth)); // RW 0x8C1C2D..0x8C1C3D
	}
	updateDamageState(false); // RW 0x8C1C43..0x8C1C49
}

// ZH ActiveBody::setInitialHealth (ActiveBody.cpp): the initial health is a percent of the max health; the current and previous health follow
void ActiveBody::setInitialHealth(int initialPercent)
{
	m_initialHealth = SimMath::divf32(SimMath::mulf32(m_maxHealth, (float)initialPercent), 100.0f); // ZH order: (max * percent) / 100
	m_currentHealth = m_initialHealth;
	m_previousHealth = m_currentHealth;
	m_reviveReference = m_currentHealth;
	updateDamageState();
}

// lane SUMMON-1: RW 0x8C47DE (vt[0x58]) with its second argument 0. A percent above 100.0 (RW 0xBD88D8, comiss) first scales the max health (interface + 0x10)
// by percent * 0.01 (RW 0xBE5600; SSE: (max * percent) * 0.01); then the health moves to percent * 0.01 of the revive reference (interface + 0x1C) through
// internalChangeHealth (vt[0x84], RW 0x8C31A5) by (percent * 0.01) * reference - health. The second argument's branch (RW 0x8C481D: the health set with three
// random draws and RW 0x8C3CB8) is not a caller's here
void ActiveBody::setInitialHealthPercent(float percent)
{
	const float scale = 0.01f; // RW 0xBE5600
	if (percent > 100.0f)
	{
		m_maxHealth = SimMath::mulf32(SimMath::mulf32(m_maxHealth, percent), scale);
	}
	const float fraction = SimMath::mulf32(percent, scale);
	internalChangeHealth(SimMath::subf32(SimMath::mulf32(fraction, m_reviveReference), m_currentHealth));
}

void ActiveBody::crc(StateHasher &h) const
{
	h.addFloat(m_currentHealth);
	h.addFloat(m_previousHealth);
	h.addFloat(m_reviveReference); // lane HERO-1
	if (m_indestructible)
	{
		h.addU32(0x1D35u); // lane SCRIPT-2: hashed only when set, so a game that never sets it keeps its hash
	}
	h.addFloat(m_initialHealth);
	h.addFloat(m_maxHealth);
	h.addFloat(m_damagedFraction);
	h.addFloat(m_reallyDamagedFraction);
	h.addFloat(m_damageScalar);
	h.addU32(m_lastDamageFrame);
	h.addU32(m_lastDamager);
	if (m_lastAttackerTaken)
	{
		h.addU32(0xB6u); // lane PLAY-4: hashed only when set
	}
	if (m_lastDamagerPlayerMask != 0)
	{
		h.addU32(m_lastDamagerPlayerMask ^ 0xCA391u); // lane CAMP-1: hashed only when set (a game without player damage keeps its hash)
	}
	h.addU32((std::uint32_t)m_curDamageState);
	if (m_lastDamageType != DAMAGE_UNDEFINED)
	{
		h.addU32((std::uint32_t)m_lastDamageType ^ 0xFA40u); // lane FX-4: hashed only when a hit set it (a game without hits keeps its hash)
	}
	// lane BLDGDEBRIS-1: the side stage and the side values / flags, hashed only when one is set (a game without siege damage keeps its hash)
	bool sides = m_sideStage != 0;
	for (int i = 0; i < 4; ++i)
	{
		sides = sides || m_sideDamage[i] != 0.0f || m_sideDestroyed[i];
	}
	if (sides)
	{
		h.addU32(0x51DEu ^ (std::uint32_t)m_sideStage);
		for (int i = 0; i < 4; ++i)
		{
			h.addFloat(m_sideDamage[i]);
			h.addBool(m_sideDestroyed[i]);
		}
	}
}

void ActiveBody::evaluateVisualCondition()
{
	// RW 0x8C1D53 -> Object RW 0x6903B6(state + 0x30, stage + 0x34): the damage flags follow the stored body state, without the construction suppression of
	// internalChangeHealth. The ambient-sound part (RW 0x67876B, the drawable's) remains S-653.
	Object &obj = *getObject();
	Object::ModelConditionBits clear{}, set{};
	const int damaged = CombatNames::modelCondition("DAMAGED"), really = CombatNames::modelCondition("REALLYDAMAGED"), rubble = CombatNames::modelCondition("RUBBLE");
	for (int bit : { damaged, really, rubble })
	{
		clear[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	const int on = m_curDamageState == BODY_DAMAGED ? damaged : m_curDamageState == BODY_REALLYDAMAGED ? really : m_curDamageState == BODY_RUBBLE ? rubble : -1;
	if (on >= 0)
	{
		set[(size_t)on >> 5] |= 1u << (on & 31);
	}
	obj.clearAndSetModelConditionFlags(clear, set);
	// lane BLDGDEBRIS-1: the tail of RW 0x6903B6 (BFME2 0x68FCD9, tier B same-shape; decomp ObjectRva0028FCD9.cpp:58). RUBBLE: an object with the model condition
	// ACTIVELY_BEING_CONSTRUCTED (0x45) gets DESTROYED_WHILST_BEING_CONSTRUCTED (0x14E) for 3 * [RW 0xD9F608] (5) frames through the SMC helper (RW 0x8E2C0F),
	// and the four DESTROYED_<side> conditions (0x8B .. 0x8E) are cleared. Else, with the model condition REALLYDAMAGED (RW 0x46E918(4), read after the flags
	// above changed): stage 1 .. 4 sets DESTROYED_FRONT / RIGHT / BACK / LEFT and clears the other three; stage 0 changes none. Without REALLYDAMAGED: the
	// four are cleared
	static const int kBeingBuilt = CombatNames::modelCondition("ACTIVELY_BEING_CONSTRUCTED");
	static const int kDestroyedBuilding = CombatNames::modelCondition("DESTROYED_WHILST_BEING_CONSTRUCTED");
	static const int kSide[4] = { CombatNames::modelCondition("DESTROYED_FRONT"), CombatNames::modelCondition("DESTROYED_RIGHT"),
		CombatNames::modelCondition("DESTROYED_BACK"), CombatNames::modelCondition("DESTROYED_LEFT") };
	constexpr UnsignedInt kLogicFramesPerSecond = 5; // RW 0xD9F608
	Object::ModelConditionBits sideClear{}, sideSet{};
	if (on == rubble)
	{
		if (obj.testModelCondition(kBeingBuilt))
		{
			obj.setSpecialModelConditionState(kDestroyedBuilding, kLogicFramesPerSecond * 3);
		}
	}
	else if (obj.testModelCondition(really))
	{
		if (m_sideStage < 1 || m_sideStage > 4)
		{
			return;
		}
		const int bit = kSide[m_sideStage - 1];
		sideSet[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	for (int bit : kSide)
	{
		if (!(sideSet[(size_t)bit >> 5] & (1u << (bit & 31))))
		{
			sideClear[(size_t)bit >> 5] |= 1u << (bit & 31);
		}
	}
	obj.clearAndSetModelConditionFlags(sideClear, sideSet);
}

// RW 0x8C2A5D (setCorrectDamageState). TARGET FACTS read from the binary: the damage state field (+0x30) is rewritten from calcDamageState (RW 0x8C1B79); when it changed vslot 0x34 runs
// (RW 0x8C283C: the Damaged / ReallyDamaged attribute modifiers, UPGRADE-1's). For a template that is KindOf STRUCTURE (tt KindOf mask byte 0x108 bit 0x80 = bit 7) the state RUBBLE does:
//   entering (new > old, new == 3): the structure's footprint leaves the pathfinder and is entered again (RW 0x6E85FB remove, 0x6E85E9 add: the add sees the RUBBLE state), the object
//   gets the status NO_COLLISIONS (RW 0x8C2E3B: 0x62684D(4, 1)), the geometry height becomes the template's StructureRubbleHeight (RW tt + 0x607, a byte; <= 0 takes GlobalData
//   DefaultStructureRubbleHeight, RW + 0xAE4) through RW 0x68B2CB, and RW 0x8C1BB7 runs (BFME2 0x4BDA67, decomp ActiveBodyDamageState.cpp:54-59,
//   tier B offsets-only): the object's ExperienceTracker (+ 0x26C) is reset (RW 0x79DA66), the upgrades go when RemoveUpgradesOnDeath
//   (data + 0x50 -> rva0028DA28), the command points are released (0x68E114 = removeFromPlayerCommandPoints) and the attribute modifier pool's
//   entries are cleared (rva00298E6A);
//   leaving it (new < 3, old == 3): the template geometry returns, the footprint is re-entered, NO_COLLISIONS is cleared (RW 0x8C2D87..0x8C2DCC).
// A WALK_ON_TOP_OF_WALL template (KindOf 60, byte 0x10F bit 0x10) runs a "Bookend" variant (BOOKENDING status 0x53, CAN_NOT_WALK_ON 0x4D) that is a wall's (S-342).
// NOT PORTED (S-342): the geometry height (the geometry is the template's, an Object has none of its own), the upgrade removal, the attribute modifier
// pool clear and the Damaged / ReallyDamaged attribute modifiers and the Bookend of walls.
// The model conditions DAMAGED / REALLYDAMAGED / RUBBLE: internalChangeHealth calls the visual update (vslot 0x94, RW 0x8C32A8) after the state or the side
// stage (+ 0x34) changed, not while the object is UNDER_CONSTRUCTION (unless dead). RW 0x8C1D53 -> Object RW 0x6903B6 sets the state's flag and clears the
// other two (and the DESTROYED_<side> flags, evaluateVisualCondition).
// lane BLDGDEBRIS-1: the side stage tail (RW 0x8C2C5D .. 0x8C2EF1, updateSideStage) runs on every call, also when the state did not change. Every caller
// here is RW's setCorrectDamageState(false) (internalChangeHealth RW 0x8C31A5 `push 0`, the constructor RW 0x8C39CD `push ebx` = 0)
void ActiveBody::updateDamageState(bool initial)
{
	const BodyDamageType old = m_curDamageState;
	const int oldStage = m_sideStage;
	const BodyDamageType now = calcDamageState();
	m_curDamageState = now;
	updateSideStage(old, false);
	if (now == old && m_sideStage == oldStage)
	{
		return;
	}
	Object &obj = *getObject();
	// lane PLAY-4, RW 0x8C3290 .. 0x8C32A8 (internalChangeHealth after vslot 0x54; BFME2 decomp ActiveBody.cpp, ZH "we just don't have all the art states for
	// that during buildup animation"): the visual change (vslot 0x94) is skipped while the object is UNDER_CONSTRUCTION, unless the health reached 0. A rising
	// structure (1 hit point at the start, RW 0x85895D) keeps its construction states instead of REALLYDAMAGED / DAMAGED.
	if (m_currentHealth <= 0.0f || !obj.testStatus((unsigned)CombatNames::statuses().underConstruction))
	{
		evaluateVisualCondition();
	}
	// lane FX-4: RotWK's setCorrectDamageState does not tell the damage modules (ZH's does); attemptDamage / attemptHealing do (notifyDamageStateChange)
	if (now == old || !obj.isKindOf((unsigned)CombatNames::kinds().structure))
	{
		return;
	}
	AIWorld *world = obj.logic().aiWorld();
	const bool mapReady = world && world->mapReady() && !initial;
	if (now == BODY_RUBBLE && old != BODY_RUBBLE)
	{
		if (mapReady)
		{
			world->removeObjectFromPathfindMap(obj);
			world->addObjectToPathfindMap(obj);
		}
		// RW 0x8C1BB7: the experience reset, then the command points; RemoveUpgradesOnDeath and the attribute pool clear are S-342's
		if (ExperienceTracker *xp = obj.getExperienceTracker())
		{
			xp->reset(); // RW 0x79DA66 on object + 0x26C (BFME2 decomp ActiveBodyDamageState.cpp:54-59, B2 0x4BDA67)
		}
		obj.removeFromPlayerCommandPoints(); // RW 0x8C1BB7 -> 0x68E114
		obj.setStatus((unsigned)CombatNames::statuses().noCollisions, true); // RW 0x8C2E3B: after the rubble reset
		++obj.logic().combat().counters().rubbleEntered;
	}
	else if (old == BODY_RUBBLE && now != BODY_RUBBLE)
	{
		if (mapReady)
		{
			world->removeObjectFromPathfindMap(obj);
			world->addObjectToPathfindMap(obj);
		}
		obj.setStatus((unsigned)CombatNames::statuses().noCollisions, false);
	}
}

// lane BLDGDEBRIS-1: the side stage of RW 0x8C2A5D (BFME2 0x4BE8BD, tier B same-shape; decomp ActiveBody.cpp:1760-1898), read on the RotWK bytes:
//   * a WALK_ON_TOP_OF_WALL template (tt + 0x10F bit 0x10, KindOf 60) always ends its branch with stage = 0 (RW 0x8C2C5D) and goes on to the side test;
//   * else a STRUCTURE (tt + 0x108 bit 0x80) in RUBBLE (RW 0x8C2DD4 .. 0x8C2E4A) ends with stage = 0 and returns, before the side test;
//   * the side test (RW 0x8C2C64): only with the model condition REALLYDAMAGED (RW 0x46E918(4) on Object + 0x10C: the decomp's `isKindOf(KINDOF_4)` is a
//     model-condition test), and only when the state did not change or `arg` is set: threshold = x87 (getMaxHealth() * 0.25) * 0.75 (RW 0xBD1904 / 0xBDC628,
//     fstp dword); the first side 0 .. 3 whose flag (+ 0xDC + i) is clear and whose value (+ 0xCC + 4i) >= threshold (comiss: unordered fails) sets stage =
//     i + 1 and its flag; none: return. With `arg` false every entry whose stage equals the new stage creates its OCL, whatever its rock (RW 0x5F0126)
void ActiveBody::updateSideStage(BodyDamageType oldState, bool arg)
{
	Object &obj = *getObject();
	static const int kWalkOnWall = ObjectTemplateInfoBuilder::kindOfIndex("WALK_ON_TOP_OF_WALL");
	if (kWalkOnWall >= 0 && obj.isKindOf((unsigned)kWalkOnWall))
	{
		m_sideStage = 0;
	}
	else if (obj.isKindOf((unsigned)CombatNames::kinds().structure) && m_curDamageState == BODY_RUBBLE)
	{
		m_sideStage = 0;
		return;
	}
	static const int kReallyDamaged = CombatNames::modelCondition("REALLYDAMAGED"); // 4
	if (!obj.testModelCondition(kReallyDamaged))
	{
		return;
	}
	if (oldState != m_curDamageState && !arg)
	{
		return;
	}
	const float threshold = SimMath::pc24Mul(SimMath::pc24Mul(getMaxHealth(), 0.25f), 0.75f); // RW 0x8C2C8C: interface vslot 0x1C
	int side = -1;
	for (int i = 0; i < 4; ++i)
	{
		if (!m_sideDestroyed[i] && m_sideDamage[i] >= threshold)
		{
			side = i;
			break;
		}
	}
	if (side < 0)
	{
		return;
	}
	m_sideStage = side + 1;
	m_sideDestroyed[side] = true;
	if (!arg)
	{
		for (const ActiveBodyModuleData::DamageCreation &c : m_data->m_damageCreationList)
		{
			if (c.stage == m_sideStage)
			{
				createDamageOCL(c.ocl);
			}
		}
	}
}

// RW 0x5F0126(ocl, object, 0, 0): the OCL at the object (the position variant of S-980, as FlammableUpdate / CritterEmitterUpdate create theirs); a NULL OCL
// ("None", an unknown name: RW 0x73A368's NULL) creates nothing
void ActiveBody::createDamageOCL(const std::string &name)
{
	if (name.empty() || !TheObjectCreationListStore)
	{
		return;
	}
	if (const ObjectCreationList *ocl = TheObjectCreationListStore->findObjectCreationList(name))
	{
		Object *obj = getObject();
		ocl->create(obj->logic(), obj, *obj->getPosition());
	}
}

// lane BLDGDEBRIS-1: RW 0x8C3CB8 (BFME2 0x4BFB1C, tier B same-shape; decomp ActiveBody.cpp:1978-2023), on the RotWK bytes. The object's matrix: X axis =
// basis column 0 (m00 m10 m20), Y axis = column 1, the translation = the position. d = object position - `from` (subss each). The side scores, SSE in this
// order: s0 = ((0 - m00) * dx + (0 - m10) * dy) + (0 - m20) * dz (the back of the X axis, FRONT), s1 = (m11 * dy + m21 * dz) + m01 * dx (RIGHT),
// s2 = 0 - s0 (BACK), s3 = 0 - s1 (LEFT). Sorted into a vector, highest first: side i goes before the first entry it is strictly greater than (comiss / ja),
// else at the end (ties keep index order). Then cap = x87 (getMaxHealth() * 0.25) * 0.75; while remaining > 0 and k < 4: room = cap - value; take =
// remaining > room ? room : remaining; take > 0 -> value = value + take, remaining = remaining - take
void ActiveBody::distributeSideDamage(float amount, const Coord3D &from)
{
	const Object &obj = *getObject();
	const float *m = obj.getBasis();
	const Coord3D &pos = *obj.getPosition();
	const float dx = SimMath::subf32(pos.x, from.x);
	const float dy = SimMath::subf32(pos.y, from.y);
	const float dz = SimMath::subf32(pos.z, from.z);
	using SimMath::addf32;
	using SimMath::mulf32;
	using SimMath::subf32;
	float score[4];
	score[0] = addf32(addf32(mulf32(subf32(0.0f, m[0]), dx), mulf32(subf32(0.0f, m[3]), dy)), mulf32(subf32(0.0f, m[6]), dz));
	score[1] = addf32(addf32(mulf32(m[4], dy), mulf32(m[7], dz)), mulf32(m[1], dx));
	score[2] = subf32(0.0f, score[0]);
	score[3] = subf32(0.0f, score[1]);
	// RW builds a vector (RW 0x478CBE, inserts RW 0x970ED8 / push_back RW 0x90BE00); a fixed four-slot array with the same insertion gives the same order
	int order[4] = { 0, 0, 0, 0 };
	int count = 0;
	for (int i = 0; i < 4; ++i)
	{
		int at = 0;
		while (at < count && !(score[i] > score[order[at]]))
		{
			++at;
		}
		for (int j = count; j > at; --j)
		{
			order[j] = order[j - 1];
		}
		order[at] = i;
		++count;
	}
	const float cap = SimMath::pc24Mul(SimMath::pc24Mul(getMaxHealth(), 0.25f), 0.75f); // RW 0x8C3DD0: interface vslot 0x1C
	float remaining = amount;
	for (int k = 0; remaining > 0.0f && k < 4; ++k)
	{
		float &value = m_sideDamage[order[k]];
		const float room = subf32(cap, value);
		const float take = remaining > room ? room : remaining;
		if (take > 0.0f)
		{
			value = addf32(value, take);
			remaining = subf32(remaining, take);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// lane COMBAT-1: the damage pipeline
// ---------------------------------------------------------------------------------------------------------------------------------
// RW 0x5D893C reads these four through the victim (scratch/weapon1/damage.md A5). The flank test (RW 0x5D89E6: the source object found by id, then
// Object::isFlankedBy RW 0x68FB63 on the victim) is lane HORDE-2's HordeFlank (S-582); an estimate (no victim / source pair) and a source that is gone answer
// "not flanked". Lane XP-1: INVULNERABLE and ARMOR are the victim's AttributeModifierPoolUpdate queries with the damage type's name (Damage.h: RW 0x68C818 ->
// 0x804F39); in the available binary both sites (RW 0x5D8A64, 0x5D8AF1) call into the community-added .danetta section, so the retail 2.01 form of these two
// queries is not verified (stop S-636, caveat S-001)
class ActiveBody::DamageHost : public AdjustDamageHost
{
public:
	explicit DamageHost(GameLogic &logic, Object *victim = nullptr, unsigned sourceId = 0)
		: m_logic(logic)
		, m_victim(victim)
		, m_sourceId(sourceId)
	{
	}
	bool victimFlankedByAttacker() override
	{
		Object *source = m_victim && m_sourceId != 0 ? m_logic.findObjectByID(m_sourceId) : nullptr;
		if (!source)
		{
			return false;
		}
		return HordeFlank::isFlankedBy(*m_victim, *source);
	}
	bool invulnerableTo(int damageType) override
	{
		float v = 0.0f;
		return m_victim && damageType >= 0 && damageType < DAMAGE_NUM_TYPES && m_victim->attributeModifierSum(ATTRIBUTE_MODIFIER_INVULNERABLE, TheDamageNames[damageType], v);
	}
	float armorModifierSum(int damageType) override
	{
		float v = 0.0f;
		if (m_victim && damageType >= 0 && damageType < DAMAGE_NUM_TYPES)
		{
			m_victim->attributeModifierSum(ATTRIBUTE_MODIFIER_ARMOR, TheDamageNames[damageType], v);
		}
		return v;
	}
	float armorMaxBonus() override { return 0.75f; } // GlobalData AttributeModifierArmorMaxBonus (RW +0xAE8, retail 75%): the table is GameData's, not loaded (S-322)

private:
	GameLogic &m_logic;
	Object *m_victim;
	unsigned m_sourceId;
};

const ArmorTemplate *ActiveBody::currentArmor() const
{
	const Object &obj = *getObject();
	const ThingTemplate *tt = obj.getTemplate();
	const ArmorTemplateSet *set = FindArmorTemplateSet(tt->armorTemplateSets(), obj.armorSetFlags());
	if (!set || set->m_armorName.empty())
	{
		return nullptr;
	}
	if (!TheArmorStore)
	{
		throw std::logic_error("ActiveBody: no ArmorStore is installed (the damage pipeline runs outside a world context)");
	}
	const ArmorTemplate *armor = TheArmorStore->findArmorTemplate(set->m_armorName);
	if (!armor && !m_armorReported)
	{
		m_armorReported = true;
		obj.logic().reportError("ActiveBody of " + tt->getName() + ": the armor '" + set->m_armorName + "' is not in the ArmorStore (RW looks the name up per hit and takes no armour)");
	}
	return armor;
}

float ActiveBody::estimateDamage(const DamageInfoInput &input) const
{
	DamageHost host(getObject()->logic(), getObject());
	const ArmorTemplate *armor = currentArmor();
	return AdjustDamage(armor, armor, input, true, host);
}

// RW 0x8C31A5. Lane BLDGDEBRIS-1 (BFME2 0x4BF005, tier B same-shape; decomp ActiveBody.cpp:1502-1545), on the RotWK bytes: the four side values follow the
// health: capped at the maximum (h > max) -> 0; else the previous health == 0 (ucomiss, a revive) -> x87 ((getMaxHealth() * 0.25) * 0.75) - 1.0 each
// (fstp dword); else delta > 0 (a heal) -> value = value - (delta * 0.25) * 0.75 (SSE), then 0 when 0 > value
void ActiveBody::internalChangeHealth(float delta)
{
	BodyHealth h;
	h.health = m_currentHealth;
	h.maxHealth = m_maxHealth;
	h.previousHealth = m_previousHealth;
	InternalChangeHealth(h, delta);
	m_currentHealth = h.health;
	m_previousHealth = h.previousHealth;
	if (h.healHistoryCleared)
	{
		for (float &v : m_sideDamage)
		{
			v = 0.0f;
		}
	}
	else if (m_previousHealth == 0.0f)
	{
		const float v = SimMath::pc24Sub(SimMath::pc24Mul(SimMath::pc24Mul(getMaxHealth(), 0.25f), 0.75f), 1.0f); // RW 0x8C31F7: vslot 0x1C
		for (float &side : m_sideDamage)
		{
			side = v;
		}
	}
	else if (delta > 0.0f)
	{
		const float reduce = SimMath::mulf32(SimMath::mulf32(delta, 0.25f), 0.75f);
		for (float &side : m_sideDamage)
		{
			side = SimMath::subf32(side, reduce);
			side = 0.0f > side ? 0.0f : side;
		}
	}
	updateDamageState(); // RW 0x8C31A5 calls vslot 0x54 = setCorrectDamageState(false)
}

// RW 0x8C4AB4: delta = max(delta, 1.0f - health) (x87 fsubr; fcomip / ja keeps delta when greater), then the base RW 0x8C31A5
void ImmortalBody::internalChangeHealth(float delta)
{
	const float floorDelta = SimMath::subf32(1.0f, getHealth());
	ActiveBody::internalChangeHealth(floorDelta > delta ? floorDelta : delta);
}

// RW 0x8C49C7
void HighlanderBody::attemptDamage(DamageInfo &info)
{
	info.m_input.m_amount = HighlanderClampAmount(info.m_input.m_amount, getHealth(), info.m_input.m_damageType);
	ActiveBody::attemptDamage(info);
}

// RW 0x8C3FA3, in the order of scratch/weapon1/damage.md 2.7
void ActiveBody::attemptDamage(DamageInfo &info)
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	const CombatNames::Status &st = CombatNames::statuses();
	const int type = info.m_input.m_damageType;
	// lane SCRIPT-2: RW 0x8C3FDA, before every other test: an indestructible body takes nothing
	if (m_indestructible)
	{
		return;
	}
	// INVUL-1: RW 0x8C3FE7 `push 0x14A; call 0x46E918` (Object::testModelCondition): model condition INVULNERABLE takes nothing, of any
	// damage type (ProductionUpdate's UnitInvulnerableTime, the castle unpack). Donor: Open-BFME-2 ActiveBody.cpp:2189 (BFME2 0x8BFE07, same
	// shape); both editions. The function's other retail differences stay open (S-322, ATTEMPTDMG-2)
	static const int kInvulnerable = CombatNames::modelCondition("INVULNERABLE"); // 0x14A
	if (obj.testModelCondition(kInvulnerable))
	{
		return;
	}
	// (3) UNATTACKABLE only takes UNRESISTABLE
	if (obj.testStatus((unsigned)st.unattackable) && type != DAMAGE_UNRESISTABLE)
	{
		return;
	}
	// (5)
	info.m_output.m_actualDamageDealt = 0.0f;
	info.m_output.m_actualDamageClipped = 0.0f;
	if (obj.isEffectivelyDead())
	{
		return;
	}
	// (6)
	DamageHost host(logic, &obj, info.m_input.m_sourceID);
	const ArmorTemplate *armor = currentArmor();
	float amount = AdjustDamage(armor, armor, info.m_input, false, host);
	if (type == DAMAGE_HEALING)
	{
		if (!info.m_input.m_kill)
		{
			attemptHealing(info);
		}
		return;
	}
	// (7) the body scalar (not for UNRESISTABLE), (9) the gate
	if (type != DAMAGE_UNRESISTABLE)
	{
		amount = SimMath::mulf32(amount, m_damageScalar);
	}
	if (Object *source = logic.findObjectByID(info.m_input.m_sourceID))
	{
		const unsigned bloodthirsty = (unsigned)CombatNames::status("BLOODTHIRSTY");
		Object *sourceHorde = source->getHordeObject(false);
		if (source->testStatus(bloodthirsty) && sourceHorde)
		{
			if (BloodthirstyUpdate *module = BloodthirstyUpdate::find(*sourceHorde); module && module->isValidSacrifice(&obj))
			{
				info.m_input.m_kill = true;
				module->addSacrifice(&obj);
			}
		}
	}
	const float previous = m_currentHealth;
	if (amount > 0.0f || info.m_input.m_kill)
	{
		// (10) the burning death fire cap is stop S-322 (the burn module and the status masks are not ported); (11) kill takes the whole health
		if (info.m_input.m_kill)
		{
			amount = m_currentHealth;
		}
		// (13) internalChangeHealth(-amount) (the info goes down with it in RW: DelayedDeathBody's override reads and clears its kill flag)
		const BodyDamageType stateBefore = m_curDamageState; // lane FX-4: RW 0x8C3FA3 keeps + 0x30 (EBP - 0x1C) before the health change (vslot 0x84, RW 0x8C4257)
		// lane BLDGDEBRIS-1: RW 0x8C422E .. 0x8C423F (BFME2 0x4BFE07, tier B same-shape; decomp ActiveBody.cpp:2119): a SIEGE hit (damage type 4), after the kill
		// override, goes to RW 0x8C3E70 (BFME2 0x4BFCD4, tier A): the source found by id (none: nothing), then the side distributor with its position
		if (type == DAMAGE_SIEGE)
		{
			if (const Object *source = logic.findObjectByID(info.m_input.m_sourceID))
			{
				distributeSideDamage(amount, *source->getPosition());
			}
		}
		m_currentInfo = &info;
		internalChangeHealth(SimMath::subf32(0.0f, amount));
		m_currentInfo = nullptr;
		// (15)
		info.m_output.m_actualDamageDealt = amount;
		info.m_output.m_actualDamageClipped = SimMath::subf32(m_previousHealth, m_currentHealth);
		// (16)
		m_lastDamageFrame = logic.getFrame();
		m_lastDamager = info.m_input.m_sourceID;
		m_lastDamagerPlayerMask = info.m_input.m_sourcePlayerMask; // lane CAMP-1
		m_lastDamageType = info.m_input.m_damageType;               // lane FX-4: the record's + 0x10 (RW 0x7441E0 copies the whole input)
		m_lastAttackerTaken = false; // lane PLAY-4: RW 0x8C42FA (+ 0xB6 cleared with the record)
		// lane CAMP-1, RW 0x8C430F .. 0x8C4341: the last damage record's source object, when it exists, marks its controlling player as an attacker of the
		// victim's controlling player (RW 0x6AAC05). INFERENCE: retail runs this after the record block whether or not the record changed this hit; here only
		// when it did
		if (const Object *source = logic.findObjectByID(m_lastDamager))
		{
			Player *victimPlayer = obj.getControllingPlayer();
			const Player *sourcePlayer = source->getControllingPlayer();
			if (victimPlayer && sourcePlayer)
			{
				victimPlayer->setAttackedBy(sourcePlayer->getPlayerIndex(), logic.getFrame());
			}
		}
		++logic.combat().counters().damageApplications;
		if (const CombatState::DamageTrace &trace = logic.combat().damageTrace())
		{
			trace(obj, info.m_input.m_sourceID, info.m_output.m_actualDamageDealt, info.m_output.m_actualDamageClipped); // lane SHIP-1: a test observer
		}
		// ZH: the damage modules hear of the hit (RW 0x79B757 CastleMemberBehavior::onDamage reads the amount dealt)
		if (info.m_output.m_actualDamageDealt > 0.0f)
		{
			for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
			{
				if (DamageModuleInterface *d = m->getDamage())
				{
					d->onDamage(info);
				}
			}
		}
		// lane FX-4, RW 0x8C4389 .. 0x8C43C1: the damage state changed by this hit: every damage module's slot 2 (info, old, new), after the record copy and the
		// onDamage loop, before the death
		if (m_curDamageState != stateBefore)
		{
			notifyDamageStateChange(&info, stateBefore, m_curDamageState);
		}
		// (18) death: health <= 0 and the hit took health
		if (m_currentHealth <= 0.0f && previous > 0.0f)
		{
			if (Object *killer = logic.findObjectByID(info.m_input.m_sourceID))
			{
				killer->scoreTheKill(obj);
			}
			// RW 0x8C4593: RespawnBody owns the death callback when RespawnUpdate exists.
			if (!dynamic_cast<RespawnUpdate *>(obj.findModule("RespawnUpdate")))
			{
				DieModuleInterface::Event event;
				event.sourceId = info.m_input.m_sourceID;
				event.deathType = info.m_input.m_deathType;
				event.damageType = type;
				event.damageSubType = info.m_input.m_damageSubType; // lane HERO-2
				event.damageAmount = info.m_input.m_amount;
				event.actualDamageDealt = info.m_output.m_actualDamageDealt;
				event.actualDamageClipped = info.m_output.m_actualDamageClipped;
				obj.friend_onDie(event);
			}
		}
		// lane XP-1: RW 0x8C46A9 .. 0x8C46EA: the attacker's player earns skill points for the share of the victim's maximum health the hit took (capped at
		// 1.0), after the kill credit and the death (ExperienceWorld::awardSkillPointsForDamage)
		if (Object *attacker = logic.findObjectByID(info.m_input.m_sourceID))
		{
			float fraction = SimMath::divf32(info.m_output.m_actualDamageClipped, m_maxHealth);
			if (fraction > 1.0f)
			{
				fraction = 1.0f;
			}
			if (fraction > 0.0f)
			{
				logic.experience().awardSkillPointsForDamage(*attacker, obj, fraction);
			}
		}
	}
	// (19) doDamageFX (RW 0x8C2F02, lane FX-2)
	doDamageFX(info);
}

// RW 0x8C2F02 doDamageFX (lane FX-2). The body's DamageFX (+0xFC) is the one of the armour set chosen for the object's armour flags (the ArmorSet's DamageFX
// field, resolved by name in TheDamageFXStore; RW keeps the pointer of the set in use); none: no FX. A hit of the type played last (+0x3C) while the frame is
// below +0x38 is throttled. Else RW 0x76256A: the FXList is getDamageFX(type, actual damage dealt, source) (RW 0x76226B: row 0, amount 0 gives none,
// amount >= AmountForMajorFX the major list) and plays as doFXObj(fx, this object, the source) through RW 0x4B1B5A; when one played, +0x3C = the type and
// +0x38 = now + ThrottleTime(type) (RW 0x76225D, row 0). Lane BLDGDEBRIS-1: then the loop RW 0x8C2F75 .. 0x8C2FB8 (BFME2 0x4BED62, tier A; decomp
// ActiveBody.cpp:1345-1350) over the DamageCreationList entries (body + 0xE0), unthrottled and also when no DamageFX played: an entry whose rock equals the
// hit's FX trigger (DamageInfoInput + 0x2C) and whose stage is 0 creates its OCL (RW 0x5F0126(object, 0, 0)). With retail data this never happens: every
// weapon with an FXTrigger is a projectile launcher, and a warhead's hit carries the warhead template's FXTrigger (WeaponStore::handleProjectileDetonation
// RW 0x6CF4D6, generateDamageInfo RW 0x90E665 .. 0x90E66B), NONE for every retail warhead; a mod's direct-damage weapon with an FXTrigger fires it
void ActiveBody::doDamageFX(const DamageInfo &info)
{
	playDamageFX(info);
	for (const ActiveBodyModuleData::DamageCreation &c : m_data->m_damageCreationList)
	{
		if (c.rock == info.m_input.m_fxTrigger && c.stage == 0)
		{
			createDamageOCL(c.ocl);
		}
	}
}

// RW 0x8C2F02 up to RW 0x8C2F72: the throttled DamageFX (lane FX-2)
void ActiveBody::playDamageFX(const DamageInfo &info)
{
	Object &obj = *getObject();
	const ThingTemplate *tt = obj.getTemplate();
	const ArmorTemplateSet *set = tt ? FindArmorTemplateSet(tt->armorTemplateSets(), obj.armorSetFlags()) : nullptr;
	if (!set || !TheDamageFXStore || !FXEventLog::isFXName(set->m_damageFXName))
	{
		return;
	}
	const DamageFX *dfx = TheDamageFXStore->findDamageFX(set->m_damageFXName);
	if (!dfx)
	{
		return; // RW 0x73AF3F: an unknown DamageFX name is NULL
	}
	GameLogic &logic = obj.logic();
	const int type = info.m_input.m_damageFXOverride;
	if (type < 0 || type >= DAMAGEFX_TYPE_COUNT)
	{
		return;
	}
	const UnsignedInt now = logic.getFrame();
	if (type == m_lastDamageFXType && now < m_damageFXThrottleUntil)
	{
		return;
	}
	const std::string &fx = dfx->getDamageFX(type, info.m_output.m_actualDamageDealt);
	if (!FXEventLog::isFXName(fx))
	{
		return;
	}
	FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, "DamageFX", now, fx, obj);
	e.secondary = logic.findObjectByID(info.m_input.m_sourceID) ? info.m_input.m_sourceID : (ObjectID)INVALID_ID;
	logic.fxEvents().emit(e);
	m_lastDamageFXType = type;
	m_damageFXThrottleUntil = now + dfx->getThrottleTime(type);
}

// lane XP-1: RW 0x8C1CD5 setMaxHealth: the maximum and the initial health become newMax; type 1 changes the health by (health / oldMax) * newMax - health,
// type 2 by newMax - oldMax (SSE single, through internalChangeHealth); then a health above the new maximum comes down to it
void ActiveBody::setMaxHealth(float newMax, int changeType)
{
	const float oldMax = m_maxHealth;
	m_maxHealth = newMax;
	m_initialHealth = newMax;
	m_reviveReference = newMax; // lane HERO-1: RW 0x8C1CD5 stores newMax at module + 0x2C, the revive's reference
	if (changeType == 1)
	{
		internalChangeHealth(SimMath::subf32(SimMath::mulf32(SimMath::divf32(m_currentHealth, oldMax), newMax), m_currentHealth));
	}
	else if (changeType == 2)
	{
		internalChangeHealth(SimMath::subf32(newMax, oldMax));
	}
	if (m_currentHealth > newMax)
	{
		internalChangeHealth(SimMath::subf32(newMax, m_currentHealth));
	}
}

// lane FX-4: the loops RW 0x8C4395 (attemptDamage) / RW 0x8C3139 (attemptHealing) run over the object's modules in list order: each module's DAMAGE
// interface (module slot 0x10) slot 2, onBodyDamageStateChange(info, old, new). setCorrectDamageState (RW 0x8C2A5D) makes no such call: a health change by
// any other path (construction, repair, scripts, the maximum health) tells no damage module
void ActiveBody::notifyDamageStateChange(const DamageInfo *info, BodyDamageType oldState, BodyDamageType newState)
{
	for (const std::unique_ptr<BehaviorModule> &m : getObject()->modules())
	{
		if (DamageModuleInterface *d = m->getDamage())
		{
			d->onBodyDamageStateChange(info, oldState, newState);
		}
	}
}

namespace
{
// the dead-but-kept test of RW 0x8C2FC1 (BFME2 0x4BEE21, decomp ActiveBody.cpp attemptHealing, tier B same-shape): the template's
// KeepSelectableWhenDead (+ 0x642, read as PlayerCommands does) or its KindOf WALL_UPGRADE (150, tt + 0x11A bit 6), BRIDGE (22) or
// BRIDGE_TOWER (24) (tt + 0x10A & 0x140)
bool healableWhenDead(const Object &obj)
{
	static const int kWallUpgrade = ObjectTemplateInfoBuilder::kindOfIndex("WALL_UPGRADE");
	static const int kBridge = ObjectTemplateInfoBuilder::kindOfIndex("BRIDGE");
	static const int kBridgeTower = ObjectTemplateInfoBuilder::kindOfIndex("BRIDGE_TOWER");
	if ((kWallUpgrade >= 0 && obj.isKindOf((unsigned)kWallUpgrade)) || (kBridge >= 0 && obj.isKindOf((unsigned)kBridge)) ||
		(kBridgeTower >= 0 && obj.isKindOf((unsigned)kBridgeTower)))
	{
		return true;
	}
	if (const ThingTemplate *tt = static_cast<const ThingTemplate *>(obj.getTemplate()))
	{
		if (const FieldValue *v = tt->getFinalOverride()->findField("KeepSelectableWhenDead"))
		{
			if (const bool *b = std::get_if<bool>(v))
			{
				return *b;
			}
		}
	}
	return false;
}
} // namespace

// RW 0x8C2FC1 attemptHealing (BFME2 0x4BEE21, decomp ActiveBody.cpp, tier B same-shape). RotWK order: validateArmorAndDamageFX (the armour
// validation is S-322's); a body whose module data +0x51 (BurningDeathBehavior) is set while the object is in the model condition
// BURNINGDEATH (index 0x220, the reading SpawnBehavior shares) returns at once; a null DamageInfo returns (ours is a reference); a
// non-HEALING hit goes to attemptDamage; an effectively dead object (Object + 0x458 bit 0) is refused unless healableWhenDead answers
// (KeepSelectableWhenDead / WALL_UPGRADE / BRIDGE / BRIDGE_TOWER). Only then are the outputs zeroed and the amount applied
void ActiveBody::attemptHealing(DamageInfo &info)
{
	Object *obj = getObject();
	static const int kBurningDeath = CombatNames::modelCondition("BURNINGDEATH"); // model condition 0x220
	if (m_data->m_burningDeathBehavior && obj->testModelCondition(kBurningDeath))
	{
		return;
	}
	if (info.m_input.m_damageType != DAMAGE_HEALING)
	{
		attemptDamage(info);
		return;
	}
	if (obj->isEffectivelyDead() && !healableWhenDead(*obj))
	{
		return;
	}
	info.m_output.m_actualDamageDealt = 0.0f;
	info.m_output.m_actualDamageClipped = 0.0f;
	const float amount = info.m_input.m_amount; // adjustDamage returns the amount for HEALING
	if (amount > 0.0f)
	{
		const BodyDamageType stateBefore = m_curDamageState; // lane FX-4: RW 0x8C3080 keeps + 0x30 before the health change (vslot 0x84, RW 0x8C308A)
		internalChangeHealth(amount);
		info.m_output.m_actualDamageDealt = amount;
		info.m_output.m_actualDamageClipped = SimMath::subf32(m_previousHealth, m_currentHealth);
		// lane OWNERFIX-1 (the owner: no particles show the heroes healing): RW 0x8C30B1 .. 0x8C3118 (BFME2 decomp ActiveBody.cpp attemptHealing, tier B
		// same-shape): when the health went up (after the damage modules' onHealing, which this port does not call here), the module data's healing FX (+ 0x48: HealingBuffFx, else GameData's
		// DefaultUnitHealingBuffFxList, RW 0x8C373F) plays on the object (RW 0x4B1B5A, no secondary) unless its template is a STRUCTURE (template + 0x108
		// bit 7) or its status has AIRBORNE_TARGET (Object + 0x94 bit 6). A client effect: the logic's FX log, nothing hashed changes
		if (m_currentHealth > m_previousHealth)
		{
			Object &obj = *getObject();
			GameLogic &logic = obj.logic();
			const std::string &fx = m_data->m_healingBuffFxSet ? m_data->m_healingBuffFx : logic.settings().defaultUnitHealingBuffFxList;
			static const int kStructure = CombatNames::kindOf("STRUCTURE");
			static const int kAirborneTarget = CombatNames::status("AIRBORNE_TARGET");
			if (FXEventLog::isFXName(fx) && !obj.isKindOf((unsigned)kStructure) && !obj.testStatus((unsigned)kAirborneTarget))
			{
				logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "HealingBuffFx", logic.getFrame(), fx, obj));
			}
		}
		// lane FX-4, RW 0x8C3131 .. : the damage state changed by the healing: every damage module's slot 2 (info, old, new)
		if (m_curDamageState != stateBefore)
		{
			notifyDamageStateChange(&info, stateBefore, m_curDamageState);
		}
	}
}

// ---- lane SAVE-4b: the snapshot (OpenBFME native, S-4190) -----------------------------------------------------------------------------------------------
// Retail's ActiveBody::xfer (RW 0x8C3388, tier A, (1, 1)) transfers RotWK's body (six reals, the damage and side-destroyed types, the particle systems, the
// subdual and armour bits ...); this body is the port's subset: its fields in the crc's order, then what the crc leaves out (the damage FX throttle). The
// current DamageInfo pointer is null outside a hit (a snapshot is taken between frames); the armour report flag is the report's.
void ActiveBody::xfer(Xfer &x)
{
	XferVersion v{ 1, 3 }; // 2: lane FX-4's last damage type; 3: lane BLDGDEBRIS-1's side stage and side values / flags
	x.xferVersion(v);
	xferBehaviorModule(x);
	x.xferReal(&m_currentHealth);
	x.xferReal(&m_previousHealth);
	x.xferReal(&m_reviveReference);
	x.xferBool(&m_indestructible);
	x.xferReal(&m_initialHealth);
	x.xferReal(&m_maxHealth);
	x.xferReal(&m_damagedFraction);
	x.xferReal(&m_reallyDamagedFraction);
	x.xferReal(&m_damageScalar);
	x.xferUnsignedInt(&m_lastDamageFrame);
	x.xferObjectID(&m_lastDamager);
	SnapshotXfer::maskWithin(x, m_lastDamagerPlayerMask, SnapshotXfer::lowBits(getObject()->logic().players().getPlayerCount()), "a body's last damager players",
		"this game's players"); // lane SAVE-4e
	x.xferBool(&m_lastAttackerTaken); // lane PLAY-4's + 0xB6
	SnapshotXfer::enumIn(x, m_curDamageState, BODY_PRISTINE, BODY_RUBBLE, "a body's damage state");
	SnapshotXfer::enumIn(x, m_lastDamageFXType, 0, SnapshotXfer::nameCount(TheDamageFXTypeNames) - 1, "a body's last damage FX type");
	x.xferUnsignedInt(&m_damageFXThrottleUntil);
	if (v.current >= 2)
	{
		SnapshotXfer::enumIn(x, m_lastDamageType, 0, SnapshotXfer::nameCount(TheDamageNames) - 1, "a body's last damage type");
	}
	if (v.current >= 3)
	{
		// RW 0x8C3388 transfers the stage (XferBodySideDestroyedType) and the four value / flag pairs
		SnapshotXfer::enumIn(x, m_sideStage, 0, 4, "a body's destroyed side");
		for (int i = 0; i < 4; ++i)
		{
			x.xferReal(&m_sideDamage[i]);
			x.xferBool(&m_sideDestroyed[i]);
		}
	}
}

void ImmortalBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	ActiveBody::xfer(x);
}

void HighlanderBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	ActiveBody::xfer(x);
}
