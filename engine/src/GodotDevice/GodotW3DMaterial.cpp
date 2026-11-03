// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotW3DMaterial.h"
#include "Common/BfmeGame.h"
#include "GodotDevice/GodotPackedTexture.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <cstdio>

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/shader.h"
#include "Libraries/WWVegas/WW3D2/w3dfxconvert.h"
#include "Libraries/WWVegas/WW3D2/textureloader.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector4.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace godot
{

namespace
{

// RotWK / BFME2: a .dds is a DDS, anything else a TGA. community PR #36, BFME1 mounted only (docs/BFME1.md): BFME1 installs carry PNG / JPEG files,
// some under .dds names, so the bytes' signature decides
Error decodeTexture(const Ref<Image> &image, const PackedByteArray &buffer, bool dds)
{
	if (bfme1Mounted())
	{
		static const unsigned char png[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
		if (buffer.size() >= 8 && std::memcmp(buffer.ptr(), png, sizeof(png)) == 0)
		{
			return image->load_png_from_buffer(buffer);
		}
		if (buffer.size() >= 3 && buffer[0] == 0xff && buffer[1] == 0xd8 && buffer[2] == 0xff)
		{
			return image->load_jpg_from_buffer(buffer);
		}
	}
	return dds ? image->load_dds_from_buffer(buffer) : image->load_tga_from_buffer(buffer);
}

// lane SDK-0: an asset pack's texture is decoded by its bytes (a modder may save a PNG for a retail .tga name)
Error decodePackTexture(const Ref<Image> &image, const PackedByteArray &buffer)
{
	static const unsigned char png[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	if (buffer.size() >= 4 && std::memcmp(buffer.ptr(), "DDS ", 4) == 0)
	{
		return image->load_dds_from_buffer(buffer);
	}
	if (buffer.size() >= 8 && std::memcmp(buffer.ptr(), png, sizeof(png)) == 0)
	{
		return image->load_png_from_buffer(buffer);
	}
	if (buffer.size() >= 3 && buffer[0] == 0xff && buffer[1] == 0xd8 && buffer[2] == 0xff)
	{
		return image->load_jpg_from_buffer(buffer);
	}
	return image->load_tga_from_buffer(buffer);
}

String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

W3DSamplerPolicy samplerPolicy()
{
	return RenderingServer::get_singleton()->get_current_rendering_driver_name() == "metal"
		? W3DSamplerPolicy::Metal16 : W3DSamplerPolicy::Anisotropic;
}

} // namespace

Ref<Image> Load_Asset_Pack_Image(const AssetPacks *packs, const std::string &name, std::string &error)
{
	const AssetPackFile *pf = packs ? packs->texture(name) : nullptr;
	if (!pf)
	{
		return Ref<Image>();
	}
	std::vector<std::uint8_t> bytes;
	if (!AssetPacks::read(*pf, bytes, &error))
	{
		return Ref<Image>();
	}
	PackedByteArray buffer;
	buffer.resize((int64_t)bytes.size());
	if (!bytes.empty())
	{
		std::memcpy(buffer.ptrw(), bytes.data(), bytes.size());
	}
	Ref<Image> image;
	image.instantiate();
	if (decodePackTexture(image, buffer) != OK || image->is_empty())
	{
		error = "asset pack " + pf->pack + ": could not decode " + pf->relative + " (the replacement of texture " + name + ")";
		return Ref<Image>();
	}
	image->set_meta("retail_path", toGodot("asset pack " + pf->pack + ": " + pf->relative));
	return image;
}

namespace
{

Vector3 sageToGodot(const float v[3])
{
	return Vector3(v[0], v[2], -v[1]);
}

// columns = lights 0..2
Basis columns(const float (&v)[3][3], bool direction)
{
	Basis b;
	for (int i = 0; i < 3; ++i)
	{
		b.set_column(i, direction ? sageToGodot(v[i]) : Vector3(v[i][0], v[i][1], v[i][2]));
	}
	return b;
}

void setSet(const char *suffix, const W3DLightSet &set)
{
	RenderingServer *rs = RenderingServer::get_singleton();
	rs->global_shader_parameter_set(StringName(String("w3d_amb_") + suffix), Vector3(set.ambient[0], set.ambient[1], set.ambient[2]));
	rs->global_shader_parameter_set(StringName(String("w3d_lcol_") + suffix), columns(set.color, false));
	rs->global_shader_parameter_set(StringName(String("w3d_ldir_") + suffix), columns(set.toLight, true));
}

W3DLightSet viewerSet()
{
	W3DLightSet s;
	for (int c = 0; c < 3; ++c)
	{
		s.ambient[c] = 0.3f;
		s.color[0][c] = 1.0f;
	}
	const float d[3] = { -0.5f, -0.5f, 0.70710678f };
	for (int c = 0; c < 3; ++c)
	{
		s.toLight[0][c] = d[c];
	}
	s.count = 1;
	return s;
}

} // namespace

void W3D_Ensure_Light_Globals()
{
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs)
	{
		return;
	}
	// once per process (the parameter list query is an editor-only function, and adding a name twice is an error)
	static bool registered = false;
	if (registered)
	{
		return;
	}
	registered = true;
	rs->global_shader_parameter_add(StringName("w3d_shadow"), RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4(1.0f, 1.0f, 1.0f, 0.0f));
	if (samplerPolicy() == W3DSamplerPolicy::Metal16)
	{
		UtilityFunctions::print("W3D [MAC-10 Metal16 limitation]: repeating mipmapped textures use linear-mipmap filtering instead of anisotropy (Godot 4.7.2 binds anisotropic-repeat at Metal slot 17; limit 15). Clamped textures retain anisotropy; simulation unchanged.");
	}
	const W3DLightSet v = viewerSet();
	for (const char *suffix : { "obj", "inf" })
	{
		rs->global_shader_parameter_add(StringName(String("w3d_amb_") + suffix), RenderingServer::GLOBAL_VAR_TYPE_VEC3, Vector3(v.ambient[0], v.ambient[1], v.ambient[2]));
		rs->global_shader_parameter_add(StringName(String("w3d_lcol_") + suffix), RenderingServer::GLOBAL_VAR_TYPE_MAT3, columns(v.color, false));
		rs->global_shader_parameter_add(StringName(String("w3d_ldir_") + suffix), RenderingServer::GLOBAL_VAR_TYPE_MAT3, columns(v.toLight, true));
	}
	// lane RENDER-4 (S-1651): the map's hardware fog (W3DHardwareFog.h), off until a map sets it
	rs->global_shader_parameter_add(StringName("w3d_fog"), RenderingServer::GLOBAL_VAR_TYPE_VEC3, Vector3(0.0f, 0.0f, 1.0f));
	rs->global_shader_parameter_add(StringName("w3d_fog_color"), RenderingServer::GLOBAL_VAR_TYPE_VEC3, Vector3(0.5f, 0.5f, 0.5f));
	// lane PLAY-1: the free camera's fog shift (TacticalCamera::fogShift): the distance every fog range is measured from; 0 = retail
	rs->global_shader_parameter_add(StringName("w3d_fog_shift"), RenderingServer::GLOBAL_VAR_TYPE_FLOAT, 0.0f);
	// lane VISUAL-2: the fogged light fraction (RW 0x46DFD0: GameData FogAlpha / ClearAlpha), set by the map's game
	rs->global_shader_parameter_add(StringName("w3d_fogged_frac"), RenderingServer::GLOBAL_VAR_TYPE_FLOAT, 1.0f);
	// lane OCCLUDE-1: the behind-building colours and their switch (W3D_Set_Occlusion), off until a game sets them
	rs->global_shader_parameter_add(StringName("w3d_occl_on"), RenderingServer::GLOBAL_VAR_TYPE_FLOAT, 0.0f);
	rs->global_shader_parameter_add(StringName("w3d_occl_colors"), RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2D, Variant());
	// lane VISUAL-2: the vegetation SwayOffsets 1 .. 10 (W3DVegetationSway, RW 0x4E61EE; W3D axes), zero until a map's trees sway
	for (int i = 1; i <= 10; ++i)
	{
		rs->global_shader_parameter_add(StringName(String("w3d_sway") + String::num_int64(i)), RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4());
	}
	// lane SELPERF-1: the selection's merge decals the terrain shader draws (godot/shaders/selection_decals.gdshaderinc; the HUD sets them, mode 0 = none)
	rs->global_shader_parameter_add(StringName("sel_data"), RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2D, Variant());
	rs->global_shader_parameter_add(StringName("sel_atlas"), RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2D, Variant());
	rs->global_shader_parameter_add(StringName("sel_grid"), RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4(0.0f, 0.0f, 1.0f, 0.0f));
	rs->global_shader_parameter_add(StringName("sel_grid2"), RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4(0.0f, 0.0f, 0.0f, 0.0f));
	rs->global_shader_parameter_add(StringName("sel_alpha_ref"), RenderingServer::GLOBAL_VAR_TYPE_FLOAT, 96.0f);
	// The terrain shader blends the targeted power's texture with selection decals into ALBEDO.
	rs->global_shader_parameter_add(StringName("radius_cursor_texture"), RenderingServer::GLOBAL_VAR_TYPE_SAMPLER2D, Variant());
	rs->global_shader_parameter_add(StringName("radius_cursor_transform"), RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4());
	rs->global_shader_parameter_add(StringName("radius_cursor_params"), RenderingServer::GLOBAL_VAR_TYPE_VEC4, Vector4());
}

void W3D_Set_Fog_Shift(float shift)
{
	W3D_Ensure_Light_Globals();
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		rs->global_shader_parameter_set(StringName("w3d_fog_shift"), shift);
	}
}

void W3D_Set_Fogged_Light_Frac(float frac)
{
	W3D_Ensure_Light_Globals();
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		rs->global_shader_parameter_set(StringName("w3d_fogged_frac"), frac);
	}
}

void W3D_Set_Occlusion(bool on, const Ref<Texture2D> &colors)
{
	W3D_Ensure_Light_Globals();
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		rs->global_shader_parameter_set(StringName("w3d_occl_on"), on ? 1.0f : 0.0f);
		rs->global_shader_parameter_set(StringName("w3d_occl_colors"), colors);
	}
}

namespace
{
float s_swayScale = 1.0f; // lane WEATHERFX-1: W3D_Set_Sway_Scale
}

void W3D_Set_Sway_Scale(double scale)
{
	s_swayScale = (float)scale;
}

void W3D_Set_Sway_Offsets(const float offsets[10][3])
{
	W3D_Ensure_Light_Globals();
	if (RenderingServer *rs = RenderingServer::get_singleton())
	{
		const float k = s_swayScale;
		for (int i = 0; i < 10; ++i)
		{
			rs->global_shader_parameter_set(StringName(String("w3d_sway") + String::num_int64(i + 1)),
				k == 1.0f ? Vector4(offsets[i][0], offsets[i][1], offsets[i][2], 0.0f) : Vector4(offsets[i][0] * k, offsets[i][1] * k, offsets[i][2] * k, 0.0f));
		}
	}
}

void W3D_Apply_Fog(bool enabled, const float color[3], float start, float end)
{
	W3D_Ensure_Light_Globals();
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs)
	{
		return;
	}
	rs->global_shader_parameter_set(StringName("w3d_fog"), Vector3(enabled ? 1.0f : 0.0f, start, end));
	rs->global_shader_parameter_set(StringName("w3d_fog_color"), Vector3(color[0], color[1], color[2]));
}

void W3D_Apply_Object_Lighting(const W3DObjectLighting &lighting)
{
	W3D_Ensure_Light_Globals();
	if (!RenderingServer::get_singleton())
	{
		return;
	}
	setSet("obj", lighting.objects);
	setSet("inf", lighting.infantry);
}

void W3D_Apply_Viewer_Lighting()
{
	W3D_Ensure_Light_Globals();
	if (!RenderingServer::get_singleton())
	{
		return;
	}
	const W3DLightSet v = viewerSet();
	setSet("obj", v);
	setSet("inf", v);
}

Ref<Shader> W3DMaterialFactory::Get_Shader(const W3DShaderKey &key)
{
	const W3DSamplerPolicy samplers = samplerPolicy();
	const std::string id = key.Id() + (samplers == W3DSamplerPolicy::Metal16 ? "_metal16" : "");
	auto it = Shaders.find(id);
	if (it != Shaders.end())
	{
		return it->second;
	}
	W3D_Ensure_Light_Globals(); // the shader names the light globals: they must exist before it compiles
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(toGodot(Generate_W3D_Shader_Code(key, samplers)));
	Shaders[id] = shader;
	return shader;
}

Ref<Texture2D> W3DMaterialFactory::Placeholder_Texture(bool flatNormal)
{
	Ref<Image> img = Image::create_empty(2, 2, false, Image::FORMAT_RGBA8);
	img->fill(flatNormal ? Color(0.5f, 0.5f, 1.0f, 1.0f) : Color(1, 1, 1, 1));
	return ImageTexture::create_from_image(img);
}

namespace
{
int g_textureReduction = 0; // lane OPTS-2: the static detail level's (W3DMaterialFactory::Set_Texture_Reduction)
}

void W3DMaterialFactory::Set_Texture_Reduction(int reduction)
{
	g_textureReduction = reduction < 0 ? 0 : reduction;
}

int W3DMaterialFactory::Get_Texture_Reduction()
{
	return g_textureReduction;
}

namespace
{
bool g_shaderMaterialReplacement = true; // see Set_Shader_Material_Replacement
}

void W3DMaterialFactory::Set_Shader_Material_Replacement(bool on)
{
	g_shaderMaterialReplacement = on;
}

bool W3DMaterialFactory::Get_Shader_Material_Replacement()
{
	return g_shaderMaterialReplacement;
}

bool W3DMaterialFactory::Reduce_Image(Ref<Image> &image, int reduction)
{
	if (image.is_null() || reduction <= 0 || !image->has_mipmaps())
	{
		return false;
	}
	const int levels = (int)image->get_mipmap_count(); // the levels below level 0
	const int drop = reduction < levels ? reduction : levels;
	if (drop <= 0)
	{
		return false;
	}
	const Image::Format format = image->get_format();
	int w = image->get_width(), h = image->get_height();
	for (int i = 0; i < drop; ++i)
	{
		w = w > 1 ? w / 2 : 1;
		h = h > 1 ? h / 2 : 1;
	}
	// a Godot image with mipmaps holds the whole chain down to its last level: the levels from `drop` on are the chain of a (w, h) image
	const int64_t offset = image->get_mipmap_offset(drop);
	const PackedByteArray data = image->get_data();
	if (offset <= 0 || offset >= data.size())
	{
		return false;
	}
	Ref<Image> reduced = Image::create_from_data(w, h, true, format, data.slice(offset));
	if (reduced.is_null() || reduced->is_empty() || reduced->get_width() != w)
	{
		return false;
	}
	image = reduced;
	return true;
}

Ref<Texture2D> W3DMaterialFactory::Load_Texture(const std::string &name, std::vector<std::string> &errors, bool &found, bool quietWhenMissing, std::vector<std::string> *notes)
{
	found = false;
	if (Packs)
	{
		if (const AssetPackFile *pf = Packs->texture(name))
		{
			const int reduction = g_textureReduction;
			const std::string cacheKey = "pack:" + pf->diskPath + (reduction > 0 ? "#reduction" + std::to_string(reduction) : std::string());
			auto cached = Textures.find(cacheKey);
			if (cached != Textures.end())
			{
				found = true;
				return cached->second;
			}
			std::string error;
			Ref<Image> image = Load_Asset_Pack_Image(Packs, name, error);
			if (image.is_null())
			{
				errors.push_back("texture " + name + ": " + error);
				TextureErrors.push_back(errors.back());
				return Ref<Texture2D>();
			}
			if (!image->has_mipmaps() && !image->is_compressed())
			{
				image->generate_mipmaps();
			}
			Reduce_Image(image, reduction);
			Ref<ImageTexture> tex = ImageTexture::create_from_image(image);
			Textures[cacheKey] = tex;
			found = true;
			return tex;
		}
	}
	TextureResolution res = Resolve_Packed_W3D_Texture(name, [&](const std::string &p) { return Fs.doesFileExist(p); }, isBfme1Data(Fs)); // community PR #36: the archives' layout
	bool isPng = false;
	if (!res.Found && !isBfme1Data(Fs) && name.size() > 4)
	{
		// lane WOTR-2 [inference, as the W3DModelBuilder's WOTR-1 fallback]: a few retail textures exist only as .png in the compiled folder
		// (LivingMap's LM_MinasTirith); the compiled-only .jpg ones (LM_Smoke, EXLavaSeq) resolve through Resolve_Packed_W3D_Texture above
		const std::string dds = W3D_Compiled_Texture_Path(name);
		const std::string png = dds.size() > 4 ? dds.substr(0, dds.size() - 4) + ".png" : std::string();
		res.Tried.push_back(png);
		if (!png.empty() && Fs.doesFileExist(png))
		{
			res.Found = true;
			res.Path = png;
			res.IsDDS = false;
			isPng = true;
		}
	}
	if (!res.Found)
	{
		if (!quietWhenMissing)
		{
			errors.push_back("texture not found: " + name
				+ (res.TerrainFolderPath.empty() ? std::string() : " (a file of that name exists only as " + res.TerrainFolderPath + ", which retail's texture lookup does not search: RW 0x477D1C)"));
			TextureErrors.push_back(errors.back());
		}
		return Ref<Texture2D>();
	}
	const int reduction = g_textureReduction;
	const std::string cacheKey = reduction > 0 ? res.Path + "#reduction" + std::to_string(reduction) : res.Path;
	auto it = Textures.find(cacheKey);
	if (it != Textures.end())
	{
		found = true;
		const auto note = PackedTextureNotes.find(cacheKey);
		if (notes && note != PackedTextureNotes.end()) notes->push_back(note->second);
		return it->second;
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!Fs.readFile(res.Path, bytes, &error))
	{
		errors.push_back("texture " + name + ": " + error);
		TextureErrors.push_back(errors.back());
		return Ref<Texture2D>();
	}
	PackedByteArray buffer;
	buffer.resize((int64_t)bytes.size());
	if (!bytes.empty()) std::memcpy(buffer.ptrw(), bytes.data(), bytes.size());
	Ref<Image> image;
	image.instantiate();
	Error err;
	if (isPng)
		err = image->load_png_from_buffer(buffer);
	else if (res.Path.size() >= 4 && res.Path.substr(res.Path.size() - 4) == ".jpg")
	{
		AptArchiveFileSource source(Fs);
		bool paired = false;
		image = loadPackedTexture(source, res.Path.substr(0, res.Path.size() - 4), &paired);
		err = image.is_valid() ? OK : ERR_FILE_CORRUPT;
		if (paired)
		{
			const std::string note = "[S-1482] model texture " + res.Path + ": packed PNG colour multiplication and alpha replacement are inferred; the loader merge was not read";
			PackedTextureNotes[cacheKey] = note;
			if (notes) notes->push_back(note);
		}
	}
	else
		err = decodeTexture(image, buffer, res.IsDDS);
	if (err != OK || image.is_null() || image->is_empty())
	{
		errors.push_back("could not decode texture " + res.Path);
		TextureErrors.push_back(errors.back());
		return Ref<Texture2D>();
	}
	if (!image->has_mipmaps() && !image->is_compressed())
	{
		image->generate_mipmaps();
	}
	Reduce_Image(image, reduction); // lane OPTS-2
	Ref<ImageTexture> tex = ImageTexture::create_from_image(image);
	Textures[cacheKey] = tex;
	found = true;
	return tex;
}

Ref<Texture2D> W3DMaterialFactory::Recolored_House_Texture(const std::string &name, const HouseColorParams &params, std::vector<std::string> &errors, std::vector<std::string> &notes)
{
	TextureResolution res = Resolve_House_Texture(name, [&](const std::string &p) { return Fs.doesFileExist(p); }, isBfme1Data(Fs)); // community PR #36: the archives' layout
	if (!res.Found)
	{
		return Ref<Texture2D>(); // as Load_Texture's quiet case: a missing house texture only means no tint
	}
	char opt[96];
	std::snprintf(opt, sizeof(opt), "%d&%d&%d&%d", params.kind, (int)params.colors[0], (int)params.colors[1], (int)params.colors[2]); // RW 0x535BCF's "%d&%d&%d&%d"
	const int reduction = g_textureReduction; // lane OPTS-2
	const std::string cacheKey = "#" + res.Path + "#" + opt + (reduction > 0 ? "#reduction" + std::to_string(reduction) : std::string());
	auto it = Textures.find(cacheKey);
	if (it != Textures.end())
	{
		const auto note = PackedHouseTextureNotes.find(cacheKey);
		if (note != PackedHouseTextureNotes.end()) notes.push_back(note->second);
		return it->second;
	}
	std::vector<std::uint8_t> bytes;
	std::string error;
	if (!Fs.readFile(res.Path, bytes, &error))
	{
		errors.push_back("texture " + name + ": " + error);
		TextureErrors.push_back(errors.back());
		return Ref<Texture2D>();
	}
	PackedByteArray buffer;
	buffer.resize((int64_t)bytes.size());
	if (!bytes.empty()) std::memcpy(buffer.ptrw(), bytes.data(), bytes.size());
	Ref<Image> image;
	image.instantiate();
	Error err;
	if (res.Path.size()>=4 && res.Path.substr(res.Path.size()-4)==".jpg") {
	 AptArchiveFileSource source(Fs);bool paired=false;
	 image=loadPackedTexture(source,res.Path.substr(0,res.Path.size()-4),&paired);
	 err=image.is_valid() ? OK : ERR_FILE_CORRUPT;
	 if (paired) {
	  const std::string note = "[S-1482] house texture " + res.Path + ": packed PNG colour multiplication and alpha replacement are inferred; the loader merge was not read";
	  notes.push_back(note);
	  PackedHouseTextureNotes[cacheKey] = note;
	 }
	} else err = decodeTexture(image, buffer, res.IsDDS);
	if (err != OK || image.is_null() || image->is_empty())
	{
		errors.push_back("could not decode texture " + res.Path);
		TextureErrors.push_back(errors.back());
		return Ref<Texture2D>();
	}
	// RW 0x531C77 recolours an A8R8G8B8 surface (0x15) texel by texel; any other format (a DXT DDS, a 24-bit TGA) is left as it is. Level 0 is recoloured,
	// then the mipmaps are made from it (INFERENCE, S-1408: retail's loader filtered the mip chain before the recolour, RW 0x531BF9 -> 0x53193E; the
	// recolour's single surface lock suggests the lower levels stay unrecoloured there)
	if (!image->is_compressed() && image->get_format() == Image::FORMAT_RGBA8)
	{
		image->clear_mipmaps();
		PackedByteArray px = image->get_data();
		Recolor_House_Pixels(params, px.ptrw(), (std::size_t)(px.size() / 4));
		image->set_data(image->get_width(), image->get_height(), false, Image::FORMAT_RGBA8, px);
		image->generate_mipmaps();
	}
	else
	{
		notes.push_back("house colour texture " + name + " is not A8R8G8B8: not recoloured (RW 0x531C77 leaves other formats)");
		if (!image->has_mipmaps() && !image->is_compressed())
		{
			image->generate_mipmaps();
		}
	}
	Reduce_Image(image, reduction); // lane OPTS-2
	Ref<ImageTexture> tex = ImageTexture::create_from_image(image);
	Textures[cacheKey] = tex;
	return tex;
}

namespace
{
// RENDER-2: simple.fxo's UV transform (W3D_SimpleFx_Uv_Matrix) as a stage 0 mapper, so a scrolling transform is updated like the time variant W3D mappers
class SimpleFxUvMapper : public TextureMapperClass
{
public:
	explicit SimpleFxUvMapper(const float transform[4]) : TextureMapperClass(0)
	{
		for (int i = 0; i < 4; ++i) T[i] = transform[i];
	}
	int Get_Type() const override { return -1; }
	bool Is_Time_Variant() const override { return T[2] != 0.0f || T[3] != 0.0f; }
	void Calculate_Texture_Matrix(W3DTexMatrix &out, std::uint32_t syncTimeMs) override
	{
		out.Make_Identity();
		W3D_SimpleFx_Uv_Matrix(T, (float)syncTimeMs * 0.001f, out.M[0], out.M[1]);
	}

private:
	float T[4];
};
} // namespace

W3DMaterialResult W3DMaterialFactory::Create(const MeshRenderData &mesh, const MeshDrawSurface &surf, const MeshModelClass &source, bool hasUv2, bool loadTextures)
{
	W3DMaterialResult out;
	W3DShaderKey key;
	key.Skin = mesh.Skin;
	key.Dual = mesh.DualBone;
	key.Camera = mesh.Camera;
	key.CullDisabled = mesh.TwoSided;
	key.HasDCG = surf.HasDCG;
	key.Tangents = !mesh.Tangent.empty() && mesh.Camera == MESH_CAMERA_NONE;
	key.Uv2 = hasUv2;

	// The effect parameters (RENDER-1). Classic: defaultw3d.fxo's defaults (ColorAmbient 1, ColorDiffuse 1, ColorSpecular 0,
	// Shininess 1, ColorEmissive 0, Opacity 1; tools/render/fx_params.py) overwritten by the vertex material.
	Color diffuse(1, 1, 1, 1);
	Color ambientMat(1, 1, 1, 1);
	Color emissive(0, 0, 0, 1);
	Color specColor(0, 0, 0, 1);
	float shininess = 1.0f;
	float alphaRef = 0.0f;
	float bump = 1.0f;
	std::string tex[2];
	W3dTextureInfoStruct info[2] = {};
	bool hasInfo[2] = { false, false };
	std::string normalMapName;
	std::shared_ptr<TextureMapperClass> simpleUv; // RENDER-2: simple.fxo's TexCoordTransform_0 as the stage 0 matrix
	W3DFxConversionSurface conv; // classic surfaces: how retail's conversion and house colour recolour see the mesh

	if (surf.Kind == MATERIAL_CLASSIC)
	{
		const W3DRenderState &st = surf.State;
		key.Blend = st.Blend;
		// lane RENDER-4 (S-1651): ShaderClass::Enable_Fog (RW 0x537CE0) on the surface's own blend fields (the W3dShaderStruct encoding is the ShaderClass one)
		key.Fog = W3DHardwareFog::ModeForBlend(surf.Shader.SrcBlend, surf.Shader.DestBlend);
		key.DepthWrite = st.DepthWrite;
		key.DepthTestDisabled = st.DepthTestDisabled;
		key.AlphaTest = st.AlphaTest;
		key.AlphaTestLessEqual = st.AlphaTestLessEqual;
		key.Texturing = st.Texturing;
		key.PriGradient = st.PriGradient;
		key.DetailColor = st.DetailColorFunc;
		key.DetailAlpha = st.DetailAlphaFunc;
		key.Unshaded = st.PriGradient == W3D_GRADIENT_DISABLE;
		key.SecGradient = st.SecGradient;
		// the mesh's pass-0 surface as retail's conversion and its house colour recolour judge it (w3dfxconvert.h)
		conv.Shader = &surf.Shader;
		conv.HasVertexMaterial = surf.HasVertexMaterial;
		conv.PassCount = 0;
		conv.Pass0Surfaces = 0; // one draw surface per (pass, shader, vertex material, textures): a second one in pass 0 is a material / shader / texture array
		const MeshDrawSurface *first = nullptr;
		for (const MeshDrawSurface &other : mesh.Surfaces)
		{
			conv.PassCount = std::max(conv.PassCount, other.Pass + 1);
			if (other.Pass != 0) continue;
			++conv.Pass0Surfaces;
			if (!first) first = &other;
			else if (other.Stage[0].TextureName != first->Stage[0].TextureName) conv.Pass0TexturesDiffer = true;
		}
		for (int s = 0; s < 2; ++s)
		{
			conv.StageTexture[s] = surf.Stage[s].HasTexture;
			conv.MapperType[s] = surf.Stage[s].MapperType;
			conv.MapperArgs[s] = surf.Stage[s].MapperArgs;
		}
		if (g_shaderMaterialReplacement)
		{
			// RW 0x516A30 / MeshModelClass::Register_For_Rendering: the conversion to DefaultW3D.fx
			key.ConvertedFx = W3D_Fx_Conversion_Refusal(conv).empty();
			const std::string stop = W3D_Fx_Conversion_Stop(conv); // S-6020
			if (!stop.empty()) out.Notes.push_back(stop);
		}
		alphaRef = st.AlphaRef / 255.0f - 0.5f / 255.0f; // GREATEREQUAL on an 8 bit value
		if (st.PriGradient == W3D_GRADIENT_ADD) out.Notes.push_back("primary gradient ADD drawn as a modulated colour");
		if (st.NeverDraws) out.Notes.push_back("depth function NEVER: nothing passes the depth test");
		if (surf.HasVertexMaterial)
		{
			const W3dVertexMaterialStruct &vm = surf.VertexMaterial;
			diffuse = Color(vm.Diffuse.R / 255.0f, vm.Diffuse.G / 255.0f, vm.Diffuse.B / 255.0f, vm.Opacity);
			ambientMat = Color(vm.Ambient.R / 255.0f, vm.Ambient.G / 255.0f, vm.Ambient.B / 255.0f, 1.0f);
			emissive = Color(vm.Emissive.R / 255.0f, vm.Emissive.G / 255.0f, vm.Emissive.B / 255.0f, 1.0f);
			specColor = Color(vm.Specular.R / 255.0f, vm.Specular.G / 255.0f, vm.Specular.B / 255.0f, 1.0f);
			shininess = vm.Shininess;
		}
		if (key.ConvertedFx && st.PriGradient == W3D_GRADIENT_DISABLE)
		{
			// MeshMatDescFXShaderConversion.cpp:286-327 / RW 0x599865-0x599a62: useMaterialColors = Get_Lighting() && primary gradient != 0, so an unlit surface
			// hands DefaultW3D.fx ColorAmbient / ColorDiffuse / ColorSpecular as the record's zero vector (the record constructor RW 0x550B8C clears it), ColorEmissive
			// 1, 1, 1 and Opacity 1.0; the vertex shader then outputs v0 = 0.5 * vertex colour (alpha: vertex colour alpha) and v1 = 0 (W3DShaderKey: the secondary
			// texture code reads v_lit0 / v_lit1)
			diffuse = Color(0, 0, 0, 1);
			ambientMat = Color(0, 0, 0, 1);
			emissive = Color(1, 1, 1, 1);
			specColor = Color(0, 0, 0, 1);
		}
		for (int s = 0; s < 2; ++s)
		{
			if (surf.Stage[s].HasTexture)
			{
				tex[s] = surf.Stage[s].TextureName;
				info[s] = surf.Stage[s].TextureInfo;
				hasInfo[s] = surf.Stage[s].HasTextureInfo;
			}
		}
	}
	else
	{
		// BFME2 FX material: drawn with normalmapped.fxo's lighting (RENDER-1; 8,481 of the 8,609 FX surfaces name normalmapped.fx,
		// the other effects stay S-023 approximations). Parameters a material does not set keep the effect's defaults
		// (normalmapped.fxo: AmbientColor 0.4, DiffuseColor 1, SpecularColor 0.8, SpecularExponent 50, BumpScale 1).
		const MeshShaderMaterialDef &fx = source.ShaderMaterials[(size_t)surf.ShaderMaterialId];
		if (AsciiStringUtil::lowered(fx.TypeName) == "simple.fx")
		{
			// RENDER-2: simple.fxo (tools/render/d3d9_disasm.py blobs 0 / 1, fx_params.py): unlit Texture_0 * ColorEmissive (default 1, 1, 1), alpha = texel alpha,
			// uv * TexCoordTransform_0.xy + Time * .zw (default 1, 1, 0, 0); pass P0 (fx_2_0 state assignments, Wine's state table): ZENABLE 1, ZFUNC LESSEQUAL,
			// ZWRITEENABLE = DepthWriteEnable (default 1), CULLMODE CW, ALPHABLENDENABLE = AlphaBlendingEnable (default 0), SRCBLEND SRCALPHA, DESTBLEND
			// INVSRCALPHA, ALPHATESTENABLE 0, FOGENABLE 0 (the parameter bindings of the two expressions are inference, S-023)
			emissive = Color(1, 1, 1, 1);
			float uvTransform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
			bool alphaBlend = false, depthWrite = true;
			for (const MeshShaderMaterialProperty &p : fx.Properties)
			{
				const std::string n = AsciiStringUtil::lowered(p.Name);
				if (n == "texture_0") tex[0] = p.StringValue;
				else if (n == "coloremissive") emissive = Color(p.Float[0], p.Float[1], p.Float[2], 1.0f);
				else if (n == "texcoordtransform_0") { for (int i = 0; i < 4; ++i) uvTransform[i] = p.Float[i]; }
				else if (n == "alphablendingenable") alphaBlend = p.Bool;
				else if (n == "depthwriteenable") depthWrite = p.Bool;
			}
			key.SimpleFx = true;
			key.Texturing = true;
			key.PriGradient = W3D_GRADIENT_DISABLE;
			key.Blend = alphaBlend ? W3D_GODOT_BLEND_MIX : W3D_GODOT_BLEND_OPAQUE;
			key.Fog = W3DHardwareFog::FOG_ENABLE; // lane RENDER-4: simple.fxo fogs in its own shaders toward the Fog colour (ps_1_1 blob 0: lrp r0, fog, colour, c1)
			key.DepthWrite = depthWrite;
			if (uvTransform[0] != 1.0f || uvTransform[1] != 1.0f || uvTransform[2] != 0.0f || uvTransform[3] != 0.0f)
			{
				simpleUv.reset(new SimpleFxUvMapper(uvTransform));
			}
		}
		else if (AsciiStringUtil::lowered(fx.TypeName) == "watershader.fx")
		{
			// WOTR-2 [S-035 approximation]: a WaterShader.FX mesh (LivingMap's WATER) draws its MaterialColorDiffuse (rgb, alpha) lit like an untextured
			// classic material and alpha blended without depth writes; the bump, environment, wave and PCA textures and the specular are not drawn.
			// Before this the effect fell to the normalmapped path with no texture: a white (on the strategic map, pink-lit) sheet.
			key.Fx = false;
			key.Texturing = false;
			key.PriGradient = W3D_GRADIENT_MODULATE;
			key.Blend = W3D_GODOT_BLEND_MIX;
			key.DepthWrite = false;
			for (const MeshShaderMaterialProperty &p : fx.Properties)
			{
				if (AsciiStringUtil::lowered(p.Name) == "materialcolordiffuse")
				{
					diffuse = Color(p.Float[0], p.Float[1], p.Float[2], p.Float[3]);
					ambientMat = Color(p.Float[0], p.Float[1], p.Float[2], 1.0f);
				}
			}
			out.Notes.push_back("WaterShader.FX drawn as its MaterialColorDiffuse (S-035)");
		}
		else
		{
		ambientMat = Color(0.4f, 0.4f, 0.4f, 1.0f);
		specColor = Color(0.8f, 0.8f, 0.8f, 1.0f);
		shininess = 50.0f;
		key.Fx = true;
		key.Texturing = true;
		key.PriGradient = W3D_GRADIENT_MODULATE;
		key.Blend = W3D_GODOT_BLEND_OPAQUE;
		for (const MeshShaderMaterialProperty &p : fx.Properties)
		{
			std::string n = AsciiStringUtil::lowered(p.Name);
			if (n == "diffusetexture") tex[0] = p.StringValue;
			else if (n == "normalmap") normalMapName = p.StringValue;
			else if (n == "bumpscale") bump = p.Float[0];
			else if (n == "diffusecolor") diffuse = Color(p.Float[0], p.Float[1], p.Float[2], p.Float[3]);
			else if (n == "ambientcolor") ambientMat = Color(p.Float[0], p.Float[1], p.Float[2], 1.0f);
			else if (n == "specularcolor") specColor = Color(p.Float[0], p.Float[1], p.Float[2], 1.0f);
			else if (n == "specularexponent") shininess = p.Float[0];
			else if (n == "alphatestenable" && p.Bool)
			{
				key.AlphaTest = true;
				alphaRef = 0x60 / 255.0f - 0.5f / 255.0f;
			}
		}
		if (diffuse.a <= 0.0f) diffuse.a = 1.0f;
		key.Fog = W3DHardwareFog::FOG_ENABLE; // lane RENDER-4: normalmapped.fxo outputs oFog (vs_2_0 blob 22); the opaque pass fogs toward the fog colour
		}
	}

	// stop S-5015: the ModelConditionState's `Texture = A B` pairs of the model being built (the SNOW state's KBHall.tga -> KBHall_snow.tga of 159 faction
	// buildings): every texture this surface names A is loaded as B, before the house colour lookup reads it. INFERENCE: retail's swap routine was not read
	for (std::string *name : { &tex[0], &tex[1], &normalMapName })
	{
		for (const std::pair<std::string, std::string> &swap : TextureSwaps)
		{
			if (!name->empty() && (swap.first == "*" || AsciiStringUtil::compareNoCase(*name, swap.first) == 0)) // "*": every texture (a floor's WeatherTexture)
			{
				out.Notes.push_back("texture " + *name + " drawn as " + swap.second + " (the model state's Texture swap, S-5015)");
				*name = swap.second;
				break;
			}
		}
	}

	// textures
	Ref<Texture2D> textures[2];
	Ref<Texture2D> normalTex;
	bool found[2] = { false, false };
	for (int s = 0; s < 2; ++s)
	{
		if (tex[s].empty()) continue;
		key.Tex[s] = true;
		textures[s] = loadTextures ? Load_Texture(tex[s], out.Errors, found[s], false, &out.Notes) : Placeholder_Texture(false);
		if (hasInfo[s])
		{
			key.ClampU[s] = (info[s].Attributes & 0x08) != 0; // W3DTEXTURE_CLAMP_U
			key.ClampV[s] = (info[s].Attributes & 0x10) != 0; // W3DTEXTURE_CLAMP_V
			key.NoLod[s] = (info[s].Attributes & 0x04) != 0;  // W3DTEXTURE_NO_LOD
		}
	}
	if (!normalMapName.empty())
	{
		bool f;
		normalTex = loadTextures ? Load_Texture(normalMapName, out.Errors, f, false, &out.Notes) : Placeholder_Texture(true);
		key.NormalMap = normalTex.is_valid() && key.Tangents;
		if (normalTex.is_valid() && !key.Tangents) out.Notes.push_back("normal map without tangents is not applied");
	}

	// house colour (S-119): the stage 0 texture's housecolor.ini texture
	Ref<Texture2D> hcTex;
	if (HouseColors && surf.Kind == MATERIAL_CLASSIC && key.Tex[0])
	{
		if (const std::string *hc = HouseColors->Find(tex[0]))
		{
			std::vector<std::string> hcErrors; // 313 housecolor.ini textures, 204 shipped as files: a missing one only means no tint
			if (loadTextures && HouseParams.kind > 0)
			{
				// lane CAH-2: a colour set (a Create-a-Hero's kind 3): the texture recoloured per texel before filtering
				hcTex = Recolored_House_Texture(*hc, HouseParams, hcErrors, out.Notes);
				key.HouseColorBaked = hcTex.is_valid();
			}
			else
			{
				hcTex = loadTextures ? Recolored_House_Texture(*hc, HouseColorParams(), hcErrors, out.Notes) : Placeholder_Texture(false);
			}
			out.Errors.insert(out.Errors.end(), hcErrors.begin(), hcErrors.end());
			if (hcTex.is_valid())
			{
				const std::string houseStop = W3D_House_Color_Stop(conv, key.ConvertedFx); // S-6022
				if (!houseStop.empty()) out.Notes.push_back(houseStop);
				const std::string specStop = W3D_FixedFunction_House_Color_Stop(key.ConvertedFx, key.PriGradient != W3D_GRADIENT_DISABLE, key.SecGradient); // S-6023
				if (!specStop.empty()) out.Notes.push_back(specStop);
				key.HouseColor = true;
				out.Notes.push_back("house colour texture " + *hc + ": the recoloured texel is combined with the lit base by its alpha (" + std::string(key.ConvertedFx ? "DefaultW3D.fx HouseColorEnable" : "fixed-function BLENDTEXTUREALPHA") + ", S-022)");
			}
			else
			{
				out.Notes.push_back("house colour texture " + *hc + " is not in the archives: this surface is not tinted");
			}
		}
	}

	// mappers (classic only)
	std::shared_ptr<TextureMapperClass> mappers[2];
	if (surf.Kind == MATERIAL_CLASSIC && surf.HasVertexMaterial)
	{
		for (int s = 0; s < 2; ++s)
		{
			if (!key.Tex[s]) continue;
			std::string error;
			bool unknown = false;
			std::unique_ptr<TextureMapperClass> m = Create_Texture_Mapper(surf.Stage[s].MapperType, (unsigned)s, surf.Stage[s].MapperArgs, 0, MapperRandom(), &error, &unknown);
			if (!error.empty()) out.Errors.push_back(error);
			if (unknown) out.Notes.push_back("mapper type " + std::to_string(surf.Stage[s].MapperType) + " is not defined; plain UV used (ZH default case)");
			if (m)
			{
				key.TexGen[s] = m->Get_TexGen();
				key.ViewDependent[s] = m->View_Dependent();
				key.Projection[s] = m->Projection_Dependent();
				if (m->Projection_Dependent()) out.Notes.push_back("screen mapper uses normalised device coordinates instead of ZH's projection matrix product");
				mappers[s] = std::shared_ptr<TextureMapperClass>(m.release());
			}
		}
	}

	if (simpleUv)
	{
		mappers[0] = simpleUv;
	}

	// Donor MeshClassRecolorPrototype.cpp, BFME2 0x54B8C0:
	// HOUSECOLOR mesh parts set ambient and diffuse to the primary colour.
	// Preserve opacity, emissive and specular. Channel unpacking follows the
	// existing target ARGB convention in Recolor_House_Texel, not the donor's
	// address-derived ABI cast expressions.
	if (HouseColors && HouseParams.kind > 0 && Is_House_Color_Material(mesh.Name))
	{
	 const auto color=HouseParams.colors[0];
	 const Color rgb(((color>>16)&255)/255.0f,((color>>8)&255)/255.0f,(color&255)/255.0f);
	 diffuse.r=rgb.r;diffuse.g=rgb.g;diffuse.b=rgb.b;
	 ambientMat=rgb;
	 out.Notes.push_back("HOUSECOLOR ambient/diffuse recoloured from donor BFME2 0x54B8C0; RotWK material path unverified (S-119)");
	}
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	key.AlphaOverride = AlphaOverride; // lane BUILD-5 r3
	mat->set_shader(Get_Shader(key));
	mat->set_shader_parameter("w3d_diffuse", Vector4(diffuse.r, diffuse.g, diffuse.b, diffuse.a));
	mat->set_shader_parameter("w3d_ambient_mat", Vector3(ambientMat.r, ambientMat.g, ambientMat.b));
	mat->set_shader_parameter("w3d_emissive", Vector3(emissive.r, emissive.g, emissive.b));
	mat->set_shader_parameter("w3d_spec_color", Vector3(specColor.r, specColor.g, specColor.b));
	mat->set_shader_parameter("w3d_shininess", shininess);
	mat->set_shader_parameter("w3d_alpha_ref", alphaRef);
	mat->set_shader_parameter("w3d_bump", bump);
	for (int s = 0; s < 2; ++s)
	{
		if (!key.Tex[s]) continue;
		if (textures[s].is_valid())
		{
			mat->set_shader_parameter(String("w3d_tex") + String::num_int64(s), textures[s]);
		}
		else
		{
			// the report carries the error; the surface draws retail's missing texture (RW 0x53193E, textureloader.h): 1 x 1, opaque magenta
			Ref<Image> img = Image::create_empty(1, 1, false, Image::FORMAT_RGBA8);
			img->fill(Color::hex(((W3D_MISSING_TEXTURE_COLOR & 0x00FFFFFFu) << 8) | (W3D_MISSING_TEXTURE_COLOR >> 24)));
			mat->set_shader_parameter(String("w3d_tex") + String::num_int64(s), ImageTexture::create_from_image(img));
		}
	}
	if (key.NormalMap) mat->set_shader_parameter("w3d_normal", normalTex);
	if (key.HouseColor) mat->set_shader_parameter("w3d_hc_tex", hcTex);
	const int priority = std::clamp(surf.Pass - std::min(surf.SortLevel, 31) * 4, -128, 127);
	mat->set_render_priority(priority);

	for (int s = 0; s < 2; ++s)
	{
		if (!mappers[s]) continue;
		W3DTexMatrix m;
		mappers[s]->Calculate_Texture_Matrix(m, 0);
		const String ss = String::num_int64(s);
		mat->set_shader_parameter(String("w3d_m") + ss + "r0", Vector4(m.M[0][0], m.M[0][1], m.M[0][2], m.M[0][3]));
		mat->set_shader_parameter(String("w3d_m") + ss + "r1", Vector4(m.M[1][0], m.M[1][1], m.M[1][2], m.M[1][3]));
		if (mappers[s]->Is_Time_Variant())
		{
			W3DMapperBinding b;
			b.Material = mat;
			b.Stage = s;
			b.Mapper = mappers[s];
			b.Source = mesh.Name;
			out.Mappers.push_back(b);
		}
	}
	out.Material = mat;
	out.Key = key;
	return out;
}

} // namespace godot
