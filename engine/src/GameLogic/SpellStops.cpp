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
		"[S-528] game start rank points: RW 0x6311ED (0x6313B6 .. 0x631407) gives the human players GameLogic + 0x94 skill points in the game kind RW 0x6253BF selects; not ported",
		"[S-529] special power runtime: ported: SpecialPowerModule's recharge / ready frame / pause / percent / requirements (RW 0x8973EE, 0x896E31, 0x896C72, 0x896CF2, "
		"0x896756, 0x8969E5), the shared timers (Player RW 0x6AD1B0 / 0x6AD22B / 0x6AD26F), the science notice (RW 0x896B56), PlayerHealSpecialPower's heal (RW 0x8CC4B4), "
		"OCLSpecialPower (RW 0x8C75D8), the base do* gate and the initiate's SpecialPowerUpdate module call (RW 0x8980A3, 0x897E87, lane HERO-1), the trigger (RW 0x897987: "
		"SetModelCondition and GiveLevels by lane HERO-1, the attribute modifiers, the weather branch and the view object by lane SPELL-2, S-920) and the other spell book "
		"classes (S-921); lane BACKLOG4: the RECHARGE_TIME attribute modifier scale of the recharge (RW 0x896EA0), the trigger's script notice (RW 0x89713F -> 0x759986, read by "
		"PLAYER_TRIGGERED_SPECIAL_POWER / _FROM_NAMED, RW 0x7E4E08 / 0x758CBF), the initiate's IdleWhenStartingPower (RW 0x897F97) and InitiateFX (RW 0x897FFA); "
		"Open (lane BACKLOG4 r1 inventory): (1) the initiate's enum switch (RW 0x897EAB: types 0x33 / 0x88 end each other's module through RW 0x8A007F, type "
		"0x8D sends a harvester back to its resource, RW 0x897F0D); (2) the target id the initiate keeps (+ 0x2C) and its + 0x28 flag (options bit 0x40000 "
		"with the update's slot 2, RW 0x897FE5); (3) the radar window override of the template's name (RW 0x897FB2 .. 0x897FD4, client); (4) the unit's voice "
		"response (RW 0x898009 .. 0x89809A, client); (5) the conditions PLAYER_MIDWAY_SPECIAL_POWER / PLAYER_COMPLETED_SPECIAL_POWER and their _FROM_NAMED "
		"forms (the lists RW + 0x1A3B8 / + 0x1A408, written by RW 0x7599B8 / 0x7599EA (ZH: the special abilities' midway / end, INFERENCE), read by RW 0x758D21 / 0x758D83): they "
		"answer false and are counted unported; (6) the light point cost of a trigger (RW 0x6AA91D, S-531); (7) the heal's FX / OCL requests are counted "
		"(PlayerHealSpecialPower); (8) the trigger's unset AttributeModifierAffects filter and the outside-the-playable-area flag (S-920, S-922), "
		"AdjustVictim's status 0x26 victim (S-920), OCLSpecialPower's secondary position (S-530). INFERENCE: the triggered list is hashed with the script "
		"engine, i.e. once map scripts are loaded (nothing else reads it); names compare by CRC_String as retail (a collision would match); a player index "
		"outside the 20 lists is refused and noted (retail does not test it); the list has no cap (retail's); the heal's partition query RW 0x8CC532 runs on "
		"ThePartitionManager (lane MODULES-2)",
		"[S-530] ObjectCreationList: the block (RW 0x5F0462) and its five nugget tables parse; lane SUMMON-1: the CreateObject nugget runs (RW 0x5F275D / 0x5F0EE6 / "
		"0x5F1B5A with their logic random draws: the upgrades test, RequiresLivePlayer, DestinationPlayer, MaxSimultaneousOfType, SpreadFormation, the lifetime, "
		"JUST_BUILT, the health, onEnslave, the dispositions and their forces, ContainInsideSourceObject, VeterancyLevel, InheritScriptingName, the secondary "
		"position's move and facing); not run (counted, noted once each as '[S-530] ObjectCreationList: not run: ...'): the other nugget kinds, PutInContainer, "
		"ClearRemovables, TREE / SHRUB / OPTIMIZED_PROP templates, WaypointSpawnPoints, OffsetInLocalSpace, MoveUsesStrafeUpdate, StartingBusyTime, InvulnerableTime, "
		"the FloatUpdate / FadeAndDieOrnamentUpdate / SpawnedModelConditionCreate dispositions; OCLSpecialPower runs CREATE_AT_LOCATION, USE_OWNER_OBJECT, "
		"CREATE_ABOVE_LOCATION, USE_SECONDARY_OBJECT_LOCATION and UpgradeName (lane SPELL-2); its secondary position, the map edge cases (0, 1, 2, 6) and the army "
		"spawn points (7) are counted",
		"[S-531] casting: the ActionManager test (RW 0x82DFB7) keeps only the module / percent-ready / requirements parts (unit cost, range, terrain, shroud and forbidden objects "
		"are not tested); a multi-member group's senior-hero choice and member order are ported (lane HEROCAST-1, RW 0x76F5DB / 0x76F774 / 0x77097E); the light point test and cost (RW 0x6AA8FB, 0x6AA91D) "
		"are skipped; the spell book is made after the starting bases (RW 0x6B183D's place in the game start is not traced)",
		"[S-532] spell book creation draws logic random numbers as retail does (RW 0x6B18E7 -> 0x6D165E -> 0x693D31 -> 0x628882 -> 0x6D328E: one object-creation "
		"seed draw, bounds 1 .. 999, per faction's book, after the bases, in player array order) and so moves the stream of a game with lobby slots; the RW 0x6B183D "
		"call's place in the game start is not traced (S-531). The smoke check it disturbed (`production: its 15 members stand around it`) passes again since HORDE-2's "
		"stranded-member fix and INTEG-1's off-grid fallback goal (RW 0x6F74D0)",
	};
}
