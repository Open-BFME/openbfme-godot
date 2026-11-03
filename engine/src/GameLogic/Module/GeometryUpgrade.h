// OpenBFME. GPL-3.0.
// Derived from the BFME2 decomp (Open-BFME-2, Code/GameEngine/Source/GameLogic/Object/Upgrade/GeometryUpgrade*.cpp: the class, its fields, its two
// implementations); no ZH donor. Every field and step below is RotWK 2.01's.
//
// GeometryUpgrade (lane MODCOMPAT-MOD1, census rank 1: every mod and vanilla RotWK; the structures' level footprints, e.g. GondorBarracks' Geom_Orig / Geom_V2):
// an upgrade module that switches named shapes of its object's own GeometryInfo on and off.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json: create RW 0x6503A4, data create RW 0x6503DC,
// registration site RW 0x65A74A, tables RW 0xC76AD8 (extra 8) and RW 0xC6F578):
//   * data: buildFieldParse RW 0x8BABA9 (the upgrade table RW 0xC76AD8 at + 8 through RW 0x8D26E0, then RW 0xC6F578): ShowGeometry (+0x138) and HideGeometry
//     (+0x144) (parseAsciiStringVectorAppend RW 0x42E59E: every token appended), WallBoundsMesh (+0x150), RampMesh1 (+0x154), RampMesh2 (+0x158)
//     (parseAsciiString RW 0x42EE5E).
//   * module (0x20 bytes): constructor RW 0x8BABCB (the UpgradeModule base RW 0x863B8F; vtables RW 0xC6F620 / 0xC6DC98, the mux at + 0x10 RW 0xC6F5D8, RW
//     0xC6FD08; the AsciiString + 0x1C, the upgrade name, cleared). Mux slots (RW 0xC6F5D8): 0 isAlreadyUpgraded RW 0x4986C4 .. 7 the base's (6 RW 0x63F3BF
//     no-op, 7 RW 0x863B85 isPermanent), 8 the removal RW 0x8BAC96, 9 setUpgradeExecuted RW 0x5B462D, 10 the implementation RW 0x8BADC0, 11 the masks RW
//     0x863B4F. (The MOD-4 stop text called RW 0x8BAC96 the implementation: it is slot 8, the removal; BFME2's slot order is the same.)
//   * upgradeImplementation (slot 10, RW 0x8BADC0): the masks (slot 11); nothing when the object's upgrades (Object + 0x28C) hold any conflicting bit, nor
//     when its controlling player's (RW 0x68B678, Player + 0x14C) do (RW 0x8097D6, testForAny, whatever RequiresAllConflictingTriggers says); else the
//     custom anim (RW 0x8D28F1); then with WallBoundsMesh / RampMesh1 / RampMesh2 named, every draw module of the drawable (RW 0x70E013, 0x60E02D) gets vslot
//     0xD4(wall) / 0xD8(ramp1, 0) / 0xD8(ramp2, 1) for the names given; nothing more when no mesh is named and both lists are empty (RW 0x8BAF60 ..
//     0x8BAF7E); else the module's upgrade name (+ 0x1C) becomes the name (+ 0x1C) of the LAST active shape (+ 0x20) of the object's GeometryInfo (Object +
//     0xA8, shapes 0x24 bytes at + 0x2C .. + 0x30, RW 0xAD1AE0), every HideGeometry name's shapes go inactive and every ShowGeometry name's active (RW
//     0x8BAC71 -> GeometryInfo::setActive RW 0xAD3520, which recomputes the bounds RW 0xAD2860), the object's geometry-changed refresh with the pathfinder
//     map (RW 0x68B244(1): Object::geometryChanged), and the object's WallUpgradeUpdate (RW 0x8AE8F0: findModule by name) gets RW 0x8AEC79.
//   * the removal (slot 8, RW 0x8BAC96): only when isAlreadyUpgraded (slot 0): the custom anim clears (RW 0x8D28F9), the object leaves the pathfinder's map
//     (RW 0x6E85FB), every draw module gets vslot 0xD4(0), 0xD8(0, 0), 0xD8(0, 1), the HideGeometry AND the ShowGeometry names both go inactive (both calls
//     push 0, RW 0x8BACFD / 0x8BAD1A: retail as written), the upgrade name, when not empty, goes active (RW 0xAD3520(name, 1)), the object enters the map
//     again (RW 0x6E85E9), RW 0x68B244(0), setUpgradeExecuted(false) (slot 9). (UpgradeMux::removeUpgrade clears the executed flag before calling it, so
//     on that path it does nothing: retail behaviour, see UpgradeModule.h.)
//   * xfer RW 0x8BAB72: version (1, 1), the upgrade name (Xfer vslot 0x6C, AsciiString), then UpgradeModule's (RW 0x8D284A).
// WHAT IS NOT PORTED / INFERENCE (stop S-1423):
//   * the draw modules' vslots 0xD4 / 0xD8 set the drawable's WallBoundsMesh / RampMesh1 / RampMesh2 (the walkable wall top and ramps the pathfinder's wall
//     classification RW 0x936B7D asks the drawable for); the simulation's wall surfaces are the template's draw module data (lane WALL-1, AIWorld.h), so a
//     GeometryUpgrade naming a mesh (RotWK: the Gondor / Rohan wall towers' trebuchet upgrades, the dwarven fortress' catapult) is counted, not applied;
//   * WallUpgradeUpdate is not ported (an UnportedModule): its RW 0x8AEC79 (the units on the wall moved away) is counted, not run;
//   * Object::geometryChanged's collision manager entry and cells (RW 0xB6C700, RW 0x68836C / 0x688354) are not ported (S-363 / S-780, S-142).

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

#include <string>
#include <vector>

class ModuleFactory;

class GeometryUpgradeModuleData : public UpgradeModuleData
{
public:
	std::vector<std::string> m_showGeometry; ///< ShowGeometry (+0x138)
	std::vector<std::string> m_hideGeometry; ///< HideGeometry (+0x144)
	std::string m_wallBoundsMesh;            ///< WallBoundsMesh (+0x150)
	std::string m_rampMesh1;                 ///< RampMesh1 (+0x154)
	std::string m_rampMesh2;                 ///< RampMesh2 (+0x158)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class GeometryUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(GeometryUpgrade)
	GeometryUpgrade(Thing *thing, const GeometryUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	static void registerModule(ModuleFactory &modules);
	void crc(StateHasher &hasher) const override;
	const GeometryUpgradeModuleData *data() const { return m_data; }
	const std::string &upgradeName() const { return m_upgradeName; } ///< + 0x1C
	// [S-1423]: what this port leaves out (reported with the retail object world's stops; the runtime texts below are noted when met)
	static std::vector<std::string> stopLines();
	static const char *const kStopDrawMeshes;
	static const char *const kStopWallUpgrade;

protected:
	void upgradeImplementation() override; // RW 0x8BADC0
	void processUpgradeRemoval() override; // RW 0x8BAC96

private:
	const GeometryUpgradeModuleData *m_data;
	std::string m_upgradeName; ///< + 0x1C (constructor RW 0x8BABCB: cleared)
};
