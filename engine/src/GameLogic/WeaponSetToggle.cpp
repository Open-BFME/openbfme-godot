// OpenBFME. GPL-3.0.
// See WeaponSetToggle.h.

#include "GameLogic/WeaponSetToggle.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AICommands.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <stdexcept>

namespace
{
// RW 0xC16958: TheWeaponConditionNames index -> TheModelConditionNames index (-1: none), 104 entries
const int kWeaponSetModelCondition[] = {
	12, 13, 14, 17, 15, 16, 143, 159, 161, 192, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 241, 247, 250, 251, 301, 302, 303, 304, 253, 409, 410, 411, 412, 413,
	414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440, 259, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, 553, 554, 555, 556, 557, 558, 559, 560, 561, 562, 563, 564, 565, 566, 567, 568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579, 580,
	581, 582, 583, 584
};
constexpr int kModelConditionToggle1 = 0x12D;   // WEAPONSET_TOGGLE_1 .. 3 = 0x12D .. 0x12F
constexpr int kModelConditionSwapping1 = 0x1BD; // SWAPPING_TO_WEAPONSET_1 .. 3 = 0x1BD .. 0x1BF
constexpr int kModelConditionAttacking = 0x25;  // ATTACKING (RW 0x694154(0x25))
constexpr unsigned kLogicFramesPerSecond = 5;   // RW 0xD9F608

bool flagOf(const Object &obj, int bit)
{
	const WeaponConditionFlags &f = obj.getWeaponSetFlags();
	return ((f[(size_t)bit >> 5] >> (bit & 31)) & 1u) != 0;
}

// RW 0x694154: the object has the model condition, or its container has
bool conditionOrContainer(const Object &obj, int bit)
{
	if (obj.testModelCondition(bit))
	{
		return true;
	}
	const Object *c = obj.getContainedBy();
	return c && c->testModelCondition(bit);
}

bool isHobbit(const ThingTemplate *tt)
{
	static const int kHobbit = ObjectTemplateInfoBuilder::kindOfIndex("HOBBIT"); // template + 0x118 bit 1 (mask at + 0x108)
	return tt && MaskTest(ObjectTemplateInfoBuilder::build(*tt->getFinalOverride()).kindOf, (unsigned)kHobbit);
}

// RW 0x6945DA / 0x694636: the members of the object's horde, then the horde; RW 0x691059 / 0x691106 on the object itself when it has no horde
void setOnSelfOrHorde(Object &obj, bool on)
{
	using WeaponSetToggle::WEAPONSET_TOGGLE_1;
	Object *horde = obj.getHordeObject(false); // RW 0x693A1A(0)
	if (!horde)
	{
		WeaponSetToggle::setObjectWeaponSetFlag(obj, WEAPONSET_TOGGLE_1, on);
		return;
	}
	ContainModuleInterface *contain = horde->getContain();
	if (!contain || !contain->getHordeContainInterface()) // RW 0x68C866: no horde interface, nothing at all
	{
		return;
	}
	if (const ContainModuleInterface::ContainedItemsList *members = contain->getContainedItemsList())
	{
		for (Object *m : *members) // slot 0x108's list, in order
		{
			if (m)
			{
				WeaponSetToggle::setObjectWeaponSetFlag(*m, WEAPONSET_TOGGLE_1, on);
			}
		}
	}
	WeaponSetToggle::setObjectWeaponSetFlag(*horde, WEAPONSET_TOGGLE_1, on);
}
} // namespace

const WeaponConditionFlags &Object::getWeaponSetFlags() const
{
	return m_weapons ? m_weapons->weaponSetFlags() : m_weaponlessSetFlags;
}

// RW 0x691059 (on) / 0x691106 (off)
void Object::setWeaponSetFlag(int bit, bool on)
{
	if (bit < 0 || bit >= 128)
	{
		throw std::logic_error("Object::setWeaponSetFlag: bit out of range");
	}
	if (m_weapons)
	{
		m_weapons->setWeaponSetFlag(bit, on); // + 0x38C and WeaponSet::updateWeaponSet (RW 0x6C99E2)
	}
	else
	{
		std::uint32_t &word = m_weaponlessSetFlags[(size_t)bit >> 5];
		word = on ? (word | (1u << (bit & 31))) : (word & ~(1u << (bit & 31)));
	}
	const int mc = WeaponSetToggle::modelConditionForWeaponSetBit(bit);
	if (mc >= 0 && testModelCondition(mc) != on)
	{
		setModelConditionState(mc, on); // RW 0x68B53C after the word change
	}
	if (mc >= kModelConditionToggle1 && mc <= kModelConditionToggle1 + 2)
	{
		setSpecialModelConditionState(kModelConditionSwapping1 + (mc - kModelConditionToggle1), kLogicFramesPerSecond); // RW 0x8E2C0F
	}
}

namespace WeaponSetToggle
{
int modelConditionForWeaponSetBit(int bit)
{
	const int n = (int)(sizeof(kWeaponSetModelCondition) / sizeof(kWeaponSetModelCondition[0]));
	return bit >= 0 && bit < n ? kWeaponSetModelCondition[bit] : -1;
}

void setObjectWeaponSetFlag(Object &obj, int bit, bool on)
{
	obj.setWeaponSetFlag(bit, on);
}

bool isRestrictedByContain(const Object &obj)
{
	ContainModuleInterface *c = obj.getContain(); // + 0x258
	return c && !c->getHordeContainInterface() && c->getContainCount() != 0; // slots 0x7C / 0x114
}

void registerHandlers(GameLogicDispatch &dispatch)
{
	dispatch.registerHandler(MSG_WEAPONSET_TOGGLE, "HUD-4", [](GameLogic &logic, const GameMessage &m) {
		const GameMessageArgument *a = m.getArgument(0);
		if (!a || a->type != ARGUMENTDATATYPE_OBJECTID)
		{
			return false;
		}
		const Object *ref = a->objectID != (ObjectID)INVALID_ID ? logic.findObjectByID(a->objectID) : nullptr;
		const bool eachDecides = ref == nullptr;
		bool wanted = ref ? !flagOf(*ref, WEAPONSET_TOGGLE_1) : false;
		for (ObjectID id : AICommands::selection(logic, m.getPlayerIndex())) // the player's group (RW 0x771A36), in order
		{
			Object *obj = logic.findObjectByID(id);
			if (!obj || isRestrictedByContain(*obj))
			{
				continue;
			}
			if (ref && !ObjectFilterMatch::isEquivalentTo(obj->getTemplate(), ref->getTemplate()) && !(isHobbit(obj->getTemplate()) && isHobbit(ref->getTemplate())))
			{
				continue;
			}
			const bool own = flagOf(*obj, WEAPONSET_TOGGLE_1);
			if (eachDecides)
			{
				wanted = !own;
			}
			else if (own == wanted)
			{
				continue;
			}
			setOnSelfOrHorde(*obj, wanted);
			AIUpdateInterface *ai = obj->getAIUpdateInterface(); // + 0x260
			if (!ai || ai->isMoving())
			{
				continue;
			}
			if (conditionOrContainer(*obj, kModelConditionAttacking))
			{
				ai->aiIdle(CMD_FROM_AI); // RW 0x5E821A
				obj->setStatus(OBJECT_STATUS_IGNORE_AI_COMMAND, true); // RW 0x6907BD
				obj->setIgnoreAICommandUntil(logic.getFrame() + kLogicFramesPerSecond);
			}
			if (ObjectWeapons *w = obj->getWeapons()) // + 0x248
			{
				w->resetFiringTracker(true); // RW 0x8E302A(1)
			}
		}
		return true;
	});
}

std::vector<std::string> acceptanceStops()
{
	return {
		"[S-1672] weapon set toggle (HUD-4): MSG_WEAPONSET_TOGGLE (RW 0x77B529), the button's message (RW 0x9412C8) and availability (RW 0x942FC6) and "
		"Object::setWeaponSetFlag / clearWeaponSetFlag (RW 0x691059 / 0x691106: the model condition of RW 0xC16958, SWAPPING_TO_WEAPONSET_n) are ported; "
		"INFERENCE: the group is the player's selection minus the objects that are gone or not his (AICommands::selection); BINARY FACT: the toggle button shows "
		"its second ButtonImage while the object has a FlagsUsedForToggle flag (RW 0x75D22E updates the image index); not ported: the immediate image update "
		"on the press itself (client), the other callers of the weapon set flag setters still skip the model condition",
	};
}
} // namespace WeaponSetToggle
