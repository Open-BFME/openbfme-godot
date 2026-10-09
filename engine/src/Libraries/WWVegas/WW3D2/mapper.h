// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Texture mappers: the per-stage texture coordinate matrices of vertex materials (spec 3.4).
//
// Target facts (BFME2 1.06, Open-BFME-2 matched TUs, retail RVAs in each comment in mapper.cpp):
// LinearOffset 0x001822E0, Scale 0x00182270, Rotate 0x00182800, SineLinearOffset 0x00182BA0, StepLinearOffset
// 0x00182E80, ZigZagLinearOffset 0x00183240, Random 0x001838B0, Edge 0x00183540, ClassicEnvironment 0x00183340,
// Grid update_temporal_state 0x00182590 / calculate_uv_offset 0x001825E0 / initialize 0x00182480. Everything else is the
// ZH mapper.cpp text (Open-BFME-1 inputs/reference/CnC_Generals_Zero_Hour/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/
// mapper.cpp and vertmaterial.cpp:593-940 for the type switch), which BFME2's own files agree with wherever they exist.
//
// A mapper produces a 4x4 matrix in WW3D's row layout (Matrix4 [row].X/.Y/.Z/.W; DX8Wrapper transposes it for D3D).
// With UV passthrough the input is (u, v, 1, 0): u' = M[0].X*u + M[0].Y*v + M[0].Z, v' = M[1].X*u + M[1].Y*v + M[1].Z.
// With a texgen source the input is (x, y, z, 1) of the camera-space normal / reflection vector.
//
// Deliberate differences from ZH:
//  * the sync time is a parameter, not WW3D::Get_Sync_Time(), so state is explicit and testable.
//  * RandomTextureMapperClass draws from an injected generator instead of the global Random4Class: render-only, and
//    retail's sequence is not reproduced.
//  * mapper arguments are validated: a key no mapper of that type reads is an error (ZH INIClass ignores it).
//  * world-space and screen mappers return the part of the matrix that does not depend on the view / projection;
//    View_Dependent() / Projection_Dependent() say what the shader must multiply in (ZH reads the D3D transforms).

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// ZH w3d_file.h W3DVERTMAT_STAGE0_MAPPING_* >> 16 (stage 1 is the same list >> 8).
enum W3DMapperType
{
	W3D_MAPPING_UV = 0,
	W3D_MAPPING_ENVIRONMENT = 1,
	W3D_MAPPING_CHEAP_ENVIRONMENT = 2,
	W3D_MAPPING_SCREEN = 3,
	W3D_MAPPING_LINEAR_OFFSET = 4,
	W3D_MAPPING_SILHOUETTE = 5,
	W3D_MAPPING_SCALE = 6,
	W3D_MAPPING_GRID = 7,
	W3D_MAPPING_ROTATE = 8,
	W3D_MAPPING_SINE_LINEAR_OFFSET = 9,
	W3D_MAPPING_STEP_LINEAR_OFFSET = 10,
	W3D_MAPPING_ZIGZAG_LINEAR_OFFSET = 11,
	W3D_MAPPING_WS_CLASSIC_ENV = 12,
	W3D_MAPPING_WS_ENVIRONMENT = 13,
	W3D_MAPPING_GRID_CLASSIC_ENV = 14,
	W3D_MAPPING_GRID_ENVIRONMENT = 15,
	W3D_MAPPING_RANDOM = 16,
	W3D_MAPPING_EDGE = 17,
	W3D_MAPPING_BUMPENV = 18,
	W3D_MAPPING_GRID_WS_CLASSIC_ENV = 19,
	W3D_MAPPING_GRID_WS_ENVIRONMENT = 20,
};

// Where the stage's input coordinates come from (D3DTSS_TEXCOORDINDEX).
enum W3DTexGen
{
	W3D_TEXGEN_UV,                   // D3DTSS_TCI_PASSTHRU | uv array index
	W3D_TEXGEN_CAMERA_NORMAL,        // D3DTSS_TCI_CAMERASPACENORMAL
	W3D_TEXGEN_CAMERA_REFLECTION,    // D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR
	W3D_TEXGEN_CAMERA_POSITION_PROJECTED, // ScreenMapper: camera space position, projected
};

struct W3DTexMatrix
{
	float M[4][4]; // M[row][column]; row = WW3D Matrix4 [row], column 0..3 = .X .Y .Z .W
	void Make_Identity();
	void Init(float m00, float m01, float m02, float m03, float m10, float m11, float m12, float m13, float m20, float m21, float m22,
		float m23, float m30, float m31, float m32, float m33);
};

// The "[Args]" INI text of W3D_CHUNK_VERTEX_MAPPER_ARGS0/1 (ZH vertmaterial.cpp:555-575 prepends "[Args]\n" and loads it
// with INIClass). Keys are case-insensitive, ';' starts a comment.
class MapperArgs
{
public:
	// False + *error for a line that is neither blank, a comment nor Key=Value.
	bool Parse(const std::string &text, std::string *error);

	bool Has(const std::string &key) const;
	float Get_Float(const std::string &key, float def) const;
	int Get_Int(const std::string &key, int def) const;
	bool Get_Bool(const std::string &key, bool def) const;
	std::string Get_String(const std::string &key, const std::string &def) const;

	// Keys present in the text that were never read: mappers call Check_All_Read() after construction.
	void Mark_Read(const std::string &key) const;
	std::vector<std::string> Unread_Keys() const;

private:
	std::map<std::string, std::string> Values; // lower-cased key -> value
	mutable std::map<std::string, bool> Read;
};

class TextureMapperClass
{
public:
	explicit TextureMapperClass(unsigned stage) : Stage(stage) {}
	virtual ~TextureMapperClass() = default;

	virtual int Get_Type() const = 0;
	virtual W3DTexGen Get_TexGen() const { return W3D_TEXGEN_UV; }
	// True when Calculate_Texture_Matrix gives a different matrix at a different sync time (ZH
	// MaterialInfoClass::Has_Time_Variant_Texture_Mappers).
	virtual bool Is_Time_Variant() const { return false; }
	// WS mappers multiply the result by the inverse view rotation, the screen mapper by the projection.
	virtual bool View_Dependent() const { return false; }
	virtual bool Projection_Dependent() const { return false; }
	// Output texture coordinate count and projection flag (D3DTTFF_*), informational.
	virtual void Calculate_Texture_Matrix(W3DTexMatrix &out, std::uint32_t syncTimeMs) = 0;
	virtual void Reset(std::uint32_t syncTimeMs) { (void)syncTimeMs; }

	unsigned Stage;
};

// Random source for RandomTextureMapperClass: returns floats in [0, 1).
typedef std::function<float()> MapperRandom;

// ZH vertmaterial.cpp:593-940 type switch. attributeType is the stage's mapping value (Attributes >> 16 & 0xFF for stage 0,
// >> 8 & 0xFF for stage 1). Returns nullptr for plain UV passthrough (types 0, 5 SILHOUETTE and any value the switch does
// not name, which fall to ZH's `default: break`; *unknownType is set when the value is not a defined type at all).
// *error is set on bad arguments (nullptr result with a non-empty error).
std::unique_ptr<TextureMapperClass> Create_Texture_Mapper(int attributeType, unsigned stage, const std::string &argsText,
	std::uint32_t syncTimeMs, const MapperRandom &random, std::string *error, bool *unknownType = nullptr);

const char *W3D_Mapper_Type_Name(int type);
