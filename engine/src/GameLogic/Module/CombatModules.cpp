// OpenBFME. GPL-3.0.
// See GameLogic/Module/CombatModules.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

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
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kSlowDeathNames[] = { "INITIAL", "MIDPOINT", "FINAL", "HIT_GROUND", nullptr }; // ZH TheSlowDeathPhaseNames + RW 0x12AE110 (HIT_GROUND is RotWK's fourth phase: a flung object landing)
const float kBeginMidpointRatio = 0.35f;
const float kEndMidpointRatio = 0.65f;

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
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

bool SlowDeathBehavior::isDieApplicable(const DieModuleInterface::Event &event) const
{
	bool angle = false;
	return m_data->m_dieMux.isDieApplicable(*getObject(), event, &angle);
}

// ZH SlowDeathBehavior::getProbabilityModifier: overkill = dealt - clipped, as a fraction of the max health, times the bonus per overkill percent
int SlowDeathBehavior::probabilityModifier(const DieModuleInterface::Event &event) const
{
	const Object *obj = getObject();
	int bonus = 0;
	if (BodyModuleInterface *body = obj->getBodyModule())
	{
		const float overkill = SimMath::subf32(event.actualDamageDealt, event.actualDamageClipped);
		const float percent = SimMath::divf32((float)SimMath::truncToInt32(overkill), body->getMaxHealth());
		bonus = SimMath::truncToInt32(SimMath::mulf32(percent, m_data->m_modifierBonusPerOverkillPercent));
	}
	const int total = m_data->m_probabilityModifier + bonus;
	return total < 1 ? 1 : total;
}

// ZH SlowDeathBehavior::onDie: the first applicable module marks the AI dead and rolls among every applicable slow death of the object
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
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (SlowDeathBehavior *sd = dynamic_cast<SlowDeathBehavior *>(m.get()))
		{
			if (sd->isDieApplicable(event))
			{
				total += sd->probabilityModifier(event);
				candidates.push_back(sd);
			}
		}
	}
	int roll = obj->logic().random().getValue(1, total, "SlowDeathBehavior.cpp", 0);
	for (SlowDeathBehavior *sd : candidates)
	{
		roll -= sd->probabilityModifier(event);
		if (roll <= 0)
		{
			sd->beginSlowDeath();
			return;
		}
	}
}

// ZH SlowDeathBehavior::beginSlowDeath (the LOD scale is 1: S-324)
void SlowDeathBehavior::beginSlowDeath()
{
	if (m_activated)
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const SlowDeathBehaviorModuleData *d = m_data;
	m_sinkFrame = d->m_sinkDelay + (unsigned)logic.random().getValue(0, (int)d->m_sinkDelayVariance, "SlowDeathBehavior.cpp", 0);
	m_destructionFrame = d->m_destructionDelay + (unsigned)logic.random().getValue(0, (int)d->m_destructionDelayVariance, "SlowDeathBehavior.cpp", 0);
	m_midpointFrame = (unsigned)logic.random().getValue(SimMath::truncToInt32(SimMath::mulf32(kBeginMidpointRatio, (float)m_destructionFrame)),
		SimMath::truncToInt32(SimMath::mulf32(kEndMidpointRatio, (float)m_destructionFrame)), "SlowDeathBehavior.cpp", 0);
	unsigned wake = m_sinkFrame;
	if (wake > m_destructionFrame)
	{
		wake = m_destructionFrame;
	}
	if (wake > m_midpointFrame)
	{
		wake = m_midpointFrame;
	}
	const unsigned now = logic.getFrame();
	setWakeFrame(obj, UPDATE_SLEEP(wake < 1 ? 1 : (int)wake));
	m_sinkFrame += now;
	m_destructionFrame += now;
	m_midpointFrame += now;
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
	if (now >= m_sinkFrame && d->m_sinkRate > 0.0f)
	{
		Coord3D pos = *obj->getPosition();
		pos.z = SimMath::subf32(pos.z, d->m_sinkRate);
		obj->setPosition(&pos);
	}
	if (now >= m_midpointFrame && !m_midpointDone)
	{
		doPhase(SDPHASE_MIDPOINT);
		m_midpointDone = true;
	}
	if (now >= m_destructionFrame)
	{
		doPhase(SDPHASE_FINAL);
		logic.destroyObject(obj);
		return UPDATE_SLEEP_FOREVER;
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
}
