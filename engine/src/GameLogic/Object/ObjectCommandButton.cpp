// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0. See GameLogic/Object/ObjectCommandButton.h.

#include "GameLogic/Object/ObjectCommandButton.h"

#include "Common/SpecialPower.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/WeaponSetToggle.h"

namespace
{

const unsigned kFiredByScript = 0x40000u; // RW 0x696FD2: options | 0x40000 (ZH COMMAND_FIRED_BY_SCRIPT)

const SpecialPowerTemplate *powerOf(const CommandButton &b) // the button's + 0x44
{
	return TheSpecialPowerStore && !b.m_specialPowerName.empty() ? TheSpecialPowerStore->findSpecialPowerTemplate(b.m_specialPowerName) : nullptr;
}

// the commands RW 0x696FD2 has a case for (jump table): the port runs 14, 24, 38, 45 and 58
bool hasRetailCase(int command)
{
	switch (command)
	{
	case 1: case 3: case 6: case 7: case 8: case 14: case 16: case 17: case 23: case 24: case 27: case 33: case 34: case 35: case 36: case 38: case 39:
	case 40: case 42: case 43: case 44: case 45: case 46: case 49: case 50: case 51: case 52: case 53: case 58: case 59: case 60:
		return true;
	default:
		return false;
	}
}

} // namespace

bool ObjectCommandButton::doCommandButton(Object &obj, const CommandButton *b, CommandSourceType source)
{
	if (obj.getDisabledMask() != 0 || !b)
	{
		return true; // RW 0x9325B4 (+ 0xA0 bit 6 read as clear)
	}
	AIUpdateInterface *ai = obj.getAIUpdateInterface(); // + 0x260
	switch (b->m_command)
	{
	case GUI_COMMAND_SPECIAL_POWER:
	case GUI_COMMAND_SPELL_BOOK:
		if (const SpecialPowerTemplate *t = powerOf(*b))
		{
			SpecialPowerModules::doSpecialPower(obj, t, b->m_options | kFiredByScript, source == CMD_FROM_SCRIPT); // RW 0x68E5A3
		}
		return true;
	case GUI_COMMAND_STOP:
		if (ai)
		{
			ai->aiIdle(source); // RW 0x5E821A
		}
		return true;
	case GUI_COMMAND_TOGGLE_WEAPONSET:
		WeaponSetToggle::toggleFromCommandButton(obj); // case 0x2D
		return true;
	case GUI_COMMAND_SET_STANCE:
		if (StancesBehavior *s = StancesBehavior::of(obj)) // case 0x3A: the first stance (RW 0x75D324(0))
		{
			if (!b->m_stances.empty() && b->m_stances[0] >= 0 && b->m_stances[0] < STANCE_COUNT)
			{
				s->setStance((StanceType)b->m_stances[0], "ObjectCommandButton:SET_STANCE");
			}
		}
		return true;
	default:
		return !hasRetailCase(b->m_command);
	}
}

bool ObjectCommandButton::doCommandButtonAtObject(Object &obj, const CommandButton *b, Object *target, CommandSourceType source)
{
	if (obj.getDisabledMask() != 0 || !b)
	{
		return true; // RW 0x697890: no + 0xA0 exception
	}
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	switch (b->m_command)
	{
	case GUI_COMMAND_STOP:
		if (ai)
		{
			ai->aiIdle(source);
		}
		return true;
	case GUI_COMMAND_SPECIAL_POWER:
	case GUI_COMMAND_SPELL_BOOK:
		if (const SpecialPowerTemplate *t = powerOf(*b))
		{
			SpecialPowerModules::doSpecialPowerAtObject(obj, t, target, b->m_options | kFiredByScript, source == CMD_FROM_SCRIPT); // RW 0x68E67A
		}
		return true;
	case GUI_COMMAND_HIJACK_VEHICLE: // 29
		if (ai && target)
		{
			ai->aiEnterObject(target, source); // RW 0x66C5A4
		}
		return true;
	case GUI_COMMAND_FIRE_WEAPON: // 23: RW 0x68C91B, the weapon lock, aiAttackObject(target, 1 shot)
		return false;
	default:
		return true;
	}
}

bool ObjectCommandButton::doCommandButtonAtPosition(Object &obj, const CommandButton *b, const Coord3D &pos, CommandSourceType source)
{
	if (obj.getDisabledMask() != 0 || !b)
	{
		return true; // RW 0x6979D9
	}
	AIUpdateInterface *ai = obj.getAIUpdateInterface();
	switch (b->m_command)
	{
	case GUI_COMMAND_STOP:
		if (ai)
		{
			ai->aiIdle(source);
		}
		return true;
	case GUI_COMMAND_SPECIAL_POWER:
	case GUI_COMMAND_SPELL_BOOK:
		if (const SpecialPowerTemplate *t = powerOf(*b))
		{
			SpecialPowerModules::doSpecialPowerAtLocation(obj, t, pos, b->m_options | kFiredByScript, source == CMD_FROM_SCRIPT); // RW 0x68E754
		}
		return true;
	case GUI_COMMAND_ATTACK_MOVE:     // 10: RW 0x696266(pos, the button's max shots, source)
	case GUI_COMMAND_FIRE_WEAPON:     // 23
	case GUI_COMMAND_DOZER_CONSTRUCT: // 53: TheBuildAssistant vslot 0x38 buildObjectNow
		return false;
	default:
		return true;
	}
}
