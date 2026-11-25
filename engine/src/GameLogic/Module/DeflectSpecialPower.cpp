// OpenBFME. GPL-3.0.
// See GameLogic/Module/DeflectSpecialPower.h for the target facts and the addresses.

#include "GameLogic/Module/DeflectSpecialPower.h"
#include "GameLogic/SnapshotXfer.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Object.h"

#include <stdexcept>

namespace
{
const char *const kStop =
	"[S-5601] DeflectSpecialPower (RW 0x8C9A86 update, 0x8C99DB initiate, 0x8C9A3C onCollide) is ported whole, but nothing in the data starts it (the shield wall button "
	"is commented out) and the deflectable projectiles are NO_COLLIDE, which the collision manager does not pair (RW 0x68E31F): its onCollide is called only by the AI "
	"units' contact pass, where it does nothing. INFERENCE from the data";

// RW 0x68BE97: the object's locomotor speed (RW 0x68B34C: the current locomotor's speed per frame when above zero, else 0) is above 0.1 (RW 0xBD83D4)
bool isMoving(Object &obj)
{
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	const Locomotor *loco = ai ? ai->curLocomotor() : nullptr;
	const float speed = loco && loco->speed() > 0.0f ? loco->speed() : 0.0f;
	return speed > 0.1f;
}

void setCondition(Object &obj, int bit, bool on)
{
	if (obj.testModelCondition(bit) != on) // RW: test the bit, change it, notify (RW 0x68B53C)
	{
		obj.setModelConditionState(bit, on);
	}
}
} // namespace

void DeflectSpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerUpdateBaseModuleData::buildFieldParse(p); // RW 0x9911B3; the own table (RW 0xC84858) is empty
}

DeflectSpecialPower::DeflectSpecialPower(Thing *thing, const DeflectSpecialPowerModuleData *data)
	: SpecialPowerUpdateBase(thing, data)
{
}

const char *DeflectSpecialPower::stopLine()
{
	return kStop;
}

DeflectSpecialPower *DeflectSpecialPower::of(const Object &obj)
{
	return dynamic_cast<DeflectSpecialPower *>(obj.findModule("DeflectSpecialPower"));
}

// RW 0x8C99DB
void DeflectSpecialPower::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *, Object *, const Coord3D *, unsigned, int)
{
	if (m_active || m_pending)
	{
		return;
	}
	m_pending = true;
	getObject()->logic().noteStop(kStop); // something started the power: its projectile contact is the stop's
	setWakeFrame(getObject(), UPDATE_SLEEP(1)); // RW 0x850C32(object, 1)
}

// RW 0x8C9A86
UpdateSleepTime DeflectSpecialPower::update()
{
	static const int kDeflecting = CombatNames::modelCondition("DEFLECT_SPECIAL_POWER"); // 158: word 4 bit 30
	if (!m_active && !m_pending)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	Object *obj = getObject();
	if (isMoving(*obj))
	{
		if (!m_active)
		{
			return UPDATE_SLEEP_NONE; // the power waits until the unit stands
		}
		m_active = false;
		setCondition(*obj, kDeflecting, false);
		return UPDATE_SLEEP_FOREVER;
	}
	if (m_pending && !m_active)
	{
		m_pending = false;
		m_active = true;
		setCondition(*obj, kDeflecting, true);
	}
	return UPDATE_SLEEP_NONE;
}

// RW 0x8C9A3C
void DeflectSpecialPower::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	static const unsigned kDeflectable = (unsigned)CombatNames::kindOf("DEFLECT_BY_SPECIAL_POWER"); // 102: template byte + 0x114 bit 6
	if (!m_active || !other || !other->isKindOf(kDeflectable))
	{
		return;
	}
	for (const std::unique_ptr<BehaviorModule> &m : other->modules())
	{
		if (ProjectileUpdateInterface *projectile = m->getProjectileUpdateInterface()) // behavior slot 18
		{
			projectile->projectileNowJammed(); // slot 5, RW 0x85E54D; the owner passed is not read
		}
	}
}

void DeflectSpecialPower::crc(StateHasher &h) const
{
	SpecialPowerUpdateBase::crc(h);
	h.addBool(m_active);
	h.addBool(m_pending);
}

// RW 0x008C99FB: base xfer (RW 0x009912DB), early return on XFER_CRC (at 0x008C9A0D .. 0x008C9A12), else version 1 with m_active and m_pending
void DeflectSpecialPower::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferSpecialPowerUpdateBase(x);
	if (x.getXferMode() == XFER_CRC)
	{
		return; // 0x008C9A0D .. 0x008C9A12 inside RW 0x008C99FB
	}
	x.xferBool(&m_active);
	x.xferBool(&m_pending);
}

// RW 0x00651E79 (create), RW 0x008C9939 (getModuleName), RW 0x008C997A (getModuleNameKey)
void DeflectSpecialPower::registerClass(ModuleFactory &modules)
{
	const char *name = "DeflectSpecialPower";
	modules.bindTypedData<DeflectSpecialPowerModuleData>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const DeflectSpecialPowerModuleData *typed = dynamic_cast<const DeflectSpecialPowerModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<DeflectSpecialPower>(thing, typed);
	});
}
