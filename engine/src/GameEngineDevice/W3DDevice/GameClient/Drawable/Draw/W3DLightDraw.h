// OpenBFME. GPL-3.0.
//
// W3DLightDraw: light sources (torches, campfires, flashes) with flickering and pulsing (lane MODGAP-2, stop S-113).
//
// Both editions:
//   RotWK 2.01:
//     - friend_newModuleInstance: RW 0x004645EE
//     - friend_newModuleData:     RW 0x00464626
//     - buildFieldParse:          RW 0x004CF4F0, table RW 0x00BDBB08
//     - ModuleData ctor:          RW 0x004CF510, vtable RW 0x00BE3790
//     - W3DLightDraw ctor:        RW 0x004CE750, vtable RW 0x00BE37D0
//     - ~W3DLightDraw:            RW 0x004CE800
//     - doDrawModule:             RW 0x004CE930
//     - reactToTransformChange:   RW 0x004CEAC0
//     - xfer:                     RW 0x004CF488
//   BFME2 1.06:
//     - friend_newModuleInstance: BFME2 0x00464DF8 (or 0x00064E1E)
//     - friend_newModuleData:     BFME2 0x00464E2F (or 0x00064E2F)
//     - buildFieldParse:          BFME2 0x000CFBDF, table BFME2 0x00BCD918
//     - ModuleData ctor:          BFME2 0x000CFB3E, vtable BFME2 0x00BCD820
//     - W3DLightDraw ctor:        BFME2 0x004CEF80 (or 0x000CF91D), vtable BFME2 0x00BCD728
//     - ~W3DLightDraw:            BFME2 0x000CFA42
//     - doDrawModule:             BFME2 0x000CFC0C
//     - reactToTransformChange:   BFME2 0x000CFDFB
//     - xfer:                     BFME2 0x000CFAB1

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/Module.h"

#include <string>

class Xfer;

class W3DLightDrawModuleData : public ModuleData
{
public:
	W3DLightDrawModuleData();
	static void buildFieldParse(MultiIniFieldParse &p);

	RGBColor m_ambient{ 0.0f, 0.0f, 0.0f };   ///< +0x08 Ambient (RW 0x4CF1A0): parsed and kept; its retail use (Init_From_Point RW 0x53F90C) is lane LOCAL-38, not ported here, and never a fallback for a black Diffuse
	RGBColor m_diffuse{ 0.0f, 0.0f, 0.0f };   ///< +0x14 Diffuse
	RGBColor m_specular{ 0.0f, 0.0f, 0.0f };  ///< +0x20 Specular
	float m_radius = 20.0f;                   ///< +0x2C Radius (default 20.0f)
	float m_intensity = 1.0f;                 ///< +0x30 Intensity (default 1.0f)
	float m_flickerAmplitude = 0.0f;          ///< +0x34 FlickerAmplitude (default 0.0f)
	float m_flickerFrequency = 0.0f;          ///< +0x38 FlickerFrequency (default 0.0f)
	float m_pulseAmplitude = 0.0f;            ///< +0x3C PulseAmplitude (default 0.0f)
	float m_pulseFrequency = 0.0f;            ///< +0x40 PulseFrequency (default 0.0f)
	std::string m_attachToBone;               ///< +0x44 AttachToBoneInAnotherModule
};

namespace W3DLightDrawTables
{
const FieldParse *light(); ///< RW 0x00BDBB08 / BFME2 0x00BCD918
}

class ModuleFactory;
class LightPulsePool;

class W3DLightDraw : public DrawableModule
{
public:
	OBFME_MODULE_XFER(W3DLightDraw)

	W3DLightDraw(Thing *thing, const ModuleData *moduleData);
	~W3DLightDraw() override;

	void reactToTransformChange(const Coord3D *oldPos, float oldAngle) override;

	void setEnabled(bool enabled);
	bool isEnabled() const { return m_enabled; }

	float phase() const { return m_phase; }
	float currentHeight() const { return m_height; }
	float currentRadius() const { return m_radius; }
	float currentAngle() const { return m_angle; }

	float lightRadius() const;
	RGBColor lightColor() const;
	Coord3D lightPosition() const;

	void stepFlicker(float dt);
	void updateLight();

	const W3DLightDrawModuleData *getData() const { return m_data; }
	void setLightPool(LightPulsePool *pool);
	int lightHandle() const { return m_lightHandle; }

	static void registerClass(ModuleFactory &modules);

private:
	LightPulsePool *getPool() const;

	Thing *m_thing = nullptr;
	const W3DLightDrawModuleData *m_data = nullptr;
	LightPulsePool *m_pool = nullptr;
	int m_lightHandle = -1;
	float m_phase = 0.0f;                     ///< +0x10
	float m_radius = 0.0f;                    ///< +0x14
	float m_angle = 0.0f;                     ///< +0x18
	float m_height = 0.0f;                    ///< +0x1C
	bool m_enabled = true;
};
