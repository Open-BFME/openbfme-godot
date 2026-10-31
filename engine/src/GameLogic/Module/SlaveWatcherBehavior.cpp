// OpenBFME. GPL-3.0.
// See GameLogic/Module/SlaveWatcherBehavior.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SlaveWatcherBehavior.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "Common/Xfer.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>
#include <stdexcept>

namespace
{
typedef SlaveWatcherBehaviorModuleData Data;

// RW 0xC609F8, in the binary's row order
const FieldParse kFields[] = {
	{ "RemoveUpgrade", INI::parseAsciiString, nullptr, (int)offsetof(Data, m_removeUpgrade) }, // RW 0x42EE5E, + 0x0C
	{ "GrantUpgrade", INI::parseAsciiString, nullptr, (int)offsetof(Data, m_grantUpgrade) },   // RW 0x42EE5E, + 0x08
	{ "ShareUpgrades", INI::parseBool, nullptr, (int)offsetof(Data, m_shareUpgrades) },        // RW 0x42E558, + 0x10
	{ "LetSlaveLive", INI::parseBool, nullptr, (int)offsetof(Data, m_letSlaveLive) },          // RW 0x42E558, + 0x11
	{ nullptr, nullptr, nullptr, 0 }
};

const UpgradeTemplate *upgradeNamed(const std::string &name)
{
	return TheUpgradeCenter && !name.empty() ? TheUpgradeCenter->findUpgrade(name) : nullptr; // RW 0x66F5E5 (an empty or unknown name: null)
}
} // namespace

void SlaveWatcherBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void SlaveWatcherBehavior::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "SlaveWatcherBehavior"; // registration RW 0x6587AF, create RW 0x64C30E
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<SlaveWatcherBehavior>(thing, typed);
	});
}

// RW 0x887857
SlaveWatcherBehavior::SlaveWatcherBehavior(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x850C32(obj, 1)
}

SlaveWatcherBehavior *SlaveWatcherBehavior::of(const Object &obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj.modules()) // RW 0x68BDA5: the first module whose name key matches
	{
		if (SlaveWatcherBehavior *s = dynamic_cast<SlaveWatcherBehavior *>(m.get()))
		{
			return s;
		}
	}
	return nullptr;
}

void SlaveWatcherBehavior::shareUpgrades(Object &slave)
{
	m_upgradeMask = getObject()->getUpgradeMask(); // rep movsd 0x24 words from + 0x28C
	slave.friend_orUpgradeMask(m_upgradeMask);     // RW 0x68CC6C
	slave.updateUpgradeModules();                  // RW 0x6936FE
}

// RW 0x887AF5
void SlaveWatcherBehavior::setSlave(ObjectID id)
{
	m_slaveID = id;
	if (m_data->m_shareUpgrades)
	{
		if (Object *slave = getObject()->logic().findObjectByID(id))
		{
			shareUpgrades(*slave);
		}
	}
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x850C32(obj, 1)
}

// RW 0x887A35
UpdateSleepTime SlaveWatcherBehavior::update()
{
	if (m_slaveID == INVALID_ID)
	{
		return UPDATE_SLEEP_NONE;
	}
	Object *me = getObject();
	Object *slave = me->logic().findObjectByID(m_slaveID);
	if (!slave || slave->isEffectivelyDead()) // + 0x458 bit 0
	{
		const UpgradeTemplate *removed = upgradeNamed(m_data->m_removeUpgrade); // data + 0x0C
		const UpgradeTemplate *granted = upgradeNamed(m_data->m_grantUpgrade);  // data + 0x08
		if (removed)
		{
			me->removeUpgrade(removed); // RW 0x691438
		}
		if (granted)
		{
			me->giveUpgrade(granted); // RW 0x69388B
		}
		me->updateUpgradeModules(); // RW 0x6936FE
		m_slaveID = INVALID_ID;
		++m_releases;
		return UPDATE_SLEEP_FOREVER; // 0x3FFFFFFF
	}
	if (m_data->m_shareUpgrades && !(me->getUpgradeMask() == m_upgradeMask)) // RW 0x862520
	{
		shareUpgrades(*slave);
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8879C5 (the destructor)
void SlaveWatcherBehavior::onDelete()
{
	if (m_data->m_letSlaveLive || m_slaveID == INVALID_ID)
	{
		return;
	}
	if (Object *slave = getObject()->logic().findObjectByID(m_slaveID))
	{
		slave->kill(DAMAGE_FORCE, 13); // RW 0x698EC3(0, 0xD)
	}
}

void SlaveWatcherBehavior::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_slaveID);
	for (unsigned i = 0; i < UpgradeMaskType::activeWords(); ++i)
	{
		h.addU32(m_upgradeMask.words[i]);
	}
	h.addU32(m_releases);
}

// RW 0x8878FF's fields (OpenBFME native snapshot, S-4190)
void SlaveWatcherBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferObjectID(&m_slaveID);
	xferUpgradeMask(x, m_upgradeMask, "a slave watcher's upgrade mask"); // RW 0x707CA0
	x.xferUnsignedInt(&m_releases);
}
