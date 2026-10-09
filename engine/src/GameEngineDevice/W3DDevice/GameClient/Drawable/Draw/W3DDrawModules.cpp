// OpenBFME. GPL-3.0.
//
// See W3DDrawModules.h.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DStreakDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"

namespace
{
// Registry order of the 20 DRAW classes (module-registry.json).
const std::vector<W3DDrawClassInfo> kClasses = {
	{ "W3DBoatWakeModelDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DBuffDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DDebrisDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DDefaultDraw", W3D_DRAWKIND_NOTHING },
	{ "W3DFloorDraw", W3D_DRAWKIND_FLOOR },
	{ "W3DHordeModelDraw", W3D_DRAWKIND_MODEL },
	{ "W3DLaserDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DLightDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DProjectileStreamDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DPropDraw", W3D_DRAWKIND_PROP },
	{ "W3DQuadrupedDraw", W3D_DRAWKIND_MODEL },
	{ "W3DRopeDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DSailModelDraw", W3D_DRAWKIND_MODEL },
	{ "W3DScriptedModelDraw", W3D_DRAWKIND_MODEL },
	{ "W3DStreakDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DSupplyDraw", W3D_DRAWKIND_MODEL },
	{ "W3DTankDraw", W3D_DRAWKIND_MODEL },
	{ "W3DTornadoDraw", W3D_DRAWKIND_NOT_DRAWN },
	{ "W3DTreeDraw", W3D_DRAWKIND_TREE },
	{ "W3DTruckDraw", W3D_DRAWKIND_MODEL },
};
} // namespace

const std::vector<W3DDrawClassInfo> &W3DDrawModules::all()
{
	return kClasses;
}

const W3DDrawClassInfo *W3DDrawModules::find(const std::string &className)
{
	for (const W3DDrawClassInfo &c : kClasses)
	{
		if (className == c.name) // module class names are case sensitive (RW 0x655A0C decorated name key)
		{
			return &c;
		}
	}
	return nullptr;
}

void W3DDrawModules::registerTypedDrawModuleData(ModuleFactory &modules)
{
	modules.bindTypedData<W3DScriptedModelDrawModuleData>("W3DScriptedModelDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DHordeModelDrawModuleData>("W3DHordeModelDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DDefaultDrawModuleData>("W3DDefaultDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DTreeDrawModuleData>("W3DTreeDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DPropDrawModuleData>("W3DPropDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DFloorDrawModuleData>("W3DFloorDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DTruckDrawModuleData>("W3DTruckDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DSailModelDrawModuleData>("W3DSailModelDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DQuadrupedDrawModuleData>("W3DQuadrupedDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DTankDrawModuleData>("W3DTankDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DSupplyDrawModuleData>("W3DSupplyDraw", MODULETYPE_DRAW);
	modules.bindTypedData<W3DStreakDrawModuleData>("W3DStreakDraw", MODULETYPE_DRAW); // RENDER-1: RW 0x46473E / 0xBE39F8
}

std::vector<std::string> W3DDrawModules::notDrawnClasses()
{
	std::vector<std::string> out;
	for (const W3DDrawClassInfo &c : kClasses)
	{
		if (c.kind == W3D_DRAWKIND_NOT_DRAWN)
		{
			out.push_back(std::string(c.name) + ": effect draw, not drawn (S-113)");
		}
		else if (c.kind == W3D_DRAWKIND_NOTHING)
		{
			out.push_back(std::string(c.name) + ": draws nothing in a release build");
		}
	}
	return out;
}
