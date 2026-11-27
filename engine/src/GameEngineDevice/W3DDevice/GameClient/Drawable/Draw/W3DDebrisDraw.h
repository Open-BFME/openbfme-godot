// OpenBFME. GPL-3.0.
//
// W3DDebrisDraw (both editions).
// The flying debris pieces of a destroyed object (GenericDebris in system.ini). The module data has no fields; the model
// name / colour and the three animation names are set through the DebrisDrawInterface (setModelName / setAnimNames) by the
// object that makes the debris (CreateDebris / OCL, not in this issue). doDrawModule advances the INITIAL -> FLYING -> FINAL
// animation state machine and picks the animation to play.
//
// TARGET FACTS (RotWK game.dat, read through tools/mod/rw.py dis):
// - name() / GetSnapshotName RW 0x004B1601 (returns "W3DDebrisDraw" at RW 0xBDBBC8), factory RW 0x00464B15,
//   getModuleNameKey RW 0x004B15BC, ~W3DDebrisDraw RW 0x004B1607, scalar deleting destructor RW 0x004B17F4.
// - setShadowsEnabled RW 0x004B12FE, setFullyObscuredByShroud RW 0x004B130F, reactToTransformChange RW 0x004B1339,
//   doDrawModule RW 0x004B135C, xfer RW 0x004B16E8, setModelName RW 0x004B1810, setAnimNames RW 0x004B19A4.
// - No fields in the module data (table RW 0x0).
// - RW 0x0054C8B6 is the animation-handle lookup both setters end in: Get_HAnim(name) (DONOR BFME2 W3DDebrisDraw.cpp), called once per
//   name from RW 0x4B19F7 / RW 0x4B1A15 / RW 0x4B1A71 into +0x1C / +0x20 / +0x24, and not at all for an empty name (RW 0x4B19EF /
//   RW 0x4B1A0D store 0). The port keeps the three names and Godot resolves a clip by name when it plays it, so the handles are the
//   names below (W3DDebrisDrawStep::anim selects which one plays); the "STOP" test is RW 0x4B1A2F (the literal RW 0xBDF7F8) and its
//   result is the byte +0x34, m_finalStop.
// DONOR BFME2 1.06 (workspace/reference/open-bfme-2, W3DDebrisDraw*.cpp): ctor 0x000B1C41, setShadowsEnabled 0x000B19D9,
//   setFullyObscuredByShroud 0x000B19EA, reactToTransformChange 0x000B1A14, doDrawModule 0x000B1A37, getModuleNameKey 0x000B1C97,
//   GetSnapshotName 0x000B1CDC, xfer 0x000B1DC3, deleting dtor 0x000B1ECF, setModelName 0x000B1EEB, setAnimNames 0x000B207F.
// Registration: RW 0x464022 (W3DDebrisDraw newInstance).

#pragma once

#include "Common/INIDataTypes.h"
#include "Common/Module.h"

#include <cstdint>
#include <string>

class ModuleFactory;
class MultiIniFieldParse;

class W3DDebrisDrawModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p); // RW table 0x0: no fields
};

// What one doDrawModule step decided (the renderer consumes this; there is no RenderObj here).
struct W3DDebrisDrawStep
{
	enum Anim
	{
		ANIM_INITIAL = 0,
		ANIM_FLYING = 1,
		ANIM_FINAL = 2,
		ANIM_COUNT = 3
	};
	enum Mode
	{
		MODE_MANUAL = 0,
		MODE_LOOP = 1,
		MODE_ONCE = 2
	};
	int anim = ANIM_INITIAL;   ///< which animation plays this frame
	bool animChanged = false;  ///< the animation must be (re)started
	int mode = MODE_ONCE;      ///< ANIM_INITIAL/ANIM_FINAL play once, ANIM_FLYING loops (RW TheAnimModes)
	bool finalFX = false;      ///< the FINAL state was entered this step (the FX list fires once)
	bool hasModel = false;     ///< a model name is set (else doDrawModule draws nothing)
};

class W3DDebrisDraw : public DrawableModule
{
public:
	OBFME_MODULE_XFER(W3DDebrisDraw)

	W3DDebrisDraw(Thing *thing, const W3DDebrisDrawModuleData *data);
	~W3DDebrisDraw() override = default; // RW 0x004B1607 / scalar deleting destructor RW 0x004B17F4

	// DebrisDrawInterface: the debris maker (CreateDebris) sets the model and the three animations.
	// RW 0x004B1810
	void setModelName(const std::string &name, std::int32_t color, int shadowType);
	// RW 0x004B19A4
	void setAnimNames(const std::string &initial, const std::string &flying, const std::string &final);

	// RW 0x004B12FE: sets shadow enabled flag on m_shadow (+0x44 -> +0x04)
	void setShadowsEnabled(bool enable);
	bool shadowsEnabled() const { return m_shadowsEnabled; }

	// RW 0x004B130F: sets shroud-invisible flag on m_shadow (+0x44 -> +0x05)
	void setFullyObscuredByShroud(bool fullyObscured);
	bool isFullyObscuredByShroud() const { return m_fullyObscuredByShroud; }

	// RW 0x004B1339: forwards drawable transform to render object when present
	void reactToTransformChange(const Coord3D *oldPos, float oldAngle) override;
	unsigned transformChangeCount() const { return m_transformChanges; }

	// RW 0x004B135C doDrawModule: advance the state machine. `animationComplete` is the RenderObj's Is_Animation_Complete /
	// HLod answer; `isAboveTerrain` is the object's height test. The step is empty when no model name is set.
	W3DDebrisDrawStep advance(bool isAboveTerrain, bool animationComplete);

	int state() const { return m_state; }
	int frames() const { return m_frames; }
	bool finalStop() const { return m_finalStop; }
	const std::string &modelName() const { return m_modelName; }
	std::int32_t modelColor() const { return m_modelColor; }
	int shadowType() const { return m_shadowType; }
	const std::string &animInitial() const { return m_animInitial; }
	const std::string &animFlying() const { return m_animFlying; }
	const std::string &animFinal() const { return m_animFinal; }

	void crc(StateHasher &hasher) const override;
	static void registerClass(ModuleFactory &modules);

private:
	enum
	{
		INITIAL = 0,
		FLYING = 1,
		FINAL = 2,
		STATECOUNT = 3,
		MIN_FINAL_FRAMES = 3
	};
	const W3DDebrisDrawModuleData *m_data = nullptr;
	std::string m_modelName;    ///< setModelName (+0x10)
	std::int32_t m_modelColor = 0; ///< (+0x14)
	int m_shadowType = 0;
	std::string m_animInitial;  ///< setAnimNames (+0x18)
	std::string m_animFlying;   ///< (+0x1C)
	std::string m_animFinal;    ///< (+0x20)
	int m_state = INITIAL;      ///< 0 INITIAL, 1 FLYING, 2 FINAL (+0x38)
	int m_frames = 0;           ///< (+0x3C)
	bool m_finalStop = false;   ///< the final name was "STOP": freeze on the last frame (+0x40)
	bool m_shadowsEnabled = true;
	bool m_fullyObscuredByShroud = false;
	unsigned m_transformChanges = 0;
};
