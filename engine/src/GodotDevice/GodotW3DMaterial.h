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

#include "GameClient/AssetPacks.h"
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

// lane SDK-1 (GameClient/AssetPacks.h): the asset pack replacement of the texture `name` decoded by its own bytes (DDS, PNG, JPEG, else TGA),
// or a null image when no pack replaces it. A pack file that cannot be read or decoded is a null image with `error` set ("asset pack P: F ...").
// Shared by every texture loader of the render side (model surfaces, particles, decals, roads / rivers / water / sky): one precedence.
Ref<Image> Load_Asset_Pack_Image(const AssetPacks *packs, const std::string &name, std::string &error);

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
	// stop S-5015: the texture swaps of the model being built (W3DInstancer::Set_Build_Texture_Swaps, a ModelConditionState's `Texture = A B` pairs):
	// a surface texture named A (any case; A = "*": every texture) is loaded as B. Empty by default.
	void Set_Texture_Swaps(const std::vector<std::pair<std::string, std::string>> &swaps) { TextureSwaps = swaps; }
	// lane SDK-0 (GameClient/AssetPacks.h): cosmetic asset packs in front of the texture lookup; their files decode by their own bytes (DDS, PNG,
	// JPEG, else TGA). nullptr (the default) = retail textures only. The packs must outlive the factory.
	void Set_Asset_Packs(const AssetPacks *packs) { Packs = packs; }
	// lane BUILD-5 r3: the surfaces this factory builds belong to a translucent drawable (W3DShaderKey::AlphaOverride); off by default
	void Set_Alpha_Override(bool on) { AlphaOverride = on; }
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

	// lane OPTS-2: the texture reduction of the static detail level (GameLODManager's state, RW 0x601D73 -> RW 0x517540, WW3D's texture reduction),
	// for every factory (one setting, as retail's): a texture with mip levels loses its `reduction` largest levels (each halves its size), at most all but
	// its last (DONOR ZH TextureLoader: the reduction is clamped to the mip count; INFERENCE for RotWK: the loader RW 0x517540 changes was not read past
	// the setter). A texture without mip levels (filter_linear surfaces, the menus' images) keeps its size. Textures loaded afterwards take it: the cache
	// keys carry the reduction, so a change reaches the models built after it (the next game; retail reloads the live textures at once, RW 0x516AB0:
	// stop S-3402).
	static void Set_Texture_Reduction(int reduction);
	static int Get_Texture_Reduction();
	// the reduction applied to one decoded image (true when it lost levels)
	static bool Reduce_Image(Ref<Image> &image, int reduction);

	// The static level's ShaderMaterialReplacement (gamelod.ini; RW 0xDD1E50, RW 0x516A30): on, every classic surface W3D_Fx_Conversion_Refusal accepts is
	// lit as retail's DefaultW3D.fx replacement (W3DShaderKey::ConvertedFx); off, none is (the fixed-function stage ops). One setting for every factory,
	// as retail's. Default on: the level the port draws (GameLODManager::kDetectedLevel, UltraHigh). INFERENCE / stop S-6021: the Options screen's
	// ShaderLOD does not reach it yet.
	static void Set_Shader_Material_Replacement(bool on);
	static bool Get_Shader_Material_Replacement();

private:
	Ref<Texture2D> Load_Texture(const std::string &name, std::vector<std::string> &errors, bool &found, bool quietWhenMissing = false, std::vector<std::string> *notes = nullptr);
	static Ref<Texture2D> Placeholder_Texture(bool flatNormal);

	ArchiveFileSystem &Fs;
	const HouseColorTable *HouseColors = nullptr;
	const AssetPacks *Packs = nullptr; // lane SDK-0
	HouseColorParams HouseParams;
	std::vector<std::pair<std::string, std::string>> TextureSwaps; // stop S-5015
	bool AlphaOverride = false;
	std::map<std::string, Ref<Shader>> Shaders;
	std::map<std::string, Ref<Texture2D>> Textures; // by virtual path
	std::map<std::string, std::string> PackedHouseTextureNotes; // retain S-1482 on cache hits
	std::map<std::string, std::string> PackedTextureNotes; // retain S-1482 on cache hits
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
// lane VISUAL-2: the vegetation SwayOffsets 1 .. 10 (W3D axes) the rigid W3D shaders read (w3dshadergen.cpp)
void W3D_Set_Sway_Offsets(const float offsets[10][3]);
// lane VISUAL-2: the fogged light fraction (RW 0x46DFD0: FogAlpha / ClearAlpha) a fogged instance's lights and ambient are scaled by
void W3D_Set_Fogged_Light_Frac(float frac);
// lane OCCLUDE-1: the behind-building colours (global w3d_occl_colors: a W3D_OCCLUSION_SLOTS x 1 RGBA8 texture, slot -> the colour, slot 0 unused) and
// their switch (w3d_occl_on: GameData UseBehindBuildingMarker and TheGameLogic + 0x99); the caller owns the texture (GameWorld)
void W3D_Set_Occlusion(bool on, const Ref<Texture2D> &colors);
void W3D_Set_Fog_Shift(float shift);
// The light of the model viewers that show no map (not retail data): ambient 0.3 (defaultw3d.fxo's own AmbientLightColor default),
// one white light toward (-0.5, -0.5, 0.7) SAGE, for both sets.
void W3D_Apply_Viewer_Lighting();

} // namespace godot
