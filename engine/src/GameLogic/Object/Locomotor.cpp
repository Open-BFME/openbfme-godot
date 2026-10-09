// OpenBFME. GPL-3.0.
//
// Locomotor data: LocomotorTemplate, LocomotorStore, LocomotorSet parsing. See GameLogic/Locomotor.h
// for the target / donor facts. Port of ZH Source/GameLogic/Object/Locomotor.cpp (LocomotorStore,
// LocomotorTemplate) with the RotWK table, defaults and parse flow read from game.dat (stop S-001
// caveat). Lane HORDE-1.

#include "GameLogic/Locomotor.h"

#include "Common/GameCommon.h"

#include <cfloat>
#include <climits>
#include <cstddef>
#include <cstring>

const char *const TheLocomotorSurfaceNames[] = { "GROUND", "WATER", "CLIFF", "AIR", "RUBBLE", "OBSTACLE", "IMPASSABLE", "DEEP_WATER", "WALL_RAILING", nullptr };

const char *const TheLocomotorZAxisNames[] = { "NO_Z_MOTIVE_FORCE", "SEA_LEVEL", "SURFACE_RELATIVE_HEIGHT", "ABSOLUTE_HEIGHT", "FIXED_SURFACE_RELATIVE_HEIGHT",
	"FIXED_ABSOLUTE_HEIGHT", "FIXED_RELATIVE_TO_GROUND_AND_BUILDINGS", "RELATIVE_TO_HIGHEST_LAYER", "FLOATING_Z", "SCALING_WALLS", nullptr };

const char *const TheLocomotorAppearanceNames[] = { "TWO_LEGS", "FOUR_WHEELS", "HOVER", "WINGS", "FOUR_LEGS_HUGE", "GIANT_BIRD", "HORDE", "HUGE_TWO_LEGS", "TREADS", "SHIP",
	"OTHER", nullptr };

const char *const TheLocomotorFormationPriorityNames[] = { "NO_FORMATION", "CAVALRY1", "CAVALRY2", "CAVALRY3", "MELEE1", "MELEE2", "MELEE3", "RANGED1", "RANGED2", "RANGED3",
	"ARTILLERY1", "ARTILLERY2", "ARTILLERY3", "UNUSED", nullptr };

const char *const TheLocomotorSetNames[] = { "SET_NORMAL", "SET_NORMAL_UPGRADED", "SET_FREEFALL", "SET_WANDER", "SET_PANIC", "SET_TAXIING", "SET_SUPERSONIC", "SET_MOUNTED",
	"SET_ENRAGED", "SET_SCARED", "SET_CONTAINED", "SET_COMBO", "SET_COMBO2", "SET_COMBO3", "SET_WALL_SCALING", "SET_CHANGING_FIRINGARC", "SET_BURNINGDEATH", nullptr };

thread_local LocomotorStore *TheLocomotorStore = nullptr; // SMOOTH-1: per thread (the world context of each thread, RetailObjectWorld::ContextScope) // RW 0xDE369C

namespace
{
const float kPi = 3.14159274f;        // RW 0xBDD388 (0x40490FDB)
const float kTwoPi = 6.28318548f;     // RW 0xBDD38C (0x40C90FDB)
const float kFifteenDegrees = 0.261799395f; // RW 0xBF4A30 (0x3E860A92)
}

// RW 0x5E4326. Frame-valued defaults read the logic-frames-per-second global (RW 0xD9F608 == 5).
LocomotorTemplate::LocomotorTemplate()
	: m_surfaces(0)
	, m_maxSpeed(0.0f)
	, m_lookAheadMult(1.0f)
	, m_nonDirtyTransform(false)
	, m_maxSpeedDamaged(1.0f)
	, m_turnTime(LOGICFRAMES_PER_SECOND)
	, m_turnTimeDamaged(0)
	, m_slowTurnRadius(0.0f)
	, m_fastTurnRadius(20.0f)
	, m_turnThreshold(kFifteenDegrees)
	, m_turnThresholdHighSpeed(kPi)
	, m_acceleration(LOGICFRAMES_PER_SECOND)
	, m_lift(0.0f)
	, m_liftDamaged(-1.0f)
	, m_braking(LOGICFRAMES_PER_SECOND)
	, m_minSpeed(0.0f)
	, m_minTurnSpeed(99999.0f)
	, m_preferredHeight(0.0f)
	, m_preferredHeightDamping(1.0f)
	, m_preferredAttackHeight(0.0f)
	, m_circlingRadius(0.0f)
	, m_speedLimitZ(999999.0f)
	, m_maxThrustAngle(0.0f)
	, m_zAxisBehavior(Z_NO_Z_MOTIVE_FORCE)
	, m_appearance(LOCO_WHEELS_FOUR)
	, m_formationPriority(FORMATION_RANGED1)
	, m_accDecTrigger(0.5f)
	, m_walkDistance(0.0f)
	, m_maxOverlappedHeight(FLT_MAX)
	, m_maxTurnWithoutReform(kTwoPi)
	, m_accelerationPitchLimit(0.0f)
	, m_bounceAmount(0.0f)
	, m_pitchStiffness(0.1f)
	, m_rollStiffness(0.1f)
	, m_pitchDamping(0.9f)
	, m_rollDamping(0.9f)
	, m_pitchInDirectionOfZVelFactor(0.0f)
	, m_forwardVelocityPitchFactor(0.0f)
	, m_lateralVelocityRollFactor(0.0f)
	, m_forwardAccelerationPitchFactor(0.0f)
	, m_lateralAccelerationRollFactor(0.0f)
	, m_uniformAxialDamping(1.0f)
	, m_turnPivotOffset(0.0f)
	, m_apply2DFrictionWhenAirborne(false)
	, m_downhillOnly(false)
	, m_allowAirborneMotiveForce(false)
	, m_locomotorWorksWhenDead(false)
	, m_airborneTargetingHeight(INT_MAX)
	, m_stickToGround(false)
	, m_canMoveBackwards(0)
	, m_hasSuspension(false)
	, m_frontWheelTurnAngle(0.0f)
	, m_maximumWheelExtension(0.0f)
	, m_maximumWheelCompression(0.0f)
	, m_closeEnoughDist(1.0f)
	, m_closeEnoughDist3D(false)
	, m_slideIntoPlaceTime(0.0f)
	, m_crewPowered(false)
	, m_useTerrainSmoothing(false)
	, m_wanderWidthFactor(0.0f)
	, m_wanderLengthFactor(1.0f)
	, m_wanderAboutPointRadius(0.0f)
	, m_burningDeathRadius(0.0f)
	, m_burningDeathIsCavalry(false)
	, m_chargeSpeed(0.0f)
	, m_chargeAvailable(false)
	, m_chargeIgnoresCondition(false)
	, m_enableHighSpeedTurnModelconditions(true)
	, m_waitForFormation(false)
	, m_rudderCorrectionDegree(0.0f)
	, m_rudderCorrectionRate(0.0f)
	, m_elevatorCorrectionDegree(0.0f)
	, m_elevatorCorrectionRate(0.0f)
	, m_aeleronCorrectionDegree(0.0f)
	, m_aeleronCorrectionRate(0.0f)
	, m_swoopStandoffRadius(200.0f)
	, m_swoopStandoffHeight(200.0f)
	, m_swoopTerminalVelocity(0.07f)
	, m_swoopAccelerationRate(0.003f)
	, m_swoopSpeedTuningFactor(1.0f)
	, m_backingUpSpeed(0.75f)
	, m_backingUpStopWhenTurning(false)
	, m_backingUpDistanceMin(0.0f)
	, m_backingUpDistanceMax(0.0f)
	, m_backingUpAngle(0.5f)
	, m_riverModifier(1.0f)
	, m_scalesWalls(false)
	, m_turnWhileMoving(true)
{
}

// RW 0x5E3143.
void LocomotorTemplate::applyParseDefaults()
{
	if (m_turnTimeDamaged == 0)
	{
		m_turnTimeDamaged = m_turnTime;
	}
	if (m_liftDamaged < 0.0f)
	{
		m_liftDamaged = m_lift;
	}
	if (m_appearance == LOCO_WINGS)
	{
		// RW 0x5E3164: comiss 0, MinSpeed; jb skip  =>  stored when MinSpeed <= 0 (RW 0xBE5600 = 0.01f)
		if (0.0f >= m_minSpeed)
		{
			m_minSpeed = 0.01f;
		}
		if (0.0f >= m_minTurnSpeed)
		{
			m_minTurnSpeed = 0.01f;
		}
	}
}

namespace
{
// MSVCR71 atoi (the CRT's _atol): skips isspace, optional sign, decimal digits, 32-bit wrap-around.
int msvcrAtoi(const char *s)
{
	while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
	{
		++s;
	}
	int sign = 1;
	if (*s == '-')
	{
		sign = -1;
		++s;
	}
	else if (*s == '+')
	{
		++s;
	}
	std::uint32_t total = 0;
	while (*s >= '0' && *s <= '9')
	{
		total = total * 10u + (std::uint32_t)(*s - '0');
		++s;
	}
	return sign < 0 ? (int)(0u - total) : (int)total;
}
}

// RW 0x5E3183: v = atoi(token); 1..4 is stored as is; anything else is scanBool(token) (0 or 1).
void LocomotorTemplate::parseCanMoveBackwards(INI *ini, void *, void *store, const void *)
{
	const char *token = ini->getNextToken();
	int value = msvcrAtoi(token);
	if (value < 1 || value > 4)
	{
		value = ini->scanBool(token) ? 1 : 0;
	}
	*(int *)store = value;
}

const FieldParse *LocomotorTemplate::getFieldParse()
{
	static const FieldParse table[] = {
		{ "Surfaces", INI::parseBitString32, TheLocomotorSurfaceNames, offsetof(LocomotorTemplate, m_surfaces) },
		{ "Speed", INI::parseVelocityReal, nullptr, offsetof(LocomotorTemplate, m_maxSpeed) },
		{ "LookAheadMult", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_lookAheadMult) },
		{ "NonDirtyTransform", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_nonDirtyTransform) },
		{ "SpeedDamaged", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_maxSpeedDamaged) },
		{ "TurnTime", INI::parseDurationUnsignedInt, nullptr, offsetof(LocomotorTemplate, m_turnTime) },
		{ "TurnTimeDamaged", INI::parseDurationUnsignedInt, nullptr, offsetof(LocomotorTemplate, m_turnTimeDamaged) },
		{ "SlowTurnRadius", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_slowTurnRadius) },
		{ "FastTurnRadius", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_fastTurnRadius) },
		{ "TurnThreshold", INI::parseAngleReal, nullptr, offsetof(LocomotorTemplate, m_turnThreshold) },
		{ "TurnThresholdHS", INI::parseAngleReal, nullptr, offsetof(LocomotorTemplate, m_turnThresholdHighSpeed) },
		{ "Acceleration", INI::parseDurationUnsignedInt, nullptr, offsetof(LocomotorTemplate, m_acceleration) },
		{ "Lift", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_lift) },
		{ "LiftDamaged", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_liftDamaged) },
		{ "Braking", INI::parseDurationUnsignedInt, nullptr, offsetof(LocomotorTemplate, m_braking) },
		{ "MinSpeed", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_minSpeed) },
		{ "MinTurnSpeed", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_minTurnSpeed) },
		{ "PreferredHeight", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_preferredHeight) },
		{ "PreferredHeightDamping", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_preferredHeightDamping) },
		{ "PreferredAttackHeight", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_preferredAttackHeight) },
		{ "CirclingRadius", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_circlingRadius) },
		{ "SpeedLimitZ", INI::parseVelocityReal, nullptr, offsetof(LocomotorTemplate, m_speedLimitZ) },
		{ "MaxThrustAngle", INI::parseAngleReal, nullptr, offsetof(LocomotorTemplate, m_maxThrustAngle) },
		{ "ZAxisBehavior", INI::parseIndexList, TheLocomotorZAxisNames, offsetof(LocomotorTemplate, m_zAxisBehavior) },
		{ "Appearance", INI::parseIndexList, TheLocomotorAppearanceNames, offsetof(LocomotorTemplate, m_appearance) },
		{ "FormationPriority", INI::parseIndexList, TheLocomotorFormationPriorityNames, offsetof(LocomotorTemplate, m_formationPriority) },
		{ "AccDecTrigger", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_accDecTrigger) },
		{ "WalkDistance", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_walkDistance) },
		{ "MaxOverlappedHeight", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_maxOverlappedHeight) },
		{ "MaxTurnWithoutReform", INI::parseAngleReal, nullptr, offsetof(LocomotorTemplate, m_maxTurnWithoutReform) },
		{ "AccelerationPitchLimit", INI::parseAngleReal, nullptr, offsetof(LocomotorTemplate, m_accelerationPitchLimit) },
		{ "BounceAmount", INI::parseAngularVelocityReal, nullptr, offsetof(LocomotorTemplate, m_bounceAmount) },
		{ "PitchStiffness", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_pitchStiffness) },
		{ "RollStiffness", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_rollStiffness) },
		{ "PitchDamping", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_pitchDamping) },
		{ "RollDamping", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_rollDamping) },
		{ "PitchInDirectionOfZVelFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_pitchInDirectionOfZVelFactor) },
		{ "ForwardVelocityPitchFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_forwardVelocityPitchFactor) },
		{ "LateralVelocityRollFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_lateralVelocityRollFactor) },
		{ "ForwardAccelerationPitchFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_forwardAccelerationPitchFactor) },
		{ "LateralAccelerationRollFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_lateralAccelerationRollFactor) },
		{ "UniformAxialDamping", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_uniformAxialDamping) },
		{ "TurnPivotOffset", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_turnPivotOffset) },
		{ "Apply2DFrictionWhenAirborne", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_apply2DFrictionWhenAirborne) },
		{ "DownhillOnly", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_downhillOnly) },
		{ "AllowAirborneMotiveForce", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_allowAirborneMotiveForce) },
		{ "LocomotorWorksWhenDead", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_locomotorWorksWhenDead) },
		{ "AirborneTargetingHeight", INI::parseInt, nullptr, offsetof(LocomotorTemplate, m_airborneTargetingHeight) },
		{ "StickToGround", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_stickToGround) },
		{ "CanMoveBackwards", LocomotorTemplate::parseCanMoveBackwards, nullptr, offsetof(LocomotorTemplate, m_canMoveBackwards) },
		{ "HasSuspension", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_hasSuspension) },
		{ "FrontWheelTurnAngle", INI::parseAngleReal, nullptr, offsetof(LocomotorTemplate, m_frontWheelTurnAngle) },
		{ "MaximumWheelExtension", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_maximumWheelExtension) },
		{ "MaximumWheelCompression", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_maximumWheelCompression) },
		{ "CloseEnoughDist", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_closeEnoughDist) },
		{ "CloseEnoughDist3D", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_closeEnoughDist3D) },
		{ "SlideIntoPlaceTime", INI::parseDurationReal, nullptr, offsetof(LocomotorTemplate, m_slideIntoPlaceTime) },
		{ "CrewPowered", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_crewPowered) },
		{ "UseTerrainSmoothing", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_useTerrainSmoothing) },
		{ "WanderWidthFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_wanderWidthFactor) },
		{ "WanderLengthFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_wanderLengthFactor) },
		{ "WanderAboutPointRadius", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_wanderAboutPointRadius) },
		{ "BurningDeathRadius", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_burningDeathRadius) },
		{ "BurningDeathIsCavalry", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_burningDeathIsCavalry) },
		{ "ChargeSpeed", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_chargeSpeed) },
		{ "ChargeAvailable", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_chargeAvailable) },
		{ "ChargeIgnoresCondition", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_chargeIgnoresCondition) },
		{ "EnableHighSpeedTurnModelconditions", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_enableHighSpeedTurnModelconditions) },
		{ "WaitForFormation", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_waitForFormation) },
		{ "RudderCorrectionDegree", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_rudderCorrectionDegree) },
		{ "RudderCorrectionRate", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_rudderCorrectionRate) },
		{ "ElevatorCorrectionDegree", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_elevatorCorrectionDegree) },
		{ "ElevatorCorrectionRate", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_elevatorCorrectionRate) },
		{ "AeleronCorrectionDegree", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_aeleronCorrectionDegree) },
		{ "AeleronCorrectionRate", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_aeleronCorrectionRate) },
		{ "SwoopStandoffRadius", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_swoopStandoffRadius) },
		{ "SwoopStandoffHeight", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_swoopStandoffHeight) },
		{ "SwoopTerminalVelocity", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_swoopTerminalVelocity) },
		{ "SwoopAccelerationRate", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_swoopAccelerationRate) },
		{ "SwoopSpeedTuningFactor", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_swoopSpeedTuningFactor) },
		{ "BackingUpSpeed", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_backingUpSpeed) },
		{ "BackingUpStopWhenTurning", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_backingUpStopWhenTurning) },
		{ "BackingUpDistanceMin", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_backingUpDistanceMin) },
		{ "BackingUpDistanceMax", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_backingUpDistanceMax) },
		{ "BackingUpAngle", INI::parseReal, nullptr, offsetof(LocomotorTemplate, m_backingUpAngle) },
		{ "RiverModifier", INI::parsePercentToReal, nullptr, offsetof(LocomotorTemplate, m_riverModifier) },
		{ "ScalesWalls", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_scalesWalls) },
		{ "TurnWhileMoving", INI::parseBool, nullptr, offsetof(LocomotorTemplate, m_turnWhileMoving) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}

const LocomotorTemplate *LocomotorTemplate::getFinalOverride() const
{
	const LocomotorTemplate *t = this;
	while (t->m_override)
	{
		t = t->m_override.get();
	}
	return t;
}

LocomotorTemplate *LocomotorTemplate::getFinalOverride()
{
	LocomotorTemplate *t = this;
	while (t->m_override)
	{
		t = t->m_override.get();
	}
	return t;
}

// ---- LocomotorStore ------------------------------------------------------------------------------------------
const LocomotorTemplate *LocomotorStore::findLocomotorTemplate(const std::string &name) const
{
	const auto it = m_templates.find(name);
	return it == m_templates.end() ? nullptr : it->second.get();
}

LocomotorTemplate *LocomotorStore::findLocomotorTemplate(const std::string &name)
{
	const auto it = m_templates.find(name);
	return it == m_templates.end() ? nullptr : it->second.get();
}

std::vector<std::string> LocomotorStore::names() const
{
	std::vector<std::string> out;
	for (const auto &e : m_templates)
	{
		out.push_back(e.first);
	}
	return out;
}

// RW 0x5E4A74: a new template copied from `base` (the final override), linked as base's override.
LocomotorTemplate *LocomotorStore::newOverride(LocomotorTemplate *base)
{
	if (!base)
	{
		return nullptr;
	}
	std::shared_ptr<LocomotorTemplate> copy = std::make_shared<LocomotorTemplate>(*base);
	copy->m_override.reset();
	copy->m_isOverride = true;
	base->m_override = copy;
	return copy.get();
}

void LocomotorStore::parseLocomotorTemplateDefinitionGlobal(INI *ini)
{
	if (!TheLocomotorStore)
	{
		throw INIException(3, "TheLocomotorStore==NULL"); // RW 0xBF4B30, RW 0x5E8290
	}
	TheLocomotorStore->parseLocomotorTemplateDefinition(ini);
}

// RW 0x5E8276.
void LocomotorStore::parseLocomotorTemplateDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	LocomotorTemplate *existing = findLocomotorTemplate(name);
	LocomotorTemplate *lt = nullptr;

	if (existing)
	{
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
		{
			lt = newOverride(existing->getFinalOverride());
		}
		else if (ini->getLoadType() == INI_LOAD_RELOAD)
		{
			// RW 0x5E831A: the old template is erased from the map and parked in a delete list; a fresh
			// template takes the name.
			m_reloaded.push_back(m_templates[name]);
			std::shared_ptr<LocomotorTemplate> fresh = std::make_shared<LocomotorTemplate>();
			m_templates[name] = fresh;
			lt = fresh.get();
		}
		else
		{
			// RW 0x5E8311 -> 0x5E83EE: nothing is read; the block's lines reach the dispatcher.
			return;
		}
	}
	else
	{
		std::shared_ptr<LocomotorTemplate> fresh = std::make_shared<LocomotorTemplate>();
		if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
		{
			fresh->m_isOverride = true; // RW 0x5E8397
		}
		m_templates[name] = fresh;
		lt = fresh.get();
	}

	if (!lt)
	{
		return;
	}
	lt->m_name = name; // RW 0x5E83B3: set before the fields are read
	ini->initFromINI(lt, LocomotorTemplate::getFieldParse());
	lt->applyParseDefaults();
}

// ---- LocomotorSet ---------------------------------------------------------------------------------------------
const LocomotorSetTemplate::Slot *LocomotorSetTemplate::find(int condition) const
{
	const auto it = m_slots.find(condition);
	return it == m_slots.end() ? nullptr : &it->second;
}

bool LocomotorSetTemplate::specified(int condition) const
{
	const Slot *s = find(condition);
	return s && !s->locomotors.empty();
}

void LocomotorSetTemplate::set(int condition, const LocomotorTemplate *locomotor, float speed)
{
	Slot &slot = m_slots[condition];
	slot.locomotors.clear();
	slot.locomotors.push_back(locomotor);
	slot.speed = speed;
	slot.hasSpeed = true;
}

namespace
{
// The temporary the entry fields parse into (RW 0x5E6869 layout: +0x18 Locomotor, +0x1C Condition,
// +0x20 Speed; both strings start as "" (RW 0x5E9B90)).
struct LocomotorSetEntry
{
	float speed = 0.0f;
	std::string condition;
	std::string locomotor;
};

const FieldParse *locomotorSetEntryFieldParse()
{
	// RW 0xBF4BE0: Speed (parseReal), Condition (parseAsciiString), Locomotor (parseAsciiString)
	static const FieldParse table[] = {
		{ "Speed", INI::parseReal, nullptr, (int)offsetof(LocomotorSetEntry, speed) },
		{ "Condition", INI::parseAsciiString, nullptr, (int)offsetof(LocomotorSetEntry, condition) },
		{ "Locomotor", INI::parseAsciiString, nullptr, (int)offsetof(LocomotorSetEntry, locomotor) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	return table;
}
}

// RW 0x73BF6B + 0x5E9AB1.
void parseLocomotorSet(INI *ini, void *instance, void *, const void *)
{
	LocomotorSetOwner *owner = static_cast<LocomotorSetOwner *>(instance);
	LocomotorSetEntry entry;
	ini->initFromINI(&entry, locomotorSetEntryFieldParse());

	if (!owner->hasAIUpdateModule())
	{
		throw INIException(3, "Attempted to specify a locomotor for object %s without an AIUpdate\tblock.", owner->locomotorSetObjectName().c_str());
	}

	const int condition = INI::scanIndexList(entry.condition.c_str(), TheLocomotorSetNames);

	if (!TheLocomotorStore)
	{
		throw INIException(3, "TheLocomotorStore==NULL");
	}
	const LocomotorTemplate *loco = entry.locomotor.empty() ? nullptr : TheLocomotorStore->findLocomotorTemplate(entry.locomotor);

	if (owner->aiUpdateHasLocomotorsFor(condition) && ini->getLoadType() != INI_LOAD_CREATE_OVERRIDES && ini->getLoadType() != INI_LOAD_CHILD_OBJECT)
	{
		throw INIException(3, "re-specifying a LocomotorSet\tis no longer allowed");
	}

	LocomotorSetTemplate &set = owner->locomotorSet();
	set.set(condition, loco, entry.speed);
	if (!loco)
	{
		set.noteUnresolved(entry.locomotor);
	}
}
