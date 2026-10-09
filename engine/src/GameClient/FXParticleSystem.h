// OpenBFME. GPL-3.0.
//
// FXParticleSystem templates: the `FXParticleSystem <Name> ... End` INI block of RotWK and the 27 module classes
// it instantiates. BFME replaced ZH's flat `ParticleSystem` template (GameClient/ParticleSys.h
// ParticleSystemTemplate) with a System sub-block plus one module per category (namespace FXParticleSystem in the
// BFME2 decompile: ParticleSystemTemplate, DefaultColorModuleInfo, BoxEmissionVolumeInfo, ...). Lane FX-1.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; every address RW; derivation in the lane notes
// fx-ini-parse.md, spec fx-and-particles.md):
//   * block parser 0x5fc7db: name = getNextToken; an existing template of that name (case sensitive, hash 0x42b6c1 +
//     memcmp) is destroyed and rebuilt in place for ANY load type (no merge, no override object); load type 2
//     (CREATE_OVERRIDES) additionally sets manager byte +0x84. The template table object 0xde3a58 has ten rows: the
//     nine category names (0xc339d8) and `System` (0xbf76b8 -> 0x5f33c2, no `=`, 21 rows built on the stack).
//   * a category row reads ONE token, the class name, and looks it up in the category's registry with an exact
//     compare (0x5f7bdb ...); a missing token throws code 3; an unknown class walks off the end of the registry
//     list in RW (access violation at address 4, no INIException): refused here as code 5 (rule 10). A repeated
//     category replaces the earlier module; `Event` appends to a list.
//   * the 27 registered classes (registry init 0x7a8c69-0x7a8dd3 and class ctors 0x7a8d84..0x7a9829) and their field
//     tables 0xc82a30..0xc83e48 are reproduced below with RW offsets in the comments.
//   * no FX field uses a duration / velocity / angle parser: Lifetime, SystemLifetime, BurstDelay, ... are stored
//     as written; the simulator decides their unit.
//   * nothing is resolved at parse time (ParticleName, SlaveSystem, PerParticleAttachedSystem, EventFX are
//     strings; the slave lookup is lazy by exact name).
// DONOR FACTS (BFME1/ZH): the flat ZH ParticleSystemInfo field set (Priority, Shader, Type, Lifetime, Size, ...).

#pragma once

#include "Common/GameClientRandomVariable.h"
#include "Common/INI.h"
#include "Common/INIDataTypes.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace FXParticleSystem
{

// RW category table 0xc339d8 (token column).
enum ModuleCategory
{
	MODULE_CATEGORY_COLOR = 0,
	MODULE_CATEGORY_ALPHA,
	MODULE_CATEGORY_UPDATE,
	MODULE_CATEGORY_PHYSICS,
	MODULE_CATEGORY_EMISSION_VELOCITY,
	MODULE_CATEGORY_EMISSION_VOLUME,
	MODULE_CATEGORY_DRAW,
	MODULE_CATEGORY_WIND,
	MODULE_CATEGORY_EVENT,
	MODULE_CATEGORY_COUNT
};
extern const char *const ModuleCategoryKeys[]; ///< "Color" ... "Event"

// The 27 module classes of the RW registry, in registration order per category.
enum ModuleClassId
{
	MODULE_DEFAULT_COLOR,
	MODULE_DEFAULT_ALPHA,
	MODULE_DEFAULT_UPDATE,
	MODULE_RENDER_OBJECT_UPDATE,
	MODULE_DEFAULT_PHYSICS,
	MODULE_ORTHO_EMISSION_VELOCITY,
	MODULE_SPHERICAL_EMISSION_VELOCITY,
	MODULE_HEMISPHERICAL_EMISSION_VELOCITY,
	MODULE_CYLINDRICAL_EMISSION_VELOCITY,
	MODULE_OUTWARD_EMISSION_VELOCITY,
	MODULE_POINT_EMISSION_VOLUME,
	MODULE_LINE_EMISSION_VOLUME,
	MODULE_BOX_EMISSION_VOLUME,
	MODULE_SPHERE_EMISSION_VOLUME,
	MODULE_CYLINDER_EMISSION_VOLUME,
	MODULE_LIGHTNING_EMISSION,
	MODULE_TERRAIN_FIRE_EMISSION,
	MODULE_DEFAULT_DRAW,
	MODULE_STREAK_DRAW,
	MODULE_QUAD_DRAW,
	MODULE_BUTTERFLY_DRAW,
	MODULE_RENDER_OBJECT_DRAW,
	MODULE_LIGHTNING_DRAW,
	MODULE_GPU_DRAW,
	MODULE_DEFAULT_WIND,
	MODULE_LIFE_EVENT,
	MODULE_TERRAIN_COLLISION,
	MODULE_CLASS_COUNT
};

// ---- index lists (RW userData tables; scanIndexList is case insensitive) ----------------------------------------
extern const char *const ParticlePriorityNames[];  ///< RW 0xc31ae8
extern const char *const ParticleShaderNames[];    ///< RW 0xc31a54
extern const char *const ParticleTypeNames[];      ///< RW 0xc31a84
extern const char *const ParticleRotationNames[];  ///< RW 0xc31b1c
extern const char *const WindMotionNames[];        ///< RW 0xc31b08

enum ParticlePriority
{
	PARTICLE_PRIORITY_NONE = 0,
	PARTICLE_PRIORITY_ULTRA_HIGH_ONLY,
	PARTICLE_PRIORITY_HIGH_OR_ABOVE,
	PARTICLE_PRIORITY_MEDIUM_OR_ABOVE,
	PARTICLE_PRIORITY_LOW_OR_ABOVE,
	PARTICLE_PRIORITY_VERY_LOW_OR_ABOVE,
	PARTICLE_PRIORITY_ALWAYS_RENDER
};

enum ParticleShader
{
	PARTICLE_SHADER_NONE = 0,
	PARTICLE_SHADER_ADDITIVE,
	PARTICLE_SHADER_ADDITIVE_ALPHA_TEST,
	PARTICLE_SHADER_ALPHA,
	PARTICLE_SHADER_ALPHA_TEST,
	PARTICLE_SHADER_MULTIPLY,
	PARTICLE_SHADER_ADDITIVE_NO_DEPTH_TEST,
	PARTICLE_SHADER_ALPHA_NO_DEPTH_TEST,
	PARTICLE_SHADER_W3D_DIFFUSE,
	PARTICLE_SHADER_W3D_ALPHA,
	PARTICLE_SHADER_W3D_EMISSIVE
};

enum ParticleType
{
	PARTICLE_TYPE_NONE = 0,
	PARTICLE_TYPE_PARTICLE,
	PARTICLE_TYPE_DRAWABLE,
	PARTICLE_TYPE_STREAK,
	PARTICLE_TYPE_VOLUME_PARTICLE,
	PARTICLE_TYPE_SMUDGE,
	PARTICLE_TYPE_TERRAIN_PARTICLE,
	PARTICLE_TYPE_GPU_PARTICLE,
	PARTICLE_TYPE_GPU_TERRAINFIRE
};

enum ParticleRotation
{
	PARTICLE_ROTATION_NONE = 0,
	PARTICLE_ROTATION_OFF,
	PARTICLE_ROTATION_X,
	PARTICLE_ROTATION_Y,
	PARTICLE_ROTATION_Z,
	PARTICLE_ROTATION_V
};

enum WindMotion
{
	WIND_MOTION_NONE = 0,
	WIND_MOTION_NOT_USED, ///< RW name "Unused"
	WIND_MOTION_PING_PONG,
	WIND_MOTION_CIRCULAR
};

// ---- keyframes ----------------------------------------------------------------------------------------------------
enum
{
	MAX_KEYFRAMES = 8
};

// RW 0x969878 (parseAlphaKey): three tokens, real real unsigned. Stored as a UNIFORM random variable plus the frame.
struct RandomAlphaKeyframe
{
	GameClientRandomVariable var{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	unsigned frame = 0;
};

// RW 0x969f88 (parseColorKey): R: G: B: components 0..255 scaled by 1/255, then an unsigned frame.
struct RGBColorKeyframe
{
	RGBColor color{ 0.0f, 0.0f, 0.0f };
	unsigned frame = 0;
};

// ---- module data --------------------------------------------------------------------------------------------------
// Every module is a plain struct whose first member says what it is. `End`-terminated body; the fields are the rows
// of the class's RW table (offset in the comment is the RW object offset).
struct ModuleData
{
	ModuleClassId classId;
	explicit ModuleData(ModuleClassId id) : classId(id) {}
	virtual ~ModuleData() = default;
};

struct DefaultColorModuleData : ModuleData // RW 0x98 bytes, table 0xc83b40
{
	DefaultColorModuleData() : ModuleData(MODULE_DEFAULT_COLOR) {}
	RGBColorKeyframe m_colorKey[MAX_KEYFRAMES];
	GameClientRandomVariable m_colorScale; // CONSTANT 0 0
};

struct DefaultAlphaModuleData : ModuleData // 0x8c bytes, table 0xc83a70
{
	DefaultAlphaModuleData() : ModuleData(MODULE_DEFAULT_ALPHA) {}
	RandomAlphaKeyframe m_alphaKey[MAX_KEYFRAMES];
};

struct DefaultUpdateModuleData : ModuleData // 0x70 bytes, table 0xc83cb0
{
	DefaultUpdateModuleData() : ModuleData(MODULE_DEFAULT_UPDATE) {}
	GameClientRandomVariable m_sizeRate{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_sizeRateDamping{ GameClientRandomVariable::UNIFORM, 1.0f, 1.0f };
	GameClientRandomVariable m_angleZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_angularRateZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_angularDamping{ GameClientRandomVariable::UNIFORM, 1.0f, 1.0f };
	int m_rotation = PARTICLE_ROTATION_OFF;
	GameClientRandomVariable m_angleXY{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_angularRateXY{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_angularDampingXY{ GameClientRandomVariable::UNIFORM, 1.0f, 1.0f };
};

struct RenderObjectUpdateModuleData : ModuleData // 0xa0 bytes, table 0xc82e40
{
	RenderObjectUpdateModuleData() : ModuleData(MODULE_RENDER_OBJECT_UPDATE) {}
	GameClientRandomVariable m_startSizeX{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_startSizeY{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_startSizeZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_sizeRateX{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_sizeRateY{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_sizeRateZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_sizeDampingX{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f }; // defaults 0, not 1 (RW 0x965e20)
	GameClientRandomVariable m_sizeDampingY{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_sizeDampingZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_angleZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_angularRateZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_angularDamping{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	int m_rotation = PARTICLE_ROTATION_OFF;
};

struct DefaultPhysicsModuleData : ModuleData // 0x2c bytes, table 0xc83c20
{
	DefaultPhysicsModuleData() : ModuleData(MODULE_DEFAULT_PHYSICS) {}
	Coord3D m_driftVelocity;
	float m_gravity = 0.0f;
	GameClientRandomVariable m_velocityDamping{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	bool m_swirly = false;
	bool m_particlesAttachToBone = false;
};

struct OrthoEmissionVelocityModuleData : ModuleData // 0x30 bytes, table 0xc831d0
{
	OrthoEmissionVelocityModuleData() : ModuleData(MODULE_ORTHO_EMISSION_VELOCITY) {}
	GameClientRandomVariable m_x{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_y{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_z{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
};

// Spherical and Hemispherical share the RW table 0xc83250 and the data; the class id tells them apart.
struct SphericalEmissionVelocityModuleData : ModuleData
{
	explicit SphericalEmissionVelocityModuleData(ModuleClassId id = MODULE_SPHERICAL_EMISSION_VELOCITY) : ModuleData(id) {}
	GameClientRandomVariable m_speed{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
};

struct CylindricalEmissionVelocityModuleData : ModuleData // table 0xc832b4
{
	CylindricalEmissionVelocityModuleData() : ModuleData(MODULE_CYLINDRICAL_EMISSION_VELOCITY) {}
	GameClientRandomVariable m_radial{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_normal{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
};

struct OutwardEmissionVelocityModuleData : ModuleData // table 0xc833b4
{
	OutwardEmissionVelocityModuleData() : ModuleData(MODULE_OUTWARD_EMISSION_VELOCITY) {}
	GameClientRandomVariable m_speed{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_otherSpeed{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
};

struct PointEmissionVolumeModuleData : ModuleData // table 0xc83424
{
	PointEmissionVolumeModuleData() : ModuleData(MODULE_POINT_EMISSION_VOLUME) {}
	bool m_isHollow = false;
};

struct LineEmissionVolumeModuleData : ModuleData // table 0xc83490
{
	LineEmissionVolumeModuleData() : ModuleData(MODULE_LINE_EMISSION_VOLUME) {}
	bool m_isHollow = false;
	Coord3D m_startPoint;
	Coord3D m_endPoint;
};

struct BoxEmissionVolumeModuleData : ModuleData // table 0xc83588
{
	BoxEmissionVolumeModuleData() : ModuleData(MODULE_BOX_EMISSION_VOLUME) {}
	bool m_isHollow = false;
	Coord3D m_halfSize;
};

struct SphereEmissionVolumeModuleData : ModuleData // table 0xc8366c
{
	SphereEmissionVolumeModuleData() : ModuleData(MODULE_SPHERE_EMISSION_VOLUME) {}
	bool m_isHollow = false;
	float m_radius = 0.0f;
};

struct CylinderEmissionVolumeModuleData : ModuleData // table 0xc83768
{
	CylinderEmissionVolumeModuleData() : ModuleData(MODULE_CYLINDER_EMISSION_VOLUME) {}
	bool m_isHollow = false;
	float m_radius = 0.0f;
	float m_radiusRate = 0.0f;
	float m_length = 0.0f;
	Coord3D m_offset;
};

struct LightningEmissionModuleData : ModuleData // table 0xc838c0 (no IsHollow)
{
	Coord3D m_startPoint;
	Coord3D m_endPoint;
	GameClientRandomVariable m_amplitude[3];
	GameClientRandomVariable m_frequency[3];
	GameClientRandomVariable m_phase[3];
	LightningEmissionModuleData()
	    : ModuleData(MODULE_LIGHTNING_EMISSION)
	{
		for (int i = 0; i < 3; ++i)
		{
			m_amplitude[i] = m_frequency[i] = m_phase[i] = GameClientRandomVariable(GameClientRandomVariable::UNIFORM, 0.0f, 0.0f);
		}
	}
};

struct TerrainFireEmissionModuleData : ModuleData // table 0xc839e0
{
	TerrainFireEmissionModuleData() : ModuleData(MODULE_TERRAIN_FIRE_EMISSION) {}
	GameClientRandomVariable m_xOffset{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_yOffset{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_zOffset{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	float m_cellEmissionChance = 0.7f; // RW 0xbde0a8
};

// DefaultDraw / StreakDraw / QuadDraw / ButterflyDraw: no fields (table 0xc84858 is empty).
struct EmptyDrawModuleData : ModuleData
{
	explicit EmptyDrawModuleData(ModuleClassId id) : ModuleData(id) {}
};

struct RenderObjectDrawModuleData : ModuleData // 0x48 bytes, table 0xc82bd0
{
	RenderObjectDrawModuleData() : ModuleData(MODULE_RENDER_OBJECT_DRAW) {}
	bool m_sinkOnTerrainCollision = false;
	float m_sinkRate = 0.0f;
	bool m_multiRenderObjects = false;
	std::string m_renderGroup[3];
	int m_numObjects[3] = { 0, 0, 0 };
	float m_percent[3] = { 0.0f, 0.0f, 0.0f };
	int m_shader[3] = { PARTICLE_SHADER_W3D_DIFFUSE, PARTICLE_SHADER_W3D_DIFFUSE, PARTICLE_SHADER_W3D_DIFFUSE };
};

struct LightningDrawModuleData : ModuleData // 0x38 bytes, table 0xc82a30
{
	LightningDrawModuleData() : ModuleData(MODULE_LIGHTNING_DRAW) {}
	GameClientRandomVariable m_offsetX{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_offsetY{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	GameClientRandomVariable m_offsetZ{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	float m_multiChance = 0.0f;
	bool m_tileTexture = false;
};

struct GpuDrawModuleData : ModuleData // 0x1c bytes, table 0xc82d10
{
	GpuDrawModuleData() : ModuleData(MODULE_GPU_DRAW) {}
	int m_framesPerRow = 1;
	int m_totalFrames = 1;
	std::string m_detailTexture;
	float m_speedMultiplier = 1.0f;
};

struct DefaultWindModuleData : ModuleData // 0x50 bytes, table 0xc83058
{
	DefaultWindModuleData() : ModuleData(MODULE_DEFAULT_WIND) {}
	int m_windMotion = WIND_MOTION_NOT_USED;
	float m_windStrength = 2.0f;
	float m_windFullStrengthDist = 75.0f;
	float m_windZeroStrengthDist = 200.0f;
	float m_windAngleChangeMin = 0.15f;
	float m_windAngleChangeMax = 0.45f;
	float m_windPingPongStartAngleMin = 0.0f;
	float m_windPingPongStartAngleMax = 0.785398f;   // pi / 4 (RW constant)
	float m_windPingPongEndAngleMin = 5.49779f;      // 7 pi / 4
	float m_windPingPongEndAngleMax = 6.28319f;      // 2 pi
	float m_turbulenceAmplitude = 0.0f;
	float m_turbulenceFrequency = 0.0f;
};

// Event base fields (RW 0x7a9039): PerParticle and KillAfterEvent default to TRUE.
struct EventModuleData : ModuleData
{
	explicit EventModuleData(ModuleClassId id) : ModuleData(id) {}
	bool m_perParticle = true;
	bool m_killAfterEvent = true;
	std::string m_eventFX;                 ///< FXList name, resolved when the event fires
};

struct LifeEventModuleData : EventModuleData // table 0xc83da0
{
	LifeEventModuleData() : EventModuleData(MODULE_LIFE_EVENT) {}
	GameClientRandomVariable m_eventTime{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
};

struct TerrainCollisionModuleData : EventModuleData // table 0xc83e48
{
	TerrainCollisionModuleData() : EventModuleData(MODULE_TERRAIN_COLLISION) {}
	GameClientRandomVariable m_heightOffset{ GameClientRandomVariable::UNIFORM, 0.0f, 0.0f };
	bool m_orientFXToTerrain = false;
};

// ---- one registered module class ---------------------------------------------------------------------------------
struct ModuleClassInfo
{
	ModuleClassId id;
	ModuleCategory category;
	const char *token;       ///< the INI class name (RW node +4)
	const char *displayName; ///< RW node +8
	bool isDefault;          ///< RW registration `isDefault` argument (unused by the INI path)
	// creates the module with RW constructor defaults and parses its `End`-terminated body
	std::shared_ptr<ModuleData> (*parse)(INI *ini);
	std::shared_ptr<ModuleData> (*createDefault)();
};

// All 27 classes, in registration order. Mod compatibility: this is the binary's full registry (rule 6).
const std::vector<ModuleClassInfo> &ModuleClassRegistry();
const ModuleClassInfo *FindModuleClass(ModuleCategory category, const std::string &token);

// ---- the system part (RW SystemInfo, `System` sub-block) ---------------------------------------------------------
struct ParticleSystemInfo
{
	// RW 0x5f44f4 base constructor defaults
	bool m_isOneShot = false;                                             // +0x04
	int m_shaderType = PARTICLE_SHADER_ADDITIVE;                          // +0x08
	int m_particleType = PARTICLE_TYPE_PARTICLE;                          // +0x0c
	std::string m_particleTypeName;                                       // +0x10 ParticleName
	GameClientRandomVariable m_lifetime;                                  // +0x14
	unsigned m_systemLifetime = 0;                                        // +0x20
	unsigned m_sortLevel = 0;                                             // +0x24
	GameClientRandomVariable m_size;                                      // +0x28
	GameClientRandomVariable m_startSizeRate;                             // +0x34
	GameClientRandomVariable m_burstDelay;                                // +0x44
	GameClientRandomVariable m_burstCount;                                // +0x50
	GameClientRandomVariable m_initialDelay;                              // +0x5c
	std::string m_slaveSystemName;                                        // +0x68
	Coord3D m_slavePosOffset;                                             // +0x6c
	std::string m_attachedSystemName;                                     // +0x78 PerParticleAttachedSystem
	int m_priority = PARTICLE_PRIORITY_ULTRA_HIGH_ONLY;                   // +0x7c
	bool m_isGroundAligned = false;                                       // +0x80
	bool m_isEmitAboveGroundOnly = false;                                 // +0x81
	bool m_isParticleUpTowardsEmitter = false;                            // +0x82
	bool m_useMaximumHeight = false;                                      // +0x83
	bool m_shroudEmitter = false;                                         // +0x84
};

// One FXParticleSystem template (RW object 0xd4 bytes, vtable name "FXParticleSystemInfo").
class ParticleSystemTemplate
{
public:
	explicit ParticleSystemTemplate(const std::string &name);

	const std::string &getName() const { return m_name; }
	const ParticleSystemInfo &info() const { return m_info; }
	ParticleSystemInfo &info() { return m_info; }

	// A module of a single-slot category, or nullptr when the INI block had none (RW leaves the slot NULL).
	const ModuleData *module(ModuleCategory category) const { return m_modules[category].get(); }
	// Event modules in INI order (RW +0xc4 vector).
	const std::vector<std::shared_ptr<ModuleData>> &events() const { return m_events; }

	// Replaces (or, for Event, appends) a module: RW 0x5f3b0a / 0x4822ad.
	void setModule(ModuleCategory category, std::shared_ptr<ModuleData> module);

	// RW 0x5fc7db body after the name: initFromINI(template, 0xde3a58).
	void parseBody(INI *ini);

	// FieldParse tables (RW 0xde3a58 rows plus the `System` row, and the 21-row System table).
	static const FieldParse *templateFieldParse();
	static const FieldParse *systemFieldParse();

private:
	std::string m_name;
	ParticleSystemInfo m_info;
	std::array<std::shared_ptr<ModuleData>, MODULE_CATEGORY_COUNT> m_modules; // Event slot unused: m_events
	std::vector<std::shared_ptr<ModuleData>> m_events;
};

// ZH ParticleSystemManager's template half (RW TheFXParticleSystemManager 0xde3744: findTemplate 0x5f889b,
// newTemplate 0x5fb981, hash map at +0x88). The simulation half lives in GameClient/ParticleSys.h.
class FXParticleSystemTemplateStore
{
public:
	// Case-sensitive exact name (RW hash 0x42b6c1 + memcmp).
	const ParticleSystemTemplate *findTemplate(const std::string &name) const;
	ParticleSystemTemplate *findTemplate(const std::string &name);
	size_t templateCount() const { return m_templates.size(); }
	// Names in definition order (retail order of the INI text).
	const std::vector<std::string> &templateNames() const { return m_order; }
	bool overrideLoaded() const { return m_overrideLoaded; }

	// RW 0x5fc7db: the `FXParticleSystem <Name>` block. Existing template: destroyed and rebuilt in place for every load type.
	void parseTemplateDefinition(INI *ini);

	void clear();

private:
	std::map<std::string, std::unique_ptr<ParticleSystemTemplate>> m_templates;
	std::vector<std::string> m_order;
	bool m_overrideLoaded = false; ///< RW manager byte +0x84, set by load type 2
};

// RW 0xde3744. A null manager makes the block fail with code 3 (the retail wiring cannot reach it: the manager is
// created by the subsystem init before FXParticleSystem.ini loads).
extern FXParticleSystemTemplateStore *TheFXParticleSystemTemplateStore;
void ParseFXParticleSystemDefinitionGlobal(INI *ini);

} // namespace FXParticleSystem
