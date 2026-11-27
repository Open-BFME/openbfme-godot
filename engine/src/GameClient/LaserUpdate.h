// OpenBFME. GPL-3.0.
//
// LaserUpdate (editions: both RotWK 2.01 and BFME2 1.06):
// Client update module that positions and scales laser beams (such as Gandalf's lightning powers:
// GandalfLaserBeam, GandalfLightningSword, GandalfLightningFromAbove) and feeds endpoints, width
// scaling, and dirty state to W3DLaserDraw.
//
// TARGET FACTS (RotWK 2.01 game.dat md5 daed3668006cd90f01c34e5a7da1901f):
// - Factory: RW 0x64D233 for module data (new 0x1C), RW 0x64D1FB for module (new 0x54).
// - Registry: "LaserUpdate", MODULETYPE_CLIENT_UPDATE = 2, interface mask 0x800.
// - ModuleData:
//   * Ctor: RW 0x764D9C / BFME2 0x363147.
//   * Dtor: RW 0x764E3D / RW 0x764E21 / BFME2 0x3631E8.
//   * Field table: RW 0xC2D890 / BFME2 0xBDD288 (5 fields, INI keywords):
//     - MuzzleParticleSystem (+0x08, parseAsciiString RW 0x42E57F)
//     - ParentFireBoneName (+0x0C, parseAsciiString RW 0x42E57F)
//     - ParentFireBoneOnTurret (+0x10, parseBool RW 0x42E558, default false)
//     - TargetParticleSystem (+0x14, parseAsciiString RW 0x42E57F)
//     - LaserLifetime (+0x18, parseReal RW 0x42ED00, default 0.0f, ms)
// - Module:
//   * Ctor: RW 0x764B43 / BFME2 0x362EEE (+0x3C currentWidthScalar starts at 1.0f RW 0xBD1908).
//   * Dtor: RW 0x764BB9 / BFME2 0x362F64 (destroys particle systems via TheParticleSystemManager).
//   * Name key: "LaserUpdate" (RW 0x764D57 / BFME2 0x363102).
//   * Decay: RW 0x764C18 / BFME2 0x362FC3.
//   * Xfer: RW 0x764C4D / BFME2 0x362FF8 (version 1.2; coords, bools, uints, float, IDs).
//   * getCurrentLaserRadius: RW 0x764CC2 / BFME2 0x363065 (template width * currentWidthScalar).
//   * initLaser: RW 0x7651B1 / BFME2 0x36355C (widening/decaying setup, bone/target endpoints,
//     midpoint drawable position, particle system triggering, 0.03 frame/ms expiry RW 0xD9F624).
//   * initFromDrawables: RW 0x7654A8 / BFME2 0x604460 (stores parent/target IDs, forwards to initLaser).
//   * clientUpdate: RW 0x764E88 / BFME2 0x363233 (expiry check, widening/decaying scalar ramp,
//     parent bone tracking via RW 0x672B5B with fallback 0.5 * parent geometry height RW 0x765062,
//     target tracking with 0.83 * target geometry height RW 0x76509D / RW 0xC2D8F0,
//     midpoint repositioning, dirty flag setting).
// - Consumer (W3DLaserDraw):
//   * RW 0x4C8AA1..0x4C8B35 queries LaserUpdate by name key RW 0xBE18A8, checks dirty byte +0x24,
//     clears dirty byte at RW 0x4C8AE2, reads endpoints at +0x0C and +0x18, reads width scalar at +0x3C.
//
// DONOR BFME2 1.06 (readable C++):
//   Code/GameEngine/Source/GameLogic/Object/Update/LaserUpdate.cpp
//   Code/GameEngine/Source/GameLogic/Object/Update/LaserUpdateClientUpdate.cpp
//   Code/GameEngine/Source/GameLogic/Object/Update/LaserUpdateXfer.cpp

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Module.h"

#include <cstdint>
#include <functional>
#include <string>

class ModuleFactory;
class MultiIniFieldParse;
class Object;
class Drawable;
typedef std::uint32_t DrawableID;
class Xfer;

class LaserUpdateModuleData : public ModuleData
{
public:
	LaserUpdateModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);

	std::string m_muzzleParticleSystem;    ///< +0x08 MuzzleParticleSystem (parseAsciiString RW 0x42E57F)
	std::string m_parentFireBoneName;      ///< +0x0C ParentFireBoneName (parseAsciiString RW 0x42E57F)
	bool m_parentFireBoneOnTurret = false; ///< +0x10 ParentFireBoneOnTurret (parseBool RW 0x42E558)
	std::string m_targetParticleSystem;    ///< +0x14 TargetParticleSystem (parseAsciiString RW 0x42E57F)
	float m_laserLifetime = 0.0f;          ///< +0x18 LaserLifetime (parseReal RW 0x42ED00, ms)
};

// RW 0x00764BB3 (getModuleName)
class LaserUpdate : public DrawableModule
{
public:
	// RW 0x764B43 / BFME2 0x362EEE
	LaserUpdate(Thing *thing, const LaserUpdateModuleData *data);
	// RW 0x764BB9 / BFME2 0x362F64
	~LaserUpdate() override;

	const LaserUpdateModuleData *data() const { return m_data; }
	static const char *moduleName() { return "LaserUpdate"; }

	// RW 0x7651B1 / BFME2 0x36355C
	void initLaser(const Object *parent, const Coord3D *startPos, const Coord3D *endPos, int sizeDeltaFrames, unsigned currentFrame = 0);

	// RW 0x7654A8 / BFME2 0x604460
	void initFromDrawables(Drawable *parent, Drawable *target, int sizeDeltaFrames, unsigned currentFrame = 0);

	// RW 0x764E88 / BFME2 0x363233
	// Returns false if expired.
	bool clientUpdate(unsigned currentFrame, const std::function<Drawable *(DrawableID)> &findDrawable = nullptr);

	// RW 0x764C18 / BFME2 0x362FC3
	void decay(unsigned frames, unsigned currentFrame);

	// RW 0x764CC2 / BFME2 0x363065
	float getCurrentLaserRadius() const;

	// RW 0x764C4D / BFME2 0x362FF8
	void xfer(Xfer &x);

	// State accessors
	const Coord3D &startPos() const { return m_startPos; }
	const Coord3D &endPos() const { return m_endPos; }
	void setStartPos(const Coord3D &p) { m_startPos = p; m_dirty = true; }
	void setEndPos(const Coord3D &p) { m_endPos = p; m_dirty = true; }

	bool isDirty() const { return m_dirty; }
	void setDirty(bool dirty) { m_dirty = dirty; }

	float currentWidthScalar() const { return m_currentWidthScalar; }
	void setCurrentWidthScalar(float s) { m_currentWidthScalar = s; m_dirty = true; }

	bool isWidening() const { return m_widening; }
	bool isDecaying() const { return m_decaying; }

	unsigned widenStartFrame() const { return m_widenStartFrame; }
	unsigned widenFinishFrame() const { return m_widenFinishFrame; }
	unsigned decayStartFrame() const { return m_decayStartFrame; }
	unsigned decayFinishFrame() const { return m_decayFinishFrame; }
	unsigned expiryFrame() const { return m_expiryFrame; }

	DrawableID parentID() const { return m_parentID; }
	DrawableID targetID() const { return m_targetID; }

	static void registerClass(ModuleFactory &modules);

private:
	const LaserUpdateModuleData *m_data = nullptr;
	Coord3D m_startPos{ 0.0f, 0.0f, 0.0f };   ///< +0x0C
	Coord3D m_endPos{ 0.0f, 0.0f, 0.0f };     ///< +0x18
	bool m_dirty = false;                     ///< +0x24
	std::uint32_t m_particleSystemID = 0;     ///< +0x28
	std::uint32_t m_targetParticleSystemID = 0; ///< +0x2C
	bool m_widening = false;                  ///< +0x30
	bool m_decaying = false;                  ///< +0x31
	unsigned m_widenStartFrame = 0;           ///< +0x34
	unsigned m_widenFinishFrame = 0;          ///< +0x38
	float m_currentWidthScalar = 1.0f;        ///< +0x3C (RW 0xBD1908: 1.0f)
	unsigned m_decayStartFrame = 0;           ///< +0x40
	unsigned m_decayFinishFrame = 0;          ///< +0x44
	unsigned m_expiryFrame = 0;               ///< +0x48
	DrawableID m_parentID = 0;                ///< +0x4C
	DrawableID m_targetID = 0;                ///< +0x50
};
