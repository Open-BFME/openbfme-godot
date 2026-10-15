// OpenBFME. GPL-3.0.
//
// UnitHelpSource (lane HOVER-1): the help box RotWK shows while the pointer is on a hero's portrait in the Palantir's hero bar: the unit's name, its
// level, maximum health, melee and ranged damage, its upgrades and its description, as an InGameSimpleHelp (CommandButtonHelp::simple).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the BFME2 decomp is the donor: UnitHelpSource.cpp / GapBodiesGE26.cpp, tier B edited for the builder):
//   * the hero bar's slot handler (RW 0x92BF34, BFME2 decomp InGameHeroSelectInterfaceRva005254C5.cpp, tier A; registered per slot by RW 0x92DFDE for the
//     paths "<clip>/Hero%d/" and RW 0x92E058 "<clip>/SelectAllHeroesBttn/"): the builder slot shows the help of the command button
//     "NonCommand_SelectNearestBuilder" (RW 0x71D6EA findCommandButton, RW 0x807A00), a hero slot a UnitHelpSource of the hero (vtable RW 0xC130E8) through the
//     control bar's provider (RW 0x807848); the select-all button (RW 0x92C265) the help of "NonCommand_SelectAllHeroes";
//   * the builder (vslot 3, RW 0x8EBC61): the object (GameLogic::findObjectByID) or no help; the template, the title and the lines:
//       - the title: a drawable's script caption (Drawable + 0x348) fetched, else the object's name (Object::getDisplayName RW 0x68FA0F for a CREATE_A_HERO
//         template, KindOf + 0x11F bit 0x40, else Object + 0x494), else the template's DisplayName text (+ 0x30); a member of a HORDE container
//         (Object + 0x27C, KindOf + 0x115 bit 0x20) whose own name is set: "TOOLTIP:VerteranHordeMemberName" (name, horde's name); the template's
//         DisplayNameInvisibleForEnemy label (+ 0x5C) for a disguised object the local player is not allied with (RW 0x691B15);
//       - "TOOLTIP:Rank" (rank, maximum rank) when the maximum rank (RW 0x8EBB14) is above 1, the rank being the current ExperienceLevel's Rank
//         (RW 0x9D25B7, 0 without a level); a Create-a-Hero counts rank 1;
//       - unless the template is HORDE, a DEFENSIVE_WALL without HAS_HEALTH_BAR, UNATTACKABLE, ROCK_VENDOR or BASE_FOUNDATION, or a Create-a-Hero:
//         "TOOLTIP:MaxHealth" with the body's maximum health truncated when above 0, "TOOLTIP:MeleeDamage" / "TOOLTIP:RangedDamage" with
//         DisplayMeleeDamage / DisplayRangedDamage (+ 0x5CC / + 0x5D0, -1 unless set: RW 0x73FF33) when not negative, raised by floor(sum of the
//         object's type 2 attribute modifiers + 0.5) and, when above 0, multiplied by the product of its type 3 modifiers as floor(damage * product + 0.5)
//         (RW 0x8EBA26);
//       - the Tooltip text of every upgrade of the object's mask (Object + 0x28C, bits 0 .. 0x47F, UpgradeTemplate + 0x2C) that has one;
//       - "TOOLTIP:BattlesFought" with Object + 0x488 when above 0;
//       - the description: the drawable's script description (+ 0x34C) fetched, else the template's Description text (+ 0x38), when not empty;
//     each line joins the text after a "\n" (RW 0x8EBC3B).
// Not ported (S-4612): the drawable script captions, Object + 0x494 / the disguise label / the Create-a-Hero name and template (RW 0x69194E), the battles
// fought count (Object + 0x488, S-852); each is named in `unported` when it could matter for the object.

#pragma once

#include "GameClient/CommandButtonHelp.h"

#include <string>
#include <vector>

class GameLogic;
class GameTextSource;
class Object;
class Player;

class UnitHelpSource
{
public:
	struct Text
	{
		UnicodeString title, text;
		std::vector<std::string> unported;
	};
	// RW 0x8EBC61 for `obj`; `localPlayer` is the local player (RW 0xDE4928 + 0x10)
	static Text compose(const Object &obj, const GameTextSource *text, GameLogic &logic, const Player *localPlayer);
	// printf's %d / %s / %u / %i in order (UnicodeString::format with the retail labels' arguments); '%%' is a percent sign
	static UnicodeString format(const UnicodeString &fmt, const std::vector<UnicodeString> &args);
	static std::vector<std::string> acceptanceStops();
};
