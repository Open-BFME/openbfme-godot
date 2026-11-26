// OpenBFME. GPL-3.0.
// See GameLogic/AI/AIRampageState.h for the sources and what is not ported.

#include "GameLogic/AI/AIRampageState.h"

#include "Common/StateHash.h"
#include "Common/Xfer.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponState.h"

namespace
{
const int kWeaponSetRampage = 8;                 // RW 0x74D74A push 8 -> 0x691059 (WeaponSetFlags RAMPAGE)
const std::uint32_t kAllPlayers = 0xFFFFFu;      // RW 0x74D769 push 0xFFFFF
} // namespace

unsigned AIUpdateInterface::rampageTime() const
{
	// RW 0x664D5D: data + 0x3C (RampageRequiresAflame) with the object (+ 8) not AFLAME (RW 0x44DDEC(10)) -> 0; else data + 0x38 (RampageTime)
	static const int kAflame = CombatNames::status("AFLAME");
	if (m_combat.rampageRequiresAflame && !getObject()->testStatus((unsigned)kAflame))
	{
		return 0;
	}
	return m_combat.rampageTime;
}

bool AIUpdateInterface::aiRampage(CommandSourceType source)
{
	if (!acceptCommand(source, 0x3A))
	{
		return false; // RW 0x66A826: aiDoCommand's gate RW 0x667174
	}
	// RW 0x66AFD8 -> AI vslot 0x150, privateRampage RW 0x662EB1
	if (m_machine->currentStateId() == (unsigned)AI_RAMPAGE)
	{
		return false; // RW 0x662EC8: -1, nothing changes
	}
	m_machine->clear();             // machine vslot 0x14
	setLastCommandSource(source);   // AI + 0x48
	m_machine->setState(AI_RAMPAGE); // machine vslot 0x20 (0x2D)
	return true;
}

// RW 0x74D6EA
StateReturnType AIRampageState::onEnter()
{
	Object &obj = owner();
	AIUpdateInterface &a = ai();
	a.chooseLocomotorSet(LOCOMOTORSET_ENRAGED); // AI vslot 0x238(8)
	AIWorld &w = a.world();
	if (w.mapReady())
	{
		w.pathfinder().removeGoal(a.adapter()); // RW 0x68B401
	}
	// RW 0x74D73E .. 0x74D743: snapPosition (RW 0x6EF225) of a copy of the position; the answer is dropped (S-202)
	a.setScriptLock(true);          // + 0x3C6
	a.setPreventPlayerCommands(true); // + 0x3C5
	obj.setWeaponSetFlag(kWeaponSetRampage, true); // RW 0x691059(8)
	obj.logic().deselectObject(obj, kAllPlayers, true); // RW 0x625759(obj, 0xFFFFF, 1)
	obj.setStatus((unsigned)CombatNames::status("UNSELECTABLE"), true); // RW 0x62684D(3, 1)
	obj.setStatus((unsigned)CombatNames::status("RAMPAGING"), true);    // RW 0x62684D(0x39, 1)
	const unsigned time = a.rampageTime(); // RW 0x664D5D
	const unsigned now = obj.logic().getFrame();
	m_endFrame = now + time; // + 0x24
	const unsigned eject = a.combatSettings().timeToEjectPassengersOnRampage; // data + 0x48
	if (eject == 0)
	{
		m_ejectFrame = kForever; // RW 0x74D7AA
	}
	else
	{
		m_ejectFrame = time != 0 ? now + eject : now; // RW 0x74D7B0 .. 0x74D7BE
	}
	return STATE_CONTINUE;
}

// RW 0x74D826
void AIRampageState::stomp(Object &obj)
{
	ObjectWeapons *w = obj.getWeapons();
	Weapon *cur = w ? w->currentWeapon() : nullptr; // RW 0x68B58C
	if (!cur)
	{
		return;
	}
	const int status = w->weaponStatusWriteBack(*cur); // RW 0x6CDCE7
	if (m_firing)
	{
		if (status == WEAPON_PRE_ATTACK)
		{
			return;
		}
		if (status == WEAPON_READY_TO_FIRE)
		{
			w->setFiringConditionForCurrentWeapon(); // RW 0x69036C
			const Coord3D at = *obj.getPosition();
			w->fireCurrentWeapon(nullptr, &at); // RW 0x6920AD(obj + 0x38)
			return;
		}
		if (status == WEAPON_FIRING)
		{
			m_firing = false; // RW 0x74D873
		}
		return;
	}
	if (status == WEAPON_READY_TO_FIRE)
	{
		m_firing = true; // RW 0x74D883
		obj.setStatus((unsigned)CombatNames::statuses().isFiringWeapon, true); // RW 0x62684D(0xD, 1)
	}
}

// RW 0x75187A
StateReturnType AIRampageState::update()
{
	Object &obj = owner();
	stomp(obj);
	const unsigned now = obj.logic().getFrame();
	if (m_ejectFrame <= now)
	{
		if (ContainModuleInterface *c = obj.getContain())
		{
			c->startRidersFalling(); // contain slot 0xF8 (RW 0x7518AF)
		}
		m_ejectFrame = kForever;
	}
	if (m_endFrame <= now)
	{
		rampageRunOffMapOrDie(obj); // RW 0x699282
		return STATE_SUCCESS;
	}
	return STATE_CONTINUE;
}

// RW 0x74D7C7
void AIRampageState::onExit(StateExitType)
{
	AIUpdateInterface &a = ai();
	if (a.isDestroying())
	{
		return; // the object is being deleted: its modules are going away (as every state's exit in the port)
	}
	Object &obj = owner();
	obj.setStatus((unsigned)CombatNames::status("RAMPAGING"), false); // RW 0x62684D(0x39, 0)
	obj.setWeaponSetFlag(kWeaponSetRampage, false);                    // RW 0x691106(8)
	a.setPreventPlayerCommands(false); // + 0x3C5
	a.setScriptLock(false);            // + 0x3C6
	static const int kCharging = CombatNames::modelCondition("CHARGING"); // Object + 0x11C bit 4
	if (!obj.testModelCondition(kCharging))
	{
		obj.setModelConditionState(kCharging, true); // RW 0x74D808 .. 0x74D811
	}
	obj.kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x698EC3(8, 0)
}

void AIRampageState::crc(StateHasher &h) const
{
	h.addBool(m_firing);
	h.addU32(m_endFrame);
	h.addU32(m_ejectFrame);
}

void AIRampageState::crcPersistent(StateHasher &h) const
{
	// + 0x20 is the one field kept between activations (onEnter rewrites + 0x24 / + 0x28): hashed only when it differs from the constructor's, so a unit
	// that never rampaged hashes as before
	if (m_firing)
	{
		h.addI32(0x2D);
	}
}

// RW 0x743E6C (state slot 0x0C): version 1, + 0x20, + 0x24, + 0x28
void AIRampageState::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&m_firing);
	x.xferUnsignedInt(&m_endFrame);
	x.xferUnsignedInt(&m_ejectFrame);
}

bool rampageRunOffMapOrDie(Object &obj)
{
	// RW 0x699282: the first behavior module answering the +0xC interface's slot 0x8C (RunOffMapBehavior RW 0x863BF3, the only one in 2.01)
	// RW 0x6992AF .. 0x6992E3: the body's last damage record (vt[0x40], its + 8 the source) scores the kill (RW 0x6955BC(obj, 1))
	if (ActiveBody *ab = dynamic_cast<ActiveBody *>(obj.getBodyModule()))
	{
		if (Object *killer = obj.logic().findObjectByID(ab->lastDamager()))
		{
			killer->scoreTheKill(obj, 1);
		}
	}
	obj.kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x69931F .. 0x699325: RW 0x698EC3(8, 0)
	return false;
}
