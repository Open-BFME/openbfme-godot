// OpenBFME. GPL-3.0.
// See GameLogic/Module/StructureModules.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/StructureModules.h"
#include "GameLogic/Module/StructureToppleUpdate.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Module/HeroModules.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/AI/AIWorld.h"
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
void parsePhaseNames(INI *ini, std::vector<std::string> *lists, const char *const *phaseNames)
{
	const int phase = INI::scanIndexList(ini->getNextToken(), phaseNames);
	for (const char *t = ini->getNextToken(); t; t = ini->getNextTokenOrNull())
	{
		lists[phase].push_back(t);
	}
}
void parseCollapseFX(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<StructureCollapseUpdateModuleData *>(instance)->m_fx, kPhaseNames);
}
void parseCollapseOCL(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<StructureCollapseUpdateModuleData *>(instance)->m_ocl, kPhaseNames);
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

// RubbleRiseUpdate: the same two parsers over its own four phase lists. RW 0x8A8A1D (FXList) and 0x8A8A82 (OCL) read the index through the phase names
// table RW 0xDB0ED0 and push every following token into the 0xC-byte vector of that phase (FX base RW +0x88, OCL base +0x58, the same strides the collapse
// uses). The stores resolve a name to a list (FX 0x5E20A2, OCL rowed) or to nothing; the FX and OCL stores are not loaded here (S-340), so the names are kept.
const char *const kRubbleRisePhaseNames[] = { "INITIAL", "DELAY", "BURST", "FINAL", nullptr }; // RW 0xDB0ED0
void parseRubbleRiseFX(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<RubbleRiseUpdateModuleData *>(instance)->m_fx, kRubbleRisePhaseNames);
}
void parseRubbleRiseOCL(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<RubbleRiseUpdateModuleData *>(instance)->m_ocl, kRubbleRisePhaseNames);
}

#define RR_OFF(member) (int)offsetof(RubbleRiseUpdateModuleData, member)
// RW table 0xC69408 in the binary's row order (MinRubbleRiseDelay +0x38, MaxRubbleRiseDelay +0x3C, MinBurstDelay +0x40, MaxBurstDelay +0x44,
// BigBurstFrequency +0x48, RubbleRiseDamping +0x4C, RubbleHeight +0x50, MaxShudder +0x54, OCL, FXList), then the DieMux table RW 0xC76BD8 at +8
const FieldParse kRubbleRiseParse[] = {
	{ "MinRubbleRiseDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_minRubbleRiseDelay) },
	{ "MaxRubbleRiseDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_maxRubbleRiseDelay) },
	{ "MinBurstDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_minBurstDelay) },
	{ "MaxBurstDelay", INI::parseDurationUnsignedInt, nullptr, RR_OFF(m_maxBurstDelay) },
	{ "BigBurstFrequency", INI::parseInt, nullptr, RR_OFF(m_bigBurstFrequency) },
	{ "RubbleRiseDamping", INI::parseReal, nullptr, RR_OFF(m_rubbleRiseDamping) },
	{ "RubbleHeight", INI::parseReal, nullptr, RR_OFF(m_rubbleHeight) },
	{ "MaxShudder", INI::parseReal, nullptr, RR_OFF(m_maxShudder) },
	{ "OCL", parseRubbleRiseOCL, nullptr, 0 },
	{ "FXList", parseRubbleRiseFX, nullptr, 0 },
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, RR_OFF(m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, RR_OFF(m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, RR_OFF(m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, RR_OFF(m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, RR_OFF(m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, RR_OFF(m_dieMux.m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RR_OFF

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

// RW 0x8A78D5 (StructureCollapseUpdate's copy, line 325) and RW 0x8A829D (RubbleRiseUpdate's own copy of the file static
// buildNonDupRandomIndexList, line 272): `count` distinct indices of [0, size), every attempt one LOGIC draw random(0, size - 1), a duplicate redrawn.
// Retail loops for ever when count > size and keeps at most MAX_IDX (0x20) picks; here that is a throw and a clamp. Callers read the draws in retail order.
int pickPhaseIndices(GameLogic &logic, int size, int count, int picked[32], const char *file, int line)
{
	if (size <= 0)
	{
		return 0;
	}
	if (count > size)
	{
		throw std::logic_error(std::string(file) + ": a phase asks for more distinct entries than its list has (retail loops forever)");
	}
	const int n = count > 32 ? 32 : count;
	for (int i = 0; i < n; ++i)
	{
		int idx;
		bool again;
		do
		{
			idx = logic.random().getValue(0, size - 1, file, line);
			again = false;
			for (int j = 0; j < i; ++j)
			{
				again = again || picked[j] == idx;
			}
		} while (again);
		picked[i] = idx;
	}
	return n;
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
	// Port 64: RW 0x659DF0 registration (name string 0xC0B148), create RW 0x64EAB7, data RW 0x6563F3, field table 0xC69408
	modules.bindTypedData<RubbleRiseUpdateModuleData>("RubbleRiseUpdate", MODULETYPE_BEHAVIOR);
	bindRuntime<RubbleRiseUpdate, RubbleRiseUpdateModuleData>(modules, "RubbleRiseUpdate");
	// lane TOPPLE-1: RW 0xC69680 table, runtime RW 0x8A8BB4 (GameLogic/Module/StructureToppleUpdate.h)
	modules.bindTypedData<StructureToppleUpdateModuleData>("StructureToppleUpdate", MODULETYPE_BEHAVIOR);
	bindRuntime<StructureToppleUpdate, StructureToppleUpdateModuleData>(modules, "StructureToppleUpdate");
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
	const float geometryHeight = ObjectGeometry::maxHeightAbovePosition(getObject()->geometryShapes()); // the object's own GeometryInfo (lane MODCOMPAT-MOD1)
	return m_data->m_collapseHeight <= geometryHeight ? geometryHeight : m_data->m_collapseHeight;
}

// RW 0x8A7CCC
void StructureCollapseUpdate::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	if (!m_data->m_dieMux.isDieApplicable(*obj, event))
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
		int picked[32];
		const int count = pickPhaseIndices(logic, (int)pass.list->size(), pass.count, picked, "StructureCollapseUpdate.cpp", 325);
		for (int i = 0; i < count; ++i)
		{
			const int idx = picked[i];
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

// RW 0x8C553F: restore ownership and register revival before Object::onDie,
// inside the health change, before ActiveBody's damage notifications and kill credit.
void RespawnBody::internalChangeHealth(float delta)
{
	if (delta == 0.0f)
	{
		return; // RW 0x8C5549: a zero delta does nothing at all
	}
	const bool lethal = wouldDie(delta);
	const bool permanent = lethal && permanentlyKilled();
	ActiveBody::internalChangeHealth(delta);
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
		obj->endDefection();
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
	DieModuleInterface::Event event;
	if (const DamageInfo *info = currentDamageInfo())
	{
		event.sourceId = info->m_input.m_sourceID;
		event.deathType = info->m_input.m_deathType;
		event.damageType = info->m_input.m_damageType;
		event.damageSubType = info->m_input.m_damageSubType;
		event.damageAmount = info->m_input.m_amount;
		event.actualDamageDealt = info->m_output.m_actualDamageDealt;
		event.actualDamageClipped = info->m_output.m_actualDamageClipped;
	}
	obj->friend_onDie(event); // RW 0x8C566B -> 0x698F06
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
	, m_waitingForWakeUp(data->m_waitForWakeUp)
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
	m_waitingForWakeUp = false; // RW 0x7A7E79: the wake notification clears +0x28 before restarting the lifetime
	setLifetimeRange(m_data->m_minLifetime, m_data->m_maxLifetime);
}

void LifetimeUpdate::wakeFromInterface(UpdateSleepTime delay)
{
	if (delay != UPDATE_SLEEP_FOREVER)
	{
		wakeUp();
	}
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
	obj->kill(DAMAGE_UNRESISTABLE, m_data->m_deathType); // RW 0x7A802D: kill(8, DeathType)
	return UPDATE_SLEEP_FOREVER;
}

void LifetimeUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_dieFrame);
	h.addU32(m_startFrame);
}

// ---- RubbleRiseUpdate (Port 64, issue fork issue 176) ------------------------------------------------------------------------------------------
// RW FACTS (game.dat md5 daed3668, caveat S-001), the whole class read linearly:
//   * registration RW 0x659DF0 (name string 0xC0B148, line 0xA7 of the factory init): create RW 0x64EAB7 (allocates 0x48, calls the constructor
//     RW 0x8A8325) and data RW 0x6563F3 (allocates 0xD8, data ctor RW 0x656350, then the parse entry RW 0x8A8AE4).
//   * data ctor RW 0x656350: DieMuxData at +8 (RW 0x8D2970); four OCL vectors at +0x58 and four FX vectors at +0x88 (0xC bytes each); the two rise
//     delays +0x38 / +0x3C = 0, MinBurstDelay +0x40 = 0x270F, BigBurstFrequency +0x48 = 0, RubbleRiseDamping +0x4C = 0, RubbleHeight +0x50 = 0,
//     MaxShudder +0x54 = 0; MaxBurstDelay +0x44 is NOT written (heap garbage in retail, as the collapse's); every OCL and FX count (+0xB8 / +0xC8) = 1.
//   * parse entry RW 0x8A8AE4: field table RW 0xC69408 (userData 0) then the DieMux table RW 0xC76BD8 with userData 8.
//   * instance ctor RW 0x8A8325: vtables 0xC69310 / +0xC 0xC69038 / +0x10 0xC69304 (update) / +0x20 0xC69300 (die); state +0x24 .. +0x44 zeroed;
//     the base (RW 0x850C32) is told UPDATE_SLEEP_FOREVER, i.e. the module sleeps until it is woken.
//   * update = vtable slot 0xC69304 -> RW 0x8A85D0; onDie = 0xC69300 -> RW 0x8A83F6; the reset it calls = RW 0x8A83CE; doPhase = RW 0x8A847C;
//     doRubbleDoneStuff = RW 0x8A841C; xfer = the module table's slot 3 (RW 0x8A8547: +0x24, +0x28, +0x2C, +0x30, +0x34, +0x38 and the Coord3D at +0x3C).
//   * update (RW 0x8A85D0) in retail order: with the state waiting (0), a drawable (Object::getDrawable RW 0x70E013) and POST_RUBBLE (bit 59: RW
//     0x8A8603 reads word 0x110 bit 27, the flags are at Object + 0x10C, as AutoDepositUpdate's RUBBLE test says) -> phase INITIAL (0) at the object's
//     position, state = rising (written twice, RW 0x8A8632 / 0x8A872D), two CLIENT random reals (RW 0x6D33AB) shudder the drawable's instance matrix
//     (RW 0x6714C0), phase BURST (2), nextBurstFrame = now + logic random(MinBurstDelay, MaxBurstDelay) (RW 0x6D328E, line 0xAB),
//     riseVelocity = (-gravity) * (1 - damping) * 20.0 (RW 0xBD2C98 = 1.0, GlobalData + 0xC4 gravity, RW 0xC693B0 = 20.0, x87 at 24 bits),
//     the target height = TerrainLogic vslot 0x1C getLayerHeight(x, y, obj->getLayer() (RW 0x68BBE0), 0, 1) at RW 0x8A87AD, currentHeight = 0 and the
//     start position is saved. Rising (1) with a drawable: currentHeight += riseVelocity (SSE, RW 0x8A87E3), two LOGIC random reals (RW 0x6D332C, line
//     0xC4; the first goes to y, the second to x) around MaxShudder plus the saved position, z = saved z + currentHeight; at nextBurstFrame a phase fires
//     - BURST when logic random(1, BigBurstFrequency) == 1 (line 0xCD) else DELAY (1) - and nextBurstFrame += logic random(MinBurstDelay, MaxBurstDelay)
//     (line 0xD6); when currentHeight >= targetHeight - savedZ (RW 0x8A88C7, comiss): the object is put at (saved x, saved y, targetHeight), state = done,
//     phase FINAL (3), doRubbleDoneStuff (RW 0x8A841C: the StructureCollapseUpdate module's bone FX stop, looked up by name 0xC0B138), POST_RUBBLE is
//     cleared and POST_COLLAPSE (bit 60) set with the drawable notified after each (RW 0x68B53C twice, RW 0x8A8932 / 0x8A8949), the orientation is
//     re-set, the body's particle systems update (object + 0x25C, slot 0x2C), the drawable's matrix translation is zeroed and the module returns
//     UPDATE_SLEEP_FOREVER; every other exit returns UPDATE_SLEEP_NONE (1).
//   * onDie (RW 0x8A83F6): when the DieMux applies (DieMuxData at data + 8), the reset (RW 0x8A83CE: +0x24 = 0, state = waiting, +0x28 = 0, velocity 0,
//     currentHeight 0, targetHeight 0, setWakeFrame(UPDATE_SLEEP_NONE)).
//   * doPhase (RW 0x8A847C): the FX list first (vector at data + 0x88 + 12 * phase, count at +0xC8), then the OCLs (data + 0x58, count +0xB8); each
//     pass draws `count` distinct indices of [0, size) from the LOGIC generator (its own copy of buildNonDupRandomIndexList, RW 0x8A829D: random(0,
//     size - 1), a duplicate redrawn) and plays every one through FXList::doFXPos (RW 0x494615) / ObjectCreationList::create (RW 0x5F00CA).
// NOT PORTED (S-346): the drawable shudder and its instance matrix (client random), the bone FX stop (the structure lane's S-340), the body's particle
// systems, the FX and OCL stores (the draw is made and the FX handed to the client as an event; an OCL would create objects), the two rise delays
// (MinRubbleRiseDelay / MaxRubbleRiseDelay: no slot of the class reads +0x38 or +0x3C - the update, onDie, the reset, doPhase, the xfer and both
// constructors were read - and the INI's RubbleHeight +0x50 is read by no slot either: the target is the terrain's height).
void RubbleRiseUpdateModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kRubbleRiseParse); }

RubbleRiseUpdate::RubbleRiseUpdate(Thing *thing, const RubbleRiseUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x8A87AD: TheTerrainLogic vslot 0x1C getLayerHeight(x, y, layer, 0, 1) with the object's own layer (Object::getLayer RW 0x68BBE0)
float RubbleRiseUpdate::layerHeightAt(const Coord3D &p) const
{
	Object *obj = getObject();
	AIWorld *world = obj->logic().aiWorld();
	if (world == nullptr)
	{
		// INFERENCE: a harness without TheAI (no pathfinder mounted) asks the terrain directly; retail always has TheTerrainLogic
		return obj->logic().getGroundHeight(p.x, p.y);
	}
	ObjectPathfindAdapter *adapter = world->findAdapter(obj->getID());
	const int layer = adapter != nullptr ? (int)adapter->getLayer() : (int)LAYER_GROUND;
	return world->pathfinder().getLayerHeight(p.x, p.y, (PathfindLayerEnum)layer);
}

// RW 0x8A83CE: the die and the constructor put the module back to waiting and wake it
void RubbleRiseUpdate::resetRise()
{
	m_unused24 = 0;
	m_state = RISESTATE_WAITING;
	m_nextBurstFrame = 0;
	m_riseVelocity = 0.0f;
	m_currentHeight = 0.0f;
	m_rubbleHeight = 0.0f;
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

// RW 0x8A83F6
void RubbleRiseUpdate::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	if (!m_data->m_dieMux.isDieApplicable(*obj, event))
	{
		return;
	}
	resetRise();
}

// RW 0x8A847C: one random entry of each non-empty list of the phase, the FX first and the OCLs after, on the LOGIC generator's draws
void RubbleRiseUpdate::doPhase(RubbleRisePhase phase)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	struct Pass
	{
		const std::vector<std::string> *list;
		int count;
		bool fx;
	};
	for (const Pass &pass : { Pass{ &m_data->m_fx[phase], m_data->m_fxCount[phase], true }, Pass{ &m_data->m_ocl[phase], m_data->m_oclCount[phase], false } })
	{
		int picked[32];
		const int count = pickPhaseIndices(logic, (int)pass.list->size(), pass.count, picked, "RubbleRiseUpdate.cpp", 272);
		for (int i = 0; i < count; ++i)
		{
			if (pass.fx)
			{
				// RW 0x494615 doFXPos(fx, target, null, 0, null): the target is the caller's position, the object's at the call
				FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, "RubbleRise", logic.getFrame(), (*pass.list)[(size_t)picked[i]], *obj);
				e.hasTransform = false;
				logic.fxEvents().emit(e);
			}
			// the OCL pass draws its indices the same way and would create at the target (RW 0x5F00CA): the OCL store is not loaded here (S-346)
		}
	}
}

// RW 0x8A85D0
UpdateSleepTime RubbleRiseUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const RubbleRiseUpdateModuleData *d = m_data;
	if (m_state == RISESTATE_WAITING && obj->clientHooks() && obj->testModelCondition(CombatNames::modelCondition("POST_RUBBLE")))
	{
		// RW 0x8A85EE: waiting, a drawable and POST_RUBBLE (the flags at Object + 0x10C, bit 59 = word 0x110 bit 27)
		doPhase(RRPHASE_INITIAL);
		m_state = RISESTATE_RISING;
		// RW 0x8A8639 .. 0x8A8720: two CLIENT random reals set the drawable's instance matrix translation (the client's, no logic input)
		m_state = RISESTATE_RISING; // RW 0x8A872D: retail writes the state a second time
		doPhase(RRPHASE_BURST);
		const unsigned now = logic.getFrame();
		m_nextBurstFrame = now + (unsigned)logic.random().getValue((int)d->m_minBurstDelay, (int)d->m_maxBurstDelay, "RubbleRiseUpdate.cpp", 171);
		if (!logic.settings().structureRulesLoaded)
		{
			throw std::logic_error("RubbleRiseUpdate: GameLogicSettings has no Gravity (GameData not loaded)");
		}
		// x87 at 24 bits, RW 0x8A8764: (-gravity) * (1 - damping), then * 20.0
		m_riseVelocity = SimMath::pc24Mul(SimMath::pc24Mul(SimMath::pc24Sub(1.0f, d->m_rubbleRiseDamping), SimMath::subf32(0.0f, logic.settings().gravity)), 20.0f);
		m_rubbleHeight = layerHeightAt(*obj->getPosition());
		m_currentHeight = 0.0f;
		m_risePosition = *obj->getPosition();
	}
	if (m_state == RISESTATE_RISING && obj->clientHooks())
	{
		const unsigned now = logic.getFrame();
		m_currentHeight = SimMath::addf32(m_currentHeight, m_riseVelocity);
		// RW 0x8A87FC / 0x8A8822: the first LOGIC draw (RW 0x6D332C) goes to y, the second to x
		const float shudderY = logic.random().getValueReal(SimMath::subf32(0.0f, d->m_maxShudder), d->m_maxShudder, "RubbleRiseUpdate.cpp", 196);
		const float shudderX = logic.random().getValueReal(SimMath::subf32(0.0f, d->m_maxShudder), d->m_maxShudder, "RubbleRiseUpdate.cpp", 196);
		Coord3D p;
		p.x = SimMath::pc24Add(shudderX, m_risePosition.x);
		p.y = SimMath::pc24Add(shudderY, m_risePosition.y);
		p.z = SimMath::addf32(m_risePosition.z, m_currentHeight);
		obj->setPosition(&p);
		if (now >= m_nextBurstFrame)
		{
			if (logic.random().getValue(1, d->m_bigBurstFrequency, "RubbleRiseUpdate.cpp", 205) == 1)
			{
				doPhase(RRPHASE_BURST);
			}
			else
			{
				doPhase(RRPHASE_DELAY);
			}
			m_nextBurstFrame += (unsigned)logic.random().getValue((int)d->m_minBurstDelay, (int)d->m_maxBurstDelay, "RubbleRiseUpdate.cpp", 214);
		}
		if (!(m_currentHeight < SimMath::subf32(m_rubbleHeight, m_risePosition.z)))
		{
			Coord3D finalPos;
			finalPos.x = m_risePosition.x;
			finalPos.y = m_risePosition.y;
			finalPos.z = m_rubbleHeight;
			obj->setPosition(&finalPos);
			m_state = RISESTATE_DONE;
			doPhase(RRPHASE_FINAL);
			// RW 0x8A841C doRubbleDoneStuff: the StructureCollapseUpdate module's bone FX stop, not ported (S-340)
			obj->setModelConditionState(CombatNames::modelCondition("POST_RUBBLE"), false);   // RW 0x8A8932 (bit 59)
			obj->setModelConditionState(CombatNames::modelCondition("POST_COLLAPSE"), true);  // RW 0x8A8949 (bit 60)
			obj->setOrientation(obj->getOrientation());
			// RW 0x8A8964: the body's particle systems (object + 0x25C, slot 0x2C) - the body has none here (S-346)
			// RW 0x8A8976 .. 0x8A8A09: the drawable's instance matrix translation is zeroed (client)
			return UPDATE_SLEEP_FOREVER;
		}
	}
	return UPDATE_SLEEP_NONE;
}

void RubbleRiseUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_nextBurstFrame);
	h.addU32((std::uint32_t)m_state);
	h.addFloat(m_riseVelocity);
	h.addFloat(m_currentHeight);
	h.addFloat(m_rubbleHeight);
	h.addFloat(m_risePosition.x);
	h.addFloat(m_risePosition.y);
	h.addFloat(m_risePosition.z);
}

// ---- lane SAVE-4b: the snapshots (OpenBFME native, S-4190: each class's fields in its crc's order) ---------------------------------------------------
void StructureBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	ActiveBody::xfer(x);
	x.xferObjectID(&m_constructorObjectID);
}

void InactiveBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	x.xferBool(&m_dieCalled);
}

void StructureCollapseUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_collapseFrame);
	x.xferUnsignedInt(&m_burstFrame);
	SnapshotXfer::enumIn(x, m_state, COLLAPSESTATE_STANDING, COLLAPSESTATE_DONE, "a structure collapse's state");
	x.xferReal(&m_velocity);
	x.xferReal(&m_currentHeight);
	SnapshotXfer::coord3D(x, m_position);
}

void RubbleRiseUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_unused24); // RW 0x8A8547 xfers +0x24 too
	x.xferUnsignedInt(&m_nextBurstFrame);
	SnapshotXfer::enumIn(x, m_state, RISESTATE_WAITING, RISESTATE_DONE, "a rubble rise's state");
	x.xferReal(&m_riseVelocity);
	x.xferReal(&m_currentHeight);
	x.xferReal(&m_rubbleHeight);
	SnapshotXfer::coord3D(x, m_risePosition);
}

void RespawnBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	ActiveBody::xfer(x);
}

void DelayedDeathBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	RespawnBody::xfer(x);
	x.xferBool(&m_started);
	x.xferBool(&m_checked);
}

void SymbioticStructuresBody::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	ActiveBody::xfer(x);
	x.xferObjectID(&m_symbioteId);
}

void LifetimeUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferUnsignedInt(&m_dieFrame);
	x.xferUnsignedInt(&m_startFrame);
}
