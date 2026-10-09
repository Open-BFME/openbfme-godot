// OpenBFME. GPL-3.0.
// See GameLogic/Module/StructureModules.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/HeroModules.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kPhaseNames[] = { "INITIAL", "DELAY", "BURST", "ALMOST_FINAL", "FINAL", nullptr }; // RW 0xDB0E7C

// FXList = <phase> <name> [<name> ...] (RW 0x8A818F) and OCL = <phase> <name> ... (RW 0x8A81F2): the phase is looked up in the name table (an unknown one is an INI error), then one
// entry per name token (the stores resolve a name to a list or to nothing: the FX and OCL stores are not loaded here, S-340, so the names are kept; "None" is a valid entry in retail
// too: it stands for no list but still counts for the random pick)
void parsePhaseNames(INI *ini, std::vector<std::string> *lists)
{
	const int phase = INI::scanIndexList(ini->getNextToken(), kPhaseNames);
	for (const char *t = ini->getNextToken(); t; t = ini->getNextTokenOrNull())
	{
		lists[phase].push_back(t);
	}
}
void parseCollapseFX(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<StructureCollapseUpdateModuleData *>(instance)->m_fx);
}
void parseCollapseOCL(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<StructureCollapseUpdateModuleData *>(instance)->m_ocl);
}

#define SC_OFF(member) (int)offsetof(StructureCollapseUpdateModuleData, member)
// RW table 0xC69238 (in the binary's row order) then the DieMux table 0xC76BD8
const FieldParse kStructureCollapseParse[] = {
	{ "MinCollapseDelay", INI::parseDurationUnsignedInt, nullptr, SC_OFF(m_minCollapseDelay) },
	{ "MaxCollapseDelay", INI::parseDurationUnsignedInt, nullptr, SC_OFF(m_maxCollapseDelay) },
	{ "MinBurstDelay", INI::parseDurationUnsignedInt, nullptr, SC_OFF(m_minBurstDelay) },
	{ "MaxBurstDelay", INI::parseDurationUnsignedInt, nullptr, SC_OFF(m_maxBurstDelay) },
	{ "CollapseDamping", INI::parseReal, nullptr, SC_OFF(m_collapseDamping) },
	{ "MaxShudder", INI::parseReal, nullptr, SC_OFF(m_maxShudder) },
	{ "BigBurstFrequency", INI::parseInt, nullptr, SC_OFF(m_bigBurstFrequency) },
	{ "OCL", parseCollapseOCL, nullptr, 0 },
	{ "FXList", parseCollapseFX, nullptr, 0 },
	{ "DestroyObjectWhenDone", INI::parseBool, nullptr, SC_OFF(m_destroyObjectWhenDone) },
	{ "CollapseHeight", INI::parseReal, nullptr, SC_OFF(m_collapseHeight) },
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, SC_OFF(m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, SC_OFF(m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, SC_OFF(m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, SC_OFF(m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, SC_OFF(m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, SC_OFF(m_dieMux.m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SC_OFF

void parseObjectFilterPtr(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}
void parseFXNameString(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73A302: the FX list store is the client's (S-340)
}
void parseUpgradeNameString(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // RW 0x73AF89 looks the upgrade template up; the Upgrade store is UPGRADE-1's
}

// RW table 0xC723FC (RespawnBody), 0xC72628 (DelayedDeathBody), 0xC0602C (SymbioticStructuresBody), 0xC31860 (LifetimeUpdate)
const FieldParse kRespawnParse[] = {
	{ "PermanentlyKilledByFilter", parseObjectFilterPtr, nullptr, (int)offsetof(RespawnBodyModuleData, m_permanentlyKilledByFilter) },
	{ "CanRespawn", INI::parseBool, nullptr, (int)offsetof(RespawnBodyModuleData, m_canRespawn) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kDelayedDeathParse[] = {
	{ "DelayedDeathTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(DelayedDeathBodyModuleData, m_delayedDeathTime) },
	{ "ImmortalUntilDeathTime", INI::parseBool, nullptr, (int)offsetof(DelayedDeathBodyModuleData, m_immortalUntilDeathTime) },
	{ "DoHealthCheck", INI::parseBool, nullptr, (int)offsetof(DelayedDeathBodyModuleData, m_doHealthCheck) },
	{ "InvulnerableFX", parseFXNameString, nullptr, (int)offsetof(DelayedDeathBodyModuleData, m_invulnerableFX) },
	{ "DelayedDeathPrerequisiteUpgrade", parseUpgradeNameString, nullptr, (int)offsetof(DelayedDeathBodyModuleData, m_prerequisiteUpgrade) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kSymbioticParse[] = {
	{ "Symbiote", INI::parseAsciiString, nullptr, (int)offsetof(SymbioticStructuresBodyModuleData, m_symbiote) },
	{ nullptr, nullptr, nullptr, 0 }
};
const char *const kDeathTypeNames[] = { "NORMAL", "NONE", "CRUSHED", "BURNED", "EXPLODED", "POISONED", "TOPPLED", "FLOODED", "SUICIDED", "LASERED", "DETONATED", "SPLATTED", "POISONED_BETA",
	"EXTRA_2", "EXTRA_3", "EXTRA_4", "EXTRA_5", "EXTRA_6", "EXTRA_7", "EXTRA_8", "KNOCKBACK", "SUPERNATURAL", "FADED", "SLAUGHTERED", nullptr }; // RW 0xDA39E8 order, the DeathType row's list
const FieldParse kLifetimeParse[] = {
	{ "MinLifetime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(LifetimeUpdateModuleData, m_minLifetime) },
	{ "MaxLifetime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(LifetimeUpdateModuleData, m_maxLifetime) },
	{ "WaitForWakeUp", INI::parseBool, nullptr, (int)offsetof(LifetimeUpdateModuleData, m_waitForWakeUp) },
	{ "ScoreKill", INI::parseBool, nullptr, (int)offsetof(LifetimeUpdateModuleData, m_scoreKill) },
	{ "DeathType", INI::parseIndexList, kDeathTypeNames, (int)offsetof(LifetimeUpdateModuleData, m_deathType) },
	{ nullptr, nullptr, nullptr, 0 }
};

template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}

void setBit(Object::ModelConditionBits &bits, int bit)
{
	bits[(size_t)bit >> 5] |= 1u << (bit & 31);
}
} // namespace

void StructureCollapseUpdateModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kStructureCollapseParse); }
void RespawnBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	ActiveBodyModuleData::buildFieldParse(p); // RW 0xC71D68
	p.add(kRespawnParse);                     // RW 0xC723FC
}
void DelayedDeathBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	RespawnBodyModuleData::buildFieldParse(p);
	p.add(kDelayedDeathParse); // RW 0xC72628
}
void SymbioticStructuresBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	ActiveBodyModuleData::buildFieldParse(p);
	p.add(kSymbioticParse); // RW 0xC0602C
}
void LifetimeUpdateModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kLifetimeParse); }
// RW 0x76406F + 0x763D11: PermanentlyKilledByFilter starts as NONE with empty masks
RespawnBodyModuleData::RespawnBodyModuleData()
	: m_permanentlyKilledByFilter(std::make_shared<const ObjectFilter>(ObjectFilter::none(KindOfMaskType{}, KindOfMaskType{})))
{
}
void InactiveBodyModuleData::buildFieldParse(MultiIniFieldParse &) {} // RW 0x708243: no field table

void StructureModules::registerAll(ModuleFactory &modules)
{
	// StructureBody: ActiveBody's data (RW tables 0xC71D68 + the empty 0xC84858)
	modules.bindTypedData<ActiveBodyModuleData>("StructureBody", MODULETYPE_BEHAVIOR);
	bindRuntime<StructureBody, ActiveBodyModuleData>(modules, "StructureBody");
	modules.bindTypedData<InactiveBodyModuleData>("InactiveBody", MODULETYPE_BEHAVIOR);
	bindRuntime<InactiveBody, InactiveBodyModuleData>(modules, "InactiveBody");
	modules.bindTypedData<RespawnBodyModuleData>("RespawnBody", MODULETYPE_BEHAVIOR);
	bindRuntime<RespawnBody, RespawnBodyModuleData>(modules, "RespawnBody");
	modules.bindTypedData<DelayedDeathBodyModuleData>("DelayedDeathBody", MODULETYPE_BEHAVIOR);
	bindRuntime<DelayedDeathBody, DelayedDeathBodyModuleData>(modules, "DelayedDeathBody");
	modules.bindTypedData<SymbioticStructuresBodyModuleData>("SymbioticStructuresBody", MODULETYPE_BEHAVIOR);
	bindRuntime<SymbioticStructuresBody, SymbioticStructuresBodyModuleData>(modules, "SymbioticStructuresBody");
	modules.bindTypedData<LifetimeUpdateModuleData>("LifetimeUpdate", MODULETYPE_BEHAVIOR);
	bindRuntime<LifetimeUpdate, LifetimeUpdateModuleData>(modules, "LifetimeUpdate");
	modules.bindTypedData<StructureCollapseUpdateModuleData>("StructureCollapseUpdate", MODULETYPE_BEHAVIOR);
	bindRuntime<StructureCollapseUpdate, StructureCollapseUpdateModuleData>(modules, "StructureCollapseUpdate");
}

// ---- StructureBody ------------------------------------------------------------------------------------------------------------------
void StructureBody::setConstructorObject(const Object *obj)
{
	if (obj)
	{
		m_constructorObjectID = obj->getID();
	}
}

void StructureBody::crc(StateHasher &h) const
{
	ActiveBody::crc(h);
	h.addU32(m_constructorObjectID);
}

// ---- InactiveBody -------------------------------------------------------------------------------------------------------------------
InactiveBody::InactiveBody(Thing *thing, const InactiveBodyModuleData *data)
	: BehaviorModule(thing, data)
{
	getObject()->friend_setEffectivelyDead(true); // RW 0x8C1A43 -> 0x68D950
}

// RW 0x8C18F7
float InactiveBody::estimateDamage(const DamageInfoInput &input) const
{
	return input.m_damageType == DAMAGE_UNRESISTABLE ? input.m_amount : 0.0f;
}

// RW 0x8C1A75
void InactiveBody::attemptDamage(DamageInfo &info)
{
	if (info.m_input.m_damageType == DAMAGE_HEALING)
	{
		attemptHealing(info);
		return;
	}
	info.m_output.m_actualDamageDealt = 0.0f;
	info.m_output.m_actualDamageClipped = 0.0f;
	info.m_output.m_noEffect = true;
	if (info.m_input.m_damageType == DAMAGE_UNRESISTABLE)
	{
		info.m_output.m_noEffect = false;
		// no health: no damage modules, no damage FX, but the die modules run (once)
		if (!m_dieCalled)
		{
			DieModuleInterface::Event event;
			event.sourceId = info.m_input.m_sourceID;
			event.deathType = info.m_input.m_deathType;
			event.damageType = info.m_input.m_damageType;
			event.damageSubType = info.m_input.m_damageSubType; // lane HERO-2
			event.damageAmount = info.m_input.m_amount;
			event.actualDamageDealt = info.m_output.m_actualDamageDealt;
			event.actualDamageClipped = info.m_output.m_actualDamageClipped;
			getObject()->friend_onDie(event);
			m_dieCalled = true;
		}
	}
}

void InactiveBody::attemptHealing(DamageInfo &info)
{
	if (info.m_input.m_damageType != DAMAGE_HEALING)
	{
		attemptDamage(info);
		return;
	}
	info.m_output.m_actualDamageDealt = 0.0f;
	info.m_output.m_actualDamageClipped = 0.0f;
	info.m_output.m_noEffect = true;
}

void InactiveBody::crc(StateHasher &h) const
{
	BehaviorModule::crc(h);
	h.addBool(m_dieCalled);
}

// ---- StructureCollapseUpdate -----------------------------------------------------------------------------------------------------------
StructureCollapseUpdate::StructureCollapseUpdate(Thing *thing, const StructureCollapseUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x8A7A04: the geometry's maximum height above the position (RW 0xAD1920 over every active shape: GameLogic/Object/ObjectGeometry.h).
float StructureCollapseUpdate::getCollapseHeight() const
{
	if (!m_data->m_destroyObjectWhenDone)
	{
		return m_data->m_collapseHeight;
	}
	const float geometryHeight = ObjectGeometry::maxHeightAbovePosition(ObjectGeometry::shapesOf(*getObject()->getTemplate()));
	return m_data->m_collapseHeight <= geometryHeight ? geometryHeight : m_data->m_collapseHeight;
}

// RW 0x8A7CCC
void StructureCollapseUpdate::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*obj, event, &angle))
	{
		return;
	}
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		ai->onDie(); // RW 0x66264E markAsDead
	}
	// RW 0x625759: the object is deselected for every player (the selection is InGameUI's, which forgets a destroyed object)
	begin();
}

// RW 0x8A7BF9
void StructureCollapseUpdate::begin()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	m_position = *obj->getPosition();
	m_collapseFrame = logic.getFrame() + (unsigned)logic.random().getValue((int)m_data->m_minCollapseDelay, (int)m_data->m_maxCollapseDelay, "StructureCollapseUpdate.cpp", 141);
	doPhase(SCPHASE_INITIAL);
	m_state = COLLAPSESTATE_WAITINGFORCOLLAPSESTART;
	m_currentHeight = 0.0f;
	++logic.combat().counters().collapsesBegun;
	if (obj->testModelCondition(CombatNames::modelCondition("DESTROYED_WHILST_BEING_CONSTRUCTED")))
	{
		// RW 0x8A7C6D..0x8A7CB5: a building destroyed during its construction starts as low as it had risen
		const float percent = obj->getConstructionPercent();
		const float h = getCollapseHeight();
		const float remaining = SimMath::pc24Sub(1.0f, SimMath::pc24Mul(percent, 0.01f));
		m_currentHeight = SimMath::subf32(0.0f, SimMath::pc24Mul(h, remaining)); // fchs
		Coord3D p;
		p.x = m_position.x;
		p.y = m_position.y;
		p.z = SimMath::pc24Add(m_currentHeight, m_position.z);
		obj->setPosition(&p);
	}
	setWakeFrame(obj, UPDATE_SLEEP_NONE);
}

// RW 0x8A7A62: one random entry of each non-empty list (RW 0x8A78D5 draws random(0, size - 1) per entry of the count, redrawing a duplicate); the FX list of the entry would play at
// the object's position and the OCL would create at it: both are not ported (S-340), the draw and the use are
void StructureCollapseUpdate::doPhase(StructureCollapsePhase phase)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	CombatState::Counters &c = logic.combat().counters();
	// FX first (RW 0x8A7A8E: the vector at data + 0x90 + 12 * phase, the count at + 0xE0), the OCLs after (data + 0x54, count + 0xCC)
	struct Pass
	{
		const std::vector<std::string> *list;
		int count;
	};
	for (const Pass &pass : { Pass{ &m_data->m_fx[phase], m_data->m_fxCount[phase] }, Pass{ &m_data->m_ocl[phase], m_data->m_oclCount[phase] } })
	{
		const int size = (int)pass.list->size();
		if (size <= 0)
		{
			continue;
		}
		if (pass.count > size)
		{
			throw std::logic_error("StructureCollapseUpdate: a phase asks for more distinct entries than its list has (retail loops forever)");
		}
		int picked[32];
		const int count = pass.count > 32 ? 32 : pass.count;
		for (int i = 0; i < count; ++i)
		{
			int idx;
			bool again;
			do
			{
				idx = logic.random().getValue(0, size - 1, "StructureCollapseUpdate.cpp", 325);
				again = false;
				for (int j = 0; j < i; ++j)
				{
					again = again || picked[j] == idx;
				}
			} while (again);
			picked[i] = idx;
			++c.collapseEffectsUnported; // hashed run counter, kept (lane FX-2 changes no hash)
			if (pass.list == &m_data->m_fx[phase])
			{
				// lane FX-2: RW 0x8A7A62 draws every pick first (RW 0x8A78D5), then plays each through RW 0x494615 doFXPos(fx, position, null, 0, null); the
				// position is the caller's argument, taken as the object's position (INFERENCE: the callers of 0x8A7A62 were not read). The draws do not interleave
				// with anything the play does (the play draws no logic random), so emitting per pick keeps retail's order
				FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, "StructureCollapse", logic.getFrame(), (*pass.list)[(size_t)idx], *obj);
				e.hasTransform = false;
				logic.fxEvents().emit(e);
			}
		}
	}
}

// RW 0x8A7D1D
UpdateSleepTime StructureCollapseUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const StructureCollapseUpdateModuleData *d = m_data;
	if (m_state == COLLAPSESTATE_STANDING)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (m_state == COLLAPSESTATE_WAITINGFORCOLLAPSESTART)
	{
		// RW draws two CLIENT random values for the drawable's shudder here (the instance matrix is the client's): no logic input
		const unsigned now = logic.getFrame();
		if (now >= m_collapseFrame)
		{
			m_state = COLLAPSESTATE_COLLAPSING;
			doPhase(SCPHASE_BURST);
			m_burstFrame = now + (unsigned)logic.random().getValue((int)d->m_minBurstDelay, (int)d->m_maxBurstDelay, "StructureCollapseUpdate.cpp", 216);
		}
	}
	if (m_state == COLLAPSESTATE_COLLAPSING)
	{
		if (!logic.settings().structureRulesLoaded)
		{
			throw std::logic_error("StructureCollapseUpdate: GameLogicSettings has no Gravity (GameData not loaded)");
		}
		const unsigned now = logic.getFrame();
		const float oldHeight = m_currentHeight;
		const float newHeight = SimMath::subf32(oldHeight, m_velocity); // SSE subss
		m_currentHeight = newHeight;
		// x87 at 24 bits: velocity - ((1 - damping) * gravity)
		m_velocity = SimMath::pc24Sub(m_velocity, SimMath::pc24Mul(SimMath::pc24Sub(1.0f, d->m_collapseDamping), logic.settings().gravity));
		// the first logic draw goes to y, the second to x (the retail evaluation order of the two calls)
		const float shudderY = logic.random().getValueReal(SimMath::subf32(0.0f, d->m_maxShudder), d->m_maxShudder, "StructureCollapseUpdate.cpp", 238);
		const float shudderX = logic.random().getValueReal(SimMath::subf32(0.0f, d->m_maxShudder), d->m_maxShudder, "StructureCollapseUpdate.cpp", 238);
		Coord3D p;
		p.x = SimMath::pc24Add(shudderX, m_position.x);
		p.y = SimMath::pc24Add(shudderY, m_position.y);
		p.z = SimMath::addf32(m_position.z, newHeight);
		obj->setPosition(&p);
		if (now >= m_burstFrame)
		{
			if (logic.random().getValue(1, d->m_bigBurstFrequency, "StructureCollapseUpdate.cpp", 247) == 1)
			{
				doPhase(SCPHASE_BURST);
			}
			else
			{
				doPhase(SCPHASE_DELAY);
			}
			m_burstFrame += (unsigned)logic.random().getValue((int)d->m_minBurstDelay, (int)d->m_maxBurstDelay, "StructureCollapseUpdate.cpp", 256);
		}
		const float h = getCollapseHeight();
		if (!(0.0f < SimMath::pc24Add(h, m_currentHeight)))
		{
			m_state = COLLAPSESTATE_DONE;
			doPhase(SCPHASE_FINAL);
			// RW 0x8A7B2A: the bone FX stop (not ported), then DestroyObjectWhenDone queues the object for destruction
			if (d->m_destroyObjectWhenDone)
			{
				logic.destroyObject(obj);
			}
			Object::ModelConditionBits clear{}, set{};
			setBit(clear, CombatNames::modelCondition("AWAITING_CONSTRUCTION"));
			setBit(clear, CombatNames::modelCondition("PARTIALLY_CONSTRUCTED"));
			setBit(clear, CombatNames::modelCondition("ACTIVELY_BEING_CONSTRUCTED"));
			setBit(clear, CombatNames::modelCondition("RUBBLE"));
			setBit(set, CombatNames::modelCondition("POST_RUBBLE"));
			obj->clearAndSetModelConditionFlags(clear, set);
			obj->setOrientation(obj->getOrientation());
			++logic.combat().counters().collapsesDone;
			return UPDATE_SLEEP_FOREVER;
		}
		// RW 0x8A80D3: in the last 30 percent of the fall the ALMOST_FINAL phase fires on every frame
		if (!(0.0f < SimMath::addf32(SimMath::mulf32(h, 0.7f), m_currentHeight)))
		{
			doPhase(SCPHASE_ALMOST_FINAL);
		}
	}
	return UPDATE_SLEEP_NONE;
}

void StructureCollapseUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_collapseFrame);
	h.addU32(m_burstFrame);
	h.addU32((std::uint32_t)m_state);
	h.addFloat(m_velocity);
	h.addFloat(m_currentHeight);
	h.addFloat(m_position.x);
	h.addFloat(m_position.y);
	h.addFloat(m_position.z);
}

// ---- RespawnBody -------------------------------------------------------------------------------------------------------------------------------
bool RespawnBody::wouldDie(float delta) const
{
	return delta == delta && !(delta > SimMath::subf32(0.0f, getHealth())); // RW 0x8C557D: -health compared with delta (jb: delta above -health is not lethal)
}

bool RespawnBody::permanentlyKilled() const
{
	if (!m_respawn->m_canRespawn)
	{
		return true;
	}
	const DamageInfo *info = currentDamageInfo();
	if (!info)
	{
		return false;
	}
	const Object &obj = *getObject();
	const Object *killer = obj.logic().findObjectByID(info->m_input.m_sourceID);
	return killer && m_respawn->m_permanentlyKilledByFilter &&
		ObjectFilterMatch::allows(obj.logic(), *m_respawn->m_permanentlyKilledByFilter, *killer, obj.getControllingPlayer());
}

// RW 0x8C553F. Lane HERO-1: the object's RespawnUpdate is told after ActiveBody's change (RW 0x8C55E2 .. 0x8C5674): a permanent lethal hit -> RW 0x8B3349; a
// lethal hit that is not permanent: a TEMPORARILY_DEFECTED object's defection ends first (RW 0x69ABA7, not ported: S-853), then an INHERITED_FROM_ALLY_TEAM
// object (status 80) is permanently killed (RW 0x8B3349 and state 0), any other starts its revival (RW 0x8B3744). The death itself (RW 0x698F06) is ActiveBody's
// attemptDamage (S-853). A lethal hit on an object without a RespawnUpdate stays counted (respawnWithoutUpdate).
void RespawnBody::internalChangeHealth(float delta)
{
	const bool lethal = delta != 0.0f && wouldDie(delta);
	const bool permanent = lethal && permanentlyKilled();
	ActiveBody::internalChangeHealth(delta);
	if (delta == 0.0f)
	{
		return; // RW 0x8C5549: a zero delta does nothing at all
	}
	Object *obj = getObject();
	RespawnUpdate *ru = dynamic_cast<RespawnUpdate *>(obj->findModule("RespawnUpdate"));
	if (lethal && !ru)
	{
		++obj->logic().combat().counters().respawnWithoutUpdate;
	}
	if (!ru)
	{
		return;
	}
	if (permanent)
	{
		ru->onPermanentDeath();
		return;
	}
	if (!lethal)
	{
		return;
	}
	constexpr unsigned kDefected = 0x3E;  // RW 0x8C562F: push 0x3E (TEMPORARILY_DEFECTED)
	constexpr unsigned kInherited = 0x50; // RW 0x8C5643: push 0x50 (INHERITED_FROM_ALLY_TEAM)
	if (obj->testStatus(kDefected))
	{
		obj->logic().noteStop("[S-853] RespawnBody: a TEMPORARILY_DEFECTED hero's lethal hit ends its defection (RW 0x69ABA7): not ported");
	}
	if (obj->testStatus(kInherited))
	{
		ru->onPermanentDeath();
		ru->setState(RespawnUpdate::STATE_ALIVE); // RW 0x8C5657
	}
	else
	{
		ru->onDeath();
	}
}

// ---- DelayedDeathBody -----------------------------------------------------------------------------------------------------------------------
void DelayedDeathBody::internalChangeHealth(float delta)
{
	Object &obj = *getObject();
	GameLogic &logic = obj.logic();
	DamageInfo *info = currentDamageInfo();
	const DelayedDeathBodyModuleData *d = m_delayed;
	const Player *player = obj.getControllingPlayer();
	bool transitioned = false;
	float amount = delta;
	if (!m_started)
	{
		bool trigger = (m_checked && info && info->m_input.m_kill) || (d->m_doHealthCheck && !(SimMath::addf32(getHealth(), amount) > 0.0f));
		if (trigger && !d->m_prerequisiteUpgrade.empty())
		{
			const bool playerHas = player && player->hasUpgradeComplete(d->m_prerequisiteUpgrade);
			const bool objectHas = obj.hasUpgrade(d->m_prerequisiteUpgrade);
			trigger = playerHas || objectHas;
		}
		if (trigger)
		{
			if (LifetimeUpdate *life = dynamic_cast<LifetimeUpdate *>(obj.findModule("LifetimeUpdate")))
			{
				life->setLifetimeRange(d->m_delayedDeathTime, d->m_delayedDeathTime);
			}
			++logic.combat().counters().collapseEffectsUnported; // the InvulnerableFX list: hashed run counter, kept (lane FX-2 changes no hash)
			// lane FX-2: the InvulnerableFX call (INFERENCE: played on the object as doFXObj(fx, obj); the RW call inside 0x8C5828 was not read)
			logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "DelayedDeathBody InvulnerableFX", logic.getFrame(), d->m_invulnerableFX, obj));
			if (info)
			{
				info->m_input.m_kill = false;
			}
			m_started = true;
			transitioned = true;
			++logic.combat().counters().delayedDeaths;
		}
	}
	if ((m_checked || m_started) && (d->m_immortalUntilDeathTime || transitioned))
	{
		amount = 0.0f;
	}
	if (m_started && info && info->m_input.m_kill)
	{
		amount = SimMath::subf32(0.0f, getHealth());
	}
	RespawnBody::internalChangeHealth(amount);
}

void DelayedDeathBody::crc(StateHasher &h) const
{
	ActiveBody::crc(h);
	h.addBool(m_started);
	h.addBool(m_checked);
}

// ---- SymbioticStructuresBody -----------------------------------------------------------------------------------------------------------------
BodyModuleInterface *SymbioticStructuresBody::symbioteBody() const
{
	if (m_symbioteId == INVALID_ID)
	{
		return nullptr;
	}
	Object *o = getObject()->logic().findObjectByID(m_symbioteId);
	return o ? o->getBodyModule() : nullptr;
}

float SymbioticStructuresBody::getHealth() const
{
	BodyModuleInterface *b = symbioteBody();
	return b ? b->getHealth() : 0.0f;
}
float SymbioticStructuresBody::getMaxHealth() const
{
	BodyModuleInterface *b = symbioteBody();
	return b ? b->getMaxHealth() : 0.0f;
}
float SymbioticStructuresBody::getInitialHealth() const
{
	BodyModuleInterface *b = symbioteBody();
	return b ? b->getInitialHealth() : 0.0f;
}
BodyDamageType SymbioticStructuresBody::getDamageState() const
{
	BodyModuleInterface *b = symbioteBody();
	return b ? b->getDamageState() : BODY_PRISTINE;
}
void SymbioticStructuresBody::attemptDamage(DamageInfo &) {} // RW slot 0 is a `ret 4` stub: the symbiotic object itself takes no damage
void SymbioticStructuresBody::attemptHealing(DamageInfo &info)
{
	if (BodyModuleInterface *b = symbioteBody())
	{
		b->attemptHealing(info);
	}
}
float SymbioticStructuresBody::estimateDamage(const DamageInfoInput &input) const
{
	BodyModuleInterface *b = symbioteBody();
	return b ? b->estimateDamage(input) : 0.0f;
}
void SymbioticStructuresBody::crc(StateHasher &h) const
{
	ActiveBody::crc(h);
	h.addU32(m_symbioteId);
}

// ---- LifetimeUpdate ----------------------------------------------------------------------------------------------------------------------------
LifetimeUpdate::LifetimeUpdate(Thing *thing, const LifetimeUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (data->m_waitForWakeUp)
	{
		setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
	}
	else
	{
		setWakeFrame(getObject(), UPDATE_SLEEP((int)calcSleepDelay(data->m_minLifetime, data->m_maxLifetime)));
	}
}

unsigned LifetimeUpdate::calcSleepDelay(unsigned minFrames, unsigned maxFrames)
{
	GameLogic &logic = getObject()->logic();
	int delay = logic.random().getValue((int)minFrames, (int)maxFrames, "LifetimeUpdate.cpp", 118);
	if ((unsigned)delay < 1u)
	{
		delay = 1;
	}
	m_startFrame = logic.getFrame();
	m_dieFrame = m_startFrame + (unsigned)delay;
	return (unsigned)delay;
}

void LifetimeUpdate::setLifetimeRange(unsigned minFrames, unsigned maxFrames)
{
	setWakeFrame(getObject(), UPDATE_SLEEP((int)calcSleepDelay(minFrames, maxFrames)));
}

void LifetimeUpdate::wakeUp()
{
	setLifetimeRange(m_data->m_minLifetime, m_data->m_maxLifetime);
}

UpdateSleepTime LifetimeUpdate::update()
{
	Object *obj = getObject();
	if (obj->testModelCondition(CombatNames::modelCondition("THROWN_PROJECTILE")))
	{
		return UPDATE_SLEEP_NONE; // RW 0x7A7FA7: a thrown projectile is not killed in the air
	}
	if (m_data->m_scoreKill)
	{
		BodyModuleInterface *body = obj->getBodyModule();
		if (ActiveBody *ab = dynamic_cast<ActiveBody *>(body))
		{
			if (Object *killer = obj->logic().findObjectByID(ab->lastDamager()))
			{
				killer->scoreTheKill(*obj); // RW 0x7A7FEA (0x6955BC)
			}
		}
	}
	else if (Player *p = obj->getControllingPlayer())
	{
		// lane END-1: RW 0x7A7FF1 .. 0x7A8021: without ScoreKill the owner's keeper takes the object back out of the built counts with counting off
		// (ScoreKeeper + 0x110 saved, cleared, addObjectBuilt(obj, -1), restored)
		ScoreKeeper &score = p->getScoreKeeper();
		const bool was = score.counting();
		score.setCounting(false);
		score.addObjectBuilt(obj->logic(), *obj, -1);
		score.setCounting(was);
	}
	obj->kill(m_data->m_deathType);
	return UPDATE_SLEEP_FOREVER;
}

void LifetimeUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_dieFrame);
	h.addU32(m_startFrame);
}
