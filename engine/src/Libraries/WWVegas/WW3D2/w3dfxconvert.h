// OpenBFME. GPL-3.0.
//
// Retail's legacy material -> DefaultW3D.fx conversion, as far as the draw side needs it: which classic W3D surfaces the engine replaces with the effect
// (and so lights with defaultw3d.fxo's pixel shaders) and which stay on the fixed-function pipeline.
//
// Sources. Donor (BFME2 decomp): Rva00173230Finish.cpp:82 (MeshModelClass::Register_For_Rendering calls the conversion when LODMeshCacheSetting is set or
// the mesh has flag 4) and MeshMatDescFXShaderConversion.cpp (rva00199FFB, BFME2 0x00199FFB: every refusal below, in its order; the parameters it hands
// DefaultW3D.fx). Target (RotWK game.dat, caveat S-001): the strings of the refusals are at RW 0x7EB978 .. 0x7EC736. The setting behind the flag is
// gamelod.ini's ShaderMaterialReplacement, Yes only at the High and UltraHigh static levels (retail gamelod.ini; GameLODManager.h), which are also the
// shader levels at which FXShaderSetup::InitializeShader (fxshadersetup.cpp:605-665: technique "Default" + the LOD suffix _L / _M / "" / _U, the nearest
// that exists) picks defaultw3d.fxo's technique "Default" (PS_Array, 288 pixel shaders, MaxSkinningBones 90); "Default_L" / "Default_M" (PS_L_Array, ps_1_1)
// serve only the levels below, where no surface is converted.

#pragma once

#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <string>

// What the conversion looks at of one mesh's pass-0 surface.
struct W3DFxConversionSurface
{
	const W3dShaderStruct *Shader = nullptr;
	bool HasVertexMaterial = true; // Has_Material_Data(0) and Has_Shader_Data(0)
	int PassCount = 1;             // MeshMatDescClass::Get_Pass_Count
	int Pass0Surfaces = 1;         // more than one: the mesh has material / shader / texture arrays in pass 0
	bool Pass0TexturesDiffer = false; // the pass-0 surfaces name different stage 0 textures (a stage 0 texture array)
	bool StageTexture[2] = { false, false };
	int MapperType[2] = { 0, 0 };  // W3DMapperType of each stage
	std::string MapperArgs[2];
};

// Empty when the conversion converts the surface; else the first refusal (the retail log message without its "Mesh ... FXShader conversion:" prefix; a
// missing material / shader is refused silently in retail and says so here).
std::string W3D_Fx_Conversion_Refusal(const W3DFxConversionSurface &surface);

// The stop line ("[S-6020] ...") of a surface whose classification rests on the unmatched ID-9 mapper class (docs/STOPS.md S-6020): a Grid mapper in either
// stage. Empty otherwise.
std::string W3D_Fx_Conversion_Stop(const W3DFxConversionSurface &surface);

// The stop line ("[S-6022] ...") of a surface retail would not give a team colour: MeshClass::RecolorHouseColor's fixed-function branch (bfme2_mesh_material_pass.cpp,
// RW 0x54BDE0) needs a single pass and no stage 0 texture array, and a surface retail converted (`converted`) takes the FX branch instead. Empty when the surface qualifies.
std::string W3D_House_Color_Stop(const W3DFxConversionSurface &surface, bool converted);

// SecondaryTextureBlendMode of the conversion's DetailColor mapping (MeshMatDescFXShaderConversion.cpp:416-433): Scale 0, Scale2X 1, DetailBlend 2,
// ModAlphaAddColor 3, HouseColor (13, which also sets HouseColorEnable) 3; -1 for every other function (refused) and for Disable (NumTextures drops to 1).
int W3D_Fx_Secondary_Blend_Mode(int detailColorFunc);
