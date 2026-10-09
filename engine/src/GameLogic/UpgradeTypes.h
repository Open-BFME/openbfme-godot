// OpenBFME. GPL-3.0.
//
// UpgradeTypeTable (lane PROD-1): the type of every upgrade by name, what the CommandSet availability test (RW 0x794F38) asks of a button's NeededUpgrade
// entries: object upgrades (the building's, RW 0x691421) or player upgrades (the owner's, RW 0x6AC2AF). Lane UPGRADE-1: the production table is filled from
// the real UpgradeCenter (Common/Upgrade.h: every block of the INI load, the #include'd Create-A-Hero upgrades and the veterancy upgrades included; a block
// without Type takes DefaultUpgrade's Type = PLAYER through the copy of RW 0x66FC27). scan() stays for synthetic tests.

#pragma once

#include <map>
#include <string>

class UpgradeCenter;

class UpgradeTypeTable
{
public:
	enum Type
	{
		UPGRADE_TYPE_PLAYER = 0,
		UPGRADE_TYPE_OBJECT = 1,
		UPGRADE_TYPE_UNKNOWN = -1
	};
	// -1 for a name that is not an Upgrade (case sensitive, like RW's NameKey lookup)
	int typeOf(const std::string &name) const
	{
		auto it = m_types.find(name);
		return it == m_types.end() ? UPGRADE_TYPE_UNKNOWN : it->second;
	}
	size_t size() const { return m_types.size(); }
	// every upgrade of the center (replaces the table)
	void assign(const UpgradeCenter &center);
	bool scan(const std::string &text, std::string *error);

private:
	std::map<std::string, int> m_types;
};
