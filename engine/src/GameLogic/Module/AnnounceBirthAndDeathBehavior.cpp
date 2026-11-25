// OpenBFME. GPL-3.0.
// See GameLogic/Module/AnnounceBirthAndDeathBehavior.h (lane MODGAP-2; editions: both RotWK 2.01 and BFME2 1.06).

#include "GameLogic/Module/AnnounceBirthAndDeathBehavior.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"

#include <stdexcept>

// RW 0x63F3BF / BFME2 0x4B3FD0
void AnnounceBirthAndDeathBehaviorModuleData::buildFieldParse(MultiIniFieldParse &/*p*/)
{
}

// RW 0x8883B1 / BFME2 0x885125
AnnounceBirthAndDeathBehavior::AnnounceBirthAndDeathBehavior(Thing *thing, const AnnounceBirthAndDeathBehaviorModuleData *data)
	: BehaviorModule(thing, data)
	, m_needToRunOnBuildComplete(true)
{
}

// RW 0x888439 / BFME2 0x8851AD
void AnnounceBirthAndDeathBehavior::onCreate()
{
	++m_birthAnnouncements;
	m_lastNotificationType = kHeroInitialSpawnType;
	if (Object *obj = getObject())
	{
		obj->logic().noteStop(stopLine());
	}
}

// RW 0x6507E3 / BFME2 0x650A49
void AnnounceBirthAndDeathBehavior::onBuildComplete()
{
	m_needToRunOnBuildComplete = false;
}

// RW 0x888453 / BFME2 0x8851C7
void AnnounceBirthAndDeathBehavior::onDie(const DieModuleInterface::Event &/*event*/)
{
	++m_deathAnnouncements;
	m_lastNotificationType = kHeroDeathType;
	if (Object *obj = getObject())
	{
		obj->logic().noteStop(stopLine());
	}
}

void AnnounceBirthAndDeathBehavior::crc(StateHasher &h) const
{
	BehaviorModule::crc(h);
	h.addBool(m_needToRunOnBuildComplete);
	h.addI32(m_birthAnnouncements);
	h.addI32(m_deathAnnouncements);
}

// RW 0x8BD56C / BFME2 0x8851E3
void AnnounceBirthAndDeathBehavior::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferBehaviorModule(x);
	x.xferBool(&m_needToRunOnBuildComplete);
	x.xferInt(&m_birthAnnouncements);
	SnapshotXfer::checkRange(m_birthAnnouncements, 0, 0x7FFFFFFF, "AnnounceBirthAndDeathBehavior m_birthAnnouncements");
	x.xferInt(&m_deathAnnouncements);
	SnapshotXfer::checkRange(m_deathAnnouncements, 0, 0x7FFFFFFF, "AnnounceBirthAndDeathBehavior m_deathAnnouncements");
	x.xferAsciiString(&m_lastNotificationType);
}

// RW 0x6A03BA .. 0x6A03DD / BFME2 0x69F965 .. 0x69F988
bool AnnounceBirthAndDeathBehavior::shouldAnnounceHeroEvent(const Object &obj, const Player *localPlayer)
{
	if (!obj.isKindOfName("HERO"))
	{
		return false;
	}
	if (localPlayer != nullptr && obj.getControllingPlayer() != localPlayer)
	{
		return false;
	}
	return true;
}

const char *AnnounceBirthAndDeathBehavior::stopLine()
{
	return "[S-6012] AnnounceBirthAndDeathBehavior: InGameUI notification box (TheInGameUI + 0x9CC; RW 0x6A03A9 / BFME2 0x69F954) is not ported; hero birth and death announcements are recorded in logic without UI popup";
}

// RW 0x6592CA / BFME2 0x658038
void AnnounceBirthAndDeathBehavior::registerClass(ModuleFactory &factory)
{
	factory.bindTypedData<AnnounceBirthAndDeathBehaviorModuleData>("AnnounceBirthAndDeathBehavior", MODULETYPE_BEHAVIOR);
	factory.bindModuleProc(
		"AnnounceBirthAndDeathBehavior",
		MODULETYPE_BEHAVIOR,
		[](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
			const auto *typed = dynamic_cast<const AnnounceBirthAndDeathBehaviorModuleData *>(data);
			if (!typed)
			{
				throw std::logic_error("AnnounceBirthAndDeathBehavior: the module data is not typed");
			}
			return std::make_unique<AnnounceBirthAndDeathBehavior>(thing, typed);
		});
}
