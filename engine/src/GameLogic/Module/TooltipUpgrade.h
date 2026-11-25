// OpenBFME. GPL-3.0.
//
// TooltipUpgrade (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06):
// Overrides an object's tooltip display name and description label when its upgrade triggers,
// and clears or replaces them on upgrade removal / conflicting upgrade replacement.
//
// TARGET FACTS:
// - Registry: RW 0x65B257 / BFME2 0x659E2F ("TooltipUpgrade", interface mask 132 = 0x84).
// - TooltipUpgradeModuleData: createData RW 0x655697 (0x140B) / BFME2 0x6558E9 (0x120B),
//   ctor RW 0x65563C (vtable RW 0xC09AD0) / BFME2 0x65588E (vtable BFME2 0xBF4788),
//   buildFieldParse RW 0x8BBB5D (table RW 0xC6F898) / BFME2 0x8B7A2E (table BFME2 0xC58C38):
//   chains UpgradeModuleData::buildBaseFieldParse (RW 0xC76AD8 / BFME2 0xC5FE38, extra 8) and adds
//   DisplayName (INI::parseAsciiString, RW +0x138 / BFME2 +0x118) and
//   Description (INI::parseAsciiString, RW +0x13C / BFME2 +0x11C).
// - TooltipUpgrade: create RW 0x6504C2 / BFME2 0x650728 (0x1CB),
//   ctor RW 0x8BBB7F (vtable RW 0xC6F868, mux RW 0xC6F820) / BFME2 0x8B7A50 (vtable BFME2 0xC58C08, mux BFME2 0xC58BC0).
// - upgradeImplementation: RW 0x8BBBCC / BFME2 0x8B7A9D:
//   * Drawable::setCustomTooltipDisplayName(m_displayName) (RW 0x6796DA / BFME2 0x6793AF):
//     if m_displayName == "ReferenceDisplayName" (RW 0xC1119C / BFME2 0xBFB08C), clears custom display name (Drawable + 0x348);
//     else if !m_displayName.empty(), sets custom display name = m_displayName;
//     if empty, leaves the existing custom display name untouched.
//   * Drawable::setCustomTooltipDescription(&m_description) (RW 0x674FAE / BFME2 0x674C88):
//     if m_description.empty(), clears custom description (Drawable + 0x34C);
//     else sets custom description = m_description.
//   * Marks TheControlBar dirty (+0x28 = 1, RW 0xDE7744 / BFME2 0xE01CFC).
// - processUpgradeRemoval: RW 0x8BBC0A / BFME2 0x8B7ADB:
//   when isAlreadyUpgraded(), clears both custom display name and custom description,
//   marks TheControlBar dirty, and calls setUpgradeExecuted(false).
// - postUpgradeCheck: RW 0x8B7CAB / BFME2 0x8B3B42 (ICF-folded with CommandSetUpgrade::postUpgradeCheck):
//   calls processUpgradeRemoval(), then re-attempts upgrade with (player->getCompletedUpgradeMask() | obj->getUpgradeMask()).
// - xfer: RW 0x8BC857 / BFME2 0x8B8710 (version 1, UpgradeModule::xfer).
// - crc: RW 0x8B88B0 -> 0x63F3BF / BFME2 0x897805 -> 0x4B3FD0 (UpgradeModule::crc).
// - Client read: UnitHelpSource::compose (RW 0x8EBCEC -> Drawable::getCustomTooltipDisplayName RW 0x674F88 / BFME2 0x674C62;
//   RW 0x8EC0C7 -> Drawable::getCustomTooltipDescription RW 0x674FD8 / BFME2 0x674CB2).

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

#include <string>

class ModuleFactory;
class Object;

class TooltipUpgradeModuleData : public UpgradeModuleData
{
public:
	std::string m_displayName; ///< RW +0x138 / BFME2 +0x118: DisplayName
	std::string m_description; ///< RW +0x13C / BFME2 +0x11C: Description

	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0x008BBAFC (getModuleName), RW 0x008BBB18 (getModuleNameKey), RW 0x008BBBB0 / RW 0x008BBADC (dtor)
class TooltipUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(TooltipUpgrade)

	TooltipUpgrade(Thing *thing, const TooltipUpgradeModuleData *data);

	const TooltipUpgradeModuleData *getTooltipUpgradeModuleData() const { return m_data; }

	void postUpgradeCheck() override; // RW 0x8B7CAB / BFME2 0x8B3B42
	void crc(StateHasher &h) const override;

	// RW 0x674F88 / BFME2 0x674C62 (Drawable + 0x348) and RW 0x674FD8 / BFME2 0x674CB2 (Drawable + 0x34C).
	static std::string displayNameOf(const Object &obj);
	static std::string descriptionOf(const Object &obj);

	const std::string &activeDisplayName() const { return m_activeDisplayName; }
	const std::string &activeDescription() const { return m_activeDescription; }
	bool isTooltipApplied() const { return m_applied; }

	static void registerClass(ModuleFactory &factory);
	static const char *stopLine();

protected:
	void upgradeImplementation() override; // RW 0x8BBBCC / BFME2 0x8B7A9D
	void processUpgradeRemoval() override; // RW 0x8BBC0A / BFME2 0x8B7ADB

private:
	void applyTooltip();

	const TooltipUpgradeModuleData *m_data = nullptr;
	std::string m_activeDisplayName;
	std::string m_activeDescription;
	bool m_applied = false;
};
