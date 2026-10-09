// OpenBFME. GPL-3.0.
// See GameLogic/SpellStops.h.

#include "GameLogic/SpellStops.h"

std::vector<std::string> SpellStops::lines()
{
	return {
		"[S-520] spell book labels and sounds: Science DisplayName / Description, Rank RankName (RW 0x73B192, GameText), SpecialPower InitiateSound / InitiateAtLocationSound "
		"(RW 0x73B217, audio events) and EvaEventToPlayOnSuccess (RW 0x5DE588, TheEva) are stored by name, not looked up: retail throws for an unknown label or sound",
		"[S-521] intrinsic sciences: PlayerTemplate IntrinsicSciences / IntrinsicSciencesMP are kept as names by the PlayerTemplate parser (S-145) and resolved when a player's "
		"sciences reset (RW 0x6AED4B); an unknown name is a runtime error at the game start instead of retail's INI error (RW 0x73B4A0 at parse time)",
		"[S-522] SpecialPower without a store: RW 0x7B212F reads TheSpecialPowerStore without a null test; the port throws INIException(3, \"TheSpecialPowerStore==NULL\")",
		"[S-523] SpecialPower ObjectFilter / ForbiddenObjectFilter defaults: the constructor (RW 0x7B1F5B) builds them with RW 0x763D11 from the KindOf mask global RW 0xDE49E4, "
		"not identified; an unset filter is null here and a parsed one starts from the parser's default",
		"[S-524] PlayerSkillPointsScalarTable: the ExperienceScalarTable store (RW 0xDE4704, block RW 0x689920) is not ported; RW 0x782AA4 scales a player's skill points by it only "
		"while GameLogic + 0x114 != 3, which no skirmish reaches (S-525)",
		"[S-525] GameLogic + 0x114 is taken as 3 in every skirmish (constructor RW 0x6301D3; the other writers RW 0x7790EA, 0x779CB4, 0x779D23, 0x779F20 and 0x82BC22, found by "
		"the review, leave it unchanged on a normal new-game init): the rank sub-object's vslot 5 "
		"(RW 0x602E64) is false, so the max rank is min(rank count, MaxLevelMP) and the skill points are not scaled; the Living World branch (RW 0x7827D9, RW 0x6B34EE "
		"InitialMaxRingLevel / GameLogic + 0x118) is not ported",
		"[S-526] science side effects: a new science tells the special power modules of the player's objects (RW 0x6AE1C9 .. 0x6AE2E6) through PlayerScience's hook (counted "
		"when none is installed); the script engine notices (RW 0x759A4E, 0x75950A), the control bar refresh and the local player's rank-up EVA (RW 0x6AA9C2) are not ported",
		"[S-527] skill points of kills: connected (lane INTEG-1): XP-1's ExperienceWorld runs RW 0x6AAFC3 / 0x6AB0D0 with the victim's ExperienceTracker value and its "
		"default sink calls PlayerScience::addSkillPoints(points, true) and the score keeper (RW 0x782AA4, 0x79DBA1); what stays open is XP-1's: the skill point veto "
		"(S-635)",
		"[S-528] game start rank points: RW 0x6311ED (0x6313B6 .. 0x631407) gives the human players GameLogic + 0x94 skill points in the game kind RW 0x6253BF selects; not ported",
		"[S-529] special power runtime: ported: SpecialPowerModule's recharge / ready frame / pause / percent / requirements (RW 0x8973EE, 0x896E31, 0x896C72, 0x896CF2, "
		"0x896756, 0x8969E5), the shared timers (Player RW 0x6AD1B0 / 0x6AD22B / 0x6AD26F), the science notice (RW 0x896B56), PlayerHealSpecialPower's heal (RW 0x8CC4B4), "
		"OCLSpecialPower (RW 0x8C75D8), the base do* gate and the initiate's SpecialPowerUpdate module call (RW 0x8980A3, 0x897E87, lane HERO-1), the trigger (RW 0x897987: "
		"SetModelCondition and GiveLevels by lane HERO-1, the attribute modifiers, the weather branch and the view object by lane SPELL-2, S-920) and the other spell book "
		"classes (S-921); not ported: the RECHARGE_TIME modifier scale and the initiate path's FX / sound / script parts (RW 0x897FDE, 0x89713F); the heal's FX / OCL counted (its "
		"partition query RW 0x8CC532 runs on ThePartitionManager: lane MODULES-2)",
		"[S-530] ObjectCreationList: the block (RW 0x5F0462) and its five nugget tables parse; only CreateObject's name pick (RW 0x5F11CD draw), template, command point "
		"test and creation on the source's team at the position + Offset (RW 0x5F0EE6) are ported: dispositions, forces, containers, formations, fades, veterancy, health, "
		"just-built, invulnerable, MaxSimultaneousOfType, RequiresLivePlayer, waypoints, the secondary position and the other nugget kinds are counted, not run (and the logic "
		"random draws they make are missing); OCLSpecialPower runs CREATE_AT_LOCATION, USE_OWNER_OBJECT, CREATE_ABOVE_LOCATION, USE_SECONDARY_OBJECT_LOCATION and "
		"UpgradeName (lane SPELL-2); the map edge cases (0, 1, 2, 6) and the army spawn points (7) are counted",
		"[S-531] casting: the ActionManager test (RW 0x82DFB7) keeps only the module / percent-ready / requirements parts (unit cost, range, terrain, shroud and forbidden objects "
		"are not tested); a multi-member group casts with every member (the HERO preference RW 0x76F7A9 is not ported); the light point test and cost (RW 0x6AA8FB, 0x6AA91D) "
		"are skipped; the spell book is made after the starting bases (RW 0x6B183D's place in the game start is not traced)",
		"[S-532] spell book creation draws logic random numbers as retail does (RW 0x6B18E7 -> 0x6D165E -> 0x693D31 -> 0x628882 -> 0x6D328E: one object-creation "
		"seed draw, bounds 1 .. 999, per faction's book, after the bases, in player array order) and so moves the stream of a game with lobby slots; the RW 0x6B183D "
		"call's place in the game start is not traced (S-531). The smoke check it disturbed (`production: its 15 members stand around it`) passes again since HORDE-2's "
		"stranded-member fix and INTEG-1's off-grid fallback goal (RW 0x6F74D0)",
	};
}
