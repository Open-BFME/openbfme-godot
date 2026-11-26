// OpenBFME. GPL-3.0.
//
// The client-side supply-bone status of W3DSupplyDraw. The model itself still uses the shared
// W3DScriptedModelDraw runtime; this adapter only applies the SupplyBonePrefix visibility delta.
//
// TARGET (RotWK game.dat, caveat S-001): W3DSupplyDraw is registered at RW 0xBDBB5C; SupplyBonePrefix is the
// field at RW 0xBE1E24 (parser RW 0x42EE5E, module-data offset +0x188). SupplyWarehouseDockUpdate::action
// RW 0x8AB53D..0x8AB63A calls the drawable dispatcher RW 0x671948 after a successful box transfer. Its supply-draw
// vtable is RW 0xBE1E58 (slot 8 / +0x20 -> RW 0x4CA784). That reader obtains the total prefix count through
// Drawable::getPristineBonePositions RW 0x672A73, computes ceil(current / starting * total) at RW 0x4CA7E1..0x4CA7F6,
// clamps to total, and changes only the numbered range at RW 0x4CA80D..0x4CA8AD. The visibility vcall is +0x88;
// RW 0x4C3B25 records its visible flag as the inverse hide bit in the plain sub-object list and applies it.
//
// INFERENCE bounded by the mod data: a non-positive StartingBoxes value is treated as empty. The supplyrocks.ini
// definitions use positive StartingBoxes (150 or 300); that invalid-input case is not exercised by this port.
// Editions: RotWK 2.01 (the RW addresses above); BFME2 1.06 has the class too (DONOR BFME2 0x000CAEDF xfer).

#pragma once

#include "Common/ModelState.h"
#include "GameClient/DrawableLaunchBones.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"

class ThingTemplate;
class W3DScriptedModelDraw;
class Xfer;

// RW 0x004CA69F (~W3DSupplyDraw), RW 0x004CA70E (~W3DSupplyDrawModuleData)
class W3DSupplyDraw
{
public:
	W3DSupplyDraw(const ThingTemplate &thingTemplate, const W3DSupplyDrawModuleData &data, W3DDrawAssets &assets);

	// RW 0x4CA784: update this supply draw's numbered sub-objects from the drawable's aggregate pristine-bone query.
	// The query is cached on first use just as the retail module caches +0x2DC/+0x2E0; subsequent status events only
	// visit the changed range. Returns true if a sub-object visibility operation was applied.
	bool setSupplyStatus(W3DScriptedModelDraw &draw, const ModelConditionFlags &flags, float scale, int currentBoxes, int startingBoxes);
	// Reapply the last ordered status after a model condition change recreates the local render object; runtime-only, never serialized.
	bool refresh(W3DScriptedModelDraw &draw, const ModelConditionFlags &flags, float scale);

	// RW 0x4CA7E1..0x4CA805: ceil(current / starting * total), clamped to [0, total].
	static int visibleBoneCount(int currentBoxes, int startingBoxes, int totalBones);

	int totalBones() const { return m_totalBones; }
	int visibleBones() const { return m_visibleBones; }
	void setTotalAndVisibleBones(int total, int visible) { m_totalBones = total; m_visibleBones = visible; }
	// RW 0x004CA6B8 (DONOR BFME2 0x000CAEDF): version(1, 1), xfers m_totalBones (+0x2E8) and m_visibleBones (+0x2EC)
	void xfer(Xfer &x);

private:
	void findTotalBones(const ModelConditionFlags &flags, float scale);

	const ThingTemplate &m_template;
	const W3DSupplyDrawModuleData &m_data;
	DrawableLaunchBones m_boneQuery;
	int m_totalBones = -1;
	int m_visibleBones = -1;
	int m_lastRenderObjectsCreated = -1;
	int m_currentBoxes = 0, m_startingBoxes = 0;
	bool m_hasSupplyStatus = false;
};
