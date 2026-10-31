// OpenBFME. GPL-3.0. See EvaDamageNotice.h (lane EVADAMAGE-1).

#include "GameLogic/Object/EvaDamageNotice.h"

#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/System/InvisibilityManager.h"

namespace
{
const FieldValue *field(const ThingTemplate &tt, const char *name)
{
	return tt.getFinalOverride()->findField(name);
}

int statusBit(const char *name)
{
	return ObjectTemplateInfoBuilder::objectStatusIndex(name);
}

bool hasStatus(const Object &o, int bit)
{
	return bit >= 0 && o.testStatus((unsigned)bit);
}

// RW 0x6ADBEB: `player`'s relationship to `other`'s default team (Player + 0x30C)
Relationship relationTo(const Player &player, const Player &other)
{
	return player.getRelationship(other.getDefaultTeam());
}

// RW 0x694154: the object has the model condition, or its horde (RW 0x693A1A(0): itself when HORDE, else its HORDE container) has it
bool conditionOnSelfOrHorde(const Object &o, int bit)
{
	if (o.testModelCondition(bit))
	{
		return true;
	}
	const Object *horde = o.getHordeObject(false);
	return horde && horde->testModelCondition(bit);
}
} // namespace

std::string EvaDamage::templateEvent(const ThingTemplate &tt, const char *name)
{
	// RW 0x5DE588 (ContainParseHooks.h): the name, "None" (stricmp) -1; an unset field keeps the ctor's -1 (RW 0x73FB7A)
	const FieldValue *f = field(tt, name);
	std::string s;
	if (const RawTokens *raw = f ? std::get_if<RawTokens>(f) : nullptr)
	{
		s = raw->tokens.empty() ? std::string() : raw->tokens.front();
	}
	else if (const std::string *str = f ? std::get_if<std::string>(f) : nullptr)
	{
		s = *str;
	}
	return AsciiStringUtil::compareNoCase(s, "None") == 0 ? std::string() : s;
}

float EvaDamage::scanRange(const ThingTemplate &tt)
{
	// parseReal (RW 0x42ED00) into + 0x5A0; the ctor's 200.0f (RW 0xBE4170)
	const FieldValue *f = field(tt, "EvaEventSecondDamageFarFromFirstScanRange");
	const float *v = f ? std::get_if<float>(f) : nullptr;
	return v ? *v : 200.0f;
}

UnsignedInt EvaDamage::timeoutFrames(const ThingTemplate &tt)
{
	// parseDurationUnsignedInt (RW 0x73A429) into + 0x5A4; the ctor's LOGICFRAMES_PER_SECOND (RW 0xD9F608) x 30
	const FieldValue *f = field(tt, "EvaEventSecondDamageFarFromFirstTimeoutMS");
	const long long *v = f ? std::get_if<long long>(f) : nullptr;
	return v ? (UnsignedInt)*v : (UnsignedInt)LOGICFRAMES_PER_SECOND * 30u;
}

bool EvaDamage::isVisibleTo(const GameLogic &logic, const Object &obj, const Player *player, bool enemyOnly)
{
	// RW 0x694CCC (Ghidra, named Object_isVisibleTo by this lane). `player` is RW's argument (pushed, then the ECX of RW 0x6ADBEB); `enemyOnly` is its mode 1
	static const int kStealthed = statusBit("STEALTHED"), kDetected = statusBit("DETECTED"); // 0xF, 0x11
	static const int kHorde = ObjectTemplateInfoBuilder::kindOfIndex("HORDE");          // template + 0x115 & 0x20
	static const int kDisguiser = ObjectTemplateInfoBuilder::kindOfIndex("DISGUISER");  // template + 0x113 & 1
	const Player *owner = obj.getControllingPlayer(); // RW 0x68B678
	// RW 0x694CD1 .. 0x694CFC: mode 1 and the owner ALLIES (2) to the player: not visible
	if (enemyOnly && owner && player && relationTo(*player, *owner) == ALLIES)
	{
		return false;
	}
	// mode 1's enemy test (RW 0x694D2E / 0x694ED3): an owner the player calls ENEMIES (0)
	const auto enemyOfPlayer = [&]() { return owner && player && relationTo(*player, *owner) == ENEMIES; };
	const bool hidden = InvisibilityManager::isInvisible(obj) || (hasStatus(obj, kStealthed) && !hasStatus(obj, kDetected)); // RW 0x694D01 .. 0x694D28
	if (hidden)
	{
		// RW 0x694E87: only a DISGUISER may still be seen: disguised (StealthUpdate RW 0x68FBD3 + 0x3C) as a player's unit (+ 0x38, RW 0x6A844E): seen
		// when the player calls that player ENEMIES; else as itself (mode 1: an enemy)
		if (kDisguiser < 0 || !obj.isKindOf((unsigned)kDisguiser))
		{
			return false;
		}
		const StealthUpdate *su = StealthUpdate::of(obj);
		const Player *disguise = su && su->disguiseTemplate() ? logic.players().getNthPlayer(su->disguisePlayerIndex()) : nullptr;
		if (player && disguise)
		{
			return relationTo(*player, *disguise) == ENEMIES;
		}
		return !enemyOnly || enemyOfPlayer();
	}
	if (enemyOnly && !enemyOfPlayer())
	{
		return false;
	}
	const ContainModuleInterface *contain = obj.getContain(); // + 0x258
	// RW 0x694D50 .. 0x694DE3: a HORDE whose contain answers vslot 0x78 is seen unless it has members (that answer's slot 0x108 list) and every member is
	// hidden (invisible (RW 0x68FC2F), STEALTHED, not detected (RW 0x693BF2), not DETECTED). INFERENCE (S-7090): slot 0x78 is read as the horde interface
	// (getHordeContainInterface, slot 0x7C: RW 0x68C866) whose slot 0x108 is the member list (Object::setStatusAcrossHorde)
	if (kHorde >= 0 && obj.isKindOf((unsigned)kHorde) && contain && const_cast<ContainModuleInterface *>(contain)->getHordeContainInterface())
	{
		bool hiddenMember = false;
		if (const ContainModuleInterface::ContainedItemsList *members = contain->getContainedItemsList())
		{
			for (const Object *m : *members)
			{
				if (!m)
				{
					continue;
				}
				if (!InvisibilityManager::isInvisible(*m) || !hasStatus(*m, kStealthed) || InvisibilityManager::isDetected(*m) || hasStatus(*m, kDetected))
				{
					return true;
				}
				hiddenMember = true;
			}
		}
		return !hiddenMember;
	}
	// RW 0x694DE8 .. 0x694E82: a container holding only STEALTH_GARRISON objects (slot 0x114(0) == slot 0x124) whose first rider (slot 0x118) is not detected
	// hides when its apparent controlling player (slot 0x4C(player)) is an enemy of the player. INFERENCE (S-7090): slot 0x4C is not ported; the container's
	// own controlling player stands for it
	if (!contain)
	{
		return true;
	}
	if (contain->getContainCount() != contain->getStealthUnitsContained())
	{
		return true;
	}
	const ContainModuleInterface::ContainedItemsList *riders = contain->getContainedItemsList();
	const Object *first = riders && !riders->empty() ? riders->front() : nullptr;
	if (!first || InvisibilityManager::isDetected(*first) || hasStatus(*first, kDetected))
	{
		return true;
	}
	return !(owner && player && relationTo(*player, *owner) == ENEMIES);
}

EvaDamageNotice EvaDamage::makeNotice(const GameLogic &logic, const Object &victim, const DamageInfo &info, const Player &owner)
{
	static const int kEngaged = Construction::modelConditionIndex("ENGAGED");            // 0x9D
	static const int kEmotionAlert = Construction::modelConditionIndex("EMOTION_ALERT"); // 0x3F
	static const int kAttacking = Construction::modelConditionIndex("ATTACKING");        // 0x25
	EvaDamageNotice n;
	const Object *attacker = logic.findObjectByID((ObjectID)info.m_input.m_sourceID); // RW 0x449681
	n.attacker = attacker ? attacker->getID() : INVALID_ID;
	n.byFire = info.m_input.m_damageType == DAMAGE_LOGICAL_FIRE; // 0x17
	// RW 0x67B5A4 .. 0x67B5E4 (the template's EvaEventAmbushed is the client's test, made first in retail; the tests here have no side effects)
	n.ambushable = !victim.testModelCondition(kEngaged) && !conditionOnSelfOrHorde(victim, kEmotionAlert) && !conditionOnSelfOrHorde(victim, kAttacking) &&
	               attacker && !isVisibleTo(logic, *attacker, &owner, true);
	// RW 0x67B6AB .. 0x67B776: the scan runs when the event is not -1, which with DamagedOwner set is always so (every row falls back to it), and the scan
	// range is above 0
	const ThingTemplate *tt = victim.getTemplate();
	const std::string damaged = tt ? templateEvent(*tt, "EvaEventDamagedOwner") : std::string();
	const float range = tt ? scanRange(*tt) : 0.0f;
	if (damaged.empty() || !(range > 0.0f))
	{
		return n;
	}
	n.scanned = true;
	// RW 0xC11098 (allow RW 0x671E36): the object's template + 0x590 is the same event; RW 0xC10E14 (allow RW 0x660AFE): the owner controls it
	PartitionFilterFn sameEvent([&](Object &o) {
		const ThingTemplate *ot = o.getTemplate();
		return ot && AsciiStringUtil::compareNoCase(templateEvent(*ot, "EvaEventDamagedOwner"), damaged) == 0;
	});
	PartitionFilterFn owned([&](Object &o) { return o.getControllingPlayer() == &owner; });
	for (const PartitionHit &h : logic.partition().iterateObjectsInRange(*victim.getPosition(), range, FROM_BOUNDINGSPHERE_2D, { &sameEvent, &owned }, ITER_FASTEST))
	{
		n.scan.push_back(h.object->getID());
	}
	return n;
}
