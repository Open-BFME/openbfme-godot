// OpenBFME. GPL-3.0.
// See GameLogic/Module/MoneyEventModules.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/MoneyEventModules.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"

#include <cstddef>

namespace
{
void parseOptionalObjectFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr);
	*static_cast<std::shared_ptr<const ObjectFilter> *>(store) = std::make_shared<const ObjectFilter>(std::move(f));
}
void parseUpgradeName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextAsciiString();
	*static_cast<std::string *>(store) = (name == "None" || name == "NONE" || name == "none") ? std::string() : name;
}

#define RD_OFF(member) (int)offsetof(RefundDieModuleData, member)
const FieldParse kRefundDieFieldParse[] = {
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, RD_OFF(m_dieMux.m_deathTypes) },
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, RD_OFF(m_dieMux.m_exemptStatus) },
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, RD_OFF(m_dieMux.m_requiredStatus) },
	{ "DamageAmountRequired", INI::parseReal, nullptr, RD_OFF(m_dieMux.m_damageAmountRequired) },
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, RD_OFF(m_dieMux.m_minKillerAngle) },
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, RD_OFF(m_dieMux.m_maxKillerAngle) },
	{ "UpgradeRequired", parseUpgradeName, nullptr, RD_OFF(m_upgradeRequired) },
	{ "BuildingRequired", parseOptionalObjectFilter, nullptr, RD_OFF(m_buildingRequired) },
	{ "RefundPercent", INI::parsePercentToReal, nullptr, RD_OFF(m_refundPercent) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef RD_OFF

#define PM_OFF(member) (int)offsetof(PillageModuleData, member)
const FieldParse kPillageFieldParse[] = {
	{ "PillageAmount", INI::parseUnsignedInt, nullptr, PM_OFF(m_pillageAmount) },
	{ "NumDamageEventsPerPillage", INI::parseUnsignedInt, nullptr, PM_OFF(m_numDamageEventsPerPillage) },
	{ "PillageFilter", parseOptionalObjectFilter, nullptr, PM_OFF(m_pillageFilter) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PM_OFF

#define SC_OFF(member) (int)offsetof(SalvageCrateCollideModuleData, member)
const FieldParse kSalvageCrateFieldParse[] = {
	{ "RequiredKindOf", ParseKindOfMask, nullptr, SC_OFF(m_requiredKindOf) },
	{ "ForbiddenKindOf", ParseKindOfMask, nullptr, SC_OFF(m_forbiddenKindOf) },
	{ "ForbidOwnerPlayer", INI::parseBool, nullptr, SC_OFF(m_forbidOwnerPlayer) },
	{ "BuildingPickup", INI::parseBool, nullptr, SC_OFF(m_buildingPickup) },
	{ "HumanOnly", INI::parseBool, nullptr, SC_OFF(m_humanOnly) },
	{ "PickupScience", INI::parseAsciiString, nullptr, SC_OFF(m_pickupScience) },
	{ "ExecuteFX", INI::parseAsciiString, nullptr, SC_OFF(m_executeFX) },
	{ "ExecuteAnimation", INI::parseAsciiString, nullptr, SC_OFF(m_executeAnimation) },
	{ "ExecuteAnimationTime", INI::parseReal, nullptr, SC_OFF(m_executeAnimationTime) },
	{ "ExecuteAnimationZRise", INI::parseReal, nullptr, SC_OFF(m_executeAnimationZRise) },
	{ "ExecuteAnimationFades", INI::parseBool, nullptr, SC_OFF(m_executeAnimationFades) },
	{ "PorterChance", INI::parsePercentToReal, nullptr, SC_OFF(m_porterChance) },
	{ "BannerChance", INI::parsePercentToReal, nullptr, SC_OFF(m_bannerChance) },
	{ "LevelUpChance", INI::parsePercentToReal, nullptr, SC_OFF(m_levelUpChance) },
	{ "LevelUpRadius", INI::parsePercentToReal, nullptr, SC_OFF(m_levelUpRadius) },
	{ "ResourceChance", INI::parsePercentToReal, nullptr, SC_OFF(m_resourceChance) },
	{ "Upgrade", INI::parseAsciiString, nullptr, SC_OFF(m_upgrade) },
	{ "MinResource", INI::parseInt, nullptr, SC_OFF(m_minResource) },
	{ "MaxResource", INI::parseInt, nullptr, SC_OFF(m_maxResource) },
	{ "AllowAIPickup", INI::parseBool, nullptr, SC_OFF(m_allowAIPickup) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef SC_OFF

// RW 0x661317: A has every bit of `required` and none of `exempt`
bool statusTest(const ObjectStatusMaskType &a, const ObjectStatusMaskType &required, const ObjectStatusMaskType &exempt)
{
	for (size_t i = 0; i < a.size(); ++i)
	{
		if ((exempt[i] & a[i]) != 0 || (required[i] & a[i]) != required[i])
		{
			return false;
		}
	}
	return true;
}
} // namespace

void RefundDieModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kRefundDieFieldParse);
}

void PillageModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kPillageFieldParse);
}

void SalvageCrateCollideModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kSalvageCrateFieldParse);
}

bool DieMuxData::isDieApplicable(const Object &obj, const DieModuleInterface::Event &event, bool *angleNotPorted) const
{
	// RW 0x8D29B6 .. 0x8D29C1: `shl eax, cl` takes the count modulo 32
	if (!(m_deathTypes & (1u << ((unsigned)(event.deathType - 1) & 31u))))
	{
		return false;
	}
	if (!statusTest(obj.getStatusBits(), m_requiredStatus, m_exemptStatus))
	{
		return false;
	}
	if (m_damageAmountRequired >= 0.0f && m_damageAmountRequired > event.damageAmount)
	{
		return false; // RW 0x8D29EC .. 0x8D29FE
	}
	if (m_maxKillerAngle > m_minKillerAngle)
	{
		if (angleNotPorted)
		{
			*angleNotPorted = true; // the killer's bearing test (RW 0x8D2A0F .. 0x8D2AAC) is not ported
		}
		return true;
	}
	return true;
}

// ---- RefundDie -----------------------------------------------------------------------------------------------------------------------
RefundDie::RefundDie(Thing *thing, const RefundDieModuleData *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
}

int RefundDie::refundAmount(const DieModuleInterface::Event &event) const
{
	const Object *obj = getObject();
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*obj, event, &angle))
	{
		return 0;
	}
	if (angle)
	{
		obj->logic().reportError("RefundDie of " + obj->getTemplate()->getName() + ": MinKillerAngle / MaxKillerAngle are not ported (the die applies as if the killer were in the window)");
	}
	const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	const int sold = ObjectTemplateInfoBuilder::objectStatusIndex("SOLD");
	if ((underConstruction >= 0 && obj->testStatus((unsigned)underConstruction)) || (sold >= 0 && obj->testStatus((unsigned)sold)))
	{
		return 0; // RW 0x888548 / 0x888559
	}
	Player *owner = obj->getControllingPlayer();
	if (!owner)
	{
		return 0;
	}
	if (!m_data->m_upgradeRequired.empty() && !owner->hasUpgradeComplete(m_data->m_upgradeRequired))
	{
		return 0;
	}
	if (ObjectFilterMatch::isValid(m_data->m_buildingRequired.get()) && !owner->hasObjectMatching(*m_data->m_buildingRequired, false))
	{
		return 0;
	}
	// RW 0x8885BF .. 0x8885E4: fld paid; fld percent; fmul; fstp qword; ceil; fstp dword; fld; fistp
	const double product = NumericState::pc24MulW((double)obj->getBuildCostPaid(), (double)m_data->m_refundPercent);
	return NumericState::fistp32((double)NumericState::fstpDword(NumericState::ceilD(product)));
}

void RefundDie::onDie(const DieModuleInterface::Event &event)
{
	const int refund = refundAmount(event);
	if (refund == 0)
	{
		return;
	}
	Object *obj = getObject();
	Player *owner = obj->getControllingPlayer();
	owner->depositMoney((std::uint32_t)refund, true); // RW 0x8885FF
	obj->logic().economy().noteIncome(*obj, *owner, refund);
}

// ---- PillageModule -------------------------------------------------------------------------------------------------------------------
PillageModule::PillageModule(Thing *thing, const PillageModuleData *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
}

void PillageModule::onDamageDealt(Object &victim)
{
	Object *self = getObject();
	const ObjectFilter filter = m_data->m_pillageFilter ? *m_data->m_pillageFilter : ObjectFilter();
	if (!ObjectFilterMatch::allows(self->logic(), filter, victim, nullptr))
	{
		return;
	}
	++m_counter;
	if (m_counter < m_data->m_numDamageEventsPerPillage)
	{
		return;
	}
	m_counter = 0;
	Player *victimOwner = victim.getControllingPlayer();
	Player *attackerOwner = self->getControllingPlayer();
	if (!victimOwner || !attackerOwner)
	{
		return;
	}
	const std::uint32_t cash = victimOwner->getMoney()->countMoney();
	const std::uint32_t amount = m_data->m_pillageAmount < cash ? m_data->m_pillageAmount : cash; // cmovb
	const std::uint32_t taken = victimOwner->withdrawMoney(amount, true);                         // RW 0x8882F5
	if (taken == 0)
	{
		return;
	}
	attackerOwner->depositMoney(taken, true); // RW 0x888314
	self->logic().economy().noteIncome(*self, *attackerOwner, (int)taken);
}

void PillageModule::crc(StateHasher &h) const
{
	h.addU32(m_counter);
}
