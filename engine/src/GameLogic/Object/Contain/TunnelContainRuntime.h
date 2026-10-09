// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// TunnelContain and the player's TunnelTracker (lane GARRISON-2): the tunnel network. RotWK 2.01 data: TunnelContain 6 templates, three of them skirmish
// structures (the Dwarves' DwarvenMineShaft, the Goblins' WildMineShaft and WildBurrowsExpansion), plus DwarvenMineShaftForUndermine, DwarvenMagicDoor and
// MineShaft_Interface. A unit (a horde) enters one tunnel and is in the player's network: every tunnel of the player sees the same riders and lets them out.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; DONOR ZH GameLogic/Object/Contain/TunnelContain.cpp, Common/RTS/TunnelTracker.cpp):
//   * TunnelContain is a HordeGarrisonContain (create RW 0x64B9D2: module 0x9E8 bytes, constructor RW 0x880E90 after HordeGarrisonContain's RW 0x87D2BE; + 0x9E4
//     "do onBuildComplete" (1), + 0x9E5 "registered with the network" (0)); data RW 0x65771B (0xD8 bytes, constructor RW 0x657694 after HordeGarrisonContain's
//     RW 0x87D4CF): HordeGarrisonContain's tables (RW 0x87D2A3) and RW 0xC04E58: TimeForFullHeal + 0xD4 (parseDurationReal RW 0x73A403, default 1.0);
//   * the contain interface RW 0xC5DCE0 overrides: onContaining RW 0x880E29 (HordeGarrisonContain's RW 0x87BEC8, then DISABLED_HELD), onRemoving RW 0x880FC6
//     (RW 0x87BFA6, DISABLED_HELD off, back in the world RW 0x68E31F, at the tunnel's position, shown, the unload sound), getContainMax RW 0x881098 (GameData
//     MaxTunnelCapacity + 0xA98, row RW 0xC00360, default 0), isValidContainerFor RW 0x88103B (RW 0x87D0E0 without the capacity, then the tracker's RW 0x8FA240:
//     no AIRCRAFT; with the capacity a non HORDE_MEMBER needs count < MaxTunnelCapacity), addToContainList RW 0x880F0E (the tracker's list RW 0x8FA4B2),
//     removeFromContain RW 0x880F21 (RW 0x87D04B, then the tracker RW 0x8FA28C), removeAllContained RW 0x881367 (each rider, a horde's members first, through
//     removeFromContain), isContainedHere RW 0x8811DD, the count RW 0x881072 and the list RW 0x8810AB (the tracker's);
//   * the create interface RW 0xC5DC9C: onBuildComplete RW 0x8810C9 (once: the tracker's onTunnelCreated RW 0x8FA4C9, + 0x9E5 set);
//   * update RW 0x8812B0: HordeGarrisonContain's RW 0x87C8DF, then with HealObjects (data + 0x98) the tracker heals every rider (RW 0x8FA359 -> 0x8FA190: a HEALING
//     hit of MaxHealth / TimeForFullHeal per frame, the whole MaxHealth once the rider has been inside TimeForFullHeal frames), the tracker's nemesis;
//   * the die interface RW 0x881272 (when the DieMux applies) and onDelete RW 0x881298 (OpenContain's RW 0x86708F first) leave the network (RW 0x88121A): the
//     tracker's onTunnelDestroyed RW 0x8FA4E8 (the count, the id; the riders that entered by this tunnel now count as entered by the first tunnel left); the
//     last tunnel lets every rider out (removeAllContained: RotWK's cave-in does not kill, ZH's did); otherwise a HORDE rider whose AI goal is this tunnel and
//     whose members are all in (horde slot 0xF4) is let out here (RW 0x881105).
// TunnelTracker (RW Player + 0x308): the tunnel ids + 0x8, the riders + 0x10 (count + 0x18), the tunnel count + 0x1C, the nemesis + 0x20 / + 0x24.
// NOT PORTED (stop S-1107, TunnelContain::stopLine()): the tracker's nemesis (RW 0x8FA702 / 0x8FA451: read by the AI's tunnel network guard state, not ported),
// the safe occlusion frame of a rider that leaves (+ 0x444, client), the unload sound, the tunnel network guard AI, the exposed stealth units.

#pragma once

#include "GameLogic/Module/GarrisonContain.h"
#include "GameLogic/Object/Contain/GarrisonContainRuntime.h"

#include <list>
#include <string>
#include <vector>

class GameLogic;
class ModuleFactory;
class Player;
class StateHasher;

// `Behavior = TunnelContain`: HordeGarrisonContain's tables, then TimeForFullHeal
class TunnelContainBehaviorData : public HordeGarrisonContainBehaviorData
{
public:
	float m_framesForFullHeal = 1.0f; // + 0xD4 (RW 0x657694: DAT 0xBD1908)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class TunnelTracker
{
public:
	typedef ContainModuleInterface::ContainedItemsList ContainedItemsList;

	ContainedItemsList &riders() { return m_riders; }
	const ContainedItemsList &riders() const { return m_riders; }
	int containCount() const { return (int)m_riders.size(); }                        // RW 0x8FA203(0): + 0x18
	static int containMax(const GameLogic &logic);                                     // RW 0x8FA152
	bool isValidContainerFor(const Object *obj, bool checkCapacity, const GameLogic &logic) const; // RW 0x8FA240
	void addToContainList(Object *obj) { m_riders.push_back(obj); }                    // RW 0x8FA4B2
	void removeFromContain(Object *obj);                                                // RW 0x8FA28C
	bool isInContainer(const Object *obj) const;                                        // RW 0x8FA2D7
	void onTunnelCreated(const Object &tunnel);                                         // RW 0x8FA4C9
	bool onTunnelDestroyed(const Object &tunnel, GameLogic &logic);                     // RW 0x8FA4E8: true when it was the last
	void healObjects(float framesForFullHeal, unsigned now);                            // RW 0x8FA359 -> 0x8FA190
	unsigned tunnelCount() const { return m_tunnelCount; }
	const std::list<ObjectID> &tunnelIds() const { return m_tunnelIds; }
	void crc(StateHasher &hasher) const;
	unsigned long long healHits() const { return m_healHits; }

private:
	std::list<ObjectID> m_tunnelIds; // + 0x08
	ContainedItemsList m_riders;     // + 0x10
	unsigned m_tunnelCount = 0;      // + 0x1C
	unsigned long long m_healHits = 0; // a statistic
};

class TunnelContain : public GarrisonContain, public CreateModuleInterface
{
public:
	TunnelContain(Thing *thing, const ModuleData *data, const TunnelContainBehaviorData &tunnel);

	static void registerClasses(ModuleFactory &modules);
	static const char *stopLine();

	// ---- BehaviorModule ----
	CreateModuleInterface *getCreate() override { return this; }
	void onDelete() override;                // RW 0x881298
	void crc(StateHasher &hasher) const override;
	UpdateSleepTime update() override;       // RW 0x8812B0
	// ---- CreateModuleInterface (RW 0xC5DC9C) ----
	void onCreate() override {}
	void onBuildComplete() override;         // RW 0x8810C9
	// ---- DieModuleInterface ----
	void onDie(const DieModuleInterface::Event &event) override; // RW 0x881272
	// ---- ContainModuleInterface ----
	const ContainedItemsList *getContainedItemsList() const override;
	unsigned getContainCount() const override;
	void removeFromContain(Object *obj) override;
	bool isValidContainerFor(const Object &obj, bool checkCapacity, bool checkPath) const override;
	int getContainMax() const override;
	void removeAllContained() override;
	// lane UI-1: the tunnel's contain table RW 0xC5DCE0 answers slot 0x10 with RW 0x9188EB (false), not GarrisonContain's true; slot 0xC8 stays true (RW 0x8BD372)
	bool isGarrisonable() const override { return false; }

	bool isRegistered() const { return m_registered; }
	// the owner's network (null without an owner)
	TunnelTracker *tracker() const;

protected:
	void onContaining(Object *obj, bool wasSelected) override; // RW 0x880E29
	void onRemoving(Object *obj) override;                     // RW 0x880FC6
	void addToContainList(Object *obj) override;               // RW 0x880F0E
	bool isContainedHere(const Object &obj) const override;    // RW 0x8811DD

private:
	void leaveNetwork(); // RW 0x88121A

	const TunnelContainBehaviorData *m_tunnel;
	bool m_doBuildComplete = true; // + 0x9E4
	bool m_registered = false;     // + 0x9E5
};
