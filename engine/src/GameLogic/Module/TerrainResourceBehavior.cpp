// OpenBFME. GPL-3.0.
// See GameLogic/Module/TerrainResourceBehavior.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/TerrainResourceBehavior.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerTemplate.h"
#include "Common/StateHash.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>

namespace
{
// an ObjectFilter field whose handle is -1 until the INI sets it (RW 0x76392F stores the interned index)
void parseOptionalObjectFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f; // RW 0x762BDF
	ParseObjectFilter(ini, instance, &f, nullptr);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}
// RW 0x73AF89 parseUpgradeTemplate: the name is kept (the UpgradeCenter that resolves it is UPGRADE-1's, stop S-252)
void parseUpgradeName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextAsciiString();
	*static_cast<std::string *>(store) = (name == "None" || name == "NONE" || name == "none") ? std::string() : name;
}

#define TR_OFF(member) (int)offsetof(TerrainResourceBehaviorModuleData, member)
const FieldParse kTerrainResourceFieldParse[] = {
	{ "Radius", INI::parseReal, nullptr, TR_OFF(m_radius) },
	{ "MaxIncome", INI::parseInt, nullptr, TR_OFF(m_maxIncome) },
	{ "IncomeInterval", INI::parseDurationUnsignedInt, nullptr, TR_OFF(m_incomeInterval) },
	{ "HighPriority", INI::parseBool, nullptr, TR_OFF(m_highPriority) },
	{ "Visible", INI::parseBool, nullptr, TR_OFF(m_visible) },
	{ "Upgrade", parseUpgradeName, nullptr, TR_OFF(m_upgrade) },
	{ "UpgradeBonusPercent", INI::parsePercentToReal, nullptr, TR_OFF(m_upgradeBonusPercent) },
	{ "UpgradeMustBePresent", parseOptionalObjectFilter, nullptr, TR_OFF(m_upgradeMustBePresent) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef TR_OFF
} // namespace

void TerrainResourceBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kTerrainResourceFieldParse);
}

TerrainResourceBehavior::TerrainResourceBehavior(Thing *thing, const TerrainResourceBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE); // RW 0x885330 -> 0x850C32
}

void TerrainResourceBehavior::onBuildComplete()
{
	// RW 0x8853A0
	Object *obj = getObject();
	setWakeFrame(obj, UPDATE_SLEEP_NONE);
	m_needToRunOnBuildComplete = false;
	TerrainResourceManager &manager = obj->logic().economy().resources();
	if (!m_claimed)
	{
		manager.claim(*obj, m_data->m_radius, m_data->m_visible, m_data->m_highPriority);
		m_claimed = true;
	}
	manager.onBuildComplete(*obj);
}

void TerrainResourceBehavior::onDie(const DieModuleInterface::Event &)
{
	// RW 0x885380 / 0x885362
	Object *obj = getObject();
	obj->logic().economy().resources().unclaim(*obj, m_data->m_radius);
}

UpdateSleepTime TerrainResourceBehavior::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Economy &economy = logic.economy();
	if (!m_claimed)
	{
		economy.resources().claim(*obj, m_data->m_radius, m_data->m_visible, m_data->m_highPriority);
		m_claimed = true;
		if (m_needToRunOnBuildComplete)
		{
			return UPDATE_SLEEP_FOREVER;
		}
	}
	const UpdateSleepTime interval = (UpdateSleepTime)m_data->m_incomeInterval;
	Player *owner = obj->getControllingPlayer();
	if (!owner || !owner->getPlayerTemplate())
	{
		return interval; // RW 0x885546: [player + 0x34] is the PlayerTemplate
	}
	// RW 0x885549 .. 0x885579: the upgrade bonus
	float bonus = 1.0f;
	if (!m_data->m_upgrade.empty() && owner->hasUpgradeComplete(m_data->m_upgrade))
	{
		const ObjectFilter requirement = m_data->m_upgradeMustBePresent ? *m_data->m_upgradeMustBePresent : ObjectFilter();
		if (owner->hasObjectMatching(requirement, true))
		{
			bonus = m_data->m_upgradeBonusPercent;
		}
	}
	// RW 0x885591 .. 0x88559E: the PRODUCTION attribute modifier product (out = 1.0 first, RW 0x805007)
	const float modifier = economy.attributeModifierProduct(*obj, Economy::ATTRIBUTE_MODIFIER_PRODUCTION);
	// RW 0x8855A3 .. 0x885650: the resource factor of the owner's template (the diminishing returns of many resource buildings)
	float resourceFactor = 1.0f;
	const PlayerTemplate *pt = owner->getPlayerTemplate();
	const ObjectFilter *rf = pt->m_resourceModifierObjectFilter.get();
	if (ObjectFilterMatch::isValid(rf) && ObjectFilterMatch::allows(logic, *rf, *obj, owner))
	{
		const int n = economy.countOwnedObjectsAllowedBy(*owner, *rf); // RW 0x6ABABD + 0x885230
		const std::vector<int> &values = pt->m_resourceModifierValues;
		const int size = (int)values.size();
		if (n < size)
		{
			resourceFactor = NumericState::sseMul(NumericState::sseFromInt32(values[(size_t)n]), 0.01f);
		}
		else
		{
			// RW 0x88561E .. 0x885649: values.back() * 0.01 - (n - size) * 0.02, never below 0 (an empty list indexes back()[-1]: retail reads before the vector)
			float f = NumericState::sseMul(NumericState::sseFromInt32(size > 0 ? values[(size_t)size - 1] : 0), 0.01f);
			f = NumericState::sseSub(f, NumericState::sseMul(NumericState::sseFromInt32(n - size), 0.02f));
			resourceFactor = f < 0.0f ? 0.0f : f;
		}
	}
	// RW 0x885650 .. 0x885699: ceil(MaxIncome * share * modifier * bonus), x87 at 24 bits
	double wide = NumericState::pc24MulW((double)m_data->m_maxIncome, (double)m_share);
	wide = NumericState::pc24MulW(wide, (double)modifier);
	wide = NumericState::pc24MulW(wide, (double)bonus);
	const float stored = NumericState::fstpDword(NumericState::ceilD(wide)); // fstp qword; MSVCR71 ceil; fstp dword
	int amount = NumericState::fistp32((double)stored);
	if (amount > 0)
	{
		// RW 0x885680 .. 0x8856A6: amount * resourceFactor, ceil, at least 1
		const double w2 = NumericState::pc24MulW((double)amount, (double)resourceFactor);
		amount = NumericState::fistp32((double)NumericState::fstpDword(NumericState::ceilD(w2)));
		if (amount <= 0)
		{
			amount = 1;
		}
		owner->depositMoney((std::uint32_t)amount, true); // RW 0x8856B6: Money::deposit(amount, &owner + 0x3DC, 1)
		economy.noteIncome(*obj, *owner, amount);          // the floating text and the debug overlay (RW 0x8856BD .. 0x885737)
	}
	// RW 0x88573C .. 0x885765: the building's experience tracker gets `amount` points whether or not anything was paid (amount <= 0 here is the unscaled one)
	economy.grantBuildingExperience(*obj, NumericState::sseFromInt32(amount));
	return interval;
}

void TerrainResourceBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_claimed);
	h.addBool(m_needToRunOnBuildComplete);
	h.addFloat(m_share);
}
