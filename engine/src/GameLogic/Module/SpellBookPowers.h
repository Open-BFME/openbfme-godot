// OpenBFME. GPL-3.0.
//
// The special power classes of the spell books besides SpecialPowerModule, PlayerHealSpecialPower and OCLSpecialPower (GameLogic/Module/
// SpecialPowerModules.h): PlayerUpgradeSpecialPower, DarknessSpecialPower, FreezingRainSpecialPower, CloudBreakSpecialPower, TaintSpecialPower,
// ElvenWoodSpecialPower, ProductionSpeedBonus, ScavengerSpecialPower, UntamedAllegianceSpecialPower and DevastateSpecialPower. Lane SPELL-2.
// They are BFME2 / RotWK classes: there is no ZH donor; Open-BFME-1 has none of them. Every fact below is read from the RotWK binary.
//
// TARGET FACTS (RotWK game.dat, caveat S-001). Registry rows from engine/data/rotwk-201/module-registry.json (create / data create / tables); the
// interface vtable slots 0x28 / 0x2C / 0x30 are doSpecialPower / doSpecialPowerAtObject / doSpecialPowerAtLocation:
//   * PlayerUpgradeSpecialPower (module RW 0x8CC022, interface RW 0xC74D48, table RW 0xC74E64: UpgradeName + 0x7C, one name per line appended by
//     RW 0x8CC228): doSpecialPower RW 0x8CC0CC: nothing while disabled; startPowerRecharge(1.0); the PLAYER upgrades of UpgradeName are granted
//     COMPLETE (the loop of OCLSpecialPower, RW 0x8CC0E5); then the trigger with no location (RW 0x897987(0)). AtObject / AtLocation (RW 0x8C82E2)
//     call doSpecialPower.
//   * the weather / terrain family shares doSpecialPower RW 0x8C92CE (not disabled: AtLocation at the object's position) and AtObject RW 0x8C7BF2
//     (not disabled, a target: AtLocation at the target's position):
//       - DarknessSpecialPower (RW 0x8C9224; table RW 0xC74448: DarknessRadius + 0x7C, DarknessFX + 0x80): AtLocation RW 0x8C93A5: not disabled:
//         the base AtLocation (RW 0x89816C), then RW 0x8C931C: DarknessFX at the map's centre when the weather is not CLOUDY, then RW 0xDE46A8 + 0x98
//         (the burn decay value) = 0;
//       - FreezingRainSpecialPower (RW 0x8C905A; table RW 0xC74348: FreezingRainRadius + 0x7C, FreezingRainFX + 0x80, BurnRateModifier + 0x84):
//         AtLocation RW 0x8C918D: the base, RW 0x8C9104 (FreezingRainFX at the centre unless RAINY), then every fire grid cell gets BurnRateModifier
//         (RW 0x687C4C, the fire logic: S-921);
//       - CloudBreakSpecialPower (RW 0x8C8991; table RW 0xC74000: CloudBreakRadius + 0x7C, CloudBreakFX + 0x80, SunbeamObject + 0x84,
//         ObjectSpacing + 0x88): AtLocation RW 0x8C8CE4: the base, RW 0x8C8ADA (CloudBreakFX at the centre; a SunbeamObject on the neutral team
//         every ObjectSpacing over the extent of TerrainLogic vslot 0x20 (RW 0x462637), RW 0x8C8A88: two logic draws per retail sunbeam), RW 0x8C8A3B (every object the AttributeModifierAffects filter allows
//         for the caster's player: its outermost container's fire is put out, RW 0x68F383 -> 0x8B4E75: S-921), then the burn decay value = 0;
//       - TaintSpecialPower (RW 0x8C8D4F; table RW 0xC741A0: TaintObject + 0x7C, TaintRadius + 0x80, TaintFX + 0x84, TaintOCL + 0x88): AtLocation
//         RW 0x8C8FF5: not disabled and a TaintObject name: the base, then RW 0x8C8E67: the terrain's taint area (RW 0x67F6F0 / 0x67D4CE, kind 1:
//         S-921), TaintFX at the location, TaintOCL at the location (source: the object), the TaintObject made on the neutral team at the location
//         (RW 0x8C8DF9: one creation draw; its RW 0x69954A call with the caster's + 0x31C: S-921) and the terrain decals (RW 0xAD4C10, client);
//       - ElvenWoodSpecialPower (RW 0x8C7B48; table RW 0xC73C98: ElvenGroveObject + 0x88, ElvenNumObjects + 0x8C, ElvenWoodObject (name, real)
//         pairs + 0x7C, ElvenWoodRadius + 0x90, ElvenWoodFX + 0x94, ElvenWoodOCL + 0x98): AtLocation RW 0x8C7FA2: not disabled and an
//         ElvenGroveObject name: the base, then RW 0x8C7DBA: the same steps as Taint with kind 2, the grove object (RW 0x8C7C22) gets status 0x54.
//   * ProductionSpeedBonus (RW 0x8C70FC; table RW 0xC73620: NumberOfFrames + 0x7C, SpeedMulitplier + 0x80, Type + 0x84 a name list): doSpecialPower
//     RW 0x8C7171: for each Type name, the player's production bonus (RW 0x6AF3C8: name -> NumberOfFrames and (mult - 1.0) / (mult * -1.0), x87
//     doubles RW 0xBD2C98 / 0xBDFC10); then, not disabled, the base AtLocation at the object's position. The production side is not ported (S-921).
//   * ScavengerSpecialPower (RW 0x8C87F2, 0x38 bytes; table RW 0xC73EFC: BountyPercent + 0x7C): doSpecialPower RW 0x8C88A0: Player bounty percent
//     (RW 0x6AA847) = BountyPercent, the base doSpecialPower, + 0x34 (active) = 1. pauseCountdown (interface slot 0x24, RW 0x8C88CF): when active,
//     the bounty becomes 0 (pause) or BountyPercent (resume), then the base pause.
//   * UntamedAllegianceSpecialPower (RW 0x8CBF25; its own table is empty): the base do*, module vslot 0x34 RW 0x8CBFCF: unless the victim has status
//     0x39, the base vslot 0x34 then the victim defects to the caster (RW 0x699368, RW 0x6938BD, producer RW 0x68B6A1, status 0x3E cleared).
//   * DevastateSpecialPower (RW 0x8CC60F; table RW 0xC75000: Radius + 0x7C, FX + 0x80, TreeValueMultiplier + 0x84 (percent), TreeValueTotalCap
//     + 0x88, FireWeapon + 0x8C): AtLocation RW 0x8CC711: not disabled with a controlling player: the base, every tree within Radius (RW 0x67F52B,
//     the terrain's tree list) is knocked down with FX and pays money (RW 0x6AA858), the player's ... (RW 0x7B18B8), then FireWeapon at the
//     location (RW 0x6CF530). Only the base part is ported (S-921).

#pragma once

#include "GameLogic/Module/SpecialPowerModules.h"

#include <string>
#include <utility>
#include <vector>

class ModuleFactory;

// ---- PlayerUpgradeSpecialPower -----------------------------------------------------------------------------------------------------------
class PlayerUpgradeSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	std::vector<std::string> m_upgradeNames; // + 0x7C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class PlayerUpgradeSpecialPower : public SpecialPowerModule
{
public:
	PlayerUpgradeSpecialPower(Thing *thing, const PlayerUpgradeSpecialPowerModuleData *data) : SpecialPowerModule(thing, data), m_data(data) {}
	void doSpecialPower(unsigned options) override;                                    // RW 0x8CC0CC
	void doSpecialPowerAtObject(Object *, unsigned options) override { doSpecialPower(options); }            // RW 0x8C82E2
	void doSpecialPowerAtLocation(const Coord3D &, unsigned options) override { doSpecialPower(options); }   // RW 0x8C82E2

private:
	const PlayerUpgradeSpecialPowerModuleData *m_data;
};

// ---- the weather / terrain family ---------------------------------------------------------------------------------------------------------
class AreaSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	float m_radius = 0.0f;   // + 0x7C (DarknessRadius / FreezingRainRadius / CloudBreakRadius); Taint: TaintRadius + 0x80
	std::string m_fx;        // + 0x80 (DarknessFX / FreezingRainFX / CloudBreakFX)
};
class DarknessSpecialPowerModuleData : public AreaSpecialPowerModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC74448
};
class FreezingRainSpecialPowerModuleData : public AreaSpecialPowerModuleData
{
public:
	int m_burnRateModifier = 0; // + 0x84
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC74348
};
class CloudBreakSpecialPowerModuleData : public AreaSpecialPowerModuleData
{
public:
	std::string m_sunbeamObject; // + 0x84
	float m_objectSpacing = 0.0f; // + 0x88
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC74000
};
class TaintSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	std::string m_taintObject; // + 0x7C
	float m_taintRadius = 0.0f; // + 0x80
	std::string m_taintFX;     // + 0x84
	std::string m_taintOCL;    // + 0x88
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC741A0
};
class ElvenWoodSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	std::vector<std::pair<std::string, float>> m_elvenWoodObjects; // + 0x7C (RW 0x8C81CD: name, real)
	std::string m_elvenGroveObject;  // + 0x88
	int m_elvenNumObjects = 0;       // + 0x8C
	float m_elvenWoodRadius = 0.0f;  // + 0x90
	std::string m_elvenWoodFX;       // + 0x94
	std::string m_elvenWoodOCL;      // + 0x98
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC73C98
};

// RW 0x8C92CE / 0x8C7BF2: the shared doSpecialPower / doSpecialPowerAtObject of the family
class WeatherFamilySpecialPower : public SpecialPowerModule
{
public:
	WeatherFamilySpecialPower(Thing *thing, const SpecialPowerModuleData *data) : SpecialPowerModule(thing, data) {}
	void doSpecialPower(unsigned options) override;
	void doSpecialPowerAtObject(Object *target, unsigned options) override;
	// the client / unported parts met (S-921): counted per cast
	unsigned long long unportedParts() const { return m_unported; }

protected:
	// RW 0xDE46A8 + 0x98 = 0 after the base (Darkness, CloudBreak)
	void zeroBurnDecay();
	// RW 0x8C8E67 / 0x8C7DBA: the terrain area, FX, OCL and the area object of Taint / ElvenWood; returns the object made (null: none)
	Object *seedArea(const Coord3D &loc, const std::string &fx, const std::string &ocl, const std::string &objectName);
	// the FX at the centre of the terrain's extent (RW 0x8C931C / 0x8C9104 / 0x8C8ADA); castLoc: its z (null: the ground height there)
	void emitCentreFX(const std::string &fx, const Coord3D *castLoc);
};

class DarknessSpecialPower : public WeatherFamilySpecialPower
{
public:
	DarknessSpecialPower(Thing *thing, const DarknessSpecialPowerModuleData *data) : WeatherFamilySpecialPower(thing, data), m_data(data) {}
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8C93A5

private:
	const DarknessSpecialPowerModuleData *m_data;
};
class FreezingRainSpecialPower : public WeatherFamilySpecialPower
{
public:
	FreezingRainSpecialPower(Thing *thing, const FreezingRainSpecialPowerModuleData *data) : WeatherFamilySpecialPower(thing, data), m_data(data) {}
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8C918D

private:
	const FreezingRainSpecialPowerModuleData *m_data;
};
class CloudBreakSpecialPower : public WeatherFamilySpecialPower
{
public:
	CloudBreakSpecialPower(Thing *thing, const CloudBreakSpecialPowerModuleData *data) : WeatherFamilySpecialPower(thing, data), m_data(data) {}
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8C8CE4
	unsigned long long sunbeamsMade() const { return m_sunbeams; }

private:
	void makeSunbeams(); // RW 0x8C8B57 .. 0x8C8C46
	const CloudBreakSpecialPowerModuleData *m_data;
	unsigned long long m_sunbeams = 0;
};
class TaintSpecialPower : public WeatherFamilySpecialPower
{
public:
	TaintSpecialPower(Thing *thing, const TaintSpecialPowerModuleData *data) : WeatherFamilySpecialPower(thing, data), m_data(data) {}
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8C8FF5
	ObjectID lastAreaObject() const { return m_lastObject; }

private:
	const TaintSpecialPowerModuleData *m_data;
	ObjectID m_lastObject = INVALID_ID;
};
class ElvenWoodSpecialPower : public WeatherFamilySpecialPower
{
public:
	ElvenWoodSpecialPower(Thing *thing, const ElvenWoodSpecialPowerModuleData *data) : WeatherFamilySpecialPower(thing, data), m_data(data) {}
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8C7FA2
	ObjectID lastAreaObject() const { return m_lastObject; }

private:
	const ElvenWoodSpecialPowerModuleData *m_data;
	ObjectID m_lastObject = INVALID_ID;
};

// ---- ProductionSpeedBonus ------------------------------------------------------------------------------------------------------------------
class ProductionSpeedBonusModuleData : public SpecialPowerModuleData
{
public:
	int m_numberOfFrames = 0;          // + 0x7C
	float m_speedMultiplier = 0.0f;    // + 0x80 ("SpeedMulitplier", retail's spelling)
	std::vector<std::string> m_types;  // + 0x84
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC73620
};
class ProductionSpeedBonus : public SpecialPowerModule
{
public:
	ProductionSpeedBonus(Thing *thing, const ProductionSpeedBonusModuleData *data) : SpecialPowerModule(thing, data), m_data(data) {}
	void doSpecialPower(unsigned options) override; // RW 0x8C7171
	// the bonus RW 0x6AF3C8 records per template name: (frames, time factor); kept here, the production side is not ported (S-921)
	const std::vector<std::pair<std::string, std::pair<int, float>>> &bonuses() const { return m_bonuses; }
	void crc(StateHasher &h) const override;

private:
	const ProductionSpeedBonusModuleData *m_data;
	std::vector<std::pair<std::string, std::pair<int, float>>> m_bonuses;
};

// ---- ScavengerSpecialPower -----------------------------------------------------------------------------------------------------------------
class ScavengerSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	float m_bountyPercent = 0.0f; // + 0x7C
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC73EFC
};
class ScavengerSpecialPower : public SpecialPowerModule
{
public:
	ScavengerSpecialPower(Thing *thing, const ScavengerSpecialPowerModuleData *data) : SpecialPowerModule(thing, data), m_data(data) {}
	void doSpecialPower(unsigned options) override; // RW 0x8C88A0
	void pauseCountdown(bool pause) override;       // RW 0x8C88CF
	bool active() const { return m_active; }
	void crc(StateHasher &h) const override;

private:
	const ScavengerSpecialPowerModuleData *m_data;
	bool m_active = false; // + 0x34
};

// ---- UntamedAllegianceSpecialPower ---------------------------------------------------------------------------------------------------------
class UntamedAllegianceSpecialPower : public SpecialPowerModule
{
public:
	UntamedAllegianceSpecialPower(Thing *thing, const SpecialPowerModuleData *data) : SpecialPowerModule(thing, data) {}
	unsigned long long defected() const { return m_defected; }

protected:
	void applyToVictim(Object &victim, unsigned until, unsigned antiMask) override; // RW 0x8CBFCF

private:
	unsigned long long m_defected = 0;
};

// ---- DevastateSpecialPower -----------------------------------------------------------------------------------------------------------------
class DevastateSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	float m_radius = 0.0f;              // + 0x7C
	std::string m_fx;                   // + 0x80
	float m_treeValueMultiplier = 0.0f; // + 0x84 (percent)
	float m_treeValueTotalCap = 0.0f;   // + 0x88
	std::string m_fireWeapon;           // + 0x8C
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0xC75000
};
class DevastateSpecialPower : public SpecialPowerModule
{
public:
	DevastateSpecialPower(Thing *thing, const DevastateSpecialPowerModuleData *data) : SpecialPowerModule(thing, data), m_data(data) {}
	void doSpecialPower(unsigned options) override;                               // RW 0x8CC658
	void doSpecialPowerAtObject(Object *target, unsigned options) override;       // RW 0x8CC6FD
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8CC711

private:
	const DevastateSpecialPowerModuleData *m_data;
};

namespace SpellBookPowers
{
void registerAll(ModuleFactory &modules);
// the stop lines of the lane (S-920 .. S-923)
std::vector<std::string> stopLines();
}
