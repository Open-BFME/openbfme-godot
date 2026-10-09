// OpenBFME unit tests: the generated Godot shader text (w3dshadergen.cpp). Review finding P2: v_wnrm was the model-space normal while
// v_wview is a world-space direction, so a view-dependent environment mapper gave different results for a rotated instance.
// Godot's NORMAL in vertex() is model-space; the instance rotation lives in MODEL_MATRIX (the MultiMesh transform is folded in), so the
// world normal is MODEL_NORMAL_MATRIX * NORMAL. Expected strings are the GLSL the spec fixes; nothing is read back from a run.

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <string>

namespace
{
W3DShaderKey envKey(bool viewDependent, W3DTexGen gen)
{
	W3DShaderKey k;
	k.Texturing = true;
	k.Tex[0] = true;
	k.TexGen[0] = gen;
	k.ViewDependent[0] = viewDependent;
	return k;
}

bool has(const std::string &text, const std::string &needle)
{
	return text.find(needle) != std::string::npos;
}
} // namespace

TEST_CASE("shader text: the world-space normal varying includes the instance rotation (MODEL_NORMAL_MATRIX)")
{
	const std::string code = Generate_W3D_Shader_Code(envKey(true, W3D_TEXGEN_CAMERA_REFLECTION));
	CHECK(has(code, "v_wnrm = normalize(MODEL_NORMAL_MATRIX * NORMAL);"));
	CHECK_FALSE(has(code, "v_wnrm = NORMAL;"));
	// v_wview is already world space: the view vector is rotated by the inverse view matrix
	CHECK(has(code, "v_wview = mat3(INV_VIEW_MATRIX) * normalize(v_vpos);"));
	// and the reflection is taken between the two world-space vectors
	CHECK(has(code, "reflect(v_wview, normalize(v_wnrm))"));
}

TEST_CASE("shader text: the camera-normal mapper's world path reads the same world-space normal")
{
	const std::string code = Generate_W3D_Shader_Code(envKey(true, W3D_TEXGEN_CAMERA_NORMAL));
	CHECK(has(code, "vec4(vec3(v_wnrm.x, -v_wnrm.z, v_wnrm.y), 1.0)"));
	CHECK(has(code, "v_wnrm = normalize(MODEL_NORMAL_MATRIX * NORMAL);"));
}

TEST_CASE("shader text: the view-space mappers keep the model-view normal")
{
	const std::string code = Generate_W3D_Shader_Code(envKey(false, W3D_TEXGEN_CAMERA_REFLECTION));
	CHECK(has(code, "v_vnrm = normalize((MODELVIEW_NORMAL_MATRIX * NORMAL));"));
	CHECK(has(code, "reflect(normalize(v_vpos), normalize(v_vnrm))"));
}
