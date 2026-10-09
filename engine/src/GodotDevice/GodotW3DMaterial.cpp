// OpenBFME. GPL-3.0.

#include "GodotDevice/GodotW3DMaterial.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DObjectLighting.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"
#include "Libraries/WWVegas/WW3D2/shader.h"
#include "Libraries/WWVegas/WW3D2/textureloader.h"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/variant/basis.hpp>
#include <godot_cpp/variant/vector3.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/vector4.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace godot
{

namespace
{

String toGodot(const std::string &s)
{
	return String::utf8(s.c_str(), (int64_t)s.size());
}

} // namespace

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
	const std::string id = key.Id();
	auto it = Shaders.find(id);
	if (it != Shaders.end())
	{
		return it->second;
	}
	W3D_Ensure_Light_Globals(); // the shader names the light globals: they must exist before it compiles
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(toGodot(Generate_W3D_Shader_Code(key)));
	Shaders[id] = shader;
	return shader;
}

Ref<Texture2D> W3DMaterialFactory::Placeholder_Texture(bool flatNormal)
{
	Ref<Image> img = Image::create_empty(2, 2, false, Image::FORMAT_RGBA8);
	img->fill(flatNormal ? Color(0.5f, 0.5f, 1.0f, 1.0f) : Color(1, 1, 1, 1));
	return ImageTexture::create_from_image(img);
}

Ref<Texture2D> W3DMaterialFactory::Load_Texture(const std::string &name, std::vector<std::string> &errors, bool &found, bool quietWhenMissing)
{
	found = false;
	TextureResolution res = Resolve_W3D_Texture(name, [&](const std::string &p) { return Fs.doesFileExist(p); });
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
	auto it = Textures.find(res.Path);
	if (it != Textures.end())
	{
		found = true;
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
	Error err = res.IsDDS ? image->load_dds_from_buffer(buffer) : image->load_tga_from_buffer(buffer);
	if (err != OK || image->is_empty())
	{
		errors.push_back("could not decode texture " + res.Path);
		TextureErrors.push_back(errors.back());
		return Ref<Texture2D>();
	}
	if (!image->has_mipmaps() && !image->is_compressed())
	{
		image->generate_mipmaps();
	}
	Ref<ImageTexture> tex = ImageTexture::create_from_image(image);
	Textures[res.Path] = tex;
	found = true;
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

	// textures
	Ref<Texture2D> textures[2];
	Ref<Texture2D> normalTex;
	bool found[2] = { false, false };
	for (int s = 0; s < 2; ++s)
	{
		if (tex[s].empty()) continue;
		key.Tex[s] = true;
		textures[s] = loadTextures ? Load_Texture(tex[s], out.Errors, found[s]) : Placeholder_Texture(false);
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
		normalTex = loadTextures ? Load_Texture(normalMapName, out.Errors, f) : Placeholder_Texture(true);
		key.NormalMap = normalTex.is_valid() && key.Tangents;
		if (normalTex.is_valid() && !key.Tangents) out.Notes.push_back("normal map without tangents is not applied");
	}

	// house colour (S-119): the stage 0 texture's housecolor.ini texture
	Ref<Texture2D> hcTex;
	if (HouseColors && surf.Kind == MATERIAL_CLASSIC && key.Tex[0])
	{
		if (const std::string *hc = HouseColors->Find(tex[0]))
		{
			bool hcFound = false;
			std::vector<std::string> hcErrors; // 313 housecolor.ini textures, 204 shipped as files: a missing one only means no tint
			hcTex = loadTextures ? Load_Texture(*hc, hcErrors, hcFound, true) : Placeholder_Texture(false);
			if (hcTex.is_valid())
			{
				key.HouseColor = true;
				out.Notes.push_back("house colour texture " + *hc + " masks the team colour (hypothesis S-119)");
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

	Ref<ShaderMaterial> mat;
	mat.instantiate();
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
