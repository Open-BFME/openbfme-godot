// OpenBFME. GPL-3.0.
//
// Device layer: W3D draw surfaces -> Godot shader materials.
//
// One generated `spatial` shader per permutation key (spec 5.3): GPU skinning from the pose palette, the stage 0 / stage 1
// combine of ZH ShaderClass::Apply, texture mappers, DCG, alpha test, and the Godot blend mode of shader.h. The unit's pose
// is a palette texture of 3 RGBA32F texels per pivot (the rows of the 3x4 world matrix); INSTANCE_CUSTOM carries
// x = first palette texel of the instance, y = opacity (pivot fade), w = bone of a rigid mesh.
//
// Vertex data stays in W3D space (right-handed, Z up); the vertex shader converts to Godot's Y-up frame after skinning:
// godot = (x, z, -y).
//
// Known divergences, all reported through the key's Notes (nothing is silent):
//  * the D3D fixed pipeline lights and blends in gamma space; Godot lights in linear space.
//  * gradient ADD is drawn as MODULATE; BUMPENVMAP gradients fall back to the diffuse colour (ZH shader.cpp does the same
//    when the device has no BUMPENV caps).
//  * BFME2 FX materials (normalmapped.fx ...) are approximated; their maths is [U] in the spec.
//  * mapper phase is per material, shared by every instance that uses it (WW3D keeps it per render object).

#pragma once

#include "GameEngineDevice/W3DDevice/GameClient/HouseColor.h"
#include "Libraries/WWVegas/WW3D2/mapper.h"
#include "Libraries/WWVegas/WW3D2/meshrender.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/shader.hpp>
#include <godot_cpp/classes/shader_material.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

class ArchiveFileSystem;
struct W3DObjectLighting;

namespace godot
{

// The shader key, palette layout and shader generator are core code (Libraries/WWVegas/WW3D2/w3dshadergen.h, unit tested without Godot).
using ::W3DShaderKey;
using ::Generate_W3D_Shader_Code;
using ::W3D_PALETTE_WIDTH;
using ::W3D_PALETTE_WIDTH_LOG2;
using ::W3D_PALETTE_TEXELS_PER_PIVOT;

// A mapper bound to one stage of one material; updated every frame with the sync time.
struct W3DMapperBinding
{
	Ref<ShaderMaterial> Material;
	int Stage = 0;
	std::shared_ptr<TextureMapperClass> Mapper;
	std::string Source;               ///< name of the mesh the material was made for (diagnostics, get_mapper_state)
};

struct W3DMaterialResult
{
	Ref<ShaderMaterial> Material;
	W3DShaderKey Key;
	std::vector<W3DMapperBinding> Mappers;
	std::vector<std::string> Notes;   // divergences of this material
	std::vector<std::string> Errors;  // textures that did not load, etc.
};

// Builds materials and caches shaders and textures. Textures are read through the mounted archives.
class W3DMaterialFactory
{
public:
	explicit W3DMaterialFactory(ArchiveFileSystem &fs) : Fs(fs) {}

	// Stop S-119 (lane MAPOBJ-1): with a housecolor.ini table set, a classic surface whose stage 0 texture has a house colour texture gets
	// the HouseColor shader permutation and that texture as `w3d_hc_tex` (the table must outlive the factory). Off (nullptr) by default.
	void Set_House_Colors(const HouseColorTable *table) { HouseColors = table; }
	// lane CAH-2 (S-1408): the colour set of the model being built (W3DInstancer::add_model_colored; kind 0 = none): its house colour textures are
	// recoloured texel by texel as RW 0x531C77 before any filtering, the mipmaps made from the recoloured level (Recolored_House_Texture)
	void Set_House_Params(const HouseColorParams &params) { HouseParams = params; }
	// RW 0x531C77 applied to the texture `name` (its A8R8G8B8 texels; another format stays as it is, as retail's switch leaves it), then mipmaps; cached
	// by "#<path>#<kind>&<c0>&<c1>&<c2>" (retail names the new texture "#<texture>#<options>", RW 0x54BDE0). Invalid when the texture is missing.
	Ref<Texture2D> Recolored_House_Texture(const std::string &name, const HouseColorParams &params, std::vector<std::string> &errors, std::vector<std::string> &notes);

	// loadTextures = false builds the same shader and parameters with white placeholder textures (shader coverage probe).
	W3DMaterialResult Create(const MeshRenderData &mesh, const MeshDrawSurface &surf, const MeshModelClass &source, bool hasUv2, bool loadTextures = true);

	Ref<Shader> Get_Shader(const W3DShaderKey &key);
	// RENDER-1: a texture by its W3D name through the same lookup and cache (W3DStreakDraw); invalid (and an error appended) when missing.
	Ref<Texture2D> Get_Texture(const std::string &name, std::vector<std::string> &errors) { bool found = false; return Load_Texture(name, errors, found); }
	size_t Shader_Count() const { return Shaders.size(); }
	const std::map<std::string, Ref<Shader>> &All_Shaders() const { return Shaders; }
	size_t Texture_Count() const { return Textures.size(); }
	const std::vector<std::string> &Texture_Errors() const { return TextureErrors; }

private:
	Ref<Texture2D> Load_Texture(const std::string &name, std::vector<std::string> &errors, bool &found, bool quietWhenMissing = false);
	static Ref<Texture2D> Placeholder_Texture(bool flatNormal);

	ArchiveFileSystem &Fs;
	const HouseColorTable *HouseColors = nullptr;
	HouseColorParams HouseParams;
	std::map<std::string, Ref<Shader>> Shaders;
	std::map<std::string, Ref<Texture2D>> Textures; // by virtual path
	std::vector<std::string> TextureErrors;
};

// RENDER-1: the light environment of every W3D surface lives in global shader parameters (w3d_amb_obj / w3d_lcol_obj / w3d_ldir_obj and
// the _inf set for infantry). Ensure registers them (before the first W3D shader compiles; the registration's defaults are the
// viewer light of W3D_Apply_Viewer_Lighting). Apply hands a map's light sets to them (SAGE directions -> Godot axes).
void W3D_Ensure_Light_Globals();
void W3D_Apply_Object_Lighting(const W3DObjectLighting &lighting);
// lane RENDER-4 (S-1651): the map's hardware fog (Weather HardwareFogEnable / Color / Start / End) for the W3D models and the CPU particles (the
// globals w3d_fog / w3d_fog_color, W3DHardwareFog.h); colour 0..1 in retail's gamma space
void W3D_Apply_Fog(bool enabled, const float color[3], float start, float end);
// lane PLAY-1: the free camera moves every fog range out by `shift` world units (the global w3d_fog_shift; 0 = retail)
void W3D_Set_Fog_Shift(float shift);
// The light of the model viewers that show no map (not retail data): ambient 0.3 (defaultw3d.fxo's own AmbientLightColor default),
// one white light toward (-0.5, -0.5, 0.7) SAGE, for both sets.
void W3D_Apply_Viewer_Lighting();

} // namespace godot
