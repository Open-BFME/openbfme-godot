// OpenBFME. GPL-3.0.
// StructureToppleUpdate (lane TOPPLE-1): a destroyed structure (a Fornost falling tower) falls over, away from its killer, and crushes what lies on its fall line.
// TARGET RotWK 2.01 (tier as the decomp's address notes give it; A: byte-matched pair, B: same shape, operands ported):
//   * module data: ctor RW 0x65776F (B), field table RW 0xC69680 (MinToppleDelay +0x38, MaxToppleDelay +0x3C, StructuralIntegrity +0x40, StructuralDecay +0x44,
//     ToppleAccelerationFactor +0x48, DamageFXTypes +0x4C, ToppleStartFX +0x50, ToppleDelayFX +0x54, TopplingFX +0x58, ToppleDoneFX +0x5C, CrushingFX +0x60,
//     CrushingWeaponName +0x64, MinToppleBurstDelay +0x68, MaxToppleBurstDelay +0x6C, OCL RW 0x8A9AC9 (A) into +0x70 (counts +0x94), AngleFX RW 0x8A9B2B into
//     +0xAC, ForceToppleAngle +0xB8), then the DieMux table (RW 0xC76BD8). The FX bone vector at +0xA0 has no row in RotWK's table (it stays empty).
//   * runtime: ctor RW 0x8A8BB4 (A), xfer RW 0x8A8F1B (A), doToppleDoneStuff RW 0x8A8C86 (A), doAngleFX RW 0x8A8E39 (B), doPhaseStuff RW 0x8A8EA9 (A),
//     doDamageLine RW 0x8A8FA5 (B), doToppleStartFX RW 0x8A9167 (B), doToppleDelayBurstFX RW 0x8A91BF (B), beginStructureTopple RW 0x8A92EA (B),
//     applyCrushingDamage RW 0x8A950A (A), update RW 0x8A96A0 (B); bound like StructureCollapseUpdate (RW 0x8A7D1D).
// DONOR: Open-BFME-2 Code/GameEngine/Source/GameLogic/Object/Update/StructureToppleUpdateCrushing.cpp (BFME2 0x8A5628 .. 0x8A6042, byte-matched),
//   StructureToppleUpdateCtor.cpp, StructureToppleUpdateModuleDataCtor.cpp, StructurePhaseListParsers.cpp, Common/StructureToppleUpdate_xfer.cpp; ZH StructureToppleUpdate.cpp.
// Editions: both (the BFME2 bodies are the same shape; RotWK's addresses above). Stops: S-340 (the client's FX), S-345 (what is not exact: the script topple direction, onDie's RotWK address).
#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>
#include <string>
#include <vector>

class Object;
class WeaponTemplate;

enum StructureTopplePhaseType
{
	STPHASE_INITIAL = 0, // RW 0x8A9AC9 phase names: INITIAL, DELAY, FINAL
	STPHASE_DELAY,
	STPHASE_FINAL,
	ST_PHASE_COUNT
};

enum StructureToppleStateType
{
	TOPPLESTATE_STANDING = 0,
	TOPPLESTATE_WAITINGFORTOPPLESTART,
	TOPPLESTATE_TOPPLING,
	TOPPLESTATE_WAITINGFORDONE,
	TOPPLESTATE_DONE
};

struct AngleFXInfo
{
	float angle = 0.0f; ///< radians (RW 0x8A9B2B: the degrees parsed by parseReal, times 3.14159265359f / 180)
	std::string fxList;
};

class StructureToppleUpdateModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;                         ///< RW +0x08
	unsigned m_minToppleDelay = 0;               ///< +0x38 (frames)
	unsigned m_maxToppleDelay = 0;               ///< +0x3C
	float m_structuralIntegrity = 0.1f;          ///< +0x40
	float m_structuralDecay = 0.0f;              ///< +0x44
	float m_toppleAccelerationFactor = 0.06f;    ///< +0x48
	std::uint32_t m_damageFXTypes = 0xFFFFFFFFu; ///< +0x4C (all types)
	std::string m_toppleStartFX;                 ///< +0x50
	std::string m_toppleDelayFX;                 ///< +0x54
	std::string m_toppleFX;                      ///< +0x58 TopplingFX (no body of the module reads it)
	std::string m_toppleDoneFX;                  ///< +0x5C
	std::string m_crushingFX;                    ///< +0x60
	std::string m_crushingWeaponName;            ///< +0x64
	unsigned m_minToppleBurstDelay = 0;          ///< +0x68
	unsigned m_maxToppleBurstDelay = 0;          ///< +0x6C
	std::vector<std::string> m_ocl[ST_PHASE_COUNT]; ///< +0x70: one entry per name token, in file order (an unknown name is a null entry: drawn, creates nothing)
	int m_oclCount[ST_PHASE_COUNT] = { 1, 1, 1 };   ///< +0x94
	std::vector<AngleFXInfo> m_angleFX;          ///< +0xAC
	float m_forceToppleAngle = -9.87654f;        ///< +0xB8 degrees; -9.87654 = none (NO_TOPPLE_ANGLE)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class StructureToppleUpdate : public UpdateModule, public DieModuleInterface
{
public:
	OBFME_MODULE_XFER(StructureToppleUpdate)
	StructureToppleUpdate(Thing *thing, const StructureToppleUpdateModuleData *data);
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;

	const StructureToppleUpdateModuleData *data() const { return m_data; }
	StructureToppleStateType state() const { return m_toppleState; }
	unsigned toppleFrame() const { return m_toppleFrame; }
	unsigned nextBurstFrame() const { return m_nextBurstFrame; }
	float toppleDirectionX() const { return m_toppleDirectionX; }
	float toppleDirectionY() const { return m_toppleDirectionY; }
	float toppleVelocity() const { return m_toppleVelocity; }
	float accumulatedAngle() const { return m_accumulatedAngle; }
	float structuralIntegrity() const { return m_structuralIntegrity; }
	float lastCrushedLocation() const { return m_lastCrushedLocation; }
	const Coord3D &delayBurstLocation() const { return m_delayBurstLocation; }
	float buildingHeight() const { return m_buildingHeight; }

private:
	void beginStructureTopple(const DieModuleInterface::Event &event);
	void applyCrushingDamage(float theta);
	void doDamageLine(Object *building, const WeaponTemplate *wt, float jcos, float jsin, float facingWidth, float toppleAngle);
	void doPhaseStuff(StructureTopplePhaseType phase, const Coord3D &target);
	void doToppleStartFX(Object *building);
	void doAngleFX(float curAngle, float newAngle);
	void doToppleDelayBurstFX();
	void doToppleDoneStuff();
	bool damageFXAllowed() const;
	void doFX(bool atPosition, const char *site, const std::string &fxList, const Coord3D *position);

	const StructureToppleUpdateModuleData *m_data;
	unsigned m_toppleFrame = 0;                         // RW +0x24
	float m_toppleDirectionX = 0.0f;                    // +0x28 (Coord2D)
	float m_toppleDirectionY = 0.0f;                    // +0x2C
	StructureToppleStateType m_toppleState = TOPPLESTATE_STANDING; // +0x30
	float m_toppleVelocity = 0.0f;                      // +0x34
	float m_accumulatedAngle = 0.001f;                  // +0x38 (RW 0x8A8BB4: 0.001, not 0)
	float m_structuralIntegrity = 0.0f;                 // +0x3C
	float m_lastCrushedLocation = 0.0f;                 // +0x40
	unsigned m_nextBurstFrame = 0xFFFFFFFFu;            // +0x44 (-1)
	Coord3D m_delayBurstLocation{ 0.0f, 0.0f, 0.0f };   // +0x48
	float m_buildingHeight = 0.0f;                      // +0x54 (the geometry's max height above the position, set by the ctor; not saved)
};
