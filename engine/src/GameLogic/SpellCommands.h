// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The spell book messages as GameLogicDispatch handlers (lane SPELL-1).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the dispatcher RW 0x779A3D):
//   * MSG_PURCHASE_SCIENCE (1044, case RW 0x77A8D3): argument 0 (integer) is a player INDEX, argument 1 (integer) the science. A science of -1, a
//     message without an issuing player (RW 0x77A8F1) or an index ThePlayerList does not have (RW 0x6A844E) does nothing; else that player's
//     attemptToPurchaseScience(science) (RW 0x6AE36F). The purchasing player is the argument's, not the issuer's (ZH: the issuer's).
//     A message whose arguments are missing or not integers is malformed (RW reads the default record RW 0xC84858 instead): counted, reported.
//   * the casts (RW 0x77A42C MSG_DO_SPECIAL_POWER {int id, int options, object source, [object toggled]}, RW 0x77A502 _AT_LOCATION {int id, location,
//     object target, int options, object source}, RW 0x77A5D7 _AT_OBJECT {int id, object target, int options, object source, [location]}, RW 0x77B886
//     MSG_DO_SPELLBOOK_SPECIAL_POWER {int id, int options}): the power by id (RW 0x7B1B05); the group is the source object alone when the message names
//     one (or the issuing player's spell book object, RW 0x6AD0F8, for 1110), else the issuer's selection (RW 0x756E5C filter 3); then
//     AIGroup::groupDoSpecialPower* (RW 0x76F5DB / 0x76F774 / 0x77097E): per member the ActionManager test (RW 0x82DFB7, ported: the member has the
//     power's module, it is fully ready (percent 1.0, RW 0x82E084) and its requirements hold (module slot 0x48 -> RW 0x897E01 -> 0x8969E5)) and
//     Object::doSpecialPower* (RW 0x68E754: the disabled test and SpecialPowerStore::canUseSpecialPower RW 0x7B1D79).
//   * lane HEROCAST-1, the group (RW 0x76F5DB / 0x76F774 / 0x77097E; tier B with the BFME2 decomp AIGroup_groupDoSpecialPower*.cpp, only Object offsets
//     differ; the RotWK bodies read in Ghidra): the SENIOR HERO is the KindOf HERO member (template + 0x113 bit 2) with the highest rank of its current
//     level (ExperienceTracker::currentLevelRank: the level's Rank, not its experience points), the lower ObjectID on a tie; a hero without a valid level
//     is no candidate. When there is one, only it is tried: skipped or refused, nobody casts. OK_FOR_MULTI_EXECUTE (option 0x100000) skips the choice in
//     groupDoSpecialPower and _AT_OBJECT (not in _AT_LOCATION). Without a hero every member is tried in group order, except that groupDoSpecialPower
//     stops at its first cast unless OK_FOR_MULTI_EXECUTE, and _AT_OBJECT tries them by ascending 2D distance estimate to the target (RW 0x403720; a
//     hero without a level is left out). Each tried member: with command source 0 (CMD_FROM_PLAYER) an AI with PreventPlayerCommands (+ 0x3C5) is
//     skipped; the AI's last command source (+ 0x48) := the source; _AT_LOCATION with source 0 aborts the abilities in use (RW 0x693919); the test, the
//     cast, then friend_setUndetectedDefector(false) (RW 0x68B303: a no-op here, no defection is ported, S-141). The sources the cases pass: 2 for
//     MSG_DO_SPECIAL_POWER and the spell book, 2 for _AT_LOCATION with a source object and 0 from the selection, 0 for _AT_OBJECT. A null template still
//     runs the bookkeeping (no case tests it before the group call). The Edain mod gate (multiExecuteGateAllows) wraps only the member loops' tests.
//     OpenBFME divergence (Sol review r1, as DEVPORT-7e): a named source object the issuing player does not control drops the message (foreignSources()).
//     NOT PORTED (S-531): the ActionManager's unit cost, range, terrain, shroud and forbidden-object tests (RW 0x82E0BE .. 0x82E128), the toggled object of
//     MSG_DO_SPECIAL_POWER (status 0x64), and the special team-id location case of _AT_OBJECT (RW 0x77A640).

#pragma once

#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/GameLogicDispatch.h"

#include <vector>

class Object;
class SpecialPowerTemplate;
struct Coord3D;

class SpellCommands
{
public:
	void registerHandlers(GameLogicDispatch &dispatcher);
	unsigned long long malformed() const { return m_malformed; }
	unsigned long long purchases() const { return m_purchases; }
	unsigned long long casts() const { return m_casts; }       // members that cast
	unsigned long long refused() const { return m_refused; }   // members the tests refused
	unsigned long long foreignSources() const { return m_foreignSources; } // casts dropped: the named source is not the issuer's (Sol HEROCAST-1 r1)

private:
	bool purchaseScience(GameLogic &logic, const GameMessage &msg);
	bool doSpecialPower(GameLogic &logic, const GameMessage &msg, int kind);
	void castWithGroup(GameLogic &logic, const std::vector<Object *> &group, int kind, const SpecialPowerTemplate *t, const Coord3D &loc, Object *target,
		unsigned options, CommandSourceType source);
	unsigned long long m_casts = 0, m_refused = 0, m_foreignSources = 0;
	unsigned long long m_malformed = 0;
	unsigned long long m_purchases = 0;
};
