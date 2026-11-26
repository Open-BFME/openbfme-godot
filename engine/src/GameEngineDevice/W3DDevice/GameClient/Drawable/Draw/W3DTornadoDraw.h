// OpenBFME. GPL-3.0.
//
// W3DTornadoDraw: tornado visual draw module managing ground radius decals (stop S-113).
// NOT PORTED here: the bound drawable's custom tint envelope (RW 0x4D120A -> 0x4D1241..0x4D1281: 1.5, 1.5, 1.5, attack 30 frames), which needs the drawable's
// tint envelope (TintEnvelope, RW 0x671B20 ..); stop S-113.
//
// Both editions:
//   RotWK 2.01:
//     - friend_newModuleInstance: RW 0x00464930
//     - friend_newModuleData:     RW 0x00464968
//     - buildFieldParse:          RW 0x004D0EB4, table RW 0x00BE4130
//     - W3DTornadoDrawModuleData: RW 0x004D0EC5, vtable RW 0x00BE4178
//     - W3DTornadoDraw ctor:      RW 0x004D102E, vtable RW 0x00BE41F8
//     - getModuleNameKey:         RW 0x004D1077
//     - ~W3DTornadoDraw:          RW 0x004D10BC
//     - rva000D18F8 (decals):     RW 0x004D1109
//     - xfer:                     RW 0x004D0F87
//   BFME2 1.06:
//     - friend_newModuleInstance: BFME2 0x0046513A
//     - friend_newModuleData:     BFME2 0x00465171
//     - buildFieldParse:          BFME2 0x004D16A3, table BFME2 0x00BCE150
//     - W3DTornadoDrawModuleData: BFME2 0x004D16B4, vtable BFME2 0x00BCE198
//     - W3DTornadoDraw ctor:      BFME2 0x004D181D, vtable BFME2 0x00BCE218
//     - getModuleNameKey:         BFME2 0x004D1866
//     - ~W3DTornadoDraw:          BFME2 0x004D18AB
//     - rva000D18F8 (decals):     BFME2 0x004D18F8
//     - xfer:                     BFME2 0x004D1776

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/Module.h"
#include "GameClient/RadiusCursor.h"
#include "GameClient/SelectionDecals.h"

#include <cstdint>
#include <vector>

class W3DTornadoDrawModuleData : public ModuleData
{
public:
	W3DTornadoDrawModuleData();
	static void buildFieldParse(MultiIniFieldParse &p);

	RadiusDecalTemplate m_decalTemplate;   ///< +0x08 DecalTemplate (RW 0x7327BC / BFME2 0x330E5D)
	unsigned m_decalCount = 0;             ///< +0x3C DecalCount (default 0)
	float m_decalMaxRadius = 200.0f;       ///< +0x40 DecalMaxRadius (default 200.0f)
};

namespace W3DTornadoDrawTables
{
const FieldParse *tornado(); ///< RW 0x00BE4130 / BFME2 0x00BCE150
}

class ModuleFactory;

class W3DTornadoDraw : public DrawableModule
{
public:
	OBFME_MODULE_XFER(W3DTornadoDraw)
	W3DTornadoDraw(Thing *thing, const ModuleData *moduleData);
	~W3DTornadoDraw() override;

	// Both editions (RW 0x4D120A / BFME2): creates the decals when the drawable is bound to its object
	void onDrawableBoundToObject() override;
	void reactToTransformChange(const Coord3D *oldPos, float oldAngle) override;
	void createDecals();
	void clearDecals();
	// Both editions (RW 0x4D0FCB -> 0x732828 + 0x732926): advances spiral radius/angle/fade/opacity once per client frame
	void stepDecals(unsigned clientFrame, bool drawIconUI = true, std::uint32_t playerColor = 0xFFFFFFu);
	void appendSelectionDecals(std::uint32_t objectId, unsigned drawableId, bool playerIsLocal, std::uint32_t playerColor,
		unsigned clientFrame, bool drawIconUI, std::vector<SelectionDecal> &out);

	unsigned decalCount() const { return (unsigned)m_decals.size(); }
	const W3DTornadoDrawModuleData *getData() const { return m_data; }

	struct DecalInstance
	{
		Coord3D position{};
		float radius = 0.0f;
		float angle = 0.0f;
		std::uint32_t color = 0xFFFFFFFFu;
		int opacity = 255;
		bool hasShadow = true;
	};
	const std::vector<DecalInstance> &decals() const { return m_decals; }

	static void registerClass(ModuleFactory &modules);

private:
	Thing *m_thing = nullptr;
	const W3DTornadoDrawModuleData *m_data = nullptr;
	std::vector<DecalInstance> m_decals;
	bool m_hasLastStepFrame = false;
	unsigned m_lastStepFrame = 0;
};
