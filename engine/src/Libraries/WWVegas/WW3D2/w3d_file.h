// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Port of ZH Libraries/Source/WWVegas/WW3D2/w3d_file.h: every chunk id and the structures the
// retail corpus uses (mesh, hierarchy, animation, HLod, box, emitter, shdmesh), plus the BFME
// additions. Names, values and layouts are the ZH ones except where marked BFME; static_asserts
// pin every layout to the byte sizes found in retail files.
//
// BFME additions (not in ZH). Layouts come from the matched BFME2 decompile where one exists,
// otherwise from OpenSAGE FileFormats.W3d (GPL-3), and were checked against every retail chunk:
//   W3dVertInfStruct                   second bone + two weights in ZH's Pad[6]
//   W3D_CHUNK_SHADER_MATERIAL_ID 0x3F, SHADER_MATERIALS 0x50..0x53, TANGENTS 0x60, BITANGENTS 0x61
//   W3D_CHUNK_VERTICES_2 0xC00 / VERTEX_NORMALS_2 0xC01 (bone-1 space streams of dual-bone skins)
//   W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL 0x284 (BFME2 Load_BFME2MotionChannel, retail 0x001A46B4)
//   animation channel type 15 = ANIM_CHANNEL_FADE (BFME2 hrawanim.cpp add_channel)

#pragma once

#include <cstddef>
#include <cstdint>

typedef std::uint8_t uint8;
typedef std::uint16_t uint16;
typedef std::uint32_t uint32;
typedef std::int32_t sint32;
typedef float float32;

#define W3D_MAKE_VERSION(major, minor) (((major) << 16) | (minor))
#define W3D_NAME_LEN 16

enum
{
	W3D_CHUNK_MESH = 0x00000000,
	W3D_CHUNK_VERTICES = 0x00000002,
	W3D_CHUNK_VERTEX_NORMALS = 0x00000003,
	W3D_CHUNK_MESH_USER_TEXT = 0x0000000C,
	W3D_CHUNK_VERTEX_INFLUENCES = 0x0000000E,
	W3D_CHUNK_MESH_HEADER3 = 0x0000001F,
	W3D_CHUNK_TRIANGLES = 0x00000020,
	W3D_CHUNK_VERTEX_SHADE_INDICES = 0x00000022,

	W3D_CHUNK_PRELIT_UNLIT = 0x00000023,
	W3D_CHUNK_PRELIT_VERTEX = 0x00000024,
	W3D_CHUNK_PRELIT_LIGHTMAP_MULTI_PASS = 0x00000025,
	W3D_CHUNK_PRELIT_LIGHTMAP_MULTI_TEXTURE = 0x00000026,

	W3D_CHUNK_MATERIAL_INFO = 0x00000028,
	W3D_CHUNK_SHADERS = 0x00000029,
	W3D_CHUNK_VERTEX_MATERIALS = 0x0000002A,
	W3D_CHUNK_VERTEX_MATERIAL = 0x0000002B,
	W3D_CHUNK_VERTEX_MATERIAL_NAME = 0x0000002C,
	W3D_CHUNK_VERTEX_MATERIAL_INFO = 0x0000002D,
	W3D_CHUNK_VERTEX_MAPPER_ARGS0 = 0x0000002E,
	W3D_CHUNK_VERTEX_MAPPER_ARGS1 = 0x0000002F,
	W3D_CHUNK_TEXTURES = 0x00000030,
	W3D_CHUNK_TEXTURE = 0x00000031,
	W3D_CHUNK_TEXTURE_NAME = 0x00000032,
	W3D_CHUNK_TEXTURE_INFO = 0x00000033,
	W3D_CHUNK_MATERIAL_PASS = 0x00000038,
	W3D_CHUNK_VERTEX_MATERIAL_IDS = 0x00000039,
	W3D_CHUNK_SHADER_IDS = 0x0000003A,
	W3D_CHUNK_DCG = 0x0000003B,
	W3D_CHUNK_DIG = 0x0000003C,
	W3D_CHUNK_SCG = 0x0000003E,
	W3D_CHUNK_SHADER_MATERIAL_ID = 0x0000003F, // BFME2: u32 index into the SHADER_MATERIALS list
	W3D_CHUNK_TEXTURE_STAGE = 0x00000048,
	W3D_CHUNK_TEXTURE_IDS = 0x00000049,
	W3D_CHUNK_STAGE_TEXCOORDS = 0x0000004A,
	W3D_CHUNK_PER_FACE_TEXCOORD_IDS = 0x0000004B,

	W3D_CHUNK_SHADER_MATERIALS = 0x00000050, // BFME2 FX shader materials
	W3D_CHUNK_SHADER_MATERIAL = 0x00000051,
	W3D_CHUNK_SHADER_MATERIAL_HEADER = 0x00000052,
	W3D_CHUNK_SHADER_MATERIAL_PROPERTY = 0x00000053,

	W3D_CHUNK_DEFORM = 0x00000058,
	W3D_CHUNK_DEFORM_SET = 0x00000059,
	W3D_CHUNK_DEFORM_KEYFRAME = 0x0000005A,
	W3D_CHUNK_DEFORM_DATA = 0x0000005B,

	W3D_CHUNK_TANGENTS = 0x00000060, // BFME2: float3[NumVertices]
	W3D_CHUNK_BITANGENTS = 0x00000061,

	W3D_CHUNK_PS2_SHADERS = 0x00000080,
	W3D_CHUNK_AABTREE = 0x00000090,
	W3D_CHUNK_AABTREE_HEADER = 0x00000091,
	W3D_CHUNK_AABTREE_POLYINDICES = 0x00000092,
	W3D_CHUNK_AABTREE_NODES = 0x00000093,

	W3D_CHUNK_HIERARCHY = 0x00000100,
	W3D_CHUNK_HIERARCHY_HEADER = 0x00000101,
	W3D_CHUNK_PIVOTS = 0x00000102,
	W3D_CHUNK_PIVOT_FIXUPS = 0x00000103,

	W3D_CHUNK_ANIMATION = 0x00000200,
	W3D_CHUNK_ANIMATION_HEADER = 0x00000201,
	W3D_CHUNK_ANIMATION_CHANNEL = 0x00000202,
	W3D_CHUNK_BIT_CHANNEL = 0x00000203,

	W3D_CHUNK_COMPRESSED_ANIMATION = 0x00000280,
	W3D_CHUNK_COMPRESSED_ANIMATION_HEADER = 0x00000281,
	W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL = 0x00000282,
	W3D_CHUNK_COMPRESSED_BIT_CHANNEL = 0x00000283,
	W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL = 0x00000284, // BFME2

	W3D_CHUNK_MORPH_ANIMATION = 0x000002C0,
	W3D_CHUNK_MORPHANIM_HEADER = 0x000002C1,
	W3D_CHUNK_MORPHANIM_CHANNEL = 0x000002C2,
	W3D_CHUNK_MORPHANIM_POSENAME = 0x000002C3,
	W3D_CHUNK_MORPHANIM_KEYDATA = 0x000002C4,
	W3D_CHUNK_MORPHANIM_PIVOTCHANNELDATA = 0x000002C5,

	W3D_CHUNK_HMODEL = 0x00000300,
	W3D_CHUNK_HMODEL_HEADER = 0x00000301,
	W3D_CHUNK_NODE = 0x00000302,
	W3D_CHUNK_COLLISION_NODE = 0x00000303,
	W3D_CHUNK_SKIN_NODE = 0x00000304,
	OBSOLETE_W3D_CHUNK_HMODEL_AUX_DATA = 0x00000305,
	OBSOLETE_W3D_CHUNK_SHADOW_NODE = 0x00000306,

	W3D_CHUNK_LODMODEL = 0x00000400,
	W3D_CHUNK_LODMODEL_HEADER = 0x00000401,
	W3D_CHUNK_LOD = 0x00000402,

	W3D_CHUNK_COLLECTION = 0x00000420,
	W3D_CHUNK_COLLECTION_HEADER = 0x00000421,
	W3D_CHUNK_COLLECTION_OBJ_NAME = 0x00000422,
	W3D_CHUNK_PLACEHOLDER = 0x00000423,
	W3D_CHUNK_TRANSFORM_NODE = 0x00000424,

	W3D_CHUNK_POINTS = 0x00000440,

	W3D_CHUNK_LIGHT = 0x00000460,
	W3D_CHUNK_LIGHT_INFO = 0x00000461,
	W3D_CHUNK_SPOT_LIGHT_INFO = 0x00000462,
	W3D_CHUNK_NEAR_ATTENUATION = 0x00000463,
	W3D_CHUNK_FAR_ATTENUATION = 0x00000464,

	W3D_CHUNK_EMITTER = 0x00000500,
	W3D_CHUNK_EMITTER_HEADER = 0x00000501,
	W3D_CHUNK_EMITTER_USER_DATA = 0x00000502,
	W3D_CHUNK_EMITTER_INFO = 0x00000503,
	W3D_CHUNK_EMITTER_INFOV2 = 0x00000504,
	W3D_CHUNK_EMITTER_PROPS = 0x00000505,
	OBSOLETE_W3D_CHUNK_EMITTER_COLOR_KEYFRAME = 0x00000506,
	OBSOLETE_W3D_CHUNK_EMITTER_OPACITY_KEYFRAME = 0x00000507,
	OBSOLETE_W3D_CHUNK_EMITTER_SIZE_KEYFRAME = 0x00000508,
	W3D_CHUNK_EMITTER_LINE_PROPERTIES = 0x00000509,
	W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES = 0x0000050A,
	W3D_CHUNK_EMITTER_FRAME_KEYFRAMES = 0x0000050B,
	W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES = 0x0000050C,
	W3D_CHUNK_EMITTER_EXTRA_INFO = 0x0000050D,

	W3D_CHUNK_AGGREGATE = 0x00000600,
	W3D_CHUNK_AGGREGATE_HEADER = 0x00000601,
	W3D_CHUNK_AGGREGATE_INFO = 0x00000602,
	W3D_CHUNK_TEXTURE_REPLACER_INFO = 0x00000603,
	W3D_CHUNK_AGGREGATE_CLASS_INFO = 0x00000604,

	W3D_CHUNK_HLOD = 0x00000700,
	W3D_CHUNK_HLOD_HEADER = 0x00000701,
	W3D_CHUNK_HLOD_LOD_ARRAY = 0x00000702,
	W3D_CHUNK_HLOD_SUB_OBJECT_ARRAY_HEADER = 0x00000703,
	W3D_CHUNK_HLOD_SUB_OBJECT = 0x00000704,
	W3D_CHUNK_HLOD_AGGREGATE_ARRAY = 0x00000705,
	W3D_CHUNK_HLOD_PROXY_ARRAY = 0x00000706,

	W3D_CHUNK_BOX = 0x00000740,
	W3D_CHUNK_SPHERE = 0x00000741,
	W3D_CHUNK_RING = 0x00000742,

	W3D_CHUNK_NULL_OBJECT = 0x00000750,

	W3D_CHUNK_LIGHTSCAPE = 0x00000800,
	W3D_CHUNK_LIGHTSCAPE_LIGHT = 0x00000801,
	W3D_CHUNK_LIGHT_TRANSFORM = 0x00000802,

	W3D_CHUNK_DAZZLE = 0x00000900,
	W3D_CHUNK_DAZZLE_NAME = 0x00000901,
	W3D_CHUNK_DAZZLE_TYPENAME = 0x00000902,

	W3D_CHUNK_SOUNDROBJ = 0x00000A00,
	W3D_CHUNK_SOUNDROBJ_HEADER = 0x00000A01,
	W3D_CHUNK_SOUNDROBJ_DEFINITION = 0x00000A02,

	W3D_CHUNK_SHDMESH = 0x00000B00,
	W3D_CHUNK_SHDMESH_NAME = 0x00000B01,
	W3D_CHUNK_SHDMESH_HEADER = 0x00000B02,
	W3D_CHUNK_SHDMESH_USER_TEXT = 0x00000B03,
	W3D_CHUNK_SHDSUBMESH = 0x00000B20,
	W3D_CHUNK_SHDSUBMESH_HEADER = 0x00000B21,
	W3D_CHUNK_SHDSUBMESH_SHADER = 0x00000B40,
	W3D_CHUNK_SHDSUBMESH_SHADER_CLASSID = 0x00000B41,
	W3D_CHUNK_SHDSUBMESH_SHADER_DEF = 0x00000B42,
	W3D_CHUNK_SHDSUBMESH_VERTICES = 0x00000B43,
	W3D_CHUNK_SHDSUBMESH_VERTEX_NORMALS = 0x00000B44,
	W3D_CHUNK_SHDSUBMESH_TRIANGLES = 0x00000B45,
	W3D_CHUNK_SHDSUBMESH_VERTEX_SHADE_INDICES = 0x00000B46,
	W3D_CHUNK_SHDSUBMESH_UV0 = 0x00000B47,
	W3D_CHUNK_SHDSUBMESH_UV1 = 0x00000B48,
	W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_S = 0x00000B49,
	W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_T = 0x00000B4A,
	W3D_CHUNK_SHDSUBMESH_TANGENT_BASIS_SxT = 0x00000B4B,
	W3D_CHUNK_SHDSUBMESH_VERTEX_COLOR = 0x00000B4C,
	W3D_CHUNK_SHDSUBMESH_VERTEX_INFLUENCES = 0x00000B4D,

	W3D_CHUNK_VERTICES_2 = 0x00000C00, // BFME: second-bone position stream of dual-bone skins
	W3D_CHUNK_VERTEX_NORMALS_2 = 0x00000C01, // BFME: second-bone normal stream

	// WWShade ids found INSIDE W3D_CHUNK_SHDSUBMESH_SHADER_DEF (the three retail ShdMesh files). They are
	// ZH WWVegas/wwshade chunk ids, not W3D ones: ShdDefClass::Save/Load_W3D writes CHUNKID_VARIABLES
	// (shddef.cpp:127) and each shader class writes its own (shdsimple.cpp:62, shdbumpdiff.cpp:64,
	// shdbumpspec.cpp:63, shdglossmask.cpp:62, shdcubemap.cpp:63).
	WWSHADE_CHUNK_SHDDEF_VARIABLES = 0x16490430,
	WWSHADE_CHUNK_SIMPLE_VARIABLES = 0x16490450, // shared by shdsimple / shdbumpdiff / shdbumpspec
	WWSHADE_CHUNK_GLOSSMASK_VARIABLES = 0x16490460,
	WWSHADE_CHUNK_CUBEMAP_VARIABLES = 0x16490470,
};

// Mesh attribute flags (W3dMeshHeader3Struct::Attributes)
#define W3D_MESH_FLAG_COLLISION_TYPE_MASK 0x00000FF0
#define W3D_MESH_FLAG_HIDDEN 0x00001000
#define W3D_MESH_FLAG_TWO_SIDED 0x00002000
#define W3D_MESH_FLAG_CAST_SHADOW 0x00008000
#define W3D_MESH_FLAG_GEOMETRY_TYPE_MASK 0x00FF0000
#define W3D_MESH_FLAG_GEOMETRY_TYPE_NORMAL 0x00000000
#define W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ALIGNED 0x00010000
#define W3D_MESH_FLAG_GEOMETRY_TYPE_SKIN 0x00020000
#define W3D_MESH_FLAG_GEOMETRY_TYPE_CAMERA_ORIENTED 0x00060000
#define W3D_MESH_FLAG_PRELIT_MASK 0x0F000000
#define W3D_MESH_FLAG_SHATTERABLE 0x10000000
#define W3D_MESH_FLAG_NPATCHABLE 0x20000000

// Shader enums used by the device layer (W3dShaderStruct fields)
enum
{
	W3DSHADER_DEPTHMASK_WRITE_DISABLE = 0,
	W3DSHADER_DEPTHMASK_WRITE_ENABLE = 1,
	W3DSHADER_DESTBLENDFUNC_ZERO = 0,
	W3DSHADER_DESTBLENDFUNC_ONE = 1,
	W3DSHADER_DESTBLENDFUNC_SRC_COLOR = 2,
	W3DSHADER_DESTBLENDFUNC_ONE_MINUS_SRC_COLOR = 3,
	W3DSHADER_DESTBLENDFUNC_SRC_ALPHA = 4,
	W3DSHADER_DESTBLENDFUNC_ONE_MINUS_SRC_ALPHA = 5,
	W3DSHADER_SRCBLENDFUNC_ZERO = 0,
	W3DSHADER_SRCBLENDFUNC_ONE = 1,
	W3DSHADER_SRCBLENDFUNC_SRC_ALPHA = 2,
	W3DSHADER_SRCBLENDFUNC_ONE_MINUS_SRC_ALPHA = 3,
	W3DSHADER_TEXTURING_DISABLE = 0,
	W3DSHADER_TEXTURING_ENABLE = 1,
	W3DSHADER_ALPHATEST_DISABLE = 0,
	W3DSHADER_ALPHATEST_ENABLE = 1,
};

struct W3dVectorStruct
{
	float32 X;
	float32 Y;
	float32 Z;
};

struct W3dQuaternionStruct
{
	float32 Q[4]; // x, y, z, w
};

struct W3dTexCoordStruct
{
	float32 U;
	float32 V;
};

struct W3dRGBStruct
{
	uint8 R;
	uint8 G;
	uint8 B;
	uint8 pad;
};

struct W3dRGBAStruct
{
	uint8 R;
	uint8 G;
	uint8 B;
	uint8 A;
};

struct W3dTriStruct
{
	uint32 Vindex[3];       // vertex,vnormal,texcoord,color indices
	uint32 Attributes;      // attributes bits
	W3dVectorStruct Normal; // plane normal
	float32 Dist;           // plane distance
};

// ZH declares BoneIdx + 6 pad bytes. BFME fills the pad with a second bone and two weights
// (OpenSAGE W3dVertexInfluence.cs; verified on all 4,323 retail influence chunks: weights sum to
// 100, or are 0/0 on 1,653 vertices in 15 meshes, which OpenSAGE reads as Weight0 = 100).
// Deformed position is (Weight0/100) * M[BoneIdx] * p0 + (Weight1/100) * M[Bone1Idx] * p1 with p1
// taken from W3D_CHUNK_VERTICES_2 (dual-bone skins only).
struct W3dVertInfStruct
{
	uint16 BoneIdx;
	uint16 Bone1Idx;
	uint16 Weight0;
	uint16 Weight1;
};

struct W3dMeshHeader3Struct
{
	uint32 Version;
	uint32 Attributes;
	char MeshName[W3D_NAME_LEN];
	char ContainerName[W3D_NAME_LEN];
	uint32 NumTris;
	uint32 NumVertices;
	uint32 NumMaterials;
	uint32 NumDamageStages;
	sint32 SortLevel;
	uint32 PrelitVersion;
	uint32 FutureCounts[1];
	uint32 VertexChannels;
	uint32 FaceChannels;
	W3dVectorStruct Min;
	W3dVectorStruct Max;
	W3dVectorStruct SphCenter;
	float32 SphRadius;
};

// W3dMeshHeader3Struct::VertexChannels bits (ZH w3d_file.h W3D_VERTEX_CHANNEL_*) with the BFME
// tangent / bitangent bits that mark the 0x60 / 0x61 streams (retail values 0x03, 0x13, 0x63, 0x73).
#define W3D_VERTEX_CHANNEL_LOCATION 0x00000001
#define W3D_VERTEX_CHANNEL_NORMAL 0x00000002
#define W3D_VERTEX_CHANNEL_TEXCOORD 0x00000004
#define W3D_VERTEX_CHANNEL_COLOR 0x00000008
#define W3D_VERTEX_CHANNEL_BONEID 0x00000010
#define W3D_VERTEX_CHANNEL_TANGENT 0x00000020
#define W3D_VERTEX_CHANNEL_BITANGENT 0x00000040

struct W3dMaterialInfoStruct
{
	uint32 PassCount;
	uint32 VertexMaterialCount;
	uint32 ShaderCount;
	uint32 TextureCount;
};

struct W3dShaderStruct
{
	uint8 DepthCompare;
	uint8 DepthMask;
	uint8 ColorMask;
	uint8 DestBlend;
	uint8 FogFunc;
	uint8 PriGradient;
	uint8 SecGradient;
	uint8 SrcBlend;
	uint8 Texturing;
	uint8 DetailColorFunc;
	uint8 DetailAlphaFunc;
	uint8 ShaderPreset;
	uint8 AlphaTest;
	uint8 PostDetailColorFunc;
	uint8 PostDetailAlphaFunc;
	uint8 pad[1];
};

struct W3dVertexMaterialStruct
{
	uint32 Attributes;
	W3dRGBStruct Ambient;
	W3dRGBStruct Diffuse;
	W3dRGBStruct Specular;
	W3dRGBStruct Emissive;
	float32 Shininess;
	float32 Opacity;
	float32 Translucency;
};

struct W3dTextureInfoStruct
{
	uint16 Attributes;
	uint16 AnimType;
	uint32 FrameCount;
	float32 FrameRate;
};

// BFME2 FX shader material header (W3D_CHUNK_SHADER_MATERIAL_HEADER, 37 bytes, OpenSAGE
// W3dShaderMaterialHeader.cs): u8 number; char TypeName[32]; u32 reserved. Packed: no padding.
#pragma pack(push, 1)
struct W3dShaderMaterialHeaderStruct
{
	uint8 Number;
	char TypeName[W3D_NAME_LEN * 2];
	uint32 Reserved;
};
#pragma pack(pop)

// W3D_CHUNK_SHADER_MATERIAL_PROPERTY value types (OpenSAGE W3dShaderMaterialPropertyType.cs).
enum
{
	W3DSHADERMATERIAL_PROPERTY_TEXTURE = 1, // u32 length (incl. NUL) + chars
	W3DSHADERMATERIAL_PROPERTY_FLOAT = 2,
	W3DSHADERMATERIAL_PROPERTY_VECTOR2 = 3,
	W3DSHADERMATERIAL_PROPERTY_VECTOR3 = 4,
	W3DSHADERMATERIAL_PROPERTY_VECTOR4 = 5,
	W3DSHADERMATERIAL_PROPERTY_INT = 6,
	W3DSHADERMATERIAL_PROPERTY_BOOL = 7, // ONE byte (0 or 1), not 4: every retail bool property chunk proves it
};

struct W3dMeshAABTreeHeader
{
	uint32 NodeCount;
	uint32 PolyCount;
	uint32 Padding[6];
};

struct W3dMeshAABTreeNode
{
	W3dVectorStruct Min;
	W3dVectorStruct Max;
	uint32 FrontOrPoly0;
	uint32 BackOrPolyCount;
};

struct W3dHierarchyStruct
{
	uint32 Version;
	char Name[W3D_NAME_LEN];
	uint32 NumPivots;
	W3dVectorStruct Center;
};

struct W3dPivotStruct
{
	char Name[W3D_NAME_LEN];
	uint32 ParentIdx; // 0xffffffff = root pivot; no parent
	W3dVectorStruct Translation;
	W3dVectorStruct EulerAngles;
	W3dQuaternionStruct Rotation;
};

struct W3dPivotFixupStruct
{
	float32 TM[4][3]; // a direct dump of a MAX 3x4 matrix (exporter only)
};

// ---- animation -----------------------------------------------------------------------------

struct W3dAnimHeaderStruct
{
	uint32 Version;
	char Name[W3D_NAME_LEN];
	char HierarchyName[W3D_NAME_LEN];
	uint32 NumFrames;
	uint32 FrameRate;
};

struct W3dCompressedAnimHeaderStruct
{
	uint32 Version;
	char Name[W3D_NAME_LEN];
	char HierarchyName[W3D_NAME_LEN];
	uint32 NumFrames;
	uint16 FrameRate;
	uint16 Flavor;
};

enum
{
	ANIM_CHANNEL_X = 0,
	ANIM_CHANNEL_Y,
	ANIM_CHANNEL_Z,
	ANIM_CHANNEL_XR,
	ANIM_CHANNEL_YR,
	ANIM_CHANNEL_ZR,
	ANIM_CHANNEL_Q,

	ANIM_CHANNEL_TIMECODED_X,
	ANIM_CHANNEL_TIMECODED_Y,
	ANIM_CHANNEL_TIMECODED_Z,
	ANIM_CHANNEL_TIMECODED_Q,

	ANIM_CHANNEL_ADAPTIVEDELTA_X,
	ANIM_CHANNEL_ADAPTIVEDELTA_Y,
	ANIM_CHANNEL_ADAPTIVEDELTA_Z,
	ANIM_CHANNEL_ADAPTIVEDELTA_Q,

	ANIM_CHANNEL_FADE = 15, // BFME: per-pivot scalar, defaults to 1.0 (Rva0095B260 Get_Fade)
};

enum
{
	ANIM_FLAVOR_TIMECODED = 0,
	ANIM_FLAVOR_ADAPTIVE_DELTA,
	ANIM_FLAVOR_VALID
};

enum
{
	BIT_CHANNEL_VIS = 0, // turn meshes on and off depending on anim frame
	BIT_CHANNEL_TIMECODED_VIS,
};

// Classic structures. ZH declares Data[1]; the chunk body is the fixed prefix followed by the data
// (the loaders use sizeof(struct) to read, then the remaining length to size the arrays).
struct W3dAnimChannelStruct
{
	uint16 FirstFrame;
	uint16 LastFrame;
	uint16 VectorLen; // length of each vector in this channel
	uint16 Flags;     // channel type
	uint16 Pivot;     // pivot affected by this channel
	uint16 pad;
	float32 Data[1];  // (LastFrame - FirstFrame + 1) * VectorLen floats
};

struct W3dBitChannelStruct
{
	uint16 FirstFrame; // frames outside First..Last are DefaultVal
	uint16 LastFrame;
	uint16 Flags;
	uint16 Pivot;
	uint8 DefaultVal;
	uint8 Data[1];     // (LastFrame - FirstFrame + 1) / 8 bytes, rounded up
};

// A time code is a uint32 that prefixes each vector; the MSB marks a binary (non-interpolated) move.
#define W3D_TIMECODED_BINARY_MOVEMENT_FLAG 0x80000000

struct W3dTimeCodedAnimChannelStruct
{
	uint32 NumTimeCodes;
	uint16 Pivot;
	uint8 VectorLen;
	uint8 Flags;
	uint32 Data[1]; // NumTimeCodes * (VectorLen * 4 + 4) bytes
};

// The bit channel is encoded into the MSB of each time code.
#define W3D_TIMECODED_BIT_MASK 0x80000000

struct W3dTimeCodedBitChannelStruct
{
	uint32 NumTimeCodes;
	uint16 Pivot;
	uint8 Flags;
	uint8 DefaultVal;
	uint32 Data[1]; // NumTimeCodes * 4 bytes
};

struct W3dAdaptiveDeltaAnimChannelStruct
{
	uint32 NumFrames;
	uint16 Pivot;
	uint8 VectorLen;
	uint8 Flags;
	float Scale;    // filter table scale
	uint32 Data[1]; // VectorLen initial floats, then the packets
};

// BFME2 motion channel chunk (W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL). Eight byte header,
// then encoding specific data (BFME2MotionChannelHeader in Load_BFME2MotionChannel):
//   Encoding 0: u16 TimeCodes[Count] (bit 15 = binary flag), pad to 4 bytes, float Samples[Count * Components]
//   Encoding 1/2 (adaptive delta, 4 / 8 bit): float Scale, float Initial[Components], then
//     ceil(Count / 16) rows of Components blocks of (u8 filter index + 8 or 16 delta bytes)
struct W3dMotionChannelHeaderStruct
{
	uint8 Version;    // must be 0
	uint8 Encoding;   // 0 timecoded, 1 adaptive delta 4 bit, 2 adaptive delta 8 bit
	uint8 Components; // vector length
	uint8 Type;       // ANIM_CHANNEL_*
	uint16 Count;     // time code count (encoding 0) or frame count (encoding 1/2)
	uint16 Pivot;
};

// ---- emitters ------------------------------------------------------------------------------

struct W3dEmitterHeaderStruct
{
	uint32 Version;
	char Name[W3D_NAME_LEN];
};

struct W3dEmitterUserInfoStruct
{
	uint32 Type;
	uint32 SizeofStringParam;
	char StringParam[1];
};

struct W3dEmitterInfoStruct
{
	char TextureFilename[260];
	float32 StartSize;
	float32 EndSize;
	float32 Lifetime;
	float32 EmissionRate;
	float32 MaxEmissions;
	float32 VelocityRandom;
	float32 PositionRandom;
	float32 FadeTime;
	float32 Gravity;
	float32 Elasticity;
	W3dVectorStruct Velocity;
	W3dVectorStruct Acceleration;
	W3dRGBAStruct StartColor;
	W3dRGBAStruct EndColor;
};

struct W3dEmitterExtraInfoStruct
{
	float32 FutureStartTime;
	uint32 Padding[9];
};

struct W3dVolumeRandomizerStruct
{
	uint32 ClassID;
	float32 Value1;
	float32 Value2;
	float32 Value3;
	uint32 reserved[4];
};

struct W3dEmitterInfoStructV2
{
	uint32 BurstSize;
	W3dVolumeRandomizerStruct CreationVolume;
	W3dVolumeRandomizerStruct VelRandom;
	float32 OutwardVel;
	float32 VelInherit;
	W3dShaderStruct Shader;
	uint32 RenderMode;
	uint32 FrameMode;
	uint32 reserved[6];
};

struct W3dEmitterPropertyStruct
{
	uint32 ColorKeyframes;
	uint32 OpacityKeyframes;
	uint32 SizeKeyframes;
	W3dRGBAStruct ColorRandom;
	float32 OpacityRandom;
	float32 SizeRandom;
	uint32 reserved[4];
};

struct W3dEmitterColorKeyframeStruct
{
	float32 Time;
	W3dRGBAStruct Color;
};

struct W3dEmitterOpacityKeyframeStruct
{
	float32 Time;
	float32 Opacity;
};

struct W3dEmitterSizeKeyframeStruct
{
	float32 Time;
	float32 Size;
};

struct W3dEmitterRotationHeaderStruct
{
	uint32 KeyframeCount;
	float32 Random;
	float32 OrientationRandom;
	uint32 Reserved[1];
};

struct W3dEmitterRotationKeyframeStruct
{
	float32 Time;
	float32 Rotation;
};

struct W3dEmitterFrameHeaderStruct
{
	uint32 KeyframeCount;
	float32 Random;
	uint32 Reserved[2];
};

struct W3dEmitterFrameKeyframeStruct
{
	float32 Time;
	float32 Frame;
};

struct W3dEmitterBlurTimeHeaderStruct
{
	uint32 KeyframeCount;
	float32 Random;
	uint32 Reserved[1];
};

struct W3dEmitterBlurTimeKeyframeStruct
{
	float32 Time;
	float32 BlurTime;
};

struct W3dEmitterLinePropertiesStruct
{
	uint32 Flags;
	uint32 SubdivisionLevel;
	float32 NoiseAmplitude;
	float32 MergeAbortFactor;
	float32 TextureTileFactor;
	float32 UPerSec;
	float32 VPerSec;
	uint32 Reserved[9];
};

#define W3D_EMITTER_RENDER_MODE_TRI_PARTICLES 0
#define W3D_EMITTER_RENDER_MODE_QUAD_PARTICLES 1
#define W3D_EMITTER_RENDER_MODE_LINE 2
#define W3D_EMITTER_RENDER_MODE_LINEGRP_TETRA 3
#define W3D_EMITTER_RENDER_MODE_LINEGRP_PRISM 4

#define W3D_EMITTER_FRAME_MODE_1x1 0
#define W3D_EMITTER_FRAME_MODE_2x2 1
#define W3D_EMITTER_FRAME_MODE_4x4 2
#define W3D_EMITTER_FRAME_MODE_8x8 3
#define W3D_EMITTER_FRAME_MODE_16x16 4

// W3dEmitterLinePropertiesStruct::Flags
#define W3D_ELINE_MERGE_INTERSECTIONS 0x00000001
#define W3D_ELINE_FREEZE_RANDOM 0x00000002
#define W3D_ELINE_DISABLE_SORTING 0x00000004
#define W3D_ELINE_END_CAPS 0x00000008
#define W3D_ELINE_TEXTURE_MAP_MODE_MASK 0xFF000000
#define W3D_ELINE_TEXTURE_MAP_MODE_OFFSET 24

// ---- HLod / box ----------------------------------------------------------------------------

struct W3dHLodHeaderStruct
{
	uint32 Version;
	uint32 LodCount;
	char Name[W3D_NAME_LEN];
	char HierarchyName[W3D_NAME_LEN];
};

struct W3dHLodArrayHeaderStruct
{
	uint32 ModelCount;
	float32 MaxScreenSize;
};

struct W3dHLodSubObjectStruct
{
	uint32 BoneIndex;
	char Name[W3D_NAME_LEN * 2];
};

struct W3dBoxStruct
{
	uint32 Version;
	uint32 Attributes;
	char Name[2 * W3D_NAME_LEN];
	W3dRGBStruct Color;
	W3dVectorStruct Center;
	W3dVectorStruct Extent;
};

// ---- ShdMesh (3 retail files; no BFME loader is identified, structure only) -------------------

struct W3dShdMeshHeaderStruct
{
	uint32 Version;
	uint32 Attributes;
	uint32 NumTris;
	uint32 NumVertices;
	uint32 NumSubMeshes;
	uint32 FutureCounts[5];
	W3dVectorStruct BoxMin;
	W3dVectorStruct BoxMax;
	W3dVectorStruct SphCenter;
	float32 SphRadius;
};

struct W3dShdSubMeshHeaderStruct
{
	uint32 NumTris;
	uint32 NumVertices;
	uint32 FutureCounts[2];
	W3dVectorStruct BoxMin;
	W3dVectorStruct BoxMax;
	W3dVectorStruct SphCenter;
	float32 SphRadius;
};

static_assert(sizeof(W3dVectorStruct) == 12, "layout");
static_assert(sizeof(W3dTriStruct) == 32, "layout");
static_assert(sizeof(W3dVertInfStruct) == 8, "layout");
static_assert(sizeof(W3dMeshHeader3Struct) == 116, "layout");
static_assert(sizeof(W3dMaterialInfoStruct) == 16, "layout");
static_assert(sizeof(W3dShaderStruct) == 16, "layout");
static_assert(sizeof(W3dVertexMaterialStruct) == 32, "layout");
static_assert(sizeof(W3dTextureInfoStruct) == 12, "layout");
static_assert(sizeof(W3dShaderMaterialHeaderStruct) == 37, "layout");
static_assert(sizeof(W3dMeshAABTreeHeader) == 32, "layout");
static_assert(sizeof(W3dMeshAABTreeNode) == 32, "layout");
static_assert(sizeof(W3dHierarchyStruct) == 36, "layout");
static_assert(sizeof(W3dPivotStruct) == 60, "layout");
static_assert(sizeof(W3dPivotFixupStruct) == 48, "layout");
static_assert(sizeof(W3dAnimHeaderStruct) == 44, "layout");
static_assert(sizeof(W3dCompressedAnimHeaderStruct) == 44, "layout");
static_assert(offsetof(W3dAnimChannelStruct, Data) == 12, "layout");
static_assert(offsetof(W3dBitChannelStruct, Data) == 9, "layout");
static_assert(offsetof(W3dTimeCodedAnimChannelStruct, Data) == 8, "layout");
static_assert(offsetof(W3dTimeCodedBitChannelStruct, Data) == 8, "layout");
static_assert(offsetof(W3dAdaptiveDeltaAnimChannelStruct, Data) == 12, "layout");
static_assert(sizeof(W3dMotionChannelHeaderStruct) == 8, "layout");
static_assert(sizeof(W3dEmitterHeaderStruct) == 20, "layout");
static_assert(sizeof(W3dEmitterUserInfoStruct) == 12, "layout"); // ZH reads this whole struct, then SizeofStringParam more bytes
static_assert(sizeof(W3dEmitterInfoStruct) == 332, "layout");
static_assert(sizeof(W3dEmitterExtraInfoStruct) == 40, "layout");
static_assert(sizeof(W3dVolumeRandomizerStruct) == 32, "layout");
static_assert(sizeof(W3dEmitterInfoStructV2) == 124, "layout");
static_assert(sizeof(W3dEmitterPropertyStruct) == 40, "layout");
static_assert(sizeof(W3dEmitterRotationHeaderStruct) == 16, "layout");
static_assert(sizeof(W3dEmitterFrameHeaderStruct) == 16, "layout");
static_assert(sizeof(W3dEmitterBlurTimeHeaderStruct) == 12, "layout");
static_assert(sizeof(W3dEmitterLinePropertiesStruct) == 64, "layout");
static_assert(sizeof(W3dHLodHeaderStruct) == 40, "layout");
static_assert(sizeof(W3dHLodArrayHeaderStruct) == 8, "layout");
static_assert(sizeof(W3dHLodSubObjectStruct) == 36, "layout");
static_assert(sizeof(W3dBoxStruct) == 68, "layout");
static_assert(sizeof(W3dShdMeshHeaderStruct) == 80, "layout");
static_assert(sizeof(W3dShdSubMeshHeaderStruct) == 56, "layout");
