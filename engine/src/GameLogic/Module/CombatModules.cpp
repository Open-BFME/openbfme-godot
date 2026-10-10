// OpenBFME. GPL-3.0.
// See GameLogic/Module/CombatModules.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/CombatModules.h"
#include "GameLogic/ObjectCreationList.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kSlowDeathNames[] = { "INITIAL", "MIDPOINT", "FINAL", "HIT_GROUND", nullptr }; // ZH TheSlowDeathPhaseNames + RW 0x12AE110 (HIT_GROUND is RotWK's fourth phase: a flung object landing)

// FX = <phase> <name> [<name> ...] (RW 0x86158d; the FX list store is the client's: the names are kept)
void parsePhaseNames(INI *ini, std::vector<std::string> *lists)
{
	const int phase = INI::scanIndexList(ini->getNextToken(), kSlowDeathNames);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		lists[phase].push_back(t);
	}
}

void parseFX(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<SlowDeathBehaviorModuleData *>(instance)->m_fx);
}
void parseOCL(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<SlowDeathBehaviorModuleData *>(instance)->m_ocl);
}
void parseWeapon(INI *ini, void *instance, void *, const void *)
{
	parsePhaseNames(ini, static_cast<SlowDeathBehaviorModuleData *>(instance)->m_weapon);
}
// Sound = <phase> <audio event> [...] (RW 0x861903): the phase index is validated; every remaining token is an entry of the phase's list (m_sounds); m_sound
// keeps the first one as written (the raw record of the earlier lanes)
void parseSound(INI *ini, void *instance, void *, const void *)
{
	SlowDeathBehaviorModuleData *d = static_cast<SlowDeathBehaviorModuleData *>(instance);
	const char *phase = ini->getNextToken();
	const int index = INI::scanIndexList(phase, kSlowDeathNames);
	const char *name = ini->getNextTokenOrNull();
	d->m_sound.push_back(std::string(phase) + " " + (name ? name : ""));
	for (; name; name = ini->getNextTokenOrNull())
	{
		d->m_sounds[index].push_back(name); // lane FX-2
	}
}
void parseDeathFlags(INI *ini, void *instance, void *, const void *)
{
	SlowDeathBehaviorModuleData *d = static_cast<SlowDeathBehaviorModuleData *>(instance);
	for (const char *t = ini->getNextTokenOrNull(); t; t = ini->getNextTokenOrNull())
	{
		d->m_deathFlags.push_back(t);
	}
}
void parseFXName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

#define DM_ROWS(T)                                                                                                                                       \
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, (int)offsetof(T, m_dieMux.m_deathTypes) },                                                              \
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, (int)offsetof(T, m_dieMux.m_exemptStatus) },                                                        \
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, (int)offsetof(T, m_dieMux.m_requiredStatus) },                                                    \
	{ "DamageAmountRequired", INI::parseReal, nullptr, (int)offsetof(T, m_dieMux.m_damageAmountRequired) },                                               \
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(T, m_dieMux.m_minKillerAngle) },                                                      \
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(T, m_dieMux.m_maxKillerAngle) }

const FieldParse kDestroyDieParse[] = { DM_ROWS(DestroyDieModuleData), { nullptr, nullptr, nullptr, 0 } };
const FieldParse kKeepObjectDieParse[] = {
	DM_ROWS(KeepObjectDieModuleData),
	{ "CollapsingTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(KeepObjectDieModuleData, m_collapsingTime) },
	{ "StayOnRadar", INI::parseBool, nullptr, (int)offsetof(KeepObjectDieModuleData, m_stayOnRadar) },
	{ nullptr, nullptr, nullptr, 0 }
};
const FieldParse kFXListDieParse[] = {
	DM_ROWS(FXListDieModuleData),
	{ "DeathFX", parseFXName, nullptr, (int)offsetof(FXListDieModuleData, m_deathFX) },
	{ "OrientToObject", INI::parseBool, nullptr, (int)offsetof(FXListDieModuleData, m_orientToObject) },
	{ nullptr, nullptr, nullptr, 0 }
};
// RW 0xC58A98 in the binary's row order
const FieldParse kSlowDeathParse[] = {
	{ "SinkRate", INI::parseVelocityReal, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_sinkRate) },
	{ "ProbabilityModifier", INI::parseInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_probabilityModifier) },
	{ "ModifierBonusPerOverkillPercent", INI::parsePercentToReal, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_modifierBonusPerOverkillPercent) },
	{ "SinkDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_sinkDelay) },
	{ "SinkDelayVariance", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_sinkDelayVariance) },
	{ "DestructionDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_destructionDelay) },
	{ "DestructionDelayVariance", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_destructionDelayVariance) },
	{ "DecayBeginTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_decayBeginTime) },
	{ "FX", parseFX, nullptr, 0 },
	{ "OCL", parseOCL, nullptr, 0 },
	{ "Weapon", parseWeapon, nullptr, 0 },
	{ "Sound", parseSound, nullptr, 0 },
	{ "FlingForce", INI::parseReal, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_flingForce) },
	{ "FlingForceVariance", INI::parseReal, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_flingForceVariance) },
	{ "FlingPitch", INI::parseAngleReal, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_flingPitch) },
	{ "FlingPitchVariance", INI::parseAngleReal, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_flingPitchVariance) },
	{ "DeathFlags", parseDeathFlags, nullptr, 0 },
	{ "ShadowWhenDead", INI::parseBool, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_shadowWhenDead) },
	{ "FadeDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_fadeDelay) },
	{ "FadeTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_fadeTime) },
	{ "DoNotRandomizeMidpoint", INI::parseBool, nullptr, (int)offsetof(SlowDeathBehaviorModuleData, m_doNotRandomizeMidpoint) },
	DM_ROWS(SlowDeathBehaviorModuleData),
	{ nullptr, nullptr, nullptr, 0 }
};
#undef DM_ROWS

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
} // namespace

namespace
{
const FieldParse kPorcupineParse[] = {
	{ "DamageWeaponTemplate", INI::parseAsciiString, nullptr, (int)offsetof(PorcupineFormationBodyModuleData, m_damageWeaponTemplate) },
	{ "CrushDamageWeaponTemplate", INI::parseAsciiString, nullptr, (int)offsetof(PorcupineFormationBodyModuleData, m_crushDamageWeaponTemplate) },
	{ "CrusherLevelResisted", INI::parseInt, nullptr, (int)offsetof(PorcupineFormationBodyModuleData, m_crusherLevelResisted) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void PorcupineFormationBodyModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	ActiveBodyModuleData::buildFieldParse(p); // RW table 0xC71D68
	p.add(kPorcupineParse);                   // RW table 0xC72E18
}

void DestroyDieModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kDestroyDieParse); }
void KeepObjectDieModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kKeepObjectDieParse); }
void FXListDieModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kFXListDieParse); }
void SlowDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kSlowDeathParse); }

void CombatModules::registerAll(ModuleFactory &modules)
{
	// ImmortalBody / HighlanderBody: ActiveBody's data (the registry golden: the same createData 0x651186 and table 0xC71D68)
	modules.bindTypedData<ActiveBodyModuleData>("ImmortalBody", MODULETYPE_BEHAVIOR);
	bindRuntime<ImmortalBody, ActiveBodyModuleData>(modules, "ImmortalBody");
	modules.bindTypedData<ActiveBodyModuleData>("HighlanderBody", MODULETYPE_BEHAVIOR);
	bindRuntime<HighlanderBody, ActiveBodyModuleData>(modules, "HighlanderBody");
	modules.bindTypedData<PorcupineFormationBodyModuleData>("PorcupineFormationBodyModule", MODULETYPE_BEHAVIOR);
	bindRuntime<PorcupineFormationBody, PorcupineFormationBodyModuleData>(modules, "PorcupineFormationBodyModule");
	modules.bindTypedData<DestroyDieModuleData>("DestroyDie", MODULETYPE_BEHAVIOR);
	bindRuntime<DestroyDie, DestroyDieModuleData>(modules, "DestroyDie");
	modules.bindTypedData<KeepObjectDieModuleData>("KeepObjectDie", MODULETYPE_BEHAVIOR);
	bindRuntime<KeepObjectDie, KeepObjectDieModuleData>(modules, "KeepObjectDie");
	modules.bindTypedData<FXListDieModuleData>("FXListDie", MODULETYPE_BEHAVIOR);
	bindRuntime<FXListDie, FXListDieModuleData>(modules, "FXListDie");
	modules.bindTypedData<SlowDeathBehaviorModuleData>("SlowDeathBehavior", MODULETYPE_BEHAVIOR);
	bindRuntime<SlowDeathBehavior, SlowDeathBehaviorModuleData>(modules, "SlowDeathBehavior");
}

// ---- DestroyDie / FXListDie -------------------------------------------------------------------------------------------------------
void DestroyDie::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*obj, event, &angle))
	{
		return;
	}
	obj->logic().destroyObject(obj); // ZH DestroyDie::onDie
}

void FXListDie::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*obj, event, &angle))
	{
		return;
	}
	++obj->logic().combat().counters().unportedNuggets; // the hashed run counter keeps counting (lane FX-2 changes no hash); the call itself is the event below
	// lane FX-2. DONOR ZH FXListDie::onDie (the RotWK onDie was not read; its data table has no upgrade mux, so ZH's upgrade gate does not apply): OrientToObject
	// plays doFXObj(DeathFX, obj, the damage dealer), else doFXPos(DeathFX, obj position) with no matrix
	GameLogic &logic = obj->logic();
	FXEvent e = FXEventLog::objectEvent(m_data->m_orientToObject ? FXEvent::OBJECT_FX : FXEvent::POSITION_FX, "FXListDie", logic.getFrame(), m_data->m_deathFX, *obj);
	if (m_data->m_orientToObject)
	{
		e.secondary = logic.findObjectByID(event.sourceId) ? event.sourceId : (ObjectID)INVALID_ID;
	}
	else
	{
		e.hasTransform = false;
	}
	logic.fxEvents().emit(e);
}

// ---- SlowDeathBehavior ------------------------------------------------------------------------------------------------------------
SlowDeathBehavior::SlowDeathBehavior(Thing *thing, const SlowDeathBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (data->m_probabilityModifier < 1)
	{
		throw std::logic_error("SlowDeathBehavior: ProbabilityModifier must be >= 1"); // ZH ctor: INI_INVALID_DATA
	}
	// RW 0x8604E2: + 0x40 is set when the HIT_GROUND phase has an FX, OCL, Weapon or Sound entry (data + 0x7C / 0xAC / 0xDC / 0x10C: the fourth list of each)
	m_hitGroundPending = !data->m_fx[SDPHASE_HIT_GROUND].empty() || !data->m_ocl[SDPHASE_HIT_GROUND].empty() || !data->m_weapon[SDPHASE_HIT_GROUND].empty() ||
		!data->m_sounds[SDPHASE_HIT_GROUND].empty();
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

bool SlowDeathBehavior::isDieApplicable(const DieModuleInterface::Event &event) const
{
	bool angle = false;
	return m_data->m_dieMux.isDieApplicable(*getObject(), event, &angle);
}

// RW 0x860608 (SlowDeathBehaviorInterface slot 1): overkill = (int)(dealt - clipped) (cvttss2si), fild; divided by the body's max health (body slot 0x1C, x87), times
// ModifierBonusPerOverkillPercent, _ftol2, plus ProbabilityModifier; at most 1 below: 1
int SlowDeathBehavior::probabilityModifier(const DieModuleInterface::Event &event) const
{
	const Object *obj = getObject();
	int bonus = 0;
	if (BodyModuleInterface *body = obj->getBodyModule())
	{
		const int overkill = SimMath::cvttss2si(SimMath::subf32(event.actualDamageDealt, event.actualDamageClipped));
		const float overkillF = SimMath::fstpDword((double)overkill); // fild; fstp dword
		bonus = SimMath::ftol2(SimMath::pc24MulW(SimMath::pc24DivW((double)overkillF, (double)body->getMaxHealth()), (double)m_data->m_modifierBonusPerOverkillPercent));
	}
	const int total = m_data->m_probabilityModifier + bonus;
	return total > 1 ? total : 1;
}

// RW 0x861712 (the die interface slot 0): unless the die mux refuses; an AI already dead (+ 0x3BD) ends it, otherwise the AI is marked dead (RW 0x66264E); the object
// leaves the selections (RW 0x625759, client) and the draw's ... (RW 0x68BE3C, template + 0x642 clear); every module with a SlowDeathBehaviorInterface (behavior slot 0x5C)
// whose die mux accepts the death joins with its probability; then roll GameLogicRandomValue(0, total - 1) (SlowDeathBehavior.cpp:0x32F), walk the weights (a roll
// below a weight picks it, otherwise the weight is taken off), take the pick's weight off the total; a pick still applicable whose slot 0xC answers (RW 0x8BD372: true)
// begins its slow death (slot 0), otherwise it leaves the candidates (swapped with the last) and the roll repeats
void SlowDeathBehavior::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	if (!isDieApplicable(event))
	{
		return;
	}
	if (AIUpdateInterface *ai = obj->getAIUpdateInterface())
	{
		if (ai->isAiInDeadState())
		{
			return;
		}
		ai->onDie();
	}
	int total = 0;
	std::vector<SlowDeathBehavior *> candidates;
	std::vector<int> weights;
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (SlowDeathBehavior *sd = dynamic_cast<SlowDeathBehavior *>(m.get()))
		{
			if (sd->isDieApplicable(event))
			{
				const int w = sd->probabilityModifier(event);
				total += w;
				candidates.push_back(sd);
				weights.push_back(w);
			}
		}
	}
	while (!candidates.empty())
	{
		int roll = obj->logic().random().getValue(0, total - 1, "SlowDeathBehavior.cpp", 0x32F);
		size_t i = 0;
		for (; i < weights.size(); ++i)
		{
			if (roll < weights[i])
			{
				break;
			}
			roll -= weights[i];
		}
		if (i == weights.size())
		{
			i = weights.size() - 1; // RW reads the end element; the weights always sum to the total, so the walk stops inside
		}
		total -= weights[i];
		SlowDeathBehavior *pick = candidates[i];
		if (pick->isDieApplicable(event))
		{
			pick->beginSlowDeath(event);
			return;
		}
		candidates[i] = candidates.back();
		weights[i] = weights.back();
		candidates.pop_back();
		weights.pop_back();
	}
}

// RW 0x70B8AE Object::getHeightAboveTerrain: the position's z over the terrain (S-161: the ground)
float SlowDeathBehavior::heightAboveTerrain() const
{
	const Object *obj = getObject();
	const Coord3D &p = *obj->getPosition();
	return SimMath::subf32(p.z, obj->logic().getGroundHeight(p.x, p.y));
}

// RW 0x860E93 beginSlowDeath (SlowDeathBehaviorInterface slot 0). TARGET FACTS:
//   * once (+ 0x3C bit 0); ATTACKING (model condition 37) cleared; the DeathFlags statuses (+ 0x174) set (RW 0x68D440); with DeathFlags model conditions (+ 0x128):
//     those and DYING (62) set; the drawable's shadow (ShadowWhenDead off) and RW 0x67093E(0, -0.2) are client;
//   * the LOD death scale (TheGameLODManager + 0x179C; 1.0 in 2.01) 0 with no OCL / Weapon entry (+ 0x18C & 6): destroyed at once;
//   * a HULK with TheGameLogic + 0xA0 != -1: sink 1, midpoint 5 / 2 + 1, destruction 5 + 1 (not reached: see the header);
//   * otherwise sink = _ftol2(fild(GameLogicRandomValue(0, SinkDelayVariance) [line 0x1A5] + SinkDelay) * scale), destruction likewise ([line 0x1A6]), decay =
//     _ftol2(DecayBeginTime * scale); midpoint: DoNotRandomizeMidpoint: _ftol2(destruction * 0.5 (double)), otherwise GameLogicRandomValue(_ftol2(destruction * 0.35),
//     _ftol2(0.65 * destruction)) [line 0x1AB]; FadeDelay other than 0xFACADE00: the fade frame _ftol2(FadeDelay * scale);
//   * FlingForce > 0: with a PhysicsBehavior: a body less than 1.0 above the ground is raised by 1.0 (RW 0x70C201); the fling vector RW 0x860664 (FlingForce +
//     FlingForceVariance, FlingPitch + FlingPitchVariance), fling (RW 0x792DBD), the angle atan2(v.y, v.x) (RW 0x441BF4, 0x70C31E), EXPLODED_FLAILING (120), flag 4; the
//     wake is the next frame; an object with a slaved update (+ 0x1C8 bit 3) tells it first (RW 0x861173: not ported, inference: no ported object sets the bit);
//   * otherwise the wake is the next frame with a HIT_GROUND entry (+ 0x40), else the earliest of sink, destruction, midpoint and decay (when not 0);
//   * every frame (decay only when not 0, the fade frame always) plus TheGameLogic's frame; flag 1; the INITIAL phase (RW 0x8609A8).
void SlowDeathBehavior::beginSlowDeath(const DieModuleInterface::Event &)
{
	if (m_flags & kActivated)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const SlowDeathBehaviorModuleData *d = m_data;
	static const int kAttacking = CombatNames::modelCondition("ATTACKING");
	static const int kDying = CombatNames::modelCondition("DYING");
	static const int kExplodedFlailing = CombatNames::modelCondition("EXPLODED_FLAILING");
	obj->setModelConditionState(kAttacking, false);
	bool anyDeathFlag = false;
	for (const std::string &f : d->m_deathFlags)
	{
		obj->setStatus((unsigned)CombatNames::status(f.c_str()), true);                 // RW + 0x174 (RW 0x68D440)
		obj->setModelConditionState(CombatNames::modelCondition(f.c_str()), true);      // RW + 0x128 (RW 0x5E3BA5)
		anyDeathFlag = true;
	}
	if (anyDeathFlag && !obj->testModelCondition(kDying))
	{
		obj->setModelConditionState(kDying, true);
	}
	const float scale = 1.0f; // TheGameLODManager's SlowDeathScale (see the header)
	auto scaled = [&](unsigned v) { return (unsigned)SimMath::ftol2(SimMath::pc24MulW((double)v, (double)scale)); }; // fild (+ 2^32 when negative); fmul; _ftol2
	m_sinkFrame = scaled((unsigned)logic.random().getValue(0, (int)d->m_sinkDelayVariance, "SlowDeathBehavior.cpp", 0x1A5) + d->m_sinkDelay);
	m_destructionFrame = scaled((unsigned)logic.random().getValue(0, (int)d->m_destructionDelayVariance, "SlowDeathBehavior.cpp", 0x1A6) + d->m_destructionDelay);
	m_decayFrame = scaled(d->m_decayBeginTime);
	if (d->m_doNotRandomizeMidpoint)
	{
		m_midpointFrame = (unsigned)SimMath::ftol2(SimMath::mulD((double)m_destructionFrame, 0.5)); // fmul qword 0.5 (RW 0xBD86A0)
	}
	else
	{
		const int hi = SimMath::ftol2(SimMath::pc24MulW(0.649999976, (double)m_destructionFrame)); // RW 0xC58804
		const int lo = SimMath::ftol2(SimMath::pc24MulW((double)m_destructionFrame, 0.349999994)); // RW 0xC58800
		m_midpointFrame = (unsigned)logic.random().getValue(lo, hi, "SlowDeathBehavior.cpp", 0x1AB);
	}
	if (d->m_fadeDelay != 0xFACADE00u)
	{
		m_fadeFrame = scaled(d->m_fadeDelay);
	}
	const unsigned now = logic.getFrame();
	unsigned wake = 1;
	PhysicsBehavior *phys = d->m_flingForce > 0.0f ? PhysicsBehavior::find(*obj) : nullptr;
	if (d->m_flingForce > 0.0f)
	{
		if (phys)
		{
			if (1.0f > heightAboveTerrain())
			{
				Coord3D p = *obj->getPosition();
				p.z = SimMath::addf32(p.z, 1.0f);
				obj->setPosition(&p);
			}
			// RW 0x860664: angle GameLogicRandomValueReal(-pi, pi) [0x139], pitch (FlingPitch, + variance) [0x13A], force (FlingForce, + variance) [0x13B]; the rows of
			// force * identity turned by the angle (CRT cos / sin) and by -pitch (SSE, the zero terms kept)
			const float force0 = d->m_flingForce, force1 = SimMath::addf32(d->m_flingForceVariance, d->m_flingForce);
			const float pitch0 = d->m_flingPitch, pitch1 = SimMath::addf32(d->m_flingPitchVariance, d->m_flingPitch);
			const float angle = logic.random().getValueReal(-3.14159274f, 3.14159274f, "SlowDeathBehavior.cpp", 0x139);
			const float pitch = logic.random().getValueReal(pitch0, pitch1, "SlowDeathBehavior.cpp", 0x13A);
			const float f = logic.random().getValueReal(force0, force1, "SlowDeathBehavior.cpp", 0x13B);
			const float z0 = SimMath::mulf32(f, 0.0f);
			const float c = SimMath::fstpDword(SimMath::cosd(angle)), sn = SimMath::fstpDword(SimMath::sind(angle));
			const float ax = SimMath::addf32(SimMath::mulf32(z0, sn), SimMath::mulf32(f, c));
			const float bx = SimMath::addf32(SimMath::mulf32(f, sn), SimMath::mulf32(z0, c));
			const float cx = SimMath::addf32(SimMath::mulf32(z0, sn), SimMath::mulf32(z0, c));
			const float negPitch = SimMath::subf32(0.0f, pitch); // fchs
			const float sp = SimMath::fstpDword(SimMath::sind(negPitch)), cp = SimMath::fstpDword(SimMath::cosd(negPitch));
			Coord3D v;
			v.x = SimMath::subf32(SimMath::mulf32(cp, ax), SimMath::mulf32(z0, sp));
			v.y = SimMath::subf32(SimMath::mulf32(bx, cp), SimMath::mulf32(z0, sp));
			v.z = SimMath::subf32(SimMath::mulf32(cx, cp), SimMath::mulf32(f, sp));
			phys->fling(v, 0, 0);
			obj->setOrientation(SimMath::fstpDword(SimMath::atan2d(v.y, v.x))); // RW 0x441BF4, 0x70C31E
			if (!obj->testModelCondition(kExplodedFlailing))
			{
				obj->setModelConditionState(kExplodedFlailing, true);
			}
			m_flags |= kFlung;
		}
	}
	else if (!m_hitGroundPending)
	{
		wake = m_sinkFrame;
		if (wake > m_destructionFrame)
		{
			wake = m_destructionFrame;
		}
		if (wake > m_midpointFrame)
		{
			wake = m_midpointFrame;
		}
		if (m_decayFrame != 0 && m_decayFrame < wake)
		{
			wake = m_decayFrame;
		}
	}
	setWakeFrame(obj, UPDATE_SLEEP(wake < 1 ? 1 : (int)wake)); // RW 0x850C32 (a 0 is the next frame)
	m_sinkFrame += now;
	m_destructionFrame += now;
	m_midpointFrame += now;
	if (m_decayFrame != 0)
	{
		m_decayFrame += now;
	}
	m_fadeFrame += now;
	m_flags |= kActivated;
	m_activated = true;
	doPhase(SDPHASE_INITIAL);
}

void SlowDeathBehavior::doPhase(SlowDeathPhase phase)
{
	const SlowDeathBehaviorModuleData *d = m_data;
	CombatState::Counters &c = getObject()->logic().combat().counters();
	// RW 0x8609A8. Nothing at all unless the data's byte +0x18C, the mask of resolved entries the phase list parsers set (RW 0x86158D / 0x8615FE / 0x861672 /
	// 0x861903: bit 1 FX, 2 OCL, 4 Weapon, 8 Sound, over every phase), is non-zero. Then, for the phase: the FX pick is a CLIENT draw (RW 0x6D32E4, state
	// 0xDA1C74, line 0x217) and the list entry plays through RW 0x4B1B5A (doFXObj(fx, obj, null)); the OCL pick (line 0x221) and the Weapon pick (line 0x22B)
	// are LOGIC draws (RW 0x6D328E, state 0xDA1CA4); the Sound pick is a CLIENT draw (line 0x238) played on the object. The client picks are the player's
	// (LiveFX draws them from the client stream when it plays the event: the logic never draws them). S-324: the OCL and weapon are counted, not executed; the
	// hashed counter counts every non-empty list as before.
	static const char *const kSites[SDPHASE_COUNT] = { "SlowDeath INITIAL", "SlowDeath MIDPOINT", "SlowDeath FINAL", "SlowDeath HIT_GROUND" };
	static const char *const kSoundSites[SDPHASE_COUNT] = { "SlowDeath Sound INITIAL", "SlowDeath Sound MIDPOINT", "SlowDeath Sound FINAL", "SlowDeath Sound HIT_GROUND" };
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (d->resolvedMask() == 0)
	{
		return;
	}
	if (!d->m_fx[phase].empty())
	{
		++c.unportedNuggets;
		FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, kSites[phase], logic.getFrame(), d->m_fx[phase][0], *obj);
		e.choices = &d->m_fx[phase];
		logic.fxEvents().emit(e);
	}
	for (const std::vector<std::string> *list : { &d->m_ocl[phase], &d->m_weapon[phase] })
	{
		if (!list->empty())
		{
			const int pick = logic.random().getValue(0, (int)list->size() - 1, "SlowDeathBehavior.cpp", list == &d->m_ocl[phase] ? 0x221 : 0x22B);
			++c.unportedNuggets;
			if (list == &d->m_ocl[phase])
			{
				// lane SPELL-2: RW 0x860A2E .. 0x860A46: the picked OCL (null: nothing) is created from the object, OCL::create(obj, 0, 0) (RW 0x5F0126: the
				// final override, then every nugget's object form); the CreateObject port makes its objects at the object's position on its team (the
				// summon eggs of the spell book hatch through this). The OCL's own unported parts are counted by ObjectCreationList (S-530)
				const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList((*list)[(size_t)pick]) : nullptr;
				if (ocl)
				{
					ocl->create(logic, obj, *obj->getPosition());
				}
			}
			else
			{
				// lane DECOMP-1: RW 0x860A6D .. 0x860A8C: the picked weapon (null: nothing) is TheWeaponStore->createAndFireTempWeapon(weapon, object, object position)
				const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate((*list)[(size_t)pick]) : nullptr;
				if (wt)
				{
					ObjectWeapons::createAndFireTempWeapon(wt, obj, *obj->getPosition());
				}
			}
		}
	}
	if (!d->m_sounds[phase].empty())
	{
		FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_SOUND, kSoundSites[phase], logic.getFrame(), d->m_sounds[phase][0], *obj);
		e.choices = &d->m_sounds[phase];
		logic.fxEvents().emit(e);
	}
}

// RW byte +0x18C (see doPhase). INFERENCE for OCL and Weapon: an entry counts as resolved when its name is not "None" (the OCL store is not ported; the
// weapon name is resolved at use); FX names are validated at parse time like RW (an unknown one is an error, "None" is NULL); a Sound entry counts unless
// it is "NoSound" (the audio registry's empty event)
int SlowDeathBehaviorModuleData::resolvedMask() const
{
	auto any = [](const std::vector<std::string> *lists, bool sound) {
		for (int p = 0; p < SDPHASE_COUNT; ++p)
		{
			for (const std::string &n : lists[p])
			{
				if (sound ? AsciiStringUtil::compareNoCase(n, "NoSound") != 0 : FXEventLog::isFXName(n))
				{
					return true;
				}
			}
		}
		return false;
	};
	return (any(m_fx, false) ? 1 : 0) | (any(m_ocl, false) ? 2 : 0) | (any(m_weapon, false) ? 4 : 0) | (any(m_sounds, true) ? 8 : 0);
}

// RW 0x860B39 (the update interface slot 0). The LOD scale's rescale (RW 0x860B5B .. 0x860BC8) is not reached (scale 1.0). Then, in this order:
//   * flung and not down: every frame (decay only when not 0) one later; once the body is no longer above the ground (RW 0x4B12E8: height > 0): EXPLODED_FLAILING goes,
//     EXPLODED_BOUNCING (121) comes (RW 0x68D607), flag 8;
//   * a HIT_GROUND entry pending: RUBBLE (5) cleared; less than 1.0 above the ground: the HIT_GROUND phase, RUBBLE set, no longer pending;
//   * the fade frame reached with a FadeDelay: once (+ 0x48) the drawable fades over FadeTime (client);
//   * the sink frame reached with a SinkRate above 0: without a flight in progress (PhysicsBehavior's points) and not significantly above the ground (RW 0x70C50C(0): not
//     CAN_CLIMB_WALLS / SHIP, height > gravity * -9.0) the object is DISABLED_HELD for good (RW 0x692432(3)); SINKING (status 0x38) set; z - SinkRate / scale (x87),
//     5.7 more (RW 0xC588F0, SSE) while that is still above the terrain; setPosition;
//   * the midpoint frame: once, the MIDPOINT phase; the destruction frame: the FINAL phase and TheGameLogic->destroyObject; the decay frame (not 0): DECAY (153).
// The update asks to run every frame (1).
UpdateSleepTime SlowDeathBehavior::update()
{
	if (!m_activated)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const unsigned now = logic.getFrame();
	const SlowDeathBehaviorModuleData *d = m_data;
	static const int kExplodedFlailing = CombatNames::modelCondition("EXPLODED_FLAILING");
	static const int kExplodedBouncing = CombatNames::modelCondition("EXPLODED_BOUNCING");
	static const int kRubble = CombatNames::modelCondition("RUBBLE");
	static const int kDecay = CombatNames::modelCondition("DECAY");
	static const int kSinking = CombatNames::status("SINKING");
	static const int kClimb = ObjectTemplateInfoBuilder::kindOfIndex("CAN_CLIMB_WALLS");
	static const int kShip = ObjectTemplateInfoBuilder::kindOfIndex("SHIP");
	if ((m_flags & kFlung) && !(m_flags & kLanded))
	{
		++m_sinkFrame;
		++m_midpointFrame;
		++m_destructionFrame;
		++m_fadeFrame;
		if (m_decayFrame != 0)
		{
			++m_decayFrame;
		}
		if (!(heightAboveTerrain() > 0.0f))
		{
			Object::ModelConditionBits clear{}, set{};
			clear[(size_t)kExplodedFlailing >> 5] |= 1u << (kExplodedFlailing & 31);
			set[(size_t)kExplodedBouncing >> 5] |= 1u << (kExplodedBouncing & 31);
			obj->clearAndSetModelConditionFlags(clear, set);
			m_flags |= kLanded;
		}
	}
	if (m_hitGroundPending)
	{
		if (obj->testModelCondition(kRubble))
		{
			obj->setModelConditionState(kRubble, false);
		}
		if (1.0f > heightAboveTerrain())
		{
			m_hitGroundPending = false;
			doPhase(SDPHASE_HIT_GROUND);
			if (!obj->testModelCondition(kRubble))
			{
				obj->setModelConditionState(kRubble, true);
			}
		}
	}
	if (m_fadeFrame <= now && d->m_fadeDelay != 0xFACADE00u && !m_fadeBegun)
	{
		m_fadeBegun = true; // RW 0x670A50(FadeTime) on the drawable: the client fades the body (fadeFrame() / fadeBegun())
	}
	if (m_sinkFrame <= now && d->m_sinkRate > 0.0f)
	{
		PhysicsBehavior *phys = PhysicsBehavior::find(*obj);
		const bool significantlyAbove = !obj->isKindOf((unsigned)kClimb) && !obj->isKindOf((unsigned)kShip) &&
			(double)heightAboveTerrain() > SimMath::pc24MulW((double)logic.settings().gravity, -9.0); // RW 0x70C50C: fld; fadd 0; fcompi with gravity * -9.0 (RW 0xC1EBC0)
		if ((!phys || !phys->isFlying()) && !significantlyAbove)
		{
			obj->setDisabled(3, 0x3FFFFFFFu); // DISABLED_HELD (RW 0x6907F1)
		}
		obj->setStatus((unsigned)kSinking, true); // RW 0x62684D(0x38, 1)
		Coord3D p = *obj->getPosition();
		float z = SimMath::fstpDword(SimMath::pc24SubW((double)p.z, SimMath::pc24DivW((double)d->m_sinkRate, 1.0))); // fld SinkRate; fdiv scale; fsubr z
		if (z > logic.getGroundHeight(p.x, p.y)) // TheTerrainLogic slot 0x1C with the layer (S-161: the ground)
		{
			z = SimMath::subf32(z, 5.69999981f);
		}
		p.z = z;
		obj->setPosition(&p);
	}
	if (m_midpointFrame <= now && !(m_flags & kMidpoint))
	{
		doPhase(SDPHASE_MIDPOINT);
		m_flags |= kMidpoint;
		m_midpointDone = true;
	}
	if (m_destructionFrame <= now)
	{
		doPhase(SDPHASE_FINAL);
		logic.destroyObject(obj);
	}
	if (m_decayFrame != 0 && m_decayFrame <= now && !obj->testModelCondition(kDecay))
	{
		obj->setModelConditionState(kDecay, true);
	}
	return UPDATE_SLEEP_NONE;
}

void SlowDeathBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_activated);
	h.addBool(m_midpointDone);
	h.addU32(m_sinkFrame);
	h.addU32(m_midpointFrame);
	h.addU32(m_destructionFrame);
	h.addU32(m_decayFrame); // lane COMBAT-4
	h.addU32(m_flags);
	h.addBool(m_hitGroundPending);
	h.addBool(m_fadeBegun);
	h.addU32(m_fadeFrame);
}
