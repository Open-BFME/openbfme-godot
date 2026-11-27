// OpenBFME. GPL-3.0.
//
// SwayClientUpdate (LOCAL-103; editions: both RotWK 2.01 and BFME2 1.06):
// Client update module that sways a drawable (e.g. cinematic trees PTree_cine / PTree_cine2)
// according to TheScriptEngine's BreezeInfo by pre-rotating Drawable::m_instanceMatrix.
//
// TARGET FACTS (RotWK 2.01, donor BFME2 1.06 Code/GameEngine/Source/GameClient/Drawable/Update/SwayClientUpdate.cpp):
// - getModuleName:     RW 0x008CD514 (returns "SwayClientUpdate", RW 0x00C0A664)
// - getModuleNameKey:  RW 0x008CD51A / DONOR BFME2 0x004C90E0 (cached NameKeyGenerator key for "SwayClientUpdate")
// - updateSway:        RW 0x008CD55F / DONOR BFME2 0x004C9125:
//     reads BreezeInfo (TheScriptEngine + 0x1A5A8); if randomness == 0 resets m_curValue = 0;
//     delta = randomness * 0.5;
//     m_curAngleLimit = intensity * rnd(1 - delta, 1 + delta);
//     m_curDelta = (2 * PI) / (fps * breezePeriod) * rnd(1 - delta, 1 + delta);
//     m_leanAngle = lean * rnd(1 - delta, 1 + delta);
//     m_curVersion = breezeVersion;
//     if (draw) { angle = direction - draw->getZRotation(); m_directionSin = sin(angle); m_directionCos = cos(angle); }
// - clientUpdate:      RW 0x008CD675 / DONOR BFME2 0x004C923B:
//     if (!m_swaying) return;
//     if (breezeVersion != m_curVersion) updateSway();
//     else if (!draw || !visible) return;
//     m_curValue += m_curDelta; if (m_curValue > 2*PI) m_curValue -= 2*PI;
//     targetAngle = sin(m_curValue) * m_curAngleLimit + m_leanAngle;
//     deltaAngle = targetAngle - m_curAngle;
//     xfrm = draw->getInstanceMatrix();
//     xfrm.In_Place_Pre_Rotate_X(-deltaAngle * m_directionSin);
//     xfrm.In_Place_Pre_Rotate_Y(deltaAngle * m_directionCos);
//     draw->setInstanceMatrix(xfrm);
//     m_curAngle = targetAngle;
//     if (burned || effectivelyDead) stopSway();
// - xfer:              RW 0x008CD93F / DONOR BFME2 0x004C9505 (ObjectModule::xfer, IsLightCRC early-out, version 1,
//     5 floats m_curValue..m_leanAngle, int16 m_curVersion, bool m_swaying, 2 floats m_directionSin/Cos)
// - loadPostProcess:   RW 0x008CD9CD / DONOR BFME2 0x004C9593 (tail-jumps to updateSway RW 0x008CD55F)
// RW 0x652730 (SwayClientUpdate newInstance)
#pragma once

#include "Common/Module.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DVegetationSway.h"

#include <cstdint>

class ModuleFactory;
class MultiIniFieldParse;
class NameKeyGenerator;
class W3DDrawRandom;
class Xfer;

class SwayClientUpdateModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SwayClientUpdate : public DrawableModule
{
public:
	SwayClientUpdate(Thing *thing, const SwayClientUpdateModuleData *data);
	~SwayClientUpdate() override = default;

	// RW 0x008CD514: returns "SwayClientUpdate"
	static const char *getModuleName();
	// RW 0x008CD51A (DONOR BFME2 0x004C90E0): cached NameKeyType for "SwayClientUpdate"
	static NameKeyType getModuleNameKey(NameKeyGenerator &gen);

	// RW 0x008CD55F (DONOR BFME2 0x004C9125)
	void updateSway(const W3DVegetationSway::Breeze &info, W3DDrawRandom &random,
		float fps = W3DVegetationSway::kRetailSwayRate);

	// RW 0x008CD675 (DONOR BFME2 0x004C923B)
	void clientUpdate(const W3DVegetationSway::Breeze &info, W3DDrawRandom &random,
		bool visible, bool burnedOrDead,
		float fps = W3DVegetationSway::kRetailSwayRate);

	// RW 0x008CD93F (DONOR BFME2 0x004C9505)
	OBFME_MODULE_XFER(SwayClientUpdate);

	// RW 0x008CD9CD (DONOR BFME2 0x004C9593): re-runs updateSway()
	void loadPostProcess(const W3DVegetationSway::Breeze &info, W3DDrawRandom &random,
		float fps = W3DVegetationSway::kRetailSwayRate);

	void stopSway() { m_swaying = false; }

	float curValue() const { return m_curValue; }
	float curAngle() const { return m_curAngle; }
	float curDelta() const { return m_curDelta; }
	float curAngleLimit() const { return m_curAngleLimit; }
	float leanAngle() const { return m_leanAngle; }
	std::int16_t curVersion() const { return m_curVersion; }
	bool isSwaying() const { return m_swaying; }
	float directionSin() const { return m_directionSin; }
	float directionCos() const { return m_directionCos; }

	static void registerClass(ModuleFactory &modules);

private:
	float m_curValue = 0.0f;       ///< +0x0C
	float m_curAngle = 0.0f;       ///< +0x10
	float m_curDelta = 0.0f;       ///< +0x14
	float m_curAngleLimit = 0.0f;  ///< +0x18
	float m_leanAngle = 0.0f;      ///< +0x1C
	std::int16_t m_curVersion = -1;///< +0x20
	bool m_swaying = true;         ///< +0x22
	float m_directionSin = 0.0f;   ///< +0x24
	float m_directionCos = 0.0f;   ///< +0x28
};
