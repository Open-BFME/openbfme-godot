// OpenBFME. GPL-3.0.
// See GameLogic/Module/AutoDepositUpdate.h for the target facts and the addresses.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/AutoDepositUpdate.h"

#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ObjectFilter.h"
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

#define AD_OFF(member) (int)offsetof(AutoDepositUpdateModuleData, member)
const FieldParse kAutoDepositFieldParse[] = {
	{ "DepositTiming", INI::parseDurationUnsignedInt, nullptr, AD_OFF(m_depositTiming) },
	{ "Upgrade", parseUpgradeName, nullptr, AD_OFF(m_upgrade) },
	{ "DepositAmount", INI::parseInt, nullptr, AD_OFF(m_depositAmount) },
	{ "InitialCaptureBonus", INI::parseInt, nullptr, AD_OFF(m_initialCaptureBonus) },
	{ "UpgradeBonusPercent", INI::parsePercentToReal, nullptr, AD_OFF(m_upgradeBonusPercent) },
	{ "UpgradeMustBePresent", parseOptionalObjectFilter, nullptr, AD_OFF(m_upgradeMustBePresent) },
	{ "GiveNoXP", INI::parseBool, nullptr, AD_OFF(m_giveNoXP) },
	{ "OnlyWhenGarrisoned", INI::parseBool, nullptr, AD_OFF(m_onlyWhenGarrisoned) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AD_OFF
} // namespace

void AutoDepositUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kAutoDepositFieldParse);
}

AutoDepositUpdate::AutoDepositUpdate(Thing *thing, const AutoDepositUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	// RW 0x89D9E9 .. 0x89D9F9: the first deposit is DepositTiming frames after creation
	m_depositOnFrame = getObject()->logic().getFrame() + m_data->m_depositTiming;
}

UpdateSleepTime AutoDepositUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Economy &economy = logic.economy();
	const UnsignedInt now = logic.getFrame();
	if (now < m_depositOnFrame)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (!m_started)
	{
		m_awardInitialCaptureBonus = true; // RW 0x89DB86
		m_started = true;
	}
	m_depositOnFrame = now + m_data->m_depositTiming;
	if (m_data->m_onlyWhenGarrisoned && !economy.objectHasModelCondition(*obj, "GARRISONED"))
	{
		return UPDATE_SLEEP_NONE;
	}
	Player *owner = obj->getControllingPlayer();
	if (owner == logic.players().getNeutralPlayer()) // RW 0x68B760
	{
		return UPDATE_SLEEP_NONE;
	}
	if (m_data->m_depositAmount <= 0)
	{
		return UPDATE_SLEEP_NONE;
	}
	const int underConstruction = ObjectTemplateInfoBuilder::objectStatusIndex("UNDER_CONSTRUCTION");
	if (underConstruction >= 0 && obj->testStatus((unsigned)underConstruction))
	{
		return UPDATE_SLEEP_NONE; // RW 0x89DBD7: the construction percent must be the "complete" value (-1.0)
	}
	if (economy.objectHasModelCondition(*obj, "RUBBLE") || economy.objectHasModelCondition(*obj, "POST_RUBBLE") || economy.objectHasModelCondition(*obj, "POST_COLLAPSE"))
	{
		return UPDATE_SLEEP_NONE;
	}
	if (!owner)
	{
		return UPDATE_SLEEP_NONE;
	}
	float bonus = economy.attributeModifierProduct(*obj, Economy::ATTRIBUTE_MODIFIER_PRODUCTION);
	if (!m_data->m_upgrade.empty() && owner->hasUpgradeComplete(m_data->m_upgrade))
	{
		const ObjectFilter requirement = m_data->m_upgradeMustBePresent ? *m_data->m_upgradeMustBePresent : ObjectFilter();
		if (owner->hasObjectMatching(requirement, false))
		{
			bonus = NumericState::sseMul(m_data->m_upgradeBonusPercent, bonus); // RW 0x89DC83 mulss
		}
	}
	const float amountF = NumericState::sseMul(NumericState::sseFromInt32(m_data->m_depositAmount), bonus);
	const int amount = economy.applyIncomeMultipliers(*owner, amountF);
	owner->depositMoney((std::uint32_t)amount, true); // RW 0x89DD08
	if (!m_data->m_giveNoXP)
	{
		economy.grantBuildingExperience(*obj, NumericState::sseMul(NumericState::sseFromInt32(m_data->m_depositAmount), bonus)); // RW 0x89DD31
	}
	economy.noteIncome(*obj, *owner, amount); // the floating text (RW 0x89DD52 .. 0x89DDD3)
	return UPDATE_SLEEP_NONE;
}

void AutoDepositUpdate::awardInitialCaptureBonus(Player *newOwner)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	Economy &economy = logic.economy();
	m_depositOnFrame = logic.getFrame() + m_data->m_depositTiming; // RW 0x89DA41 .. 0x89DA47
	if (!newOwner || !m_awardInitialCaptureBonus || m_data->m_initialCaptureBonus <= 0)
	{
		return;
	}
	int amount = m_data->m_initialCaptureBonus;
	if (economy.isMultiplayerGame())
	{
		const float mult = economy.multiPlayMoneyMult(economy.livePlayableCount(false));
		// fild amount; fmul st(1) (the multiplier); _ftol: truncation of the 24-bit product
		amount = (int)NumericState::ftol2Low32(NumericState::pc24MulW((double)amount, (double)mult));
	}
	amount = newOwner->applyHandicapMoney(amount);
	newOwner->depositMoney((std::uint32_t)amount, true);
	economy.noteIncome(*obj, *newOwner, amount);
	m_awardInitialCaptureBonus = false; // RW 0x89DB49
}

void AutoDepositUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_depositOnFrame);
	h.addBool(m_awardInitialCaptureBonus);
	h.addBool(m_started);
}
