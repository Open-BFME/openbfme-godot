// OpenBFME. GPL-3.0.
//
// CombatNames (lane COMBAT-1): the registry bits (object status, KindOf, model condition) the combat code tests, resolved ONCE by name against the binary's own
// tables (RW 0xD8AFF0 object status, RW 0xDA0E68 KindOf, RW 0xD9FAD8 model condition), so a mod's order cannot move them. A name the registry lacks is a logic error
// (PLAN rule 10), never a silent -1.

#pragma once

#include <string>

namespace CombatNames
{
int status(const char *name);       // OBJECT_STATUS_* index; throws std::logic_error when absent
int kindOf(const char *name);       // KindOf index; throws when absent
int modelCondition(const char *name); // model condition bit; throws when absent
int weaponSetBit(const char *name);   // WeaponSet condition bit (RW 0xDA1328 names, "PLAYER_UPGRADE" ...); throws when absent
int armorSetBit(const char *name);    // ArmorSet condition bit (RW TheArmorSetNames); throws when absent

struct Status
{
	int isAttacking = status("IS_ATTACKING");
	int isFiringWeapon = status("IS_FIRING_WEAPON");
	int isAimingWeapon = status("IS_AIMING_WEAPON");
	int isMeleeAttacking = status("IS_MELEE_ATTACKING");
	int underConstruction = status("UNDER_CONSTRUCTION");
	int unattackable = status("UNATTACKABLE");
	int hordeMember = status("HORDE_MEMBER");
	int stealthed = status("STEALTHED");
	int uncontrollablyScared = status("UNCONTROLLABLY_SCARED");
	int noAttackFromAI = status("NO_ATTACK_FROM_AI");
	int runningDownFromBehind = status("RUNNING_DOWN_FROM_BEHIND");
	int updatingAI = status("UPDATING_AI");
	int canAttack = status("CAN_ATTACK");
	int noAttack = status("NO_ATTACK");
	int noCollisions = status("NO_COLLISIONS");
	int insideGarrison = status("INSIDE_GARRISON"); // bit 0x3A (RW 0x6CAC0F)
};
const Status &statuses();

struct Kind
{
	int structure = kindOf("STRUCTURE");
	int infantry = kindOf("INFANTRY");
	int horde = kindOf("HORDE");
	int meleeHorde = kindOf("MELEE_HORDE");
	int immobile = kindOf("IMMOBILE");
	int projectile = kindOf("PROJECTILE");
	int canAttack = kindOf("CAN_ATTACK");
	int unattackable = kindOf("UNATTACKABLE");
	int inert = kindOf("INERT");
	int aircraft = kindOf("AIRCRAFT");
	int mine = kindOf("MINE");
	int hero = kindOf("HERO");
};
const Kind &kinds();
} // namespace CombatNames
