// OpenBFME. GPL-3.0.
// See GameLogic/Module/AreaScanModules.h for the target facts and the stop S-1023.

#include "GameLogic/Module/AreaScanModules.h"

#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Upgrade.h"
#include "GameClient/FXList.h"
#include "GameLogic/AttributeModifiers.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SimMath.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace
{
const char *const kLargeGroupFile = "LargeGroupBonusUpdate.cpp"; // RW 0xC63A68 (the path ends with this name)

const char *const kStop =
	"[S-1023] area scans: LargeGroupBonusUpdate (RW 0x8938D9) and PassiveAreaEffectBehavior (RW 0x887DF7) run on ThePartitionManager; not ported: "
	"LargeGroupBonusUpdate's FlagSubObjectNames (client sub objects) and the horde's second member list (horde slot 0x180 also counts the ids at HordeContain + "
	"0x54 / + 0x150, S-486); PassiveAreaEffectBehavior's NonStackable (the body's last heal frame, body slot 0x48, as S-858: the heal stacks) and the AntiCategories "
	"disable (RW 0x804FCC, S-633): both noted when met; the construction test is the object's UNDER_CONSTRUCTION status (RW 0x68C3E6 asks the first module answering "
	"behavior slot 0x80 first)";

#define LG_OFF(field) (int)offsetof(LargeGroupBonusUpdateModuleData, field)
#define PA_OFF(field) (int)offsetof(PassiveAreaEffectBehaviorModuleData, field)

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0x73A302: "None" (any case) stores none; any other name must be an FXList of TheFXListStore
void parseFX(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	if (AsciiStringUtil::compareNoCase(name, "None") == 0)
	{
		static_cast<std::string *>(store)->clear();
		return;
	}
	if (!TheFXListStore)
	{
		throw INIException(3, "TheFXListStore==NULL");
	}
	TheFXListStore->parseFXListRef(name);
	*static_cast<std::string *>(store) = name;
}

void parseAntiCategories(INI *ini, void *, void *store, const void *)
{
	ParseBitFlags(ini, static_cast<std::uint32_t *>(store), 1, TheAntiCategoryNames); // RW 0x89F32D over RW 0xD9FA40
}

// RW 0xC639D8
const FieldParse kLargeGroupParse[] = {
	{ "UpdateRate", INI::parseDurationUnsignedInt, nullptr, LG_OFF(m_updateRate) },
	{ "HordeMemberFilter", parseFilter, nullptr, LG_OFF(m_hordeMemberFilter) },
	{ "Count", INI::parseInt, nullptr, LG_OFF(m_count) },
	{ "Radius", INI::parseReal, nullptr, LG_OFF(m_radius) },
	{ "RubOffRadius", INI::parseReal, nullptr, LG_OFF(m_rubOffRadius) },
	{ "AlliesOnly", INI::parseBool, nullptr, LG_OFF(m_alliesOnly) },
	{ "FlagSubObjectNames", INI::parseAsciiStringVector, nullptr, LG_OFF(m_flagSubObjectNames) },
	{ "AttributeModifier", INI::parseAsciiString, nullptr, LG_OFF(m_attributeModifier) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC60B78
const FieldParse kPassiveAreaParse[] = {
	{ "EffectRadius", INI::parseReal, nullptr, PA_OFF(m_effectRadius) },
	{ "PingDelay", INI::parseDurationUnsignedInt, nullptr, PA_OFF(m_pingDelay) },
	{ "HealPercentPerSecond", INI::parsePercentToReal, nullptr, PA_OFF(m_healPercentPerSecond) },
	{ "ModifierName", INI::parseAsciiStringVectorAppend, nullptr, PA_OFF(m_modifierNames) },
	{ "AllowFilter", parseFilter, nullptr, PA_OFF(m_allowFilter) },
	{ "UpgradeRequired", INI::parseAsciiString, nullptr, PA_OFF(m_upgradeRequired) },
	{ "NonStackable", INI::parseBool, nullptr, PA_OFF(m_nonStackable) },
	{ "AntiCategories", parseAntiCategories, nullptr, PA_OFF(m_antiCategories) },
	{ "AntiFX", parseFX, nullptr, PA_OFF(m_antiFX) },
	{ "HealFX", parseFX, nullptr, PA_OFF(m_healFX) },
	{ nullptr, nullptr, nullptr, 0 }
};

int kindBit(const char *name)
{
	return ObjectTemplateInfoBuilder::kindOfIndex(name);
}

// horde interface slot 0x180 (RW 0x8706DF, a filter): the contained members the filter allows for the horde's controlling player
unsigned hordeMembersAllowed(GameLogic &logic, Object &horde, const ObjectFilter &filter)
{
	ContainModuleInterface *c = horde.getContain();
	if (!c || !c->getHordeContainInterface())
	{
		return 0;
	}
	const ContainModuleInterface::ContainedItemsList *items = c->getContainedItemsList();
	if (!items)
	{
		return 0;
	}
	const Player *p = horde.getControllingPlayer();
	unsigned n = 0;
	for (Object *m : *items)
	{
		if (ObjectFilterMatch::allows(logic, filter, *m, p)) // RW 0x7640C1
		{
			++n;
		}
	}
	return n;
}

template <class T>
T *findModuleOf(Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules())
	{
		if (T *t = dynamic_cast<T *>(m.get()))
		{
			return t;
		}
	}
	return nullptr;
}

void emitFX(GameLogic &logic, const char *site, const std::string &fx, Object &primary)
{
	if (fx.empty())
	{
		return;
	}
	FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, site, logic.getFrame(), fx, primary); // RW 0x4B1B5A(fx, target, 0)
	logic.fxEvents().emit(e);
}
} // namespace

// ---- LargeGroupBonusUpdate ------------------------------------------------------------------------------------------------------------------------------------

void LargeGroupBonusUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kLargeGroupParse);
}

LargeGroupBonusUpdate::LargeGroupBonusUpdate(Thing *thing, const LargeGroupBonusUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	m_lastScan = logic.getFrame(); // RW 0x8937A6: + 0x24 = now
	// RW 0x8937B5: the first wake, GameLogicRandomValue(1, UpdateRate) line 0x6B
	const int first = logic.random().getValue(1, (int)data->m_updateRate, kLargeGroupFile, 0x6B);
	setWakeFrame(obj, UPDATE_SLEEP(first));
}

UpdateSleepTime LargeGroupBonusUpdate::update()
{
	// RW 0x8938D9
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	static const int kInfantry = kindBit("INFANTRY"), kCavalry = kindBit("CAVALRY");
	const bool unit = (kInfantry >= 0 && obj->isKindOf((unsigned)kInfantry)) || (kCavalry >= 0 && obj->isKindOf((unsigned)kCavalry));
	const bool wasActive = m_active;
	const UnsignedInt now = logic.getFrame();
	if (unit || m_data->m_updateRate + m_lastScan < now)
	{
		m_lastScan = now;
		const Player *owner = obj->getControllingPlayer();
		// RW 0x89396E: RW 0xC0F374 (bit 3: never set), RW 0xC10E20, RW 0xC10E14 (the same controlling player), RW 0xC63870 (a horde with an allowed member)
		// the filter's member count of each accepted object, kept for the sum below (performance: the hits of an unsorted scan are the accepted objects in
		// acceptance order, so the count need not be recomputed; the same pure function either way)
		m_scanCounts.clear();
		PartitionFilterFn filter([&](Object &o) {
			if (o.isEffectivelyDead() || o.getControllingPlayer() != owner)
			{
				return false;
			}
			const unsigned n = hordeMembersAllowed(logic, o, m_data->m_hordeMemberFilter);
			if (n > 0)
			{
				m_scanCounts.push_back(std::make_pair(&o, n));
			}
			return n > 0;
		});
		const PartitionHits hits = logic.partition().iterateObjectsInRange(*obj->getPosition(), m_data->m_radius, FROM_BOUNDINGSPHERE_3D, { &filter }, ITER_FASTEST);
		unsigned count = 0;
		for (size_t i = 0; i < hits.size(); ++i)
		{
			const Object *hit = hits[i].object;
			// RW 0x8939D8: horde slot 0x180
			count += (i < m_scanCounts.size() && m_scanCounts[i].first == hit) ? m_scanCounts[i].second : hordeMembersAllowed(logic, *hits[i].object, m_data->m_hordeMemberFilter);
		}
		m_lastCount = count;
		if (count < (unsigned)(m_data->m_count - 1))
		{
			m_active = false;
			m_reached = false;
			// RW 0x893A30: a neighbour within RubOffRadius that reached the count passes the bonus on (its active and reached bytes, RW 0x8936BB)
			const float rub2 = SimMath::pc24Mul(m_data->m_rubOffRadius, m_data->m_rubOffRadius);
			for (const PartitionHit &h : hits)
			{
				LargeGroupBonusUpdate *other = findModuleOf<LargeGroupBonusUpdate>(*h.object);
				if (!other || !(other->m_active && other->m_reached))
				{
					continue;
				}
				// RW 0x66137C: the object's x87 2D distance squared to the hit
				const Coord3D *a = obj->getPosition();
				const Coord3D *b = h.object->getPosition();
				const double dx = SimMath::pc24SubW((double)a->x, (double)b->x);
				const double dy = SimMath::pc24SubW((double)a->y, (double)b->y);
				const double d2 = SimMath::pc24AddW(SimMath::pc24MulW(dx, dx), SimMath::pc24MulW(dy, dy));
				if ((double)rub2 < d2)
				{
					continue;
				}
				m_active = true;
				break;
			}
		}
		else
		{
			m_reached = true;
			m_active = true;
		}
		if (wasActive != m_active)
		{
			if (m_active)
			{
				obj->addAttributeModifier(m_data->m_attributeModifier, 0); // RW 0x68F1A8
			}
			else
			{
				obj->removeAttributeModifier(m_data->m_attributeModifier); // RW 0x68F259
			}
		}
	}
	if (obj->isEffectivelyDead())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	return unit ? UPDATE_SLEEP((int)m_data->m_updateRate) : UPDATE_SLEEP_NONE;
}

void LargeGroupBonusUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_lastScan);
	h.addBool(m_active);
	h.addBool(m_reached);
	h.addU32(m_lastCount);
}

// ---- PassiveAreaEffectBehavior --------------------------------------------------------------------------------------------------------------------------------

void PassiveAreaEffectBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kPassiveAreaParse);
}

PassiveAreaEffectBehavior::PassiveAreaEffectBehavior(Thing *thing, const PassiveAreaEffectBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x887D49
}

void PassiveAreaEffectBehavior::scan()
{
	// RW 0x887F17
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	m_targets.clear();
	const Player *owner = obj->getControllingPlayer();
	PartitionFilterFn filter([&](Object &o) {
		// RW 0xC11DC0 (the object's ALLIES, mask 4), RW 0xC10E20, RW 0xC0F374 (bit 3: never set), the AllowFilter for the owner (RW 0xBE4CC8)
		return obj->getRelationship(o) == ALLIES && !o.isEffectivelyDead() && ObjectFilterMatch::allows(logic, m_data->m_allowFilter, o, owner);
	});
	static const int kIgnored = kindBit("IGNORED_IN_GUI"), kArmyOfDead = kindBit("ARMY_OF_DEAD");
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(*obj->getPosition(), m_data->m_effectRadius, FROM_CENTER_2D, { &filter }, ITER_SORTED_NEAR_TO_FAR))
	{
		Object *o = h.object;
		if (o == obj || (kIgnored >= 0 && o->isKindOf((unsigned)kIgnored)) || (kArmyOfDead >= 0 && o->isKindOf((unsigned)kArmyOfDead)))
		{
			continue; // RW 0x887FF1 / 0x888009
		}
		if (std::find(m_targets.begin(), m_targets.end(), o->getID()) == m_targets.end())
		{
			m_targets.push_back(o->getID()); // RW 0x888051
		}
	}
}

void PassiveAreaEffectBehavior::affect(Object &target)
{
	// RW 0x887B72: a live target gets the heal, then the modifiers
	if (target.isEffectivelyDead())
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const PassiveAreaEffectBehaviorModuleData *d = m_data;
	const UnsignedInt now = logic.getFrame();
	// RW 0x887BAB: not damaged within 4 seconds (RW 0x68C933: the body's last damage frame + 4 * 5 >= now)
	bool recentlyDamaged = false;
	ActiveBody *body = dynamic_cast<ActiveBody *>(target.getBodyModule());
	if (body && body->lastDamageFrame() != 0xFFFFFFFFu && body->lastDamageFrame() + 20u >= now)
	{
		recentlyDamaged = true;
	}
	if (!recentlyDamaged && d->m_healPercentPerSecond > 0.0f && target.getBodyModule())
	{
		BodyModuleInterface *b = target.getBodyModule();
		const float health = b->getHealth();
		const float maxHealth = b->getMaxHealth();
		if (health != maxHealth) // RW 0x887C10: fucompi, equal skips
		{
			bool blocked = false;
			if (d->m_nonStackable)
			{
				// RW 0x887C2C: the body's last heal frame (slot 0x48) + PingDelay > now blocks (S-1023: not ported, the heal stacks)
				logic.noteStop("[S-1023] PassiveAreaEffectBehavior: NonStackable needs the body's last heal frame (body slot 0x48): the heal stacks");
			}
			if (!blocked)
			{
				// RW 0x887C39: MaxHealth * (percent / 5) * PingDelay (x87: fild 5, fdivr, fmulp, fild PingDelay, fmulp)
				double amount = SimMath::pc24MulW((double)maxHealth, SimMath::pc24DivW((double)d->m_healPercentPerSecond, 5.0));
				if (d->m_pingDelay != 0)
				{
					amount = SimMath::pc24MulW(amount, SimMath::fildU32(d->m_pingDelay));
				}
				target.attemptHealingFromSoleBenefactor(SimMath::fstpDword(amount), obj, d->m_pingDelay); // RW 0x690584
				++m_heals;
				emitFX(logic, "PassiveAreaEffectBehavior HealFX", d->m_healFX, target);
			}
		}
	}
	// RW 0x887C94: every ModifierName for its own duration, then the pool's AntiCategories until now + PingDelay and AntiFX
	for (const std::string &name : d->m_modifierNames)
	{
		target.addAttributeModifier(name, -1);
		++m_modifiers;
	}
	if (target.findModule("AttributeModifierPoolUpdate"))
	{
		if (d->m_antiCategories != 0)
		{
			logic.noteStop("[S-1023] PassiveAreaEffectBehavior: the AntiCategories disable of the target's pool (RW 0x804FCC, S-633) is not run");
		}
		emitFX(logic, "PassiveAreaEffectBehavior AntiFX", d->m_antiFX, target);
	}
}

UpdateSleepTime PassiveAreaEffectBehavior::update()
{
	// RW 0x887DF7
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const UpdateSleepTime sleep = UPDATE_SLEEP(m_data->m_pingDelay == 0 ? 1 : (int)m_data->m_pingDelay);
	if (!m_data->m_upgradeRequired.empty())
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(m_data->m_upgradeRequired) : nullptr; // RW 0x66F5E5
		if (!u || !obj->hasUpgrade(u)) // RW 0x691421
		{
			return sleep;
		}
	}
	if (obj->isUnderConstruction())
	{
		return sleep;
	}
	if (obj->isEffectivelyDead())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const UnsignedInt now = logic.getFrame();
	if (now - m_lastScan >= m_data->m_pingDelay)
	{
		scan(); // slot 0x34
		m_lastScan = now;
	}
	const std::vector<ObjectID> ids(m_targets.begin(), m_targets.end());
	for (ObjectID id : ids)
	{
		if (Object *o = logic.findObjectByID(id))
		{
			affect(*o); // slot 0x38
		}
	}
	return sleep;
}

void PassiveAreaEffectBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_lastScan);
	h.addU32((std::uint32_t)m_targets.size());
	for (ObjectID id : m_targets)
	{
		h.addU32(id);
	}
	h.addU64(m_heals);
	h.addU64(m_modifiers);
}

// ---- registration ---------------------------------------------------------------------------------------------------------------------------------------------

namespace
{
template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
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
} // namespace

void AreaScanModules::registerAll(ModuleFactory &modules)
{
	bind<LargeGroupBonusUpdate, LargeGroupBonusUpdateModuleData>(modules, "LargeGroupBonusUpdate");
	bind<PassiveAreaEffectBehavior, PassiveAreaEffectBehaviorModuleData>(modules, "PassiveAreaEffectBehavior");
}

std::vector<std::string> AreaScanModules::stopLines()
{
	return { kStop };
}
