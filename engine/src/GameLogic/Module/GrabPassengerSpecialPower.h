// OpenBFME. GPL-3.0.
// GrabPassengerSpecialPower (lane MODCOMPAT-MOD1, census rank 11): the special power module of the grabs: the mountain troll's tree / rock / orc grab, the
// cave trolls', the Watcher's. It finds what to grab (its own targetless form) or takes the order's target (a horde gives one of its members) and starts the
// SPECIAL_GRAB_PASSENGER (0x27) ability's SpecialAbilityUpdate on it. RotWK only (no ZH class): every field and step below is read from the RotWK binary;
// the BFME2 decomp (Code/GameEngine/Source/GameLogic/Object/SpecialPower/GrabPassengerSpecialPower*.cpp) has the same shape.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json: create RW 0x651DED, data RW 0x651E25, tables
// RW 0xC64DB0 and RW 0xC74534):
//   * data: RW 0x651E25 news 0x84 bytes; constructor RW 0x8C98AA: SpecialPowerModuleData (RW 0x896834), + 0x7C GrabRadius 0.0, + 0x80 AllowTree true. Tables:
//     SpecialPowerModuleData's RW 0xC64DB0, then RW 0xC74534: GrabRadius (+ 0x7C, parseReal RW 0x42ED00), AllowTree (+ 0x80, parseBool RW 0x42E558).
//   * module: RW 0x651DED news 0x34 bytes; constructor RW 0x8C942D: SpecialPowerModule (RW 0x8973EE) and its vtables RW 0xC745D0 / 0xC06050 / 0xC74568 (the
//     special power interface: RW 0xC64FF0's slots but slot 0x28 doSpecialPower RW 0x8C9629 and slot 0x2C doSpecialPowerAtObject RW 0x8C94D7; slot 0x30
//     doSpecialPowerAtLocation is the base's RW 0x89816C). No state of its own: the snapshot is SpecialPowerModule's.
//   * doSpecialPowerAtObject(target, options) RW 0x8C94D7: without option 0x40000 a paused module (+ 0x1C > 0) or a disabled object (RW 0x9325B4) does
//     nothing. A target that is a HORDE (template + 0x115 bit 5): its contain (Object + 0x258, called unchecked) vslot 0x78; with one, vslot 0x48(1, 0, 0,
//     0, 0) picks a member (the first not TAGGED: HordeContain RW 0x86FA87 with no position), none -> nothing; then (power type 0x27) the check below
//     against the HORDE, then the member gets TAGGED (status 0x3F, RW 0x62684D) and is the target. Not a horde: the check alone. The check (only when
//     this module's power, its final override's + 0x1C, is 0x27): the object's module of type 0x27 (RW 0x691526; none -> nothing) and
//     ActionManager::canDoSpecialPowerAtObject(object, target, (options >> 18) & 1, that module's power, options, 1) (RW 0x82D925; no -> nothing). Then
//     the initiate RW 0x897E87(target, 0, options, 0) and, unless UpdateModuleStartsAttack (data + 0xC), the trigger RW 0x897987 at the target's position.
//   * doSpecialPower(options) RW 0x8C9629 (the targetless grab, from the object's position, ThePartitionManager's closest object within GrabRadius, distance
//     type 0, RW 0xA39090): option 0x8000 (a rock or tree): with AllowTree the closest grabbable tree (TheTerrainLogic RW 0x67F52B(pos, GrabRadius,
//     ignoreToppled 1, mode 1)) and the closest object of KindOf ROCK or ROCK_VENDOR (RW 0x46E72F(0, 0x61, 0x88), the any-kind filter RW 0x797E8D); a tree
//     and no object, or a tree no farther (Coord3D::length, RW 0x403111) than the object, is the point; else option 0x2000: the closest ROCK_VENDOR (0x88);
//     else the closest INFANTRY (KindOf 8) that is not UNSELECTABLE (status 3, RW 0xC1D6A0 rejecting it) and that the object holds as ENEMIES (RW 0xC11DC0).
//     Then the object's module of type 0x27 (RW 0x691526): the point -> its slot 0x30 (doSpecialPowerAtLocation) with options 0x2000, else an object ->
//     its slot 0x2C (doSpecialPowerAtObject) with 0x2000.
// WHAT IS INFERENCE (OpenBFME): the HORDE target's contain slot 0x78 is read as the HordeContain (another contain class answers none and the horde is the
// target, as RW's null branch). The grab itself is SpecialAbilityUpdate's (SPECIAL_GRAB_PASSENGER / SPECIAL_GRAB_CHUNK, lane MODCOMPAT-MOD1; its tree and
// vendor grabs are S-860) and the object test ActionManager's (RW 0x82DB6A; its fake tree branch is S-7140). A tree with no ROCK / ROCK_VENDOR object: RW copies the tree's position to the point
// (EBP-0x3C, RW 0x8C96E7) only on the both-found path (RW 0x8C96DC jumps past it), so retail passes an unset stack point to slot 0x30; OpenBFME passes the
// tree's position (the point the both-found path would give).

#pragma once

#include "GameLogic/Module/SpecialPowerModules.h"

class ModuleFactory;

class GrabPassengerSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	float m_grabRadius = 0.0f; ///< + 0x7C GrabRadius
	bool m_allowTree = true;   ///< + 0x80 AllowTree
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC64DB0 then RW 0xC74534
};

class GrabPassengerSpecialPower : public SpecialPowerModule
{
public:
	OBFME_MODULE_XFER(GrabPassengerSpecialPower)
	GrabPassengerSpecialPower(Thing *thing, const GrabPassengerSpecialPowerModuleData *data); // RW 0x8C942D
	void doSpecialPower(unsigned options) override;                         // RW 0x8C9629
	void doSpecialPowerAtObject(Object *target, unsigned options) override; // RW 0x8C94D7
	const GrabPassengerSpecialPowerModuleData *grabData() const { return m_data; }
	// tests and the hash: grabs started (the initiate reached), and what the targetless form handed to the grab module
	unsigned long long grabsStarted() const { return m_started; }
	unsigned long long autoGrabsAtObject() const { return m_autoAtObject; }
	unsigned long long autoGrabsAtTree() const { return m_autoAtTree; }
	void crc(StateHasher &h) const override;
	static void registerClass(ModuleFactory &modules);

private:
	// RW 0x691526: the object's first module whose power's type is `type`
	SpecialPowerModuleInterface *moduleOfType(int type) const;
	const GrabPassengerSpecialPowerModuleData *m_data;
	unsigned long long m_started = 0;
	unsigned long long m_autoAtObject = 0;
	unsigned long long m_autoAtTree = 0;
};
