// OpenBFME. GPL-3.0.
// See GameLogic/Module/TooltipUpgrade.h (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06).

#include "GameLogic/Module/TooltipUpgrade.h"

#include "Common/INI.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace
{
// RW 0xC6F898 / BFME2 0xC58C38
const FieldParse kTooltipUpgradeFieldParse[] = {
	{ "DisplayName", INI::parseAsciiString, nullptr, (int)offsetof(TooltipUpgradeModuleData, m_displayName) },
	{ "Description", INI::parseAsciiString, nullptr, (int)offsetof(TooltipUpgradeModuleData, m_description) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC1119C / BFME2 0xBFB08C: sentinel that resets the custom display name back to the template's DisplayName.
constexpr const char *kReferenceDisplayName = "ReferenceDisplayName";
} // namespace

// RW 0x8BBB5D / BFME2 0x8B7A2E
void TooltipUpgradeModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	UpgradeModuleData::buildBaseFieldParse(p);
	p.add(kTooltipUpgradeFieldParse);
}

// RW 0x8BBB7F / BFME2 0x8B7A50
TooltipUpgrade::TooltipUpgrade(Thing *thing, const TooltipUpgradeModuleData *data)
	: UpgradeModule(thing, data)
	, m_data(data)
{
}

void TooltipUpgrade::applyTooltip()
{
	if (!m_data)
	{
		return;
	}
	m_applied = true;
	// RW 0x8BBBE9 -> Drawable::setCustomTooltipDisplayName (RW 0x6796DA / BFME2 0x6793AF):
	// if m_displayName == "ReferenceDisplayName" (RW 0xC1119C / BFME2 0xBFB08C), clears custom display name (Drawable + 0x348);
	// else if !m_displayName.empty(), sets custom display name = m_displayName;
	// if empty, preserves the existing custom display name.
	if (m_data->m_displayName == kReferenceDisplayName)
	{
		m_activeDisplayName.clear();
	}
	else if (!m_data->m_displayName.empty())
	{
		m_activeDisplayName = m_data->m_displayName;
	}

	// RW 0x8BBBF7 -> Drawable::setCustomTooltipDescription (RW 0x674FAE / BFME2 0x674C88):
	// if m_description.empty(), clears custom description (Drawable + 0x34C);
	// else sets custom description = m_description.
	if (m_data->m_description.empty())
	{
		m_activeDescription.clear();
	}
	else
	{
		m_activeDescription = m_data->m_description;
	}

	if (Object *obj = getObject())
	{
		obj->logic().noteStop(stopLine());
	}
}

// RW 0x8BBBCC / BFME2 0x8B7A9D
void TooltipUpgrade::upgradeImplementation()
{
	applyTooltip();
}

// RW 0x8BBC0A / BFME2 0x8B7ADB
void TooltipUpgrade::processUpgradeRemoval()
{
	m_applied = false;
	m_activeDisplayName.clear();
	m_activeDescription.clear();
	setUpgradeExecuted(false);
	if (Object *obj = getObject())
	{
		obj->logic().noteStop(stopLine());
	}
}

// RW 0x8B7CAB / BFME2 0x8B3B42 (mux slot 6, ICF-folded with CommandSetUpgrade::postUpgradeCheck)
void TooltipUpgrade::postUpgradeCheck()
{
	processUpgradeRemoval();
	Object *obj = getObject();
	if (!obj)
	{
		return;
	}
	const Player *player = obj->getControllingPlayer();
	UpgradeMaskType mask = player ? player->getCompletedUpgradeMask() : UpgradeMaskType{};
	mask.orWith(obj->getUpgradeMask());
	attemptUpgrade(mask);
}

// RW 0x674F88 / BFME2 0x674C62
std::string TooltipUpgrade::displayNameOf(const Object &obj)
{
	const std::vector<std::unique_ptr<BehaviorModule>> &mods = obj.modules();
	for (auto it = mods.rbegin(); it != mods.rend(); ++it)
	{
		if (const auto *tu = dynamic_cast<const TooltipUpgrade *>(it->get()))
		{
			if (tu->isTooltipApplied())
			{
				return tu->m_activeDisplayName;
			}
		}
	}
	return {};
}

// RW 0x674FD8 / BFME2 0x674CB2
std::string TooltipUpgrade::descriptionOf(const Object &obj)
{
	const std::vector<std::unique_ptr<BehaviorModule>> &mods = obj.modules();
	for (auto it = mods.rbegin(); it != mods.rend(); ++it)
	{
		if (const auto *tu = dynamic_cast<const TooltipUpgrade *>(it->get()))
		{
			if (tu->isTooltipApplied())
			{
				return tu->m_activeDescription;
			}
		}
	}
	return {};
}

const char *TooltipUpgrade::stopLine()
{
	return "[S-6013] TooltipUpgrade: Drawable custom tooltip display and TheControlBar refresh (RW 0x6796DA / 0x674FAE; BFME2 0x6793AF / 0x674C88) are not ported; custom display name and description are held on the object's canonical TooltipUpgrade module";
}

// RW 0x8B88B0 -> 0x63F3BF / BFME2 0x897805 -> 0x4B3FD0
void TooltipUpgrade::crc(StateHasher &h) const
{
	UpgradeModule::crc(h);
}

// RW 0x8BC857 / BFME2 0x8B8710
void TooltipUpgrade::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	UpgradeModule::xfer(x);
	x.xferBool(&m_applied);
	x.xferAsciiString(&m_activeDisplayName);
	x.xferAsciiString(&m_activeDescription);
	if (x.getXferMode() == XFER_LOAD && isAlreadyUpgraded())
	{
		applyTooltip();
	}
}

// RW 0x65B257 / BFME2 0x659E2F
void TooltipUpgrade::registerClass(ModuleFactory &factory)
{
	factory.bindTypedData<TooltipUpgradeModuleData>("TooltipUpgrade", MODULETYPE_BEHAVIOR);
	factory.bindModuleProc(
		"TooltipUpgrade",
		MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const TooltipUpgradeModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("TooltipUpgrade: the module data is not typed");
			}
			return std::make_unique<TooltipUpgrade>(thing, typed);
		});
}
