// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001) with the BFME2 decomp as the shape (decomp-progress bfme2 Code/GameEngine/Source/GameLogic/Object/
// Collide/CrateCollide/{CrateCollideIsValidToExecute,SalvageCrateCollide,SalvageCrateCollideModuleDataCtor}.cpp, retail path
// "...\GameLogic\Object\Collide\CrateCollide\SalvageCrateCollide.cpp"); ZH donor: CrateCollide.cpp / SalvageCrateCollide.cpp (onCollide, isValidToExecute,
// the doMoney shape). Where RotWK differs from ZH (no weapon / armor outcomes, the level-up and upgrade outcomes, the computer-player refusal) RotWK wins.
//
// SalvageCrateCollide (lane MODCOMPAT-MOD1, closes the runtime half of S-260): the collide module of every CRATE of the 2.01 data (SalvageCrate,
// SalvageCrate_Final and their TreasureChest* children of crate.ini, and the chests the creep lairs' OCLs drop). A unit that touches the crate collects it:
// one level for the unit, the player's upgrade, or money for its player; the crate's FX plays on the unit and the crate is destroyed.
//
// TARGET FACTS (RotWK game.dat; registry golden engine/data/rotwk-201/module-registry.json: create RW 0x651052, createData RW 0x655980, registration site
// RW 0x65B081, interface mask 0x10 = collide; tables RW 0xC71390 (CrateCollide) then RW 0xC05F80):
//   * CrateCollideModuleData, constructor RW 0x8C07A0 (vtable RW 0xC71248): RequiredKindOf + 0x08 and ForbiddenKindOf + 0x24 (KindOf masks, empty,
//     RW 0x6564E7), ForbidOwnerPlayer + 0x40 / BuildingPickup + 0x41 / HumanOnly + 0x42 (parseBool, false), PickupScience + 0x44 (parseScience RW 0x73AFE3,
//     -1 = none), ExecuteFX + 0x48 (parseFXList RW 0x73A302, null), ExecuteAnimation + 0x4C (AsciiString), ExecuteAnimationTime + 0x50 and
//     ExecuteAnimationZRise + 0x54 (parseReal, 0.0), ExecuteAnimationFades + 0x58 (parseBool, TRUE).
//   * SalvageCrateCollideModuleData, constructor RW 0x65592C (vtable RW 0xC09D50, 0x80 bytes): PorterChance + 0x5C = 0.0, BannerChance + 0x60 = 0.0,
//     LevelUpChance + 0x64 = 0.5, LevelUpRadius + 0x68 = 100.0 (RW 0xBD88D8), ResourceChance + 0x6C = 0.5 (all parsePercentToReal RW 0x42EEFA, LevelUpRadius
//     too), MinResource + 0x70 = 100, MaxResource + 0x74 = 5000 (parseInt RW 0x42EC5E), Upgrade + 0x78 (AsciiString, empty), AllowAIPickup + 0x7C (false).
//   * module: constructor RW 0x8C12C2 over CrateCollide's RW 0x8C066C (BehaviorModule RW 0x8BF84B; + 0x14 the ever-executed byte = 0); vtables primary
//     RW 0xC715C0 (slot 3 xfer RW 0x8C1442, slot 12 = vslot 0x30 executeCrateBehavior RW 0x8C158B, slot 13 = vslot 0x34 isValidToExecute RW 0x8C145D),
//     behaviour + 0x0C RW 0xC70CF8, collide + 0x10 RW 0xC715A8 (slot 0 onCollide RW 0x8C06B6, slot 1 wouldLikeToCollideWith RW 0x8C0664 = a jump to
//     vslot 0x34, slots 2 .. 4 RW 0x9188EB false).
//   * onCollide RW 0x8C06B6 (other, location, normal): isValidToExecute(other) (vslot 0x34) false -> nothing. executeCrateBehavior(other) (vslot 0x30)
//     true -> FXList::doFXObj(ExecuteFX, other, 0) when the FX is set (RW 0x4B1B5A), + 0x14 = 1, TheGameLogic->destroyObject(crate) (RW 0x62BBAB at RW
//     0x8C0702). Then (either answer of executeCrateBehavior) the client half RW 0x8C0707 .. 0x8C0754: with TheAnim2DCollection (RW 0xDE4AB0), a non-empty
//     ExecuteAnimation and TheGameLogic + 0x9A set, an Anim2D at the crate's position for ExecuteAnimationTime / ZRise (RW 0x6A1875).
//   * CrateCollide::isValidToExecute RW 0x8C080B (other): false for no other, the ever-executed byte, an other that is neutral controlled (RW 0x68B760);
//     "a valid building attempt" = BuildingPickup and the other's KindOf STRUCTURE (template + 0x108 bit 7); false when the other has no AI (Object + 0x260)
//     and it is not a valid building attempt; a CREEP other (template + 0x11D bit 6, KindOf 174) needs a controlling player whose template is a PlayableSide
//     (RW 0x6AAC66); false unless the other's KindOf has every RequiredKindOf bit and no ForbiddenKindOf bit (RW 0x70C4FE); false for an effectively dead
//     other (+ 0x458 bit 0); false when the CRATE is above the terrain (RW 0x4B12E8: height > 0) and it is not a valid building attempt; ForbidOwnerPlayer:
//     false when the crate's and the other's controlling players are the same; HumanOnly: false when the other has a controlling player that is not
//     PLAYER_HUMAN (Player + 0x5C != 0); PickupScience != -1: false when the other has a controlling player without the science (RW 0x6AC207); else true
//     unless the crate is an undetected defector (+ 0x458 bit 1).
//   * SalvageCrateCollide::isValidToExecute RW 0x8C145D: CrateCollide's, the other has an AI (+ 0x260) and its KindOf has no IMMOBILE (template + 0x108 bit 2).
//   * executeCrateBehavior RW 0x8C158B (other): without AllowAIPickup, an other whose controlling player is a PLAYER_COMPUTER (Player + 0x5C == 1) is
//     refused (false: the crate stays). The outcome RW 0x8C136C: r = GetGameLogicRandomValueReal(0.0, 1.0, SalvageCrateCollide.cpp, 0x45) (RW 0x6D332C,
//     one draw for every executed crate); LevelUpChance > r -> 2; else 4 when Upgrade is not empty, else 3. 2: the other's experience tracker (Object +
//     0x26C), when there is one, gainExpForLevel(1, true, false) (RW 0x79DA0A) and the floating text "GUI:GainRank" (client). 4: TheUpgradeCenter's
//     findUpgrade(Upgrade) (RW 0x66F5E5); a PLAYER upgrade (+ 4 == 0) is given to the other's controlling player complete (addUpgrade(u, 2, 0), RW 0x6AEE22;
//     retail calls it without a null test); an unknown or object upgrade does nothing. 3: the money RW 0x8C148B. Returns true for every outcome.
//     The porter / banner branch (outcomes 0 and 1: every object of the other's player within LevelUpRadius gains a level, RW 0x8C168D .. 0x8C1718) is
//     unreachable in 2.01 (RW 0x8C136C returns 2, 3 or 4 only); PorterChance, BannerChance and ResourceChance are parsed and never read.
//   * the money RW 0x8C148B (other): amount = MinResource when it equals MaxResource, else GetGameLogicRandomValue(MinResource, MaxResource,
//     SalvageCrateCollide.cpp, 0xDB) (RW 0x6D328E); nothing more when amount <= 0. In a multiplayer game (RW 0x625456) amount = _ftol(amount *
//     MultiPlayMoneyMult[live playable players - 1]) (RW 0x6A8630(0), RW 0x642002: fild, fmul at the game's 24-bit precision, RW 0xA3CFA4). With a
//     controlling player: the money handicap (RW 0x6AA858), then Money::deposit(amount, the player's ScoreKeeper (+ 0x3DC), sound) (RW 0x7B18B8 on
//     Player + 0x90). The floating text "GUI:AddCash" with the amount at the CRATE's position + 10 in the other's player colour (RW 0x8C13BB, client).
//   * xfer: SalvageCrateCollide RW 0x8C1442 = version (1, 1) (RW 0x404F15), then CrateCollide's RW 0x8C0760: version (current 2), BehaviorModule
//     (RW 0x9933FE), version >= 2: the ever-executed byte (+ 0x14, Xfer slot 0x90 bool).
// WHAT IS INFERENCE (OpenBFME):
//   * the contacts: RotWK's collision manager registers every object whose KindOf has no NO_COLLIDE and (with GlobalData's flag off) no HORDE (RW 0x68E31F:
//     template + 0x108 bit 30, + 0x115 bit 5, then RW 0xB6C470) and meets the overlapping pairs both ways (RW 0xB6D060). The port's pairs come from the
//     AI units' overlap pass (AIWorld::processCollisions), which has no crate, so the crates get their own pass in the same row, after it
//     (AIWorld::processCrateContacts): each live crate in id order meets the AI objects (no NO_COLLIDE, no HORDE) whose shapes overlap its own (the
//     projectile pass's narrow test, lower id's geometry first), in id order, both ways, the lower id's collide modules first (S-780 / S-581). An object
//     without an AI can never execute a 2.01 crate (no BuildingPickup in the data), so the pass leaves them out;
//   * the undetected defector bit is never set in this build (S-1020): the last test of isValidToExecute is true;
//   * the floating texts: the money's goes through Economy::noteIncome (the crate as its anchor, raised 10, as RW 0x8C13BB); "GUI:GainRank" and the
//     ExecuteAnimation (client only, no 2.01 crate sets one) are not shown (S-260);
//   * the snapshot is OpenBFME native (S-4190): RW's two versions and the ever-executed byte.

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CollideModule.h"

class ModuleFactory;
class Object;
class CrateCollideModuleData;
class SalvageCrateCollideModuleData;
class StateHasher;

class CrateCollide : public BehaviorModule, public CollideModuleInterface
{
public:
	CrateCollide(Thing *thing, const CrateCollideModuleData *data); // RW 0x8C066C

	CollideModuleInterface *getCollide() override { return this; }
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override; // RW 0x8C06B6
	bool wouldLikeToCollideWith(const Object &other) const override;                    // RW 0x8C0664 (vslot 0x34)
	void crc(StateHasher &hasher) const override;

	// RW 0x8C080B (CrateCollide's)
	virtual bool isValidToExecute(const Object *other) const;
	// vslot 0x30: true when the crate was used up
	virtual bool executeCrateBehavior(Object &other) = 0;

	bool everExecuted() const { return m_everExecuted; } ///< + 0x14
	unsigned long long executions() const { return m_executions; }

	// true when one of the object's collide modules is a crate collide this port runs (AIWorld's crate contact pass)
	static bool isCrate(const Object &obj);

protected:
	// RW 0x8C0760: version (current 2), BehaviorModule, + 0x14
	void xferCrateCollide(Xfer &x);
	const CrateCollideModuleData *crateData() const { return m_crateData; }

private:
	const CrateCollideModuleData *m_crateData;
	bool m_everExecuted = false;         ///< + 0x14
	unsigned long long m_executions = 0; ///< executeCrateBehavior calls that used the crate up (OpenBFME, hashed)
};

class SalvageCrateCollide : public CrateCollide
{
public:
	OBFME_MODULE_XFER(SalvageCrateCollide)
	SalvageCrateCollide(Thing *thing, const SalvageCrateCollideModuleData *data); // RW 0x8C12C2

	bool isValidToExecute(const Object *other) const override; // RW 0x8C145D
	bool executeCrateBehavior(Object &other) override;         // RW 0x8C158B
	void crc(StateHasher &hasher) const override;

	enum Outcome
	{
		OUTCOME_LEVEL_UP = 2, ///< RW 0x8C1625
		OUTCOME_MONEY = 3,    ///< RW 0x8C171A
		OUTCOME_UPGRADE = 4   ///< RW 0x8C15EE
	};
	// tests: what the module did (OpenBFME counters, hashed)
	unsigned long long refusedComputer() const { return m_refusedComputer; }
	int lastOutcome() const { return m_lastOutcome; }
	int lastMoney() const { return m_lastMoney; }

private:
	const SalvageCrateCollideModuleData *salvageData() const { return m_salvageData; }
	int pickOutcome();          // RW 0x8C136C
	void doMoney(Object &other); // RW 0x8C148B

	const SalvageCrateCollideModuleData *m_salvageData;
	unsigned long long m_refusedComputer = 0; ///< executeCrateBehavior's computer-player refusals (AllowAIPickup clear)
	int m_lastOutcome = 0;                    ///< the last outcome (0: none yet)
	int m_lastMoney = 0;                      ///< the last money outcome's deposit (after the multiplier and the handicap)
};
