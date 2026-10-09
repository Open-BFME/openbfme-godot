// OpenBFME. GPL-3.0.
// See GameLogic/Module/HeroAbilityModules.h (lane HERO-2).

#include "GameLogic/Module/HeroAbilityModules.h"

#include "Common/INI.h"
#include "Common/PlayerScience.h"
#include "Common/GameCommon.h"
#include "Common/Upgrade.h"
#include "GameLogic/AI/AIStateMachine.h"
#include "GameLogic/Module/EmotionModules.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Damage.h"
#include "GameLogic/BitFlags.h"
#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/NameKeyGenerator.h"
#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/AI/AIEmotionStates.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace
{
template <class Runtime, class Data>
void bindRuntime(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}

int statusIndex(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

int kindIndex(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

bool hasStatus(const Object &o, const char *name)
{
	const int i = statusIndex(name);
	return i >= 0 && o.testStatus((unsigned)i);
}

bool isKind(const Object &o, const char *name)
{
	const int i = kindIndex(name);
	return i >= 0 && o.isKindOf((unsigned)i);
}

void objectFX(GameLogic &logic, const char *site, const std::string &fx, const Object &at)
{
	if (FXEventLog::isFXName(fx))
	{
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, logic.getFrame(), fx, at)); // RW 0x4B1B5A
	}
}

void positionFX(GameLogic &logic, const char *site, const std::string &fx, const Object &source, const Coord3D &pos)
{
	if (FXEventLog::isFXName(fx))
	{
		FXEvent e = FXEventLog::objectEvent(FXEvent::POSITION_FX, site, logic.getFrame(), fx, source); // RW 0x494615
		e.primary = INVALID_ID;
		e.position = pos;
		e.hasTransform = false;
		logic.fxEvents().emit(e);
	}
}

#define TD_OFF(m) (int)offsetof(TemporarilyDefectUpdateModuleData, m)
const FieldParse kTemporarilyDefect[] = { // RW 0xC76214
	{ "DefectDuration", INI::parseDurationUnsignedInt, nullptr, TD_OFF(m_defectDuration) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef TD_OFF

#define DE_OFF(m) (int)offsetof(DominateEnemySpecialPowerModuleData, m)
const FieldParse kDominateEnemy[] = { // RW 0xC76268
	{ "DominateRadius", INI::parseReal, nullptr, DE_OFF(m_dominateRadius) },
	{ "TriggerFX", SpecialAbilityModules::parseFXField, nullptr, DE_OFF(m_triggerFX) },
	{ "DominatedFX", SpecialAbilityModules::parseFXField, nullptr, DE_OFF(m_dominatedFX) },
	{ "PermanentlyConvert", INI::parseBool, nullptr, DE_OFF(m_permanentlyConvert) },
	{ "AttributeModifierAffects", SpecialAbilityModules::parseObjectFilterField, nullptr, DE_OFF(m_affects) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef DE_OFF

// RW 0x8D239B: TriggerSpecialPower = <module tag> [TARGETPOS | OBJECTPOS]; at most 8 entries (a ninth is ignored)
void parseTriggerSpecialPower(INI *ini, void *, void *store, const void *)
{
	std::vector<ActivateModuleSpecialPowerModuleData::Entry> &v = *static_cast<std::vector<ActivateModuleSpecialPowerModuleData::Entry> *>(store);
	if (v.size() >= 8)
	{
		return; // RW 0x8D23BB: `cmp eax, 8; jae` (the rest of the line is not read)
	}
	const char *tag = ini->getNextTokenOrNull();
	if (!tag || !*tag)
	{
		return; // RW 0x8D23C9 / 0x8D23CE: nothing stored
	}
	ActivateModuleSpecialPowerModuleData::Entry e;
	e.tag = tag;
	e.mode = 1; // RW 0x8D23E5
	static const char *const kModes[] = { "TARGETPOS", "OBJECTPOS" }; // RW 0xDB4B08
	if (const char *m = ini->getNextTokenOrNull())
	{
		for (int i = 0; i < 2; ++i)
		{
			if (std::strcmp(m, kModes[i]) == 0) // RW 0xA3CF40 strcmp
			{
				e.mode = i;
				break;
			}
		}
	}
	v.push_back(e); // RW 0x97D718
}

#define AM_OFF(m) (int)offsetof(ActivateModuleSpecialPowerModuleData, m)
const FieldParse kActivateModule[] = { // RW 0xC769A0
	{ "TriggerSpecialPower", parseTriggerSpecialPower, nullptr, AM_OFF(m_triggers) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef AM_OFF
#define AS_OFF(m) (int)offsetof(ArrowStormUpdateModuleData, m)
const FieldParse kArrowStorm[] = { // RW 0xC05168
	{ "WeaponTemplate", INI::parseAsciiString, nullptr, AS_OFF(m_weaponTemplate) },
	{ "TargetRadius", INI::parseReal, nullptr, AS_OFF(m_targetRadius) },
	{ "ShotsPerTarget", INI::parseInt, nullptr, AS_OFF(m_shotsPerTarget) },
	{ "ShotsPerBurst", INI::parseInt, nullptr, AS_OFF(m_shotsPerBurst) },
	{ "MaxShots", INI::parseInt, nullptr, AS_OFF(m_maxShots) },
	{ "CanShootEmptyGround", INI::parseBool, nullptr, AS_OFF(m_canShootEmptyGround) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef AS_OFF

#define RS_OFF(m) (int)offsetof(RousingSpeechUpdateModuleData, m)
const FieldParse kRousingSpeech[] = { // RW 0xC6BA48
	{ "BonusRadius", INI::parseReal, nullptr, RS_OFF(m_bonusRadius) },
	{ "SpeechDuration", INI::parseDurationUnsignedInt, nullptr, RS_OFF(m_speechDuration) },
	{ "UpdateInterval", INI::parseDurationUnsignedInt, nullptr, RS_OFF(m_updateInterval) },
	{ "LeaderFX", SpecialAbilityModules::parseFXField, nullptr, RS_OFF(m_leaderFX) },
	{ "FollowerFX", SpecialAbilityModules::parseFXField, nullptr, RS_OFF(m_followerFX) },
	{ "CreateWave", INI::parseBool, nullptr, RS_OFF(m_createWave) },
	{ "WaveWidth", INI::parseReal, nullptr, RS_OFF(m_waveWidth) },
	{ "ModifierName", INI::parseAsciiStringVectorAppend, nullptr, RS_OFF(m_modifierNames) },
	{ "LevelUp", INI::parseBool, nullptr, RS_OFF(m_levelUp) },
	{ "ObjectFilter", SpecialAbilityModules::parseObjectFilterField, nullptr, RS_OFF(m_filter) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef RS_OFF

#define TP_OFF(m) (int)offsetof(TeleportSpecialAbilityUpdateModuleData, m)
const FieldParse kTeleport[] = { // RW 0xC054B0
	{ "BusyForDuration", INI::parseDurationUnsignedInt, nullptr, TP_OFF(m_busyForFrames) },
	{ "DestinationWeaponName", INI::parseAsciiString, nullptr, TP_OFF(m_destinationWeapon) },
	{ "SourceWeaponName", INI::parseAsciiString, nullptr, TP_OFF(m_sourceWeapon) },
	{ "MaxDistance", INI::parseReal, nullptr, TP_OFF(m_maxDistance) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef TP_OFF

#define CU_OFF(m) (int)offsetof(CurseSpecialPowerModuleData, m)
const FieldParse kCurse[] = { // RW 0xC76440
	{ "TriggerFX", SpecialAbilityModules::parseFXField, nullptr, CU_OFF(m_triggerFX) },
	{ "CursedFX", SpecialAbilityModules::parseFXField, nullptr, CU_OFF(m_cursedFX) },
	{ "CursePercentage", INI::parsePercentToReal, nullptr, CU_OFF(m_cursePercentage) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef CU_OFF
} // namespace

// ---------------------------------------------------------------------------------------------------------------------------------
// the contain side of a defection (Object::defect / endDefection)
// ---------------------------------------------------------------------------------------------------------------------------------
void ContainModuleInterface::onDefect(Object *newOwner, bool permanent)
{
	(void)newOwner;
	(void)permanent;
	if (getContainCount() != 0)
	{
		if (const ContainedItemsList *l = getContainedItemsList())
		{
			if (!l->empty() && l->front())
			{
				l->front()->logic().noteStop("[S-1222] a defecting container's contain vslot 0x108 (not HordeContain) is not read: its passengers keep their side");
			}
		}
	}
}

void ContainModuleInterface::onDefectionEnded()
{
}

// RW 0x86ED25: every member, then every member on its way into a garrison (interface + 0x150, a map keyed by id: m_garrisonEntering), defects too
void HordeContain::onDefect(Object *newOwner, bool permanent)
{
	std::vector<Object *> members(m_members.begin(), m_members.end()); // RW 0x865598: the list is copied first
	for (Object *m : members)
	{
		if (m)
		{
			m->defect(newOwner, permanent);
		}
	}
	const std::vector<ObjectID> entering(m_garrisonEntering.begin(), m_garrisonEntering.end());
	for (ObjectID id : entering)
	{
		if (Object *m = getObject()->logic().findObjectByID(id))
		{
			m->defect(newOwner, permanent);
		}
	}
}

// RW 0x86EDB0: the same two lists end their defection (RW 0x69ABA7)
void HordeContain::onDefectionEnded()
{
	std::vector<Object *> members(m_members.begin(), m_members.end());
	for (Object *m : members)
	{
		if (m)
		{
			m->endDefection();
		}
	}
	const std::vector<ObjectID> entering(m_garrisonEntering.begin(), m_garrisonEntering.end());
	for (ObjectID id : entering)
	{
		if (Object *m = getObject()->logic().findObjectByID(id))
		{
			m->endDefection();
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ShareExperienceBehavior
// ---------------------------------------------------------------------------------------------------------------------------------
#define SE_OFF(m) (int)offsetof(ShareExperienceBehaviorModuleData, m)
const FieldParse kShareExperience[] = { // RW 0xC5EF30
	{ "Radius", INI::parseReal, nullptr, SE_OFF(m_radius) },
	{ "DropOff", INI::parseReal, nullptr, SE_OFF(m_dropOff) },
	{ "Percentage", INI::parseReal, nullptr, SE_OFF(m_percentage) },
	{ "ObjectFilter", SpecialAbilityModules::parseObjectFilterField, nullptr, SE_OFF(m_filter) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef SE_OFF

void ShareExperienceBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kShareExperience);
}

ShareExperienceBehavior::ShareExperienceBehavior(Thing *thing, const ShareExperienceBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x883196: RW 0x850C32(object, 0x3FFFFFFF)
}

ShareExperienceBehavior *ShareExperienceBehavior::of(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules()) // RW 0x88305B: the first module with the listener interface
	{
		if (ShareExperienceBehavior *s = dynamic_cast<ShareExperienceBehavior *>(m.get()))
		{
			return s;
		}
	}
	return nullptr;
}

float ShareExperienceBehavior::dropOffFactor(const Coord3D &from, const Object &candidate) const
{
	if (m_data->m_dropOff != 1.0f) // RW 0x883265 .. 0x883275 (ucomiss; jp: not equal)
	{
		return 1.0f;
	}
	const Coord3D *p = candidate.getPosition();
	const float dx = SimMath::subf32(p->x, from.x), dy = SimMath::subf32(p->y, from.y), dz = SimMath::subf32(p->z, from.z);
	// RW 0x4054F5: x87 (z*z + y*y) + x*x, fstp qword, CRT sqrt, fstp dword
	const double sum = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(dz, dz), NumericState::pc24MulW(dy, dy)), NumericState::pc24MulW(dx, dx));
	const float dist = NumericState::fstpDword(NumericState::sqrtPC24(sum));
	const float f = SimMath::subf32(1.0f, SimMath::divf32(dist, m_data->m_radius));
	return f >= 0.0f ? f : 0.0f; // RW 0x8832CA: comiss with 0.0; jb -> 0
}

void ShareExperienceBehavior::share(float value)
{
	Object *obj = getObject();
	if (!(m_data->m_percentage > 0.0f) || hasStatus(*obj, "BLOODTHIRSTY")) // RW 0x883305 .. 0x883325
	{
		return;
	}
	GameLogic &logic = obj->logic();
	const bool heroSource = isKind(*obj, "HERO"); // RW 0x88332F: template + 0x110 bit 26 (KindOf 90)
	const Player *owner = obj->getControllingPlayer();
	const Coord3D from = *obj->getPosition();
	PartitionFilterFn chain([&](Object &o) {
		if (&o == obj || o.isEffectivelyDead() || obj->getRelationship(o) != ALLIES) // RW 0xC1D660, 0xC10E20, 0xC11DC0 (ALLIES 4); RW 0xC0F374 always passes
		{
			return false;
		}
		return !m_data->m_filter || ObjectFilterMatch::allows(logic, *m_data->m_filter, o, owner); // RW 0xBE4CC8
	});
	std::vector<Object *> hits;
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(from, m_data->m_radius, FROM_CENTER_2D, { &chain }, ITER_FASTEST))
	{
		hits.push_back(h.object);
	}
	const bool heroesShareWithHeroes = true; // TheGameLogic + 0x9F (INFERENCE: 1 in a game, RW 0x62D299)
	for (Object *c : hits)
	{
		if (heroSource && !heroesShareWithHeroes && isKind(*c, "HERO"))
		{
			continue;
		}
		ExperienceTracker *t = c->getExperienceTracker();
		if (!t || !t->isAcceptingExperiencePoints()) // RW 0x88343D .. 0x883451
		{
			continue;
		}
		value = NumericState::pc24Mul(value, m_data->m_percentage); // RW 0x883453: fld; fmul; fstp [ebp + 8]
		value = NumericState::pc24Mul(dropOffFactor(from, *c), value); // RW 0x88346C: fmul [ebp + 8]; fstp
		if (value > 0.0f)
		{
			t->addExperiencePoints(value, true, true, true, false); // RW 0x88348F
			++m_shares;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------------------
// DamageFilteredCreateObjectDie
// ---------------------------------------------------------------------------------------------------------------------------------
const char *const kDamageSubTypeNames[] = { "NORMAL", "BECOME_UNDEAD", "SELF", "BECOME_UNDEAD_ONCE", nullptr }; // RW 0xDAF5CC

void parseDamageSubType(INI *ini, void *, void *store, const void *)
{
	INI::parseIndexList(ini, nullptr, store, kDamageSubTypeNames); // RW 0x42E956
}

// RW 0x73A368: an ObjectCreationList name, "None" stores none
void parseOclName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(name, "None") == 0 ? std::string() : name;
}

#define DF_OFF(m) (int)offsetof(DamageFilteredCreateObjectDieModuleData, m)
const FieldParse kDamageFilteredMux[] = { // RW 0xC76BD8
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, DF_OFF(m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, DF_OFF(m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, DF_OFF(m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, DF_OFF(m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, DF_OFF(m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, DF_OFF(m_dieMux.m_maxKillerAngle) },
	{ nullptr, nullptr, nullptr, 0 },
};
const FieldParse kDamageFiltered[] = { // RW 0xC613D8
	{ "CreationList", parseOclName, nullptr, DF_OFF(m_creationList) },
	{ "DamageTypeTriggersInstantly", parseDamageSubType, nullptr, DF_OFF(m_instantType) },
	{ "DamageTypeTriggersForDuration", parseDamageSubType, nullptr, DF_OFF(m_durationType) },
	{ "PostFilterTriggeredDuration", INI::parseDurationUnsignedInt, nullptr, DF_OFF(m_postFilterDuration) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef DF_OFF

void DamageFilteredCreateObjectDieModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDamageFilteredMux);
	p.add(kDamageFiltered);
}

void DamageFilteredCreateObjectDie::onDamage(const DamageInfo &info)
{
	if (info.m_input.m_damageSubType != m_data->m_durationType) // RW 0x8893AC
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	m_hitFrame = logic.getFrame(); // + 0x18
	if (Object *src = logic.findObjectByID(info.m_input.m_sourceID))
	{
		if (Player *p = src->getControllingPlayer())
		{
			m_hitPlayer = p->getPlayerIndex(); // Player + 0x54
		}
	}
}

void DamageFilteredCreateObjectDie::create(Object *primary)
{
	if (m_data->m_creationList.empty() || !primary)
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(m_data->m_creationList) : nullptr;
	if (!ocl)
	{
		return;
	}
	const Coord3D at = *getObject()->getPosition(); // RW 0x889323: the dead object's position
	m_created += (unsigned)ocl->create(logic, primary, at).size(); // RW 0x5F00CA
}

void DamageFilteredCreateObjectDie::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	if (!m_data->m_dieMux.isDieApplicable(*obj, event)) // RW 0x85FED5
	{
		return;
	}
	GameLogic &logic = obj->logic();
	if (m_data->m_instantType == event.damageSubType) // RW 0x8892E9
	{
		Object *src = logic.findObjectByID(event.sourceId);
		if (src && src->friend_allowUndeadKill(event.damageSubType, logic.getFrame())) // RW 0x889141
		{
			create(src);
		}
		logic.destroyObject(obj); // RW 0x889336
		return;
	}
	if (logic.getFrame() - m_hitFrame >= m_data->m_postFilterDuration) // RW 0x88933D .. 0x88934B (unsigned)
	{
		m_hitPlayer = -1;
		return;
	}
	if (m_hitPlayer == -1)
	{
		return;
	}
	Player *p = logic.players().getNthPlayer(m_hitPlayer); // RW 0x6A844E
	if (!p || p->isDefeated()) // RW 0x6AAC4B
	{
		return;
	}
	Object *book = SpecialPowerModules::getSpellBookObject(logic, *p); // RW 0x6AD0F8
	if (!book)
	{
		return;
	}
	create(book);
	logic.destroyObject(obj);
}

void DamageFilteredCreateObjectDie::crc(StateHasher &h) const
{
	h.addU32(m_hitFrame);
	h.addI32(m_hitPlayer);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// AutoAbilityBehavior
// ---------------------------------------------------------------------------------------------------------------------------------
// RW 0x85D29C: the next unused of the 6 query slots: the count (parseInt RW 0x42EC5E), then the ObjectFilter (RW 0x76392F)
void parseAutoAbilityQuery(INI *ini, void *instance, void *store, const void *userData)
{
	auto &slots = *static_cast<std::array<AutoAbilityBehaviorModuleData::Query, 6> *>(store);
	for (AutoAbilityBehaviorModuleData::Query &q : slots)
	{
		if (q.count < 0)
		{
			INI::parseInt(ini, instance, &q.count, userData);
			SpecialAbilityModules::parseObjectFilterField(ini, instance, &q.filter, userData);
			return;
		}
	}
	throw INIException(1, "iniParseQuery: Too many queries (%d)", 6); // RW 0xC57D60
}

#define AA_OFF(m) (int)offsetof(AutoAbilityBehaviorModuleData, m)
const FieldParse kAutoAbility[] = { // RW 0xC57EC0
	{ "SpecialAbility", INI::parseAsciiString, nullptr, AA_OFF(m_specialAbility) },
	{ "MaxScanRange", INI::parseReal, nullptr, AA_OFF(m_maxScanRange) },
	{ "MinScanRange", INI::parseReal, nullptr, AA_OFF(m_minScanRange) },
	{ "WorkingRadius", INI::parseReal, nullptr, AA_OFF(m_workingRadius) },
	{ "StartsActive", INI::parseBool, nullptr, AA_OFF(m_startsActive) },
	{ "BaseMaxRangeFromStartPos", INI::parseBool, nullptr, AA_OFF(m_baseMaxRangeFromStartPos) },
	{ "AdjustAttackMeleePosition", INI::parseBool, nullptr, AA_OFF(m_adjustAttackMeleePosition) },
	{ "Query", parseAutoAbilityQuery, nullptr, AA_OFF(m_queries) },
	{ "AllowSelf", INI::parseBool, nullptr, AA_OFF(m_allowSelf) },
	{ "IdleTimeSeconds", INI::parseReal, nullptr, AA_OFF(m_idleTimeSeconds) },
	{ "ForbiddenStatus", ParseObjectStatusMask, nullptr, AA_OFF(m_forbiddenStatus) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef AA_OFF

const unsigned kOptTargetPos = 0x20u;         // NEED_TARGET_POS (bit 5)
const unsigned kOptTargetObjects = 0x7u;      // NEED_TARGET_ENEMY / NEUTRAL / ALLY_OBJECT
const unsigned kOptNoAutoSound = 0x8u;        // NO_PLAY_UNIT_SPECIFIC_SOUND_FOR_AUTO_ABILITY
const unsigned kOptNeedUpgrade = 0x40u;       // NEED_UPGRADE (bit 6)
const unsigned kOptMountedOnly = 0x4000000u;  // MOUNTED_ONLY (bit 26)
const unsigned kOptUnmountedOnly = 0x8000000u; // UNMOUNTED_ONLY (bit 27)
const unsigned kOptAutoTriggered = 0x20000000u; // AUTO_ABILITY_TRIGGERED (bit 29)

int modelConditionIndex(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::strcmp(TheModelConditionNames[i], name) == 0)
		{
			return i;
		}
	}
	return -1;
}

bool anyBit(const Object::ModelConditionBits &a, const std::array<std::uint32_t, 19> &b) // RW 0x6632E9
{
	for (size_t i = 0; i < 19; ++i)
	{
		if (a[i] & b[i])
		{
			return true;
		}
	}
	return false;
}

bool anySet(const std::array<std::uint32_t, 19> &b) // RW 0x4B3783
{
	for (std::uint32_t w : b)
	{
		if (w)
		{
			return true;
		}
	}
	return false;
}

float visionRangeOf(const Object &o) // RW 0x68E43B: Object + 0x1B0 (the template's VisionRange; the VISION modifier and height terms as STEALTH-1, S-1227)
{
	if (const FieldValue *v = o.getTemplate()->getFinalOverride()->findField("VisionRange"))
	{
		if (const float *f = std::get_if<float>(v))
		{
			return *f;
		}
	}
	return 0.0f;
}

void AutoAbilityBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAutoAbility);
}

AutoAbilityBehavior::Stats &AutoAbilityBehavior::stats()
{
	static Stats s;
	return s;
}

AutoAbilityBehavior::AutoAbilityBehavior(Thing *thing, const AutoAbilityBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	if (!data->m_startsActive)
	{
		clearButton();
		friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x85D90E
	}
}

void AutoAbilityBehavior::onObjectCreated()
{
	if (!m_data->m_startsActive)
	{
		return;
	}
	// RW 0x85D860 .. 0x85D905: the first SPECIAL_POWER button of the command set whose power is SpecialAbility (by id and name); none leaves the module asleep
	// at its first update (no button: cleared, asleep)
	Object *obj = getObject();
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(obj->getCommandSetName()) : nullptr;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (b && b->m_command == GUI_COMMAND_SPECIAL_POWER && b->m_specialPowerName == m_data->m_specialAbility)
		{
			setAutoButton(b);
			return;
		}
	}
}

void AutoAbilityBehavior::clearButton()
{
	m_button.clear();
}

void AutoAbilityBehavior::setAutoButton(const CommandButton *button)
{
	if (button && button->m_autoAbility && button->m_name != m_button)
	{
		m_button = button->m_name; // RW 0x85D48F: and the update wakes
		wakeFromInterface(UPDATE_SLEEP_NONE);
		return;
	}
	clearButton();
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER);
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

AutoAbilityBehavior *AutoAbilityBehavior::forPower(Object &obj, const std::string &power)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		AutoAbilityBehavior *a = dynamic_cast<AutoAbilityBehavior *>(m.get());
		if (a && (a->m_data->m_specialAbility.empty() || a->m_data->m_specialAbility == power))
		{
			return a;
		}
	}
	return nullptr;
}

bool AutoAbilityBehavior::canAct(const CommandButton &b) const
{
	Object *obj = getObject();
	const ObjectStatusMaskType &forbidden = m_data->m_forbiddenStatus;
	bool anyForbidden = false;
	for (unsigned i = 0; i < 128; ++i)
	{
		if (MaskTest(forbidden, i) && obj->testStatus(i))
		{
			anyForbidden = true;
		}
	}
	if (anyForbidden) // RW 0x776243 / 0x75CDC4
	{
		return false;
	}
	// RW 0x75CC3E: the power's module fully ready, or an upgrade the object may take and lacks
	const SpecialPowerTemplate *t = TheSpecialPowerStore && !b.m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
	SpecialPowerModuleInterface *sp = t ? SpecialPowerModules::findModule(*obj, t) : nullptr;
	bool valid = sp && sp->getPercentReady() == 1.0f;
	if (!valid && !b.m_upgradeName.empty() && TheUpgradeCenter)
	{
		const UpgradeTemplate *u = TheUpgradeCenter->findUpgrade(b.m_upgradeName);
		valid = u && obj->affectedByUpgrade(u) && !obj->hasUpgrade(u); // RW 0x694914 / 0x691421
	}
	if (!valid)
	{
		return false;
	}
	// RW 0x692A75: no power, or its module; and the button is in the object's command set
	if (!b.m_specialPowerName.empty() && !sp)
	{
		return false;
	}
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(obj->getCommandSetName()) : nullptr;
	bool inSet = false;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
	{
		inSet = inSet || set->getCommandButton(i) == &b;
	}
	if (!inSet)
	{
		return false;
	}
	static const int kStunned = modelConditionIndex("STUNNED"), kFlailing = modelConditionIndex("STUNNED_FLAILING"),
					 kStandingUp = modelConditionIndex("STUNNED_STANDING_UP"), kStealth = modelConditionIndex("INVISIBLE_STEALTH");
	if (obj->testModelCondition(kStunned) || obj->testModelCondition(kFlailing) || obj->testModelCondition(kStandingUp)) // + 0x11C bit 0, + 0x118 bit 31, + 0x120 bit 3
	{
		return false;
	}
	if (obj->testModelCondition(kStealth)) // RW 0x68FC06 answers 0
	{
		return false;
	}
	if (anyBit(obj->getModelConditionBits(), b.m_autoAbilityDisallowedOnModelCondition)) // + 0x13C
	{
		return false;
	}
	if (hasStatus(*obj, "IGNORE_AI_COMMAND")) // status 74
	{
		return false;
	}
	return true; // the AI's + 0x34 (INFERENCE: 0)
}

Object *AutoAbilityBehavior::pickTarget(const std::vector<Object *> &hits, const CommandButton &b) const
{
	Object *obj = getObject();
	for (Object *c : hits)
	{
		if (!m_data->m_allowSelf && c == obj)
		{
			continue;
		}
		if (b.m_needDamagedTarget)
		{
			if (const BodyModuleInterface *body = c->getBodyModule())
			{
				// body vslot 0x14 (RW 0x8C1D75): health / max health in the FPU, compared with 0.8f (RW 0xBDE8D8)
				const float maxHealth = body->getMaxHealth();
				const double ratio = maxHealth > 0.0f ? SimMath::pc24DivW((double)body->getHealth(), (double)maxHealth) : 0.0;
				if (ratio > (double)0.8f)
				{
					continue;
				}
			}
		}
		if (!(m_data->m_minScanRange > 0.0f))
		{
			return c;
		}
		const Coord3D *p = obj->getPosition(), *q = c->getPosition();
		// Coord3D::length of object - candidate (RW 0x4054F5)
		const float dx = SimMath::subf32(p->x, q->x), dy = SimMath::subf32(p->y, q->y), dz = SimMath::subf32(p->z, q->z);
		const double sum = NumericState::pc24AddW(NumericState::pc24AddW(NumericState::pc24MulW(dz, dz), NumericState::pc24MulW(dy, dy)), NumericState::pc24MulW(dx, dx));
		const float dist = NumericState::fstpDword(NumericState::sqrtPC24(sum));
		if (m_data->m_minScanRange <= dist)
		{
			return c;
		}
	}
	return nullptr;
}

void AutoAbilityBehavior::doCommand(const CommandButton &b, Object *target, const Coord3D *pos, bool fromAuto)
{
	Object *obj = getObject();
	if (b.m_command != GUI_COMMAND_SPECIAL_POWER && b.m_command != GUI_COMMAND_SPELL_BOOK)
	{
		++stats().unportedCommands; // RW 0x696FD2's other command types (S-1227)
		return;
	}
	const SpecialPowerTemplate *t = TheSpecialPowerStore && !b.m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
	if (!t)
	{
		return; // RW: a button without a power (+ 0x44 null) does nothing
	}
	unsigned options = b.m_options | 0x40000u;
	if (fromAuto)
	{
		options |= kOptAutoTriggered;
	}
	bool done = false;
	if (pos)
	{
		done = SpecialPowerModules::doSpecialPowerAtLocation(*obj, t, *pos, options, false);
	}
	else if (target)
	{
		done = SpecialPowerModules::doSpecialPowerAtObject(*obj, t, target, options, false);
	}
	else
	{
		done = SpecialPowerModules::doSpecialPower(*obj, t, options, false);
	}
	if (done)
	{
		++stats().casts;
	}
}

UpdateSleepTime AutoAbilityBehavior::update()
{
	Object *obj = getObject();
	const CommandButton *b = TheCommandStore && !m_button.empty() ? TheCommandStore->findCommandButton(m_button) : nullptr; // RW 0x71D6EA
	static const int kMounted = modelConditionIndex("MOUNTED");
	auto abort = [&]() {
		clearButton(); // RW 0x85DE13
		return UPDATE_SLEEP_FOREVER;
	};
	if (!b || obj->isEffectivelyDead() || anyBit(obj->getModelConditionBits(), b->m_disableOnModelCondition))
	{
		return abort();
	}
	if (anySet(b->m_enableOnModelCondition) && !anyBit(obj->getModelConditionBits(), b->m_enableOnModelCondition))
	{
		return abort();
	}
	const bool mounted = obj->testModelCondition(kMounted);
	if (((b->m_options & kOptMountedOnly) && !mounted) || ((b->m_options & kOptUnmountedOnly) && mounted))
	{
		return abort();
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	const int framesPerSecond = LOGICFRAMES_PER_SECOND; // RW 0xD9F608
	if (ai && m_data->m_idleTimeSeconds > 0.0f && (!ai->isIdle() || ai->stateMachine().inTemporaryState()))
	{
		return (UpdateSleepTime)SimMath::truncToInt32(SimMath::mulf32((float)framesPerSecond, m_data->m_idleTimeSeconds));
	}
	if (!canAct(*b))
	{
		return (UpdateSleepTime)(framesPerSecond + 1);
	}
	UpdateSleepTime delay = (UpdateSleepTime)(framesPerSecond * 10);
	if (b->m_autoDelay > 0.0f)
	{
		delay = (UpdateSleepTime)SimMath::truncToInt32(SimMath::mulf32((float)framesPerSecond, b->m_autoDelay));
	}
	if (b->m_triggerWhenReady) // + 0x12C
	{
		doCommand(*b, nullptr, nullptr, false); // RW 0x696FD2(button, 2, 0)
		return delay;
	}
	float range = visionRangeOf(*obj);
	if (m_data->m_maxScanRange > 0.0f)
	{
		range = m_data->m_maxScanRange;
	}
	else if (b->m_presetRange > 0.0f)
	{
		range = b->m_presetRange;
	}
	Coord3D center = *obj->getPosition();
	if (m_data->m_baseMaxRangeFromStartPos)
	{
		if (!m_haveStartPos)
		{
			m_haveStartPos = true;
			m_startPos = center;
		}
		center = m_startPos;
	}
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();
	static const int kUnattackableKind = ObjectTemplateInfoBuilder::kindOfIndex("UNATTACKABLE");
	Object *target = nullptr;
	bool anyQuery = false;
	for (const AutoAbilityBehaviorModuleData::Query &q : m_data->m_queries)
	{
		if (q.count < 0)
		{
			continue;
		}
		anyQuery = true;
		PartitionFilterFn chain([&](Object &o) {
			if (o.isEffectivelyDead() || !EmotionModules::canSeeObject(*obj, o, range)) // RW 0xC10E20, 0xC0F374, 0xC1D66C (canSee == true)
			{
				return false;
			}
			if (hasStatus(o, "UNATTACKABLE") || hasStatus(o, "INSIDE_GARRISON") || (kUnattackableKind >= 0 && o.isKindOf((unsigned)kUnattackableKind)))
			{
				return false; // RW 0x76A28D (status 60, 58), RW 0x445139 (KindOf 54)
			}
			return !q.filter || ObjectFilterMatch::allows(logic, *q.filter, o, owner); // RW 0xBE4CC8
		});
		std::vector<Object *> hits;
		for (const PartitionHit &h : logic.partition().iterateObjectsInRange(center, range, FROM_CENTER_2D, { &chain }, ITER_SORTED_NEAR_TO_FAR))
		{
			hits.push_back(h.object);
		}
		if ((unsigned)q.count <= hits.size())
		{
			target = pickTarget(hits, *b);
			if (target)
			{
				break;
			}
		}
	}
	if (!anyQuery)
	{
		// RW 0x701443(object, range, 0x4A, 0, 0, 0) -> RW 0x7006D7: the AI's closest enemy (INFERENCE, S-1227: the closest alive ENEMIES object it can see)
		++stats().closestEnemyQueries;
		PartitionFilterFn chain([&](Object &o) { return !o.isEffectivelyDead() && obj->getRelationship(o) == ENEMIES && EmotionModules::canSeeObject(*obj, o, range); });
		for (const PartitionHit &h : logic.partition().iterateObjectsInRange(*obj->getPosition(), range, FROM_CENTER_2D, { &chain }, ITER_SORTED_NEAR_TO_FAR))
		{
			target = h.object;
			break;
		}
	}
	if (!target)
	{
		return (UpdateSleepTime)(framesPerSecond + 1);
	}
	// RW 0x85DD6B: the AI's + 0x48 := 2 (the field is not identified: S-1227)
	// RW 0x85D930: the auto ability sound (client) unless NO_PLAY_UNIT_SPECIFIC_SOUND_FOR_AUTO_ABILITY
	Coord3D pos = *target->getPosition();
	if (ai && m_data->m_adjustAttackMeleePosition)
	{
		++stats().meleeAdjust; // RW 0x6EEBC1 (the melee position offset from large objects) is not ported (S-1227)
	}
	if (b->m_options & kOptTargetPos)
	{
		doCommand(*b, nullptr, &pos, true); // RW 0x6979D9(button, pos, 2, 1)
	}
	else if (b->m_options & kOptTargetObjects)
	{
		doCommand(*b, target, nullptr, true); // RW 0x697890(button, target, 2, 1)
	}
	else
	{
		doCommand(*b, nullptr, nullptr, true); // RW 0x696FD2(button, 2, 1)
	}
	return delay;
}

void AutoAbilityBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addString(m_button);
	h.addFloat(m_startPos.x);
	h.addFloat(m_startPos.y);
	h.addFloat(m_startPos.z);
	h.addBool(m_haveStartPos);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// WeaponModeSpecialPowerUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
const LookupListRec kWeaponSlotNames[] = { { "PRIMARY", 0 }, { "SECONDARY", 1 }, { "TERTIARY", 2 }, { "QUATERNARY", 3 }, { "QUINARY", 4 }, { nullptr, 0 } }; // RW 0xC16928

void parseWeaponModePower(INI *ini, void *instance, void *, const void *) // RW 0x73B22F
{
	WeaponModeSpecialPowerUpdateModuleData *d = static_cast<WeaponModeSpecialPowerUpdateModuleData *>(instance);
	d->m_specialPowerTemplateName = ini->getNextToken();
	d->m_specialPowerTemplate = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(d->m_specialPowerTemplateName) : nullptr;
}

void parseWeaponSetFlagWords(INI *ini, void *, void *store, const void *) // RW 0x6C9951
{
	std::array<std::uint32_t, 4> *mask = static_cast<std::array<std::uint32_t, 4> *>(store);
	ParseBitFlags(ini, mask->data(), mask->size(), TheWeaponConditionNames);
}

void parseNextName(INI *ini, void *, void *store, const void *) // RW 0x73ACEF (a sound name, kept)
{
	*static_cast<std::string *>(store) = ini->getNextToken();
}

#define WM_OFF(m) (int)offsetof(WeaponModeSpecialPowerUpdateModuleData, m)
const FieldParse kWeaponModeBase[] = { // RW 0xC87588
	{ "SpecialPowerTemplate", parseWeaponModePower, nullptr, 0 },
	{ "InitiateSound", parseNextName, nullptr, WM_OFF(m_initiateSound) },
	{ "StartsPaused", INI::parseBool, nullptr, WM_OFF(m_startsPaused) },
	{ nullptr, nullptr, nullptr, 0 },
};
const FieldParse kWeaponMode[] = { // RW 0xC650A0
	{ "Duration", INI::parseDurationUnsignedInt, nullptr, WM_OFF(m_duration) },
	{ "AttributeModifier", INI::parseAsciiString, nullptr, WM_OFF(m_attributeModifier) },
	{ "LockWeaponSlot", INI::parseLookupList, kWeaponSlotNames, WM_OFF(m_lockWeaponSlot) },
	{ "WeaponSetFlags", parseWeaponSetFlagWords, nullptr, WM_OFF(m_weaponSetFlags) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef WM_OFF

void WeaponModeSpecialPowerUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kWeaponModeBase); // RW 0x9911B3
	p.add(kWeaponMode);
}

WeaponModeSpecialPowerUpdate::WeaponModeSpecialPowerUpdate(Thing *thing, const WeaponModeSpecialPowerUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	const SpecialPowerTemplate *t = data->m_specialPowerTemplate;
	if (t && !t->isSharedNSync())
	{
		startPowerRecharge(1.0f); // RW 0x991694
	}
	if (data->m_startsPaused)
	{
		pauseCountdown(true); // RW 0x9916A9
	}
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x8982F5 / 0x9916B5
}

unsigned WeaponModeSpecialPowerUpdate::now() const
{
	return getObject()->logic().getFrame();
}

bool WeaponModeSpecialPowerUpdate::readyImpl(bool insert) const
{
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	Object *obj = getObject();
	Player *p = obj ? obj->getControllingPlayer() : nullptr;
	if (t && p && t->isSharedNSync()) // RW 0x991367 .. 0x991399
	{
		const unsigned shared = insert ? p->science().getSharedReadyFrame(t->getID(), now()) : p->science().peekSharedReadyFrame(t->getID(), now());
		return shared <= now();
	}
	return m_pauseCount == 0 && m_readyFrame <= now();
}

float WeaponModeSpecialPowerUpdate::percentImpl(bool insert) const
{
	if (readyImpl(insert))
	{
		return 1.0f;
	}
	if (m_pauseCount > 0)
	{
		return m_pausedPercent; // RW 0x9913DE
	}
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	if (!t)
	{
		return 0.0f;
	}
	unsigned ready = m_readyFrame;
	Player *p = getObject()->getControllingPlayer();
	if (p && t->isSharedNSync())
	{
		ready = insert ? p->science().getSharedReadyFrame(t->getID(), now()) : p->science().peekSharedReadyFrame(t->getID(), now());
	}
	// RW 0x991434 .. 0x991469: fild (unsigned)(ready - now); fstp dword; fild (unsigned)ReloadTime; fdivr; fsubr 1.0
	const float left = NumericState::fstpDword(NumericState::fildU32(ready - now()));
	const double q = NumericState::pc24DivW((double)left, NumericState::fildU32(t->getReloadTime()));
	return NumericState::fstpDword(NumericState::pc24SubW(1.0, q));
}

unsigned WeaponModeSpecialPowerUpdate::getReadyFrame() const
{
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	Player *p = getObject()->getControllingPlayer();
	if (t && p && t->isSharedNSync())
	{
		return p->science().getSharedReadyFrame(t->getID(), now());
	}
	if (m_pauseCount < 1 && getObject()->getDisabledMask() == 0)
	{
		return m_readyFrame;
	}
	return now() - m_pauseFrame + m_readyFrame;
}

void WeaponModeSpecialPowerUpdate::pauseCountdown(bool pause)
{
	if (pause)
	{
		if (m_pauseCount == 0)
		{
			m_pauseFrame = now();
			m_pausedPercent = getPercentReady();
		}
		++m_pauseCount;
		return;
	}
	if (m_pauseCount > 0 && --m_pauseCount == 0)
	{
		m_readyFrame += now() - m_pauseFrame;
	}
}

void WeaponModeSpecialPowerUpdate::startPowerRecharge(float percentOfCurrent)
{
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	Object *obj = getObject();
	Player *p = obj ? obj->getControllingPlayer() : nullptr;
	if (!t || !p)
	{
		return;
	}
	if (t->isSharedNSync())
	{
		p->science().resetOrStartSharedReadyFrame(t->getID(), t->getReloadTime(), now()); // RW 0x991549
		return;
	}
	const float modifierScale = 1.0f; // RW 0x991564: the RECHARGE attribute modifier product (not ported, as SpecialPowerModule's: S-529)
	const float playerScale = SimMath::pc24Add(p->science().rechargeDiscount(), 1.0f); // RW 0x99156C .. 0x99157B
	const double wide = SimMath::pc24MulW(SimMath::pc24MulW(NumericState::fildU32(t->getReloadTime()), playerScale), modifierScale);
	const unsigned reload = SimMath::ftol2Low32(wide); // RW 0x9915A1
	if (1.0f > percentOfCurrent)
	{
		const float pct = getPercentReady();
		if (pct == 0.0f)
		{
			return; // RW 0x9915CD
		}
		float rest = SimMath::sseSub(pct, percentOfCurrent);
		if (0.0f > rest)
		{
			rest = 0.0f;
		}
		const double scaled = SimMath::pc24MulW(SimMath::pc24SubW(1.0, rest), NumericState::fildU32(reload)); // fld1 ; fsub ; fild ; fmulp
		m_readyFrame = SimMath::ftol2Low32(scaled) + now();
		return;
	}
	m_readyFrame = now() + reload;
}

void WeaponModeSpecialPowerUpdate::doSpecialPower(unsigned)
{
	if (m_pauseCount < 1 && getObject()->getDisabledMask() == 0) // RW 0x9911FB: not paused, not disabled (RW 0x9325B4)
	{
		initiate();
	}
}

void WeaponModeSpecialPowerUpdate::doSpecialPowerAtObject(Object *, unsigned options)
{
	doSpecialPower(options); // RW 0x991231: the same initiate with the target (WeaponMode ignores it)
}

void WeaponModeSpecialPowerUpdate::doSpecialPowerAtLocation(const Coord3D &, unsigned options)
{
	doSpecialPower(options); // RW 0x991269
}

void WeaponModeSpecialPowerUpdate::initiate()
{
	Object *obj = getObject();
	ObjectWeapons *weapons = obj->getWeapons();
	if (m_data->m_lockWeaponSlot == 5)
	{
		if (weapons) // RW 0x68FDF7
		{
			for (int bit = 0; bit < 0x68; ++bit)
			{
				if ((m_data->m_weaponSetFlags[(size_t)bit >> 5] >> (bit & 31)) & 1u)
				{
					weapons->setWeaponSetFlag(bit, true); // RW 0x691059
				}
			}
		}
	}
	else if (weapons)
	{
		weapons->setWeaponLock(m_data->m_lockWeaponSlot, LOCKED_PERMANENTLY); // RW 0x69121A(slot, 2)
	}
	if (!m_data->m_attributeModifier.empty())
	{
		obj->addAttributeModifier(m_data->m_attributeModifier, (int)m_data->m_duration); // RW 0x68F1A8
	}
	setWakeFrame(obj, (UpdateSleepTime)m_data->m_duration); // RW 0x850C32(object, Duration)
	m_active = true;
	startPowerRecharge(1.0f); // slot 0x3C (1.0)
	// RW 0x8984B7: the object's FiringTracker reset (RW 0x8E302A(1)) is COMBAT-1's: not called (S-1228)
}

UpdateSleepTime WeaponModeSpecialPowerUpdate::update()
{
	Object *obj = getObject();
	if (ObjectWeapons *weapons = obj->getWeapons())
	{
		weapons->releaseWeaponLock(LOCKED_PERMANENTLY); // RW 0x68DF11(2)
		for (int bit = 0; bit < 0x68; ++bit)
		{
			if ((m_data->m_weaponSetFlags[(size_t)bit >> 5] >> (bit & 31)) & 1u)
			{
				weapons->setWeaponSetFlag(bit, false); // RW 0x691106
			}
		}
	}
	if (!m_data->m_attributeModifier.empty())
	{
		obj->removeAttributeModifier(m_data->m_attributeModifier); // RW 0x68F259
	}
	m_active = false;
	pauseCountdown(false); // slot 0x24 (0)
	return UPDATE_SLEEP_FOREVER;
}

void WeaponModeSpecialPowerUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_readyFrame);
	h.addI32(m_pauseCount);
	h.addU32(m_pauseFrame);
	h.addFloat(m_pausedPercent);
	h.addBool(m_active);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// DualWeaponBehavior
// ---------------------------------------------------------------------------------------------------------------------------------
#define DW_OFF(m) (int)offsetof(DualWeaponBehaviorModuleData, m)
const FieldParse kDualWeapon[] = { // RW 0xC58088
	{ "SwitchWeaponOnCloseRangeDistance", INI::parseReal, nullptr, DW_OFF(m_switchDistance) },
	{ "UseCloseRangeWhileMounted", INI::parseBool, nullptr, DW_OFF(m_useCloseRangeWhileMounted) },
	{ "MinimumSwitchTime", INI::parseDurationUnsignedInt, nullptr, DW_OFF(m_minimumSwitchTime) },
	{ "UseHordeRangeWeapon", INI::parseBool, nullptr, DW_OFF(m_useHordeRangeWeapon) },
	{ "UseRealVictimRange", INI::parseBool, nullptr, DW_OFF(m_useRealVictimRange) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef DW_OFF

const int kWeaponSetCloseRange = 7; // TheWeaponConditionNames[7] CLOSE_RANGE
const int kWeaponSetRampage = 8;    // RAMPAGE

void DualWeaponBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kDualWeapon);
}

DualWeaponBehavior::DualWeaponBehavior(Thing *thing, const DualWeaponBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), (UpdateSleepTime)1); // RW 0x85DF0B: RW 0x850C32(object, 1)
}

UpdateSleepTime DualWeaponBehavior::setClose(bool close)
{
	Object *obj = getObject();
	ObjectWeapons *weapons = obj->getWeapons();
	static const int kMcClose = modelConditionIndex("WEAPONSTATE_CLOSE_RANGE");
	const bool isClose = weapons && MaskTest(weapons->weaponSetFlags(), (unsigned)kWeaponSetCloseRange);
	if (close == isClose)
	{
		return (UpdateSleepTime)LOGICFRAMES_PER_SECOND; // RW 0x85E119 / 0x85E16B
	}
	if (weapons)
	{
		weapons->setWeaponSetFlag(kWeaponSetCloseRange, close); // RW 0x691059 / 0x691106 (7)
	}
	if (obj->testModelCondition(kMcClose) != close) // Object + 0x120 bit 0, then RW 0x68B53C
	{
		Object::ModelConditionBits clearBits{}, setBits{};
		(close ? setBits : clearBits)[(size_t)kMcClose >> 5] = 1u << (kMcClose & 31);
		obj->clearAndSetModelConditionFlags(clearBits, setBits);
	}
	++m_switches;
	return (UpdateSleepTime)LOGICFRAMES_PER_SECOND;
}

UpdateSleepTime DualWeaponBehavior::update()
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai || obj->isEffectivelyDead())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const int fps = LOGICFRAMES_PER_SECOND;
	ObjectWeapons *weapons = obj->getWeapons();
	const bool rampage = weapons && MaskTest(weapons->weaponSetFlags(), (unsigned)kWeaponSetRampage);
	if (rampage)
	{
		return (UpdateSleepTime)fps;
	}
	Object *container = obj->getContainedBy();
	const bool hordeContainer = container && isKind(*container, "HORDE"); // template + 0x114 bit 13 (KindOf 109)
	if (container && !hordeContainer)
	{
		return (UpdateSleepTime)fps;
	}
	if (m_data->m_useHordeRangeWeapon && hordeContainer)
	{
		return setClose(weapons && MaskTest(weapons->weaponSetFlags(), (unsigned)kWeaponSetCloseRange)); // RW 0x85E003 .. 0x85E019
	}
	static const int kUsingAbility = modelConditionIndex("USING_SPECIAL_ABILITY"), kMounted = modelConditionIndex("MOUNTED");
	if (obj->testModelCondition(kUsingAbility))
	{
		return (UpdateSleepTime)fps;
	}
	if (m_closeRequest)
	{
		return setClose(true);
	}
	const float range = m_data->m_switchDistance;
	Object *victim = nullptr;
	Object *goal = m_data->m_useRealVictimRange ? ai->currentVictim() : nullptr; // RW 0x668303: AIUpdate + 0x40 by id, the current victim (MOD-4 r2: was the state machine's goal object)
	if (goal)
	{
		const Coord3D *p = obj->getPosition(), *q = goal->getPosition();
		const float dz = SimMath::subf32(p->z, q->z), dy = SimMath::subf32(p->y, q->y), dx = SimMath::subf32(p->x, q->x);
		const float d2 = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dz, dz), SimMath::mulf32(dy, dy)), SimMath::mulf32(dx, dx));
		victim = d2 > SimMath::mulf32(range, range) ? nullptr : goal;
	}
	else
	{
		// RW 0x701443(object, range, 0x62) -> RW 0x7006D7 (the AI's target search: INFERENCE, S-1228: the closest visible alive enemy)
		PartitionFilterFn chain([&](Object &o) { return !o.isEffectivelyDead() && obj->getRelationship(o) == ENEMIES && EmotionModules::canSeeObject(*obj, o, range); });
		for (const PartitionHit &h : obj->logic().partition().iterateObjectsInRange(*obj->getPosition(), range, FROM_CENTER_2D, { &chain }, ITER_SORTED_NEAR_TO_FAR))
		{
			victim = h.object;
			break;
		}
	}
	if (victim)
	{
		// RW 0x85E0CD .. 0x85E0EE: fld victim z; fsub z; fstp qword; CRT fabs; compare with range * 0.5 (x87)
		const double dz = (double)SimMath::pc24Sub(victim->getPosition()->z, obj->getPosition()->z);
		const double half = SimMath::pc24MulW((double)range, 0.5);
		if ((dz < 0.0 ? -dz : dz) > half)
		{
			victim = nullptr;
		}
	}
	if (m_data->m_useCloseRangeWhileMounted && obj->testModelCondition(kMounted))
	{
		return setClose(true);
	}
	const unsigned now = obj->logic().getFrame();
	const int wait = (int)(m_data->m_minimumSwitchTime - now + m_lastSwitch);
	if (wait > 0)
	{
		return (UpdateSleepTime)wait;
	}
	m_lastSwitch = now;
	return setClose(victim && !victim->isEffectivelyDead());
}

void DualWeaponBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_closeRequest);
	h.addU32(m_lastSwitch);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// TemporarilyDefectUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
void TemporarilyDefectUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kTemporarilyDefect);
}

TemporarilyDefectUpdate::TemporarilyDefectUpdate(Thing *thing, const TemporarilyDefectUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	friend_setNextCallFrame((UnsignedInt)UPDATE_SLEEP_FOREVER); // RW 0x8D0B5C: RW 0x850C32(object, 0x3FFFFFFF)
}

TemporarilyDefectUpdate *TemporarilyDefectUpdate::of(Object &obj)
{
	return dynamic_cast<TemporarilyDefectUpdate *>(obj.findModule("TemporarilyDefectUpdate"));
}

// RW 0x8D0BF3
void TemporarilyDefectUpdate::startDefection(Object *dominator, unsigned untilFrame)
{
	Object *obj = getObject();
	if (m_endFrame != 0 || !dominator || !obj || hasStatus(*obj, "TEMPORARILY_DEFECTED"))
	{
		return;
	}
	m_dominator = dominator->getID();     // + 0x28
	obj->setTemporaryTeam(dominator->getTeam()); // RW 0x699513
	if (!isKind(*dominator, "SPELL_BOOK")) // template + 0x117 bit 3
	{
		const unsigned now = obj->logic().getFrame();
		m_startFrame = now;
		m_endFrame = untilFrame != 0 ? untilFrame : m_data->m_defectDuration + now;
		setWakeFrame(obj, (UpdateSleepTime)(5 * 2)); // RW 0x8D0C58: RW global 0xD9F608 (5) * 2
		m_ended = false;
	}
	else
	{
		setWakeFrame(obj, UPDATE_SLEEP_FOREVER);
	}
}

// RW 0x8D0C87
bool TemporarilyDefectUpdate::defectionOver()
{
	if (m_dominator == INVALID_ID)
	{
		return true;
	}
	GameLogic &logic = getObject()->logic();
	Object *d = logic.findObjectByID(m_dominator);
	bool over;
	if (!d)
	{
		over = true;
	}
	else if (isKind(*d, "SPELL_BOOK"))
	{
		const Player *p = d->getControllingPlayer();
		over = p && p->isDefeated(); // RW 0x6AAC4B: Player + 0x754
	}
	else
	{
		over = d->isEffectivelyDead() || logic.getFrame() >= m_endFrame; // + 0x458 bit 0; TheGameLogic + 0x40 >= + 0x20
	}
	if (over)
	{
		m_dominator = INVALID_ID;
	}
	return over;
}

// RW 0x8D0CE0
UpdateSleepTime TemporarilyDefectUpdate::update()
{
	Object *obj = getObject();
	if (m_ended)
	{
		m_startFrame = 0;
		m_endFrame = 0;
		m_ended = false;
		// RW 0x8D0D12 .. 0x8D0D4C: the local owner's selection / UI refresh (client)
		return UPDATE_SLEEP_FOREVER;
	}
	if (!defectionOver())
	{
		return (UpdateSleepTime)10;
	}
	m_startFrame = 0;
	m_endFrame = 0;
	m_ended = false;
	if (!obj->getContainedBy()) // + 0x27C
	{
		obj->endDefection(); // RW 0x69ABA7
	}
	return UPDATE_SLEEP_FOREVER;
}

void TemporarilyDefectUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_endFrame);
	h.addU32(m_startFrame);
	h.addU32(m_dominator);
	h.addBool(m_ended);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// DominateEnemySpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
void DominateEnemySpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kDominateEnemy);
}

DominateEnemySpecialPower::DominateEnemySpecialPower(Thing *thing, const DominateEnemySpecialPowerModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_de(data)
{
}

// RW 0x8D0F6C
void DominateEnemySpecialPower::dominate(Object *victim, bool direct)
{
	Object *owner = getObject();
	GameLogic &logic = owner->logic();
	if (!victim || victim == owner || victim->getTeam() == owner->getTeam())
	{
		return;
	}
	if (m_de->m_affects && !ObjectFilterMatch::allows(logic, *m_de->m_affects, *victim, owner->getControllingPlayer())) // RW 0x7640C1
	{
		return;
	}
	if (hasStatus(*victim, "UNDER_CONSTRUCTION")) // status 2
	{
		return;
	}
	if (Object *producer = logic.findObjectByID(victim->getProducerID())) // Object + 0x78
	{
		if (isKind(*producer, "HORDE")) // template + 0x115 bit 5
		{
			if (!direct)
			{
				return;
			}
			victim = producer;
		}
	}
	if (hasStatus(*victim, "AIRBORNE_TARGET")) // + 0x94 bit 6
	{
		return;
	}
	victim->defect(owner, m_de->m_permanentlyConvert); // RW 0x699368
	++m_dominated;
	objectFX(logic, "DominateEnemySpecialPower", m_de->m_dominatedFX, *victim); // the drawable's vslot 0x1EC / RW 0x4B1B5A
}

// RW 0x8D103D
void DominateEnemySpecialPower::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	Object *owner = getObject();
	GameLogic &logic = owner->logic();
	if (Object *t = logic.findObjectByID(m_targetID)) // + 0x40
	{
		dominate(t, true);
		return;
	}
	PartitionFilterFn chain([&](Object &o) {
		return owner->getRelationship(o) == ENEMIES && !o.isEffectivelyDead() && !hasStatus(o, "HORDE_MEMBER"); // RW 0xC11DC0, 0xC10E20, 0xC0F374, 0xC1D6A0
	});
	std::vector<Object *> victims;
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(m_targetPos, m_de->m_dominateRadius, FROM_CENTER_2D, { &chain }, ITER_SORTED_NEAR_TO_FAR))
	{
		victims.push_back(h.object);
	}
	for (Object *v : victims)
	{
		dominate(v, false);
	}
	positionFX(logic, "DominateEnemySpecialPower", m_de->m_triggerFX, *owner, m_targetPos); // RW 0x8D116A: RW 0x494615(TriggerFX, location)
}

void DominateEnemySpecialPower::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_dominated);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// ActivateModuleSpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
void ActivateModuleSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kActivateModule);
}

ActivateModuleSpecialPower::ActivateModuleSpecialPower(Thing *thing, const ActivateModuleSpecialPowerModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_am(data)
{
}

// RW 0x8D222A
void ActivateModuleSpecialPower::setModulesActive(bool deactivate)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	for (const ActivateModuleSpecialPowerModuleData::Entry &e : m_am->m_triggers)
	{
		const NameKeyType key = logic.things().nameKeys().findKey(e.tag);
		BehaviorModule *m = key == NAMEKEY_INVALID ? nullptr : obj->findModuleByTag(key); // RW 0x68F9DB
		if (!m)
		{
			continue;
		}
		if (m == this)
		{
			logic.reportError("ActivateModuleSpecialPower: " + obj->getTemplate()->getName() + " triggers its own module (RW 0x8D2272)");
			continue;
		}
		if (UpdateModule *u = dynamic_cast<UpdateModule *>(m)) // the + 0xC table's vslot 0x24
		{
			u->wakeFromInterface(deactivate ? UPDATE_SLEEP_FOREVER : (UpdateSleepTime)1); // update interface vslot 8
			continue;
		}
		if (deactivate)
		{
			continue;
		}
		if (SpecialPowerModuleInterface *sp = dynamic_cast<SpecialPowerModuleInterface *>(m)) // the + 0xC table's vslot 0x20
		{
			const Coord3D at = e.mode == 1 ? *obj->getPosition() : m_targetPos; // Object + 0x38 / the module's + 0x44
			sp->doSpecialPowerAtLocation(at, 0); // vslot 0x30
			++m_activations;
		}
	}
}

// RW 0x8D238D
void ActivateModuleSpecialPower::onObjectCreated()
{
	SpecialAbilityUpdate::onObjectCreated();
	if (getObject())
	{
		setModulesActive(true);
	}
}

// RW 0x8D235D
void ActivateModuleSpecialPower::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	setModulesActive(false);
}

// RW 0x8D2370
void ActivateModuleSpecialPower::finishAbility(bool ownerDying, bool aborted)
{
	SpecialAbilityUpdate::finishAbility(ownerDying, aborted);
	setModulesActive(true);
}

void ActivateModuleSpecialPower::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_activations);
}


// ---------------------------------------------------------------------------------------------------------------------------------
// ArrowStormUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
void ArrowStormUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kArrowStorm);
}

ArrowStormUpdate::ArrowStormUpdate(Thing *thing, const ArrowStormUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_as(data)
{
}

// RW 0x894396
void ArrowStormUpdate::startPreparation()
{
	SpecialAbilityUpdate::startPreparation();
	m_targets.clear();
	m_current = INVALID_ID;
	m_shotsAtCurrent = 0;
	m_shots = 0;
	m_done = false;
	gatherTargets();
}

// RW 0x894151
void ArrowStormUpdate::gatherTargets()
{
	Object *owner = getObject();
	GameLogic &logic = owner->logic();
	PartitionFilterFn chain([&](Object &o) { return owner->getRelationship(o) == ENEMIES && !o.isEffectivelyDead(); }); // RW 0xC11DC0, 0xC10E20, 0xC0F374
	static const char *const kSkip[] = { "BASE_FOUNDATION", "INERT", "IGNORED_IN_GUI", "WALL_UPGRADE", "UNATTACKABLE", "MOVE_ONLY" }; // RW 0x8941FB .. 0x894235
	std::vector<ObjectID> monsters;
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(m_targetPos, m_as->m_targetRadius, FROM_CENTER_2D, { &chain }, ITER_SORTED_NEAR_TO_FAR))
	{
		bool skip = false;
		for (const char *k : kSkip)
		{
			skip = skip || isKind(*h.object, k);
		}
		if (skip)
		{
			continue;
		}
		m_targets.push_back(h.object->getID()); // RW 0x894247
		if (isKind(*h.object, "MONSTER"))       // template + 0x109 bit 2
		{
			monsters.push_back(h.object->getID()); // RW 0x894271
		}
	}
	if (m_targets.empty())
	{
		return;
	}
	// RW 0x8942A3 .. 0x8942FA: cycle through the MONSTER list (the list itself when there is none) appending until MaxShots ids are queued
	const bool fromMonsters = !monsters.empty();
	size_t i = 0;
	while ((int)m_targets.size() < m_as->m_maxShots)
	{
		const std::vector<ObjectID> &src = fromMonsters ? monsters : m_targets;
		if (i >= src.size())
		{
			i = 0;
		}
		m_targets.push_back(src[i]);
		++i;
	}
}

// RW 0x893CBC
bool ArrowStormUpdate::continuePreparation()
{
	if (m_done)
	{
		return false;
	}
	return SpecialAbilityUpdate::continuePreparation();
}

// RW 0x893EC8
bool ArrowStormUpdate::shootOne()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (m_shotsAtCurrent >= m_as->m_shotsPerTarget)
	{
		m_current = INVALID_ID;
		m_shotsAtCurrent = 0;
	}
	Object *victim = m_current != INVALID_ID ? logic.findObjectByID(m_current) : nullptr;
	while (!victim && !m_targets.empty())
	{
		m_current = m_targets.front();
		m_shotsAtCurrent = 0;
		m_targets.erase(m_targets.begin()); // RW 0x893E36
		victim = logic.findObjectByID(m_current);
		if (victim && victim->isEffectivelyDead()) // + 0x458 bit 0
		{
			victim = nullptr;
		}
	}
	const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(m_as->m_weaponTemplate) : nullptr; // RW 0x6CC5DF
	ObjectWeapons *weapons = obj->getWeapons();
	if (!victim)
	{
		if (!m_as->m_canShootEmptyGround)
		{
			return true;
		}
		const float r = m_as->m_targetRadius;
		Coord3D at;
		at.x = logic.random().getValueReal(SimMath::subf32(m_targetPos.x, r), SimMath::addf32(m_targetPos.x, r), "ArrowStormUpdate.cpp", 0xD9); // RW 0x893FB8
		at.y = logic.random().getValueReal(SimMath::subf32(m_targetPos.y, r), SimMath::addf32(m_targetPos.y, r), "ArrowStormUpdate.cpp", 0xDA); // RW 0x893FF0
		at.z = logic.getGroundHeight(at.x, at.y); // TheTerrainLogic vslot 0x18
		if (wt && weapons)
		{
			std::unique_ptr<Weapon> temp = weapons->makeExtraWeapon(wt); // RW 0x6CF530: a temporary weapon, fired once and deleted
			temp->setOwnerID(obj->getID());
			weapons->fireExtraWeaponAt(*temp, at);
		}
	}
	else
	{
		if (wt && weapons)
		{
			std::unique_ptr<Weapon> temp = weapons->makeExtraWeapon(wt); // RW 0x6CF590
			temp->setOwnerID(obj->getID());
			weapons->fireExtraWeapon(*temp, *victim);
		}
		++m_shotsAtCurrent;
	}
	++m_shots;
	return m_shots >= m_as->m_maxShots;
}

// RW 0x894100
void ArrowStormUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	if (!m_as->m_canShootEmptyGround && m_targets.empty())
	{
		m_done = true;
		return;
	}
	for (int i = 0; i < m_as->m_shotsPerBurst; ++i)
	{
		m_done = shootOne();
		if (m_done)
		{
			return;
		}
	}
}

void ArrowStormUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32((std::uint32_t)m_targets.size());
	for (ObjectID id : m_targets)
	{
		h.addU32(id);
	}
	h.addU32(m_current);
	h.addI32(m_shotsAtCurrent);
	h.addI32(m_shots);
	h.addBool(m_done);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// CurseSpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
void CurseSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kCurse);
}

CurseSpecialPower::CurseSpecialPower(Thing *thing, const CurseSpecialPowerModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_cs(data)
{
}

// RW 0x8D12E3
void CurseSpecialPower::curse(Object &victim)
{
	Object *owner = getObject();
	if (&victim == owner || !isKind(victim, "HERO") || owner->getRelationship(victim) != ENEMIES) // template + 0x113 bit 2; RW 0x68D7AB
	{
		return;
	}
	if (hasStatus(victim, "DESTROYED") || hasStatus(victim, "UNDER_CONSTRUCTION")) // statuses 3, 2
	{
		return;
	}
	// RW 0x68BDD0: every module's + 0xC slot 0xAC; a SpecialPowerModule's (RW 0x6519AF) and WeaponModeSpecialPowerUpdate's base (RW 0x898263) restart
	// their special power interface's recharge at the percentage (vslot 0x3C); the other classes' slot is a no-op
	for (const std::unique_ptr<BehaviorModule> &m : victim.modules())
	{
		if (SpecialPowerModuleInterface *sp = m->getSpecialPower())
		{
			sp->startPowerRecharge(m_cs->m_cursePercentage);
		}
	}
	++m_cursed;
	objectFX(owner->logic(), "CurseSpecialPower", m_cs->m_cursedFX, victim); // RW 0x4B1B5A
}

// RW 0x8D134C
void CurseSpecialPower::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	Object *owner = getObject();
	GameLogic &logic = owner->logic();
	if (m_targetID != INVALID_ID) // + 0x40
	{
		if (Object *t = logic.findObjectByID(m_targetID))
		{
			curse(*t);
			positionFX(logic, "CurseSpecialPower", m_cs->m_triggerFX, *owner, m_targetPos);
		}
		return;
	}
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate; // RW 0x8513D6
	if (!t)
	{
		return;
	}
	PartitionFilterFn chain([&](Object &o) { return owner->getRelationship(o) == ENEMIES && isKind(o, "HERO") && !o.isEffectivelyDead(); }); // RW 0xC11DC0, 0x445139, 0xC10E20
	std::vector<Object *> hits;
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(m_targetPos, t->getRadiusCursorRadius(), FROM_CENTER_2D, { &chain }, ITER_FASTEST))
	{
		hits.push_back(h.object);
	}
	for (Object *o : hits)
	{
		curse(*o);
	}
	if (!hits.empty())
	{
		positionFX(logic, "CurseSpecialPower", m_cs->m_triggerFX, *owner, m_targetPos);
	}
}

void CurseSpecialPower::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_cursed);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// FellBeastSwoopPower
// ---------------------------------------------------------------------------------------------------------------------------------
FellBeastSwoopPower::FellBeastSwoopPower(Thing *thing, const SpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
{
}

// RW 0x8CB2CE
UpdateSleepTime FellBeastSwoopPower::update()
{
	Object *obj = getObject();
	if (!m_started)
	{
		triggerAbilityEffect(); // vslot 0x44
		m_started = true;
		return (UpdateSleepTime)1;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (ai && ai->isIdle()) // AI vslot 0x1B8 (vslot 0x1D0 not identified: S-1223)
	{
		finishAbility(false, false); // vslot 0x34
		return UPDATE_SLEEP_FOREVER;
	}
	// RW 0x8CB336 .. 0x8CB3A9: SPECIAL_WEAPON_ONE (+ 0x128 bit 20) while the height above the terrain is below the bounding sphere radius (GeometryInfo + 0x14)
	const Coord3D &p = *obj->getPosition();
	const float above = SimMath::subf32(p.z, obj->logic().getGroundHeight(p.x, p.y));
	const bool low = CombatQueries::boundingSphereRadius(*obj) > above;
	if (obj->testModelCondition(244) != low)
	{
		obj->setModelConditionState(244, low);
	}
	return (UpdateSleepTime)1;
}

// RW 0x8CB28B: the AI command 0x3F (the target object) or 0x40 (the location), source 2
void FellBeastSwoopPower::triggerAbilityEffect()
{
	Object *obj = getObject();
	AIUpdateInterface *ai = obj->getAIUpdateInterface();
	if (!ai)
	{
		return;
	}
	Object *t = obj->logic().findObjectByID(m_targetID);
	ai->aiUnported(t ? "level attack object (AI command 0x3F: GiantBirdAIUpdate vslot 0x90)" : "level attack position (AI command 0x40: GiantBirdAIUpdate vslot 0x94)", CMD_FROM_AI);
	++m_commands;
}

// RW 0x8CB194
void FellBeastSwoopPower::finishAbility(bool ownerDying, bool aborted)
{
	(void)ownerDying;
	(void)aborted;
	Object *obj = getObject();
	if (obj->testModelCondition(244))
	{
		obj->setModelConditionState(244, false); // + 0x12A bit 4
	}
	m_started = false; // + 0x88 (retail's end does not call SpecialAbilityUpdate's: the module's + 0x74 and the object's statuses stay as the initiate set them)
}

void FellBeastSwoopPower::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addBool(m_started);
	h.addU32(m_commands);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// RousingSpeechUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
constexpr int kMcFollower = 201; // + 0x124 bit 9 / + 0x125 bit 1
constexpr int kMcSpeech = 207;   // + 0x124 bit 15 / + 0x125 bit 7
}

void RousingSpeechUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kRousingSpeech);
}

RousingSpeechUpdate::RousingSpeechUpdate(Thing *thing, const RousingSpeechUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_rs(data)
{
}

// RW 0x8B0591
void RousingSpeechUpdate::releaseFollowers()
{
	GameLogic &logic = getObject()->logic();
	for (ObjectID id : m_followers)
	{
		if (Object *f = logic.findObjectByID(id))
		{
			if (f->testModelCondition(kMcFollower))
			{
				f->setModelConditionState(kMcFollower, false);
			}
			f->setSpeechLeader(INVALID_ID); // + 0x46C
		}
	}
	m_followers.clear();
}

// RW 0x8B068D
UpdateSleepTime RousingSpeechUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	releaseFollowers();
	if (!m_started)
	{
		startPreparation(); // vslot 0x3C
		m_endFrame = m_rs->m_speechDuration + logic.getFrame();
		m_started = true;
	}
	if (logic.getFrame() < m_endFrame && m_inner < m_rs->m_bonusRadius)
	{
		if (m_rs->m_createWave)
		{
			m_inner = m_outer;
			m_outer = SimMath::addf32(m_rs->m_waveWidth, m_outer); // RW 0x8B06FC: addss
			if (m_outer > m_rs->m_bonusRadius)
			{
				m_outer = m_rs->m_bonusRadius;
			}
		}
		triggerAbilityEffect(); // vslot 0x44
		return (UpdateSleepTime)m_rs->m_updateInterval;
	}
	m_started = false;
	if (obj->testModelCondition(kMcSpeech))
	{
		obj->setModelConditionState(kMcSpeech, false);
	}
	return UPDATE_SLEEP_FOREVER;
}

// RW 0x8B0485
void RousingSpeechUpdate::startPreparation()
{
	SpecialAbilityUpdate::startPreparation();
	Object *obj = getObject();
	if (!obj->testModelCondition(kMcSpeech))
	{
		obj->setModelConditionState(kMcSpeech, true);
	}
	objectFX(obj->logic(), "RousingSpeechUpdate", m_rs->m_leaderFX, *obj); // RW 0x8B04BC
	m_inner = 0.0f;
	if (m_rs->m_createWave)
	{
		m_outer = m_rs->m_waveWidth > m_rs->m_bonusRadius ? m_rs->m_bonusRadius : m_rs->m_waveWidth; // RW 0x8B04F0: comiss
	}
	else
	{
		m_outer = m_rs->m_bonusRadius;
	}
}

// RW 0x8B07EA
void RousingSpeechUpdate::gatherFollowers()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *owner = obj->getControllingPlayer();
	const Coord3D center = *obj->getPosition();
	const float inner = m_inner;
	PartitionFilterFn chain([&](Object &o) {
		if (obj->getRelationship(o) != ALLIES) // RW 0xC11DC0 mask 4
		{
			return false;
		}
		if (m_rs->m_filter && !ObjectFilterMatch::allows(logic, *m_rs->m_filter, o, owner)) // RW 0xBE4CC8
		{
			return false;
		}
		if (o.isEffectivelyDead()) // RW 0xC10E20
		{
			return false;
		}
		const Coord3D &p = *o.getPosition(); // RW 0x8B0420: |p - centre| > inner (subss each axis, the x87 length, fcomi)
		return SimMath::length3d(SimMath::subf32(p.x, center.x), SimMath::subf32(p.y, center.y), SimMath::subf32(p.z, center.z)) > (double)inner;
	});
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(center, m_outer, FROM_CENTER_2D, { &chain }, ITER_SORTED_NEAR_TO_FAR))
	{
		if (h.object != obj)
		{
			m_followers.push_back(h.object->getID()); // RW 0x8B08F7
		}
	}
}

// RW 0x8B0A0D
void RousingSpeechUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	gatherFollowers();
	if (m_followers.empty())
	{
		return;
	}
	for (const std::string &name : m_rs->m_modifierNames)
	{
		for (ObjectID id : m_followers)
		{
			Object *f = logic.findObjectByID(id);
			if (!f)
			{
				continue;
			}
			f->addAttributeModifier(name, -1); // RW 0x68F1A8(name, -1)
			if (!f->testModelCondition(kMcFollower))
			{
				f->setModelConditionState(kMcFollower, true);
			}
			f->setSpeechLeader(obj->getID()); // + 0x46C
			objectFX(logic, "RousingSpeechUpdate", m_rs->m_followerFX, *f); // RW 0x8B0AC2
			++m_inspired;
		}
	}
	if (!m_rs->m_levelUp)
	{
		return;
	}
	for (ObjectID id : m_followers)
	{
		Object *f = logic.findObjectByID(id);
		if (!f)
		{
			continue; // RW 0x8B0B24 reads the contain of a null object: an id that is gone is skipped here (INFERENCE, S-1222)
		}
		if (ExperienceTracker *t = f->getExperienceTracker())
		{
			t->gainExpForLevel(1, true, false); // RW 0x79DA0A(1, 1, 0)
		}
		if (ContainModuleInterface *c = f->getContain())
		{
			if (const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList())
			{
				const std::vector<Object *> members(items->begin(), items->end()); // vslot 0x110 with RW 0x8B0402
				for (Object *m : members)
				{
					if (m && m->getExperienceTracker())
					{
						m->getExperienceTracker()->gainExpForLevel(1, true, false);
					}
				}
			}
		}
	}
}

void RousingSpeechUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32((std::uint32_t)m_followers.size());
	for (ObjectID id : m_followers)
	{
		h.addU32(id);
	}
	h.addU32(m_endFrame);
	h.addBool(m_started);
	h.addFloat(m_inner);
	h.addFloat(m_outer);
	h.addU32(m_inspired);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// TeleportSpecialAbilityUpdate
// ---------------------------------------------------------------------------------------------------------------------------------
void TeleportSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kTeleport);
}

TeleportSpecialAbilityUpdate::TeleportSpecialAbilityUpdate(Thing *thing, const TeleportSpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_tp(data)
{
}

// TheWeaponStore->createAndFireTempWeapon(weapon, object, position) (RW 0x6CC5DF + 0x6CF530)
void TeleportSpecialAbilityUpdate::fireTempWeapon(const std::string &name, const Coord3D &at)
{
	if (name.empty())
	{
		return;
	}
	Object *obj = getObject();
	const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
	ObjectWeapons *weapons = obj->getWeapons();
	if (!wt || !weapons)
	{
		if (!wt)
		{
			obj->logic().reportError("TeleportSpecialAbilityUpdate: no weapon '" + name + "' (RW 0x6CC5DF)");
		}
		return;
	}
	std::unique_ptr<Weapon> temp = weapons->makeExtraWeapon(wt);
	temp->setOwnerID(obj->getID());
	weapons->fireExtraWeaponAt(*temp, at);
}

// RW 0x89653A
void TeleportSpecialAbilityUpdate::startUnpacking()
{
	Object *obj = getObject();
	fireTempWeapon(m_tp->m_sourceWeapon, *obj->getPosition()); // RW 0x896545 .. 0x896579
	SpecialAbilityUpdate::startUnpacking();
	obj->setStatus((unsigned)statusIndex("IGNORE_AI_COMMAND"), true); // RW 0x6907BD(now + BusyForDuration)
	obj->setIgnoreAICommandUntil(obj->logic().getFrame() + m_tp->m_busyForFrames);
}

// RW 0x895C6B
void TeleportSpecialAbilityUpdate::startPacking(bool)
{
	SpecialAbilityUpdate::startPacking(true);
}

// RW 0x8964A0
void TeleportSpecialAbilityUpdate::triggerAbilityEffect()
{
	Object *obj = getObject();
	const Coord3D dest = m_targetPos; // + 0x44
	const float angle = SimMath::pc24Add(emotionRelAngle(*obj, dest), obj->getOrientation()); // RW 0x4B3D8D; fadd dword (Object + 0x44)
	obj->setPosition(&dest);  // RW 0x696E63(location, 1)
	obj->setOrientation(angle); // RW 0x70C31E
	fireTempWeapon(m_tp->m_destinationWeapon, dest); // RW 0x896506 .. 0x896529
	++m_teleports;
	SpecialAbilityUpdate::triggerAbilityEffect(); // RW 0x896530
}

void TeleportSpecialAbilityUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_teleports);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// SpecialPowerTimerRefreshSpecialPower
// ---------------------------------------------------------------------------------------------------------------------------------
// RW 0x8C7B26
void SpecialPowerTimerRefreshSpecialPower::applyToVictim(Object &victim, unsigned until, unsigned antiMask)
{
	SpecialPowerModule::applyToVictim(victim, until, antiMask); // RW 0x89763B
	for (const std::unique_ptr<BehaviorModule> &m : victim.modules()) // RW 0x68BDF9: every module's + 0xC slot 0xB0
	{
		if (SpecialPowerModule *sp = dynamic_cast<SpecialPowerModule *>(m.get()))
		{
			sp->refreshTimer(); // RW 0x6519C2 -> 0x896F99
		}
	}
	++m_refreshed;
}

void SpecialPowerTimerRefreshSpecialPower::crc(StateHasher &h) const
{
	SpecialPowerModule::crc(h);
	h.addU32(m_refreshed);
}
// ---------------------------------------------------------------------------------------------------------------------------------
void HeroAbilityModules::registerAll(ModuleFactory &modules)
{
	bindRuntime<ShareExperienceBehavior, ShareExperienceBehaviorModuleData>(modules, "ShareExperienceBehavior");
	bindRuntime<DamageFilteredCreateObjectDie, DamageFilteredCreateObjectDieModuleData>(modules, "DamageFilteredCreateObjectDie");
	bindRuntime<AutoAbilityBehavior, AutoAbilityBehaviorModuleData>(modules, "AutoAbilityBehavior");
	bindRuntime<WeaponModeSpecialPowerUpdate, WeaponModeSpecialPowerUpdateModuleData>(modules, "WeaponModeSpecialPowerUpdate");
	bindRuntime<DualWeaponBehavior, DualWeaponBehaviorModuleData>(modules, "DualWeaponBehavior");
	bindRuntime<TemporarilyDefectUpdate, TemporarilyDefectUpdateModuleData>(modules, "TemporarilyDefectUpdate");
	bindRuntime<DominateEnemySpecialPower, DominateEnemySpecialPowerModuleData>(modules, "DominateEnemySpecialPower");
	bindRuntime<ActivateModuleSpecialPower, ActivateModuleSpecialPowerModuleData>(modules, "ActivateModuleSpecialPower");
	bindRuntime<ArrowStormUpdate, ArrowStormUpdateModuleData>(modules, "ArrowStormUpdate");
	bindRuntime<CurseSpecialPower, CurseSpecialPowerModuleData>(modules, "CurseSpecialPower");
	bindRuntime<FellBeastSwoopPower, SpecialAbilityUpdateModuleData>(modules, "FellBeastSwoopPower");
	bindRuntime<RousingSpeechUpdate, RousingSpeechUpdateModuleData>(modules, "RousingSpeechUpdate");
	bindRuntime<TeleportSpecialAbilityUpdate, TeleportSpecialAbilityUpdateModuleData>(modules, "TeleportSpecialAbilityUpdate");
	bindRuntime<SpecialPowerTimerRefreshSpecialPower, SpecialPowerModuleData>(modules, "SpecialPowerTimerRefreshSpecialPower");
}

void HeroAbilityModules::registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_DO_AUTO_ABILITY, "HERO-2", [](GameLogic &logic, const GameMessage &m) {
		const GameMessageArgument *a0 = m.getArgument(0), *a1 = m.getArgument(1);
		if (!a0 || !a1 || a0->type != ARGUMENTDATATYPE_INTEGER || a1->type != ARGUMENTDATATYPE_OBJECTID)
		{
			return false;
		}
		const SpecialPowerTemplate *t = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplateByID((unsigned)a0->integer) : nullptr; // RW 0x7B1B05
		Object *obj = logic.findObjectByID(a1->objectID); // RW 0x449681
		const CommandSet *set = obj && TheCommandStore ? TheCommandStore->findCommandSet(obj->getCommandSetName()) : nullptr; // RW 0x69156B / 0x71EFA2
		if (!t || !set)
		{
			return true;
		}
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i) // RW 0x77BA2A: the first SPECIAL_POWER button of that power (id and name)
		{
			const CommandButton *b = set->getCommandButton(i);
			if (!b || b->m_command != GUI_COMMAND_SPECIAL_POWER || b->m_specialPowerName != t->getName())
			{
				continue;
			}
			if (AutoAbilityBehavior *aa = AutoAbilityBehavior::forPower(*obj, t->getName())) // RW 0x85D6AD
			{
				aa->setAutoButton(b); // RW 0x85D76C
			}
			return true;
		}
		return true;
	});
}

std::vector<std::string> HeroAbilityModules::stopLines()
{
	return {
		"[S-1222] defection and domination (lane HERO-2): Object::defect / setTemporaryTeam / endDefection (RW 0x699368 / 0x699513 / 0x69ABA7), "
		"TemporarilyDefectUpdate and DominateEnemySpecialPower run; INFERENCE / not ported: the AI's temporary state machine kept and given back (RW 0x6630D7 / "
		"0x663128 / 0x663171: the AI goes idle), RW 0x693919 for an object using an ability, the stealth and special power helpers RW 0x8A4ED8 / 0x68C3A3, the "
		"contain vslots 0x108 / 0x10C of the classes other than HordeContain, the original team kept as the Team (RW 0x69959D keeps its name, RW 0x7A7483 looks it "
		"up), the module tags of ActivateModuleSpecialPower resolved when used (retail makes the keys while parsing); the selection / UI refresh is the client's",
		"[S-1223] FellBeastSwoopPower (lane HERO-2): the module runs (RW 0x8CB2CE / 0x8CB28B / 0x8CB194); the level attack itself is GiantBirdAIUpdate's swoop "
		"(AI commands 0x3F / 0x40, AI vslots 0x90 / 0x94: GiantBirdAIUpdate runs as the base AI, S-222): the command is counted by the AI (aiUnported) and the "
		"ability ends when the AI idles; AI vslot 0x1D0 is not identified",
		"[S-1227] AutoAbilityBehavior (lane HERO-2): the auto cast runs (RW 0x85D9D1 with RW 0x85D5EA / 0x85D4B5, MSG_DO_AUTO_ABILITY RW 0x77B9BA, StartsActive); "
		"INFERENCE / not ported: the closest enemy of a module without Query (RW 0x701443 flags 0x4A read as the closest alive enemy it can see), the AI + 0x34 / "
		"+ 0x48 fields (+ 0x34 read as 0, + 0x48 := 2 not written), the melee position offset (AdjustAttackMeleePosition, RW 0x6EEBC1), the command types other than "
		"SPECIAL_POWER / SPELL_BOOK (FIRE_WEAPON's MSG_DO_AUTO_ABILITY_WEAPON, RW 0x77B9xx), the vision range modifiers (RW 0x68E43B), the auto sound (client), a "
		"disabled object's auto cast exception (RW 0x68E5A3 with 0x20000000); the control bar's auto toggle (right click) is the HUD's",
		"[S-1228] WeaponModeSpecialPowerUpdate and DualWeaponBehavior (lane HERO-2): the mode runs (RW 0x89841E / 0x8983B5 over the base timer RW 0x991660 / "
		"0x991500 / 0x9913BC) and the close range switch runs (RW 0x85DF88); not ported: the firing tracker reset at the start (RW 0x8E302A, COMBAT-1's), the "
		"RECHARGE attribute modifier of the recharge (1.0, as S-529), the initiate sound (client); INFERENCE: DualWeaponBehavior's victim without "
		"UseRealVictimRange (RW 0x701443 -> 0x7006D7 flags 0x62, the AI's target search) is the closest visible alive enemy; its + 0x20 request has no writer",
		"[S-1224] RousingSpeechUpdate (lane HERO-2): the speech runs (RW 0x8B068D / 0x8B0485 / 0x8B0A0D / 0x8B07EA); retail's update never ends the ability "
		"(no call of finishAbility found: the base startPreparation's USING_ABILITY / SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING stay set, so the hero's "
		"other powers are not ready, RW 0x896C83, until something finishes it; RW 0x693919 aborts only abilities in use, RW 0x851C92): kept as read, "
		"pending a retail trace; a follower id that is gone is skipped in the LevelUp pass (RW 0x8B0B24 reads it)",
	};
}
