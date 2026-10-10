// OpenBFME. GPL-3.0.
//
// The spell book special power classes. See GameLogic/Module/SpellBookPowers.h for the target facts. Lane SPELL-2.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/SpellBookPowers.h"
#include "GameLogic/Module/SpellEffectModules.h"

#include "Common/Player.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIPathfindHost.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/GlobalWeatherSystem.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SimMath.h"
#include "Common/StateHash.h"
#include "Common/Team.h"

#include <cstddef>
#include <stdexcept>

namespace
{
void parseName(INI *ini, void *, void *store, const void *)
{
	*static_cast<std::string *>(store) = ini->getNextToken(); // the FXList / OCL / template names (RW 0x73A302, 0x73A368, 0x42EE5E): kept by name
}
// RW 0x8CC228: one token appended
void parseAppendName(INI *ini, void *, void *store, const void *)
{
	static_cast<std::vector<std::string> *>(store)->push_back(ini->getNextToken());
}
// RW 0x8C81CD: <name> <real> appended (RW 0x8C818E)
void parseElvenWoodObject(INI *ini, void *instance, void *, const void *)
{
	ElvenWoodSpecialPowerModuleData *d = static_cast<ElvenWoodSpecialPowerModuleData *>(instance);
	const std::string name = ini->getNextToken();
	float value = 0.0f;
	INI::parseReal(ini, instance, &value, nullptr); // RW 0x8C81FE (RW 0x42ED00)
	d->m_elvenWoodObjects.push_back({ name, value });
}

#define OFF(T, m) (int)offsetof(T, m)
const FieldParse kPlayerUpgradeFields[] = { { "UpgradeName", parseAppendName, nullptr, OFF(PlayerUpgradeSpecialPowerModuleData, m_upgradeNames) },
	{ nullptr, nullptr, nullptr, 0 } };
const FieldParse kDarknessFields[] = { { "DarknessRadius", INI::parseReal, nullptr, OFF(DarknessSpecialPowerModuleData, m_radius) },
	{ "DarknessFX", parseName, nullptr, OFF(DarknessSpecialPowerModuleData, m_fx) }, { nullptr, nullptr, nullptr, 0 } };
const FieldParse kFreezingRainFields[] = { { "FreezingRainRadius", INI::parseReal, nullptr, OFF(FreezingRainSpecialPowerModuleData, m_radius) },
	{ "FreezingRainFX", parseName, nullptr, OFF(FreezingRainSpecialPowerModuleData, m_fx) },
	{ "BurnRateModifier", INI::parseInt, nullptr, OFF(FreezingRainSpecialPowerModuleData, m_burnRateModifier) }, { nullptr, nullptr, nullptr, 0 } };
const FieldParse kCloudBreakFields[] = { { "CloudBreakRadius", INI::parseReal, nullptr, OFF(CloudBreakSpecialPowerModuleData, m_radius) },
	{ "CloudBreakFX", parseName, nullptr, OFF(CloudBreakSpecialPowerModuleData, m_fx) },
	{ "SunbeamObject", INI::parseAsciiString, nullptr, OFF(CloudBreakSpecialPowerModuleData, m_sunbeamObject) },
	{ "ObjectSpacing", INI::parseReal, nullptr, OFF(CloudBreakSpecialPowerModuleData, m_objectSpacing) }, { nullptr, nullptr, nullptr, 0 } };
const FieldParse kTaintFields[] = { { "TaintObject", INI::parseAsciiString, nullptr, OFF(TaintSpecialPowerModuleData, m_taintObject) },
	{ "TaintRadius", INI::parseReal, nullptr, OFF(TaintSpecialPowerModuleData, m_taintRadius) },
	{ "TaintFX", parseName, nullptr, OFF(TaintSpecialPowerModuleData, m_taintFX) },
	{ "TaintOCL", parseName, nullptr, OFF(TaintSpecialPowerModuleData, m_taintOCL) }, { nullptr, nullptr, nullptr, 0 } };
const FieldParse kElvenWoodFields[] = { { "ElvenGroveObject", INI::parseAsciiString, nullptr, OFF(ElvenWoodSpecialPowerModuleData, m_elvenGroveObject) },
	{ "ElvenNumObjects", INI::parseInt, nullptr, OFF(ElvenWoodSpecialPowerModuleData, m_elvenNumObjects) },
	{ "ElvenWoodObject", parseElvenWoodObject, nullptr, 0 },
	{ "ElvenWoodRadius", INI::parseReal, nullptr, OFF(ElvenWoodSpecialPowerModuleData, m_elvenWoodRadius) },
	{ "ElvenWoodFX", parseName, nullptr, OFF(ElvenWoodSpecialPowerModuleData, m_elvenWoodFX) },
	{ "ElvenWoodOCL", parseName, nullptr, OFF(ElvenWoodSpecialPowerModuleData, m_elvenWoodOCL) }, { nullptr, nullptr, nullptr, 0 } };
const FieldParse kProductionSpeedBonusFields[] = { { "NumberOfFrames", INI::parseInt, nullptr, OFF(ProductionSpeedBonusModuleData, m_numberOfFrames) },
	{ "SpeedMulitplier", INI::parseReal, nullptr, OFF(ProductionSpeedBonusModuleData, m_speedMultiplier) },
	{ "Type", INI::parseAsciiStringVector, nullptr, OFF(ProductionSpeedBonusModuleData, m_types) }, { nullptr, nullptr, nullptr, 0 } };
const FieldParse kScavengerFields[] = { { "BountyPercent", INI::parseReal, nullptr, OFF(ScavengerSpecialPowerModuleData, m_bountyPercent) },
	{ nullptr, nullptr, nullptr, 0 } };
const FieldParse kDevastateFields[] = { { "Radius", INI::parseReal, nullptr, OFF(DevastateSpecialPowerModuleData, m_radius) },
	{ "FX", parseName, nullptr, OFF(DevastateSpecialPowerModuleData, m_fx) },
	{ "TreeValueMultiplier", INI::parsePercentToReal, nullptr, OFF(DevastateSpecialPowerModuleData, m_treeValueMultiplier) },
	{ "TreeValueTotalCap", INI::parseReal, nullptr, OFF(DevastateSpecialPowerModuleData, m_treeValueTotalCap) },
	{ "FireWeapon", INI::parseAsciiString, nullptr, OFF(DevastateSpecialPowerModuleData, m_fireWeapon) }, { nullptr, nullptr, nullptr, 0 } };
#undef OFF

void emitPositionFX(GameLogic &logic, const char *site, const std::string &fx, const Object &source, const Coord3D &pos)
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

template <class Module, class Data>
void bindPower(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Module>(thing, typed);
	});
}
} // namespace

void PlayerUpgradeSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kPlayerUpgradeFields);
}
void DarknessSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kDarknessFields);
}
void FreezingRainSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kFreezingRainFields);
}
void CloudBreakSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kCloudBreakFields);
}
void TaintSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kTaintFields);
}
void ElvenWoodSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kElvenWoodFields);
}
void ProductionSpeedBonusModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kProductionSpeedBonusFields);
}
void ScavengerSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kScavengerFields);
}
void DevastateSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kDevastateFields);
}

// ---- PlayerUpgradeSpecialPower -------------------------------------------------------------------------------------------------------------
void PlayerUpgradeSpecialPower::doSpecialPower(unsigned)
{
	if (getObject()->getDisabledMask() != 0)
	{
		return; // RW 0x8CC0DA
	}
	startPowerRecharge(1.0f);                                          // RW 0x8CC0E3 (slot 0x3C)
	SpecialPowerModules::grantPlayerUpgrades(*getObject(), m_data->m_upgradeNames); // RW 0x8CC0E5 .. 0x8CC158
	triggerSpecialPower(nullptr, nullptr);                             // RW 0x8CC15F: RW 0x897987(0)
}

// ---- the weather / terrain family ----------------------------------------------------------------------------------------------------------
void WeatherFamilySpecialPower::doSpecialPower(unsigned options)
{
	if (getObject()->getDisabledMask() == 0)
	{
		const Coord3D pos = *getObject()->getPosition();
		doSpecialPowerAtLocation(pos, options); // RW 0x8C92F7 (slot 0x30)
	}
}

void WeatherFamilySpecialPower::doSpecialPowerAtObject(Object *target, unsigned options)
{
	if (getObject()->getDisabledMask() == 0 && target)
	{
		const Coord3D pos = *target->getPosition();
		doSpecialPowerAtLocation(pos, options); // RW 0x8C7C0D
	}
}

void WeatherFamilySpecialPower::zeroBurnDecay()
{
	getObject()->logic().weather().setBurnDecay(0); // RW 0xDE46A8 + 0x98 = 0
}

namespace
{
// TerrainLogic vslot 0x20 (RW 0x462637, W3DTerrainLogic::getExtent): lo = (0, 0), hi = the ACTIVE boundary (+ 0x3C) * 10. The active boundary is taken as 0
// (only a script changes it: S-921). A logic without TerrainLogic (the test games) takes TheAI's pathfind terrain (its getExtent) instead.
bool spellTerrainExtent(GameLogic &logic, float &loX, float &loY, float &hiX, float &hiY)
{
	if (const TerrainLogic *t = logic.terrain())
	{
		loX = 0.0f;
		loY = 0.0f;
		return t->getExtent(0, hiX, hiY);
	}
	if (AIWorld *ai = logic.aiWorld())
	{
		if (const PathfindTerrain *pt = ai->pathfinder().terrainView())
		{
			pt->getExtent(loX, loY, hiX, hiY);
			return true;
		}
	}
	return false;
}
// TerrainLogic vslot 0x18 (RW 0x462355, getGroundHeight(x, y, null))
float spellGroundHeight(GameLogic &logic, float x, float y)
{
	if (logic.terrain())
	{
		return logic.getGroundHeight(x, y);
	}
	if (AIWorld *ai = logic.aiWorld())
	{
		if (const PathfindTerrain *pt = ai->pathfinder().terrainView())
		{
			return pt->getGroundHeight(x, y);
		}
	}
	return 0.0f;
}
} // namespace

// RW 0x8C931C / 0x8C9104 / 0x8C8ADA: the FX at the extent's centre: x87 (hi + lo) * 0.5f (RW 0xBD869C), stored; z is the cast location's (Darkness,
// FreezingRain) or the ground height there (CloudBreak)
void WeatherFamilySpecialPower::emitCentreFX(const std::string &fx, const Coord3D *castLoc)
{
	if (!FXEventLog::isFXName(fx))
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
	if (!spellTerrainExtent(logic, loX, loY, hiX, hiY))
	{
		++m_unported; // no terrain at all: no centre (S-921)
		return;
	}
	Coord3D c;
	c.x = SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24AddW(hiX, loX), 0.5));
	c.y = SimMath::fstpDword(SimMath::pc24MulW(SimMath::pc24AddW(hiY, loY), 0.5));
	c.z = castLoc ? castLoc->z : spellGroundHeight(logic, c.x, c.y);
	emitPositionFX(logic, "SpecialPower map centre FX", fx, *getObject(), c); // RW 0x494615
}

Object *WeatherFamilySpecialPower::seedArea(const Coord3D &loc, const std::string &fx, const std::string &ocl, const std::string &objectName)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	++m_unported; // RW 0x67F6F0 / 0x67D4CE: the terrain's taint / elven wood area of the radius (S-921)
	emitPositionFX(logic, "SpecialPower area FX", fx, *obj, loc); // RW 0x494615
	if (!ocl.empty())
	{
		const ObjectCreationList *list = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(ocl) : nullptr;
		if (list)
		{
			list->create(logic, obj, loc); // RW 0x5F00CA (source: the object)
		}
	}
	// RW 0x8C8DF9 / 0x8C7C22: the template by name (RW 0x6D1305); newObject on no team (the neutral player's, RW 0x699E4A) at the location
	const ThingTemplate *tt = logic.things().findTemplate(objectName);
	if (!tt)
	{
		return nullptr;
	}
	Object *made = logic.newObject(tt, nullptr, ObjectStatusMaskType{});
	if (made)
	{
		made->setPosition(&loc); // RW 0x70C201
		++m_unported;            // RW 0x69954A (with the caster's + 0x31C) and the terrain decals (RW 0xAD4C10, client): S-921
	}
	return made;
}

void DarknessSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	if (getObject()->getDisabledMask() != 0)
	{
		return;
	}
	baseDo(&loc, nullptr, options); // RW 0x89816C
	if (getObject()->logic().weather().weather() != 1) // RW 0x8C931C: not CLOUDY
	{
		emitCentreFX(m_data->m_fx, &loc);
	}
	zeroBurnDecay();
}

void FreezingRainSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	if (getObject()->getDisabledMask() != 0)
	{
		return;
	}
	baseDo(&loc, nullptr, options);
	if (getObject()->logic().weather().weather() != 2) // RW 0x8C9104: not RAINY
	{
		emitCentreFX(m_data->m_fx, &loc);
	}
	++m_unported; // RW 0x687C4C: every fire grid cell's burn rate = BurnRateModifier (the fire logic is not ported): S-921
}

void CloudBreakSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	if (getObject()->getDisabledMask() != 0)
	{
		return;
	}
	baseDo(&loc, nullptr, options);
	emitCentreFX(m_data->m_fx, nullptr); // RW 0x8C8B01
	makeSunbeams();                       // RW 0x8C8B57
	// RW 0x8C8A3B: every object the filter allows for the caster's player has its outermost container's fire put out (RW 0x68F383): S-921
	Object *obj = getObject();
	if (m_data->m_attributeModifierAffects)
	{
		for (Object *o = obj->logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (ObjectFilterMatch::allows(obj->logic(), *m_data->m_attributeModifierAffects, *o, obj->getControllingPlayer()))
			{
				++m_unported;
			}
		}
	}
	zeroBurnDecay();
}

// RW 0x8C8B57 .. 0x8C8C46 (SSE): spacing > 0: y = (float)(int)(spacing + lo.y) while y < hi.y - spacing; x = (float)(int)(spacing + lo.x) while x < hi.x - spacing:
// a SunbeamObject at (x, y, ground) (RW 0x8C8A88: the template by name, newObject on no team (the neutral player's), setPosition), x = (float)(int)(x + spacing);
// y = (float)(int)(y + spacing). Each object's creation draws the logic random numbers its constructor and sendObjectCreated draw (the retail CloudBreakSunbeam:
// LifetimeUpdate's lifetime (RW 0x7A7D7E) and the object seed, two per sunbeam)
void CloudBreakSpecialPower::makeSunbeams()
{
	const float spacing = m_data->m_objectSpacing;
	if (!(spacing > 0.0f)) // RW 0x8C8B5F `comiss spacing, 0.0`: jbe skips
	{
		return;
	}
	GameLogic &logic = getObject()->logic();
	float loX = 0.0f, loY = 0.0f, hiX = 0.0f, hiY = 0.0f;
	if (!spellTerrainExtent(logic, loX, loY, hiX, hiY))
	{
		++m_unported;
		return;
	}
	const ThingTemplate *tt = m_data->m_sunbeamObject.empty() ? nullptr : logic.things().findTemplate(m_data->m_sunbeamObject); // RW 0x6D1305
	auto snap = [](float v) { return SimMath::sseFromInt32(SimMath::cvttss2si(v)); }; // cvttss2si ; cvtsi2ss
	for (float y = snap(SimMath::addf32(spacing, loY)); SimMath::subf32(hiY, spacing) > y;
		 y = snap(SimMath::addf32(y, spacing)))
	{
		for (float x = snap(SimMath::addf32(spacing, loX)); SimMath::subf32(hiX, spacing) > x;
			 x = snap(SimMath::addf32(x, spacing)))
		{
			Coord3D at{ x, y, spellGroundHeight(logic, x, y) };
			if (!tt)
			{
				continue; // RW 0x8C8A90: no template, nothing made
			}
			if (Object *beam = logic.newObject(tt, nullptr, ObjectStatusMaskType{}))
			{
				beam->setPosition(&at); // RW 0x70C201
				++m_sunbeams;
			}
		}
	}
}

void TaintSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	if (getObject()->getDisabledMask() != 0 || m_data->m_taintObject.empty())
	{
		return;
	}
	baseDo(&loc, nullptr, options);
	Object *made = seedArea(loc, m_data->m_taintFX, m_data->m_taintOCL, m_data->m_taintObject);
	m_lastObject = made ? made->getID() : (ObjectID)INVALID_ID;
}

void ElvenWoodSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	if (getObject()->getDisabledMask() != 0 || m_data->m_elvenGroveObject.empty())
	{
		return;
	}
	baseDo(&loc, nullptr, options);
	Object *made = seedArea(loc, m_data->m_elvenWoodFX, m_data->m_elvenWoodOCL, m_data->m_elvenGroveObject);
	m_lastObject = made ? made->getID() : (ObjectID)INVALID_ID;
	if (made)
	{
		made->setStatus(0x54, true); // RW 0x8C7E3A: status 0x54 on the grove
	}
}

// ---- ProductionSpeedBonus ------------------------------------------------------------------------------------------------------------------
void ProductionSpeedBonus::doSpecialPower(unsigned options)
{
	// RW 0x8C7191 .. 0x8C71B7: fld mult; fsub 1.0 (double); fld mult; fmul -1.0 (double); fdivp; fstp single
	const float mult = m_data->m_speedMultiplier;
	const float factor = SimMath::fstpDword(SimMath::pc24DivW(SimMath::pc24SubW((double)mult, 1.0), SimMath::pc24MulW((double)mult, -1.0)));
	for (const std::string &type : m_data->m_types)
	{
		bool found = false;
		for (auto &b : m_bonuses) // RW 0x6AF3C8: the name's record is found or made
		{
			if (b.first == type)
			{
				b.second = { m_data->m_numberOfFrames, factor };
				found = true;
				break;
			}
		}
		if (!found)
		{
			m_bonuses.push_back({ type, { m_data->m_numberOfFrames, factor } });
		}
		++m_unported; // the player's production bonus is not read by production (S-921)
	}
	if (getObject()->getDisabledMask() == 0)
	{
		const Coord3D pos = *getObject()->getPosition();
		SpecialPowerModule::doSpecialPowerAtLocation(pos, options); // RW 0x8C71E2 (slot 0x30: the base RW 0x89816C)
	}
}

void ProductionSpeedBonus::crc(StateHasher &h) const
{
	SpecialPowerModule::crc(h);
	h.addU32((std::uint32_t)m_bonuses.size());
	for (const auto &b : m_bonuses)
	{
		h.addString(b.first);
		h.addI32(b.second.first);
		h.addFloat(b.second.second);
	}
}

// ---- ScavengerSpecialPower -----------------------------------------------------------------------------------------------------------------
void ScavengerSpecialPower::doSpecialPower(unsigned options)
{
	if (Player *p = getObject()->getControllingPlayer())
	{
		p->setBountyPercent(m_data->m_bountyPercent); // RW 0x6AA847
	}
	SpecialPowerModule::doSpecialPower(options); // RW 0x8980A3
	m_active = true;
}

void ScavengerSpecialPower::pauseCountdown(bool pause)
{
	if (m_active)
	{
		if (Player *p = getObject()->getControllingPlayer())
		{
			p->setBountyPercent(pause ? 0.0f : m_data->m_bountyPercent);
		}
	}
	SpecialPowerModule::pauseCountdown(pause); // RW 0x896756
}

void ScavengerSpecialPower::crc(StateHasher &h) const
{
	SpecialPowerModule::crc(h);
	h.addBool(m_active);
}

// ---- UntamedAllegianceSpecialPower ---------------------------------------------------------------------------------------------------------
void UntamedAllegianceSpecialPower::applyToVictim(Object &victim, unsigned until, unsigned antiMask)
{
	if (victim.testStatus(0x39))
	{
		return; // RW 0x8CBFD6
	}
	SpecialPowerModule::applyToVictim(victim, until, antiMask); // RW 0x89763B
	Object *caster = getObject();
	// RW 0x699368 (Object::defect to the caster): the team change; its AI / stealth / contain / TemporarilyDefectUpdate / status side effects and
	// RW 0x6938BD are not ported (S-921)
	if (caster->getTeam() && victim.getTeam() != caster->getTeam())
	{
		victim.setTeam(caster->getTeam());
	}
	victim.setProducer(caster);  // RW 0x68B6A1
	victim.setStatus(0x3E, false); // RW 0x8CC00B
	++m_defected;
	++m_unported;
}

// ---- DevastateSpecialPower -----------------------------------------------------------------------------------------------------------------
void DevastateSpecialPower::doSpecialPower(unsigned)
{
	// RW 0x8CC658: the debug message "Error! Devastate Power requires either a target object or location" only (a release build does nothing)
	++m_unported;
}

void DevastateSpecialPower::doSpecialPowerAtObject(Object *target, unsigned options)
{
	const Coord3D pos = *target->getPosition(); // RW 0x8CC6FD: no null test
	doSpecialPowerAtLocation(pos, options);
}

void DevastateSpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	if (getObject()->getDisabledMask() != 0 || !getObject()->getControllingPlayer())
	{
		return;
	}
	baseDo(&loc, nullptr, options);
	++m_unported; // RW 0x8CC753 ..: the trees within Radius fall with FX and pay money, RW 0x7B18B8: S-921
	// RW 0x8CC8BB .. 0x8CC8DD (lane DECOMP-1; BFME2 decomp attempt 0x004c82e5.cpp, tier B): a FireWeapon the store knows is TheWeaponStore->createAndFireTempWeapon
	// (weapon, object, target) (RW 0x6CF530)
	const DevastateSpecialPowerModuleData *d = static_cast<const DevastateSpecialPowerModuleData *>(getModuleData());
	if (!d->m_fireWeapon.empty() && TheWeaponStore)
	{
		if (const WeaponTemplate *wt = TheWeaponStore->findWeaponTemplate(d->m_fireWeapon))
		{
			ObjectWeapons::createAndFireTempWeapon(wt, getObject(), loc);
		}
	}
}

// ---- registration and stops ----------------------------------------------------------------------------------------------------------------
void SpellBookPowers::registerAll(ModuleFactory &modules)
{
	bindPower<PlayerUpgradeSpecialPower, PlayerUpgradeSpecialPowerModuleData>(modules, "PlayerUpgradeSpecialPower");
	bindPower<DarknessSpecialPower, DarknessSpecialPowerModuleData>(modules, "DarknessSpecialPower");
	bindPower<FreezingRainSpecialPower, FreezingRainSpecialPowerModuleData>(modules, "FreezingRainSpecialPower");
	bindPower<CloudBreakSpecialPower, CloudBreakSpecialPowerModuleData>(modules, "CloudBreakSpecialPower");
	bindPower<TaintSpecialPower, TaintSpecialPowerModuleData>(modules, "TaintSpecialPower");
	bindPower<ElvenWoodSpecialPower, ElvenWoodSpecialPowerModuleData>(modules, "ElvenWoodSpecialPower");
	bindPower<ProductionSpeedBonus, ProductionSpeedBonusModuleData>(modules, "ProductionSpeedBonus");
	bindPower<ScavengerSpecialPower, ScavengerSpecialPowerModuleData>(modules, "ScavengerSpecialPower");
	bindPower<UntamedAllegianceSpecialPower, SpecialPowerModuleData>(modules, "UntamedAllegianceSpecialPower");
	bindPower<DevastateSpecialPower, DevastateSpecialPowerModuleData>(modules, "DevastateSpecialPower");
}

std::vector<std::string> SpellBookPowers::stopLines()
{
	std::vector<std::string> out = {
		"[S-920] SpecialPowerModule trigger (RW 0x897987): the recharge, OnTriggerRechargeSpecialPower, SetModelCondition and GiveLevels (lane HERO-1), TriggerFX, the attribute modifier scan (RW 0x8977BF, "
		"0x89763B: relationship mask, AttributeModifierAffects, AffectGood / Evil / Allies, AntiCategory disable, AntiFX, AttributeModifierFX), the "
		"weather-based modifier, the disguise stop (type 0x85, lane HERO-2) and the SpecialPowerViewObject (RW 0x896FD9: with a target location, a template, ViewObjectRange != 0, ViewObjectDuration "
		"!= 0, a GlobalData SpecialPowerViewObject name and its template, the object is made, placed and its DeletionUpdate lifetime set: THREE logic random "
		"draws for the retail SuperweaponPing (DeletionUpdate's constructor delay, sendObjectCreated's seed, the [duration, duration] override; other "
		"templates draw what their constructors draw)) run, with its own shroud clearing range and forced look (RW 0x68C234 / 0x68C7E9, lane DECOMP-1); not run: the script / EVA notices (RW 0x89713F), "
		"AdjustVictim, and the 'outside the playable area' filter of non-spell-book casters (RW 0xC0F374, Object + 0x458 bit 3); the scan's distance is "
		"retail's FROM_CENTER_2D; OCLSpecialPower's USE_SECONDARY_OBJECT_LOCATION search uses the 2D centre distance",
		"[S-921] spell book power classes (lane SPELL-2): PlayerUpgrade, Darkness, FreezingRain, CloudBreak, Taint, ElvenWood, ProductionSpeedBonus, "
		"Scavenger, UntamedAllegiance and Devastate run their logic do* paths, with the map-centre FX (TerrainLogic vslot 0x20 = RW 0x462637, the "
		"active boundary taken as 0: only scripts change it) and CloudBreak's SunbeamObject grid (RW 0x8C8B57: every created sunbeam draws what its "
		"constructor and sendObjectCreated draw, TWO logic random draws each for the retail CloudBreakSunbeam (LifetimeUpdate's lifetime and the seed), "
		"2 * N per cast for N grid points); not ported (counted per cast): the fire grid burn rate (RW 0x687C4C), CloudBreak's fire put-out (RW 0x68F383) "
		"and the sunbeams' Lua terror (their panic gameplay), the terrain taint / elven wood areas and decals (RW 0x67F6F0, 0x67D4CE, 0xAD4C10), RW "
		"0x69954A on the area object, the production speed bonus consumer (RW 0x6AF3C8 is stored only), the defect side effects (RW 0x699368 / 0x6938BD), "
		"Devastate's trees and money (its FireWeapon fires since lane DECOMP-1)",
	};
	for (const std::string &s : GlobalWeatherSystem::stopLines())
	{
		out.push_back(s);
	}
	out.push_back("[S-923] spell book screens (lane SPELL-2, review r1): the RETAIL movies run: InGameSpellBook in the Palantir (RW 0x93178C / 0x9312B9: "
				  "SetState _show, SetButtonState(slot, RW 0xC7F4DC name), FlashButton, the InGameSpellBookSpell%dImage / %dTimer native keys, the slot press "
				  "RW 0x930DE5) and SpellStore.apt (AptSpellStore RW 0x82379A / 0x822E43: SetLayout, ShowSpellHelpText and the help records, "
				  "APT:SpellStoreSpellPoints, APT:Spell%dCost, SetSpellButtonState(index + 1, RW 0xC50BF8 name), OnBttnSpell / Reset / Close with the purchases "
				  "sent at the close); inference: a SPELL_BOOK button's availability (RW 0x942733) is taken from ownership, usability and readiness, a ready "
				  "button's timer key is absent instead of 0, the store is loaded into a free window slot over the Palantir instead of the shell push and "
				  "single-player pause of RW 0x822CF7, RW 0x8227BB's disabled / hidden sciences are never set (no _disabled_level), the store's command set is "
				  "the local player's PlayerTemplate PurchaseScienceCommandSetMP (skirmish / multiplayer) or PurchaseScienceCommandSet (RW 0x822AB3 reads "
				  "it through RW 0x71F933, not traced), and a pending purchase sent at the close (RW 0x940435, ControlBar::processCommandUI of the "
				  "button) is taken as the player's MSG_PURCHASE_SCIENCE");
	out.push_back(SpellEffectModules::stopLine());
	return out;
}
