// OpenBFME. GPL-3.0.
// See GameLogic/Module/SMCHelper.h.

#include "GameLogic/Module/SMCHelper.h"

#include "Common/StateHash.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"

SMCHelper::SMCHelper(Thing *thing)
	: ObjectHelperShell(thing, "SMCHelper", "ModuleTag_SMCHelper")
{
}

// RW 0x8E2ACA
UpdateSleepTime SMCHelper::calcSleep() const
{
	if (m_list.empty())
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const UnsignedInt now = getObject()->logic().getFrame();
	UnsignedInt soonest = 0x5F5E0FFu; // RW 0x8E2AEA: the initial minimum
	for (const Entry &e : m_list)
	{
		if (e.expiry < soonest)
		{
			soonest = e.expiry;
		}
	}
	const int delta = (int)(soonest - now);
	return delta > 0 ? UPDATE_SLEEP(delta) : UPDATE_SLEEP_NONE;
}

// RW 0x8E2C0F
void SMCHelper::setSpecialModelConditionState(int bit, UnsignedInt frames)
{
	if (bit < 0 || bit >= 0x24F)
	{
		return;
	}
	Object *obj = getObject();
	const UnsignedInt now = obj->logic().getFrame();
	bool found = false;
	for (Entry &e : m_list)
	{
		if (e.bit == bit)
		{
			const UnsignedInt candidate = now + frames;
			if (candidate > e.expiry)
			{
				e.expiry = candidate;
			}
			found = true;
			break;
		}
	}
	if (!found)
	{
		m_list.push_back(Entry{ bit, now + frames });
		obj->setModelConditionState(bit, true);
	}
	setWakeFrame(obj, calcSleep());
}

bool SMCHelper::hasSpecialModelConditionState(int bit) const
{
	const UnsignedInt now = getObject()->logic().getFrame();
	for (const Entry &e : m_list)
	{
		if (e.bit == bit && e.expiry > now)
		{
			return true;
		}
	}
	return false;
}

// RW 0x8E2B72
UpdateSleepTime SMCHelper::update()
{
	Object *obj = getObject();
	const UnsignedInt now = obj->logic().getFrame();
	for (auto it = m_list.begin(); it != m_list.end();)
	{
		if (now >= it->expiry)
		{
			obj->setModelConditionState(it->bit, false);
			it = m_list.erase(it);
		}
		else
		{
			++it;
		}
	}
	return calcSleep();
}

void SMCHelper::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32((std::uint32_t)m_list.size());
	for (const Entry &e : m_list)
	{
		hasher.addI32(e.bit);
		hasher.addU32(e.expiry);
	}
}

void SMCHelper::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	ObjectHelperShell::xfer(x);
	SnapshotXfer::each(x, m_list, 8, [&](Entry &e) {
		x.xferInt(&e.bit);
		SnapshotXfer::checkRange(e.bit, 0, SnapshotXfer::modelConditionCount() - 1, "a special model condition's bit"); // lane SAVE-4e
		x.xferUnsignedInt(&e.expiry);
	});
}
