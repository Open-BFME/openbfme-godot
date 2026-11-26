// OpenBFME. GPL-3.0.
// RotWK 2.01 W3DSailModelDraw runtime (RW 0x4CFF82 ctor, vtable 0xBE3C40, +0x104 = RW 0x4D00CD).
// The scripted-model base renders the model; only its assembled matrix gets the sail's local Z turn.
// The fixed direction, clamp and per-call damping are read from RW 0x4D00CD..0x4D033B
// (DONOR BFME2 0x000D08C3, W3DSailModelDrawCtor.cpp). BlowingThresholdDegrees is parsed but
// no function of the 68-slot sail vtable reads it.
// The RW client xfer (0x4D0009) saves the angle and valid flag; the port does not yet
// snapshot *client draw runtimes*, so this visual-only state is not simulation state.
// Editions: RotWK 2.01 (RW addresses above); BFME2 1.06 has the same class (DONOR BFME2 0x000D08C3 ctor, 0x000D0816 xfer).
#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

class Matrix3D;
class Xfer;

// RW 0x004D00C8 (~W3DSailModelDraw)
class W3DSailModelDraw final : public W3DScriptedModelDraw
{
public:
	W3DSailModelDraw(const W3DSailModelDrawModuleData &data, W3DDrawAssets &assets, W3DDrawRandom &random,
		W3DDrawScriptHost *scripts = nullptr, const Options &options = Options());
	void adjustAttachedTransform(Matrix3D &matrix) override;
	// RW 0x004D0009 (DONOR BFME2 0x000D0816): version(1, 1), xfers m_lastAngle (+0x2E8) and m_hasLastAngle (+0x2EC)
	void xfer(Xfer &x);
	float lastAngle() const { return m_lastAngle; }
	bool hasLastAngle() const { return m_hasLastAngle; }

private:
	const W3DSailModelDrawModuleData &m_sailData;
	float m_lastAngle = 0.0f; // RW +0x2E8
	bool m_hasLastAngle = false; // RW +0x2EC: first matrix takes the target without damping
};
