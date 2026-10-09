// OpenBFME. GPL-3.0.
//
// FXList: the `FXList <Name> ... End` INI block of RotWK (TheFXListStore) with every nugget type the binary registers,
// and the dispatcher (FXList::doFXPos / doFXObj) that plays them. Port of ZH GameEngine/Source/GameClient/FXList.cpp
// as RotWK changed it. Lane FX-1.
//
// TARGET FACTS (RotWK game.dat, stop S-001 caveat; derivation in the lane notes fx-fxlist.md, spec fx-and-particles.md):
//   * block parser RW 0x5e2518: name = getNextToken, key = NameKeyGenerator hash (case sensitive); loadType 5 (RELOAD)
//     alone erases an existing entry and flags it superseded (the old object is redirected by name at play time);
//     every other load type replaces the map entry with a new FXList and leaks the old one, which keeps playing for
//     anyone holding its pointer. There is no merge mode. The field table 0xbf2898 has 18 rows: 16 nugget keywords (one
//     of them single-line), `CullingInfo` and `PlayEvenIfShrouded`.
//   * no `Tracer` nugget (ZH has one; RW rejects it as an unknown field, code 5).
//   * `INI::parseFXList` (RW 0x73a302) resolves the name at parse time: "None" (any case) gives NULL, an unknown name is
//     a code-3 error `iniParseFXList -- FXList %s not found! ...`.
//   * Sound.Name (RW 0x73b217) and EvaEvent names (RW 0x5de588) are validated against the audio and Eva registries at
//     parse time in RW; those registries belong to other lanes: the validation is injected (FXListParseServices) and,
//     when absent, reported through FXListStore::unverified() (stop S-190).
//   * doFXPos / doFXObj run the nuggets in INI order; only the Weather filter applies on the position path; the cull
//     test (FXList::shouldCull) lives in the static wrappers; every random draw uses the CLIENT stream.
//
// The engine services a nugget calls (audio, camera, lights, terrain, particles, drawables) are the FXServices
// interface; objects reach the dispatcher through FXObject. Neither is implemented here: this lane does not create
// Object classes (LOGIC-1 owns them).

#pragma once

#include "Common/GameClientRandomVariable.h"
#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/ModelState.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

class W3DDrawRandom;
struct ObjectFilter; // GameLogic/ObjectFilter.h cannot share a translation unit with Common/ModelState.h (two ModelConditionFlags typedefs)

class FXList;
class FXServices;

// ---- what a nugget needs to know about an object ------------------------------------------------------------------
// Implemented by the object lane. RW layout facts the nuggets rely on: position +0x38, matrix +0x08, model condition
// flags +0x10c, drawable +0x84.
class FXObject
{
public:
	virtual ~FXObject() = default;
	virtual std::uint32_t objectId() const = 0;
	virtual Coord3D position() const = 0;
	virtual Matrix3D transform() const = 0;
	virtual ModelConditionFlags modelConditions() const = 0;
	// ObjectFilter::passes(obj) (RW 0x7640c1): the filter is evaluated by the object lane (relationship, side, KindOf, template)
	virtual bool passesObjectFilter(const ObjectFilter &filter) const = 0;
	virtual bool hasDrawable() const = 0;
	virtual int drawableState() const { return 0; }    ///< RW drawable field +0x164 (5 blocks a nugget; meaning unresolved)
	virtual float boundingCircleRadius() const = 0;
	virtual int controllingPlayerIndex() const = 0;    ///< RW player +0x54, -1 when none
	virtual bool isControlledByLocalPlayer() const = 0;
	virtual int relationshipToLocalPlayer() const = 0; ///< RW relationship enum: 0 enemy, 1 neutral, 2 allies
	virtual bool hasKindOf(const char *kindOfName) const = 0; ///< BuffNugget template selection
	virtual bool isHorde() const = 0;                  ///< KindOf HORDE (bit 109)
};

enum FXShroudStatus
{
	FX_SHROUD_CLEAR = 0,
	FX_SHROUD_FOGGED = 1,
	FX_SHROUD_SHROUDED = 2
};

// A particle system the FXList just created (the particle simulator implements it).
class FXParticleSystemRef
{
public:
	virtual ~FXParticleSystemRef() = default;
	virtual void setLocalTransform(const Matrix3D &m) = 0;            ///< RW 0x5f2f55
	virtual void rotateLocalX(float radians) = 0;                     ///< RW 0x5f2fc2
	virtual void rotateLocalY(float radians) = 0;                     ///< RW 0x5f30b9
	virtual void rotateLocalZ(float radians) = 0;                     ///< RW 0x5f31b0
	virtual void setPosition(const Coord3D &p) = 0;                   ///< RW 0x5f2f2d
	virtual void attachToObject(std::uint32_t objectId) = 0;          ///< RW 0x5f32d7
	virtual void attachToBone(const std::string &boneName) = 0;       ///< RW 0x5f32f4
	virtual void setTarget(const Coord3D &p) = 0;                     ///< RW system +0x188 / flag +0x194
	virtual void setSystemLife(int frames) = 0;                       ///< RW system +0x128
	virtual void setInitialDelayFrames(int frames) = 0;               ///< RW system +0x120
	virtual void destroy() = 0;                                       ///< RW 0x5f3c65
};

struct FXBoneTransform
{
	Coord3D position;
	Matrix3D transform;
};

struct FXDecalDesc
{
	std::string name;
	int shader = 0; ///< 0 ALPHA, 1 ADDITIVE, 2 SUBTRACT
	float size = 0.0f;
	Coord3D position;
	float yaw = 0.0f;
	std::uint32_t color = 0xFFFFFF; ///< 0xRRGGBB
	std::uint32_t opacityStart = 0, opacityPeak = 0, opacityEnd = 0;
	int startDelayFrames = 0, lifetimeFrames = 0, fadeOneFrames = 0, peakFrames = 0, fadeTwoFrames = 0; ///< client frames: trunc(ms * 0.03)
};

// The engine calls the nuggets make. Every call here names the RW call site it replaces (notes fx-fxlist.md 7, 9).
class FXServices
{
public:
	virtual ~FXServices() = default;
	// the client random stream (never the logic stream)
	virtual W3DDrawRandom &clientRandom() = 0;
	// the current logic frame (FXList culling window, RW TheGameLogic +0x40)
	virtual std::uint32_t logicFrame() const = 0;
	// shroud
	virtual int shroudStatusAt(const Coord3D &pos) = 0;            ///< RW 0xb4d9a0 for the local player
	virtual bool objectShroudedForLocalPlayer(const FXObject &obj) = 0; ///< RW 0x68d8f7 > 2, or the position status != clear when the object has no partition data (RW 0x5e2282)
	// audio / eva
	virtual void playSound(const std::string &eventName, const Coord3D *pos, int playerIndex) = 0; ///< RW 0x5df7f3 / 0x5df85e; playerIndex -1 when not an object play
	virtual void playEva(const std::string &evaEventName) = 0;     ///< RW 0x5dd9ee
	// camera / lights / terrain
	virtual void viewShake(const Coord3D &pos, int shakeType) = 0; ///< RW TheView vtbl+0x1ac
	virtual void cameraShaker(const Coord3D &pos, float radius, float durationSeconds, float amplitudeDegrees) = 0; ///< vtbl+0xa8
	virtual void lightPulse(const Coord3D &pos, const RGBColor &color, float radius, std::uint32_t increaseFrames, std::uint32_t decreaseFrames) = 0; ///< RW 0x5dfaa3
	virtual void addScorch(const Coord3D &pos, float radius, int type) = 0; ///< RW 0x5e0045
	virtual float groundHeight(float x, float y, const Coord3D &layerRef) = 0; ///< RW TerrainLogic vtbl+0x18
	virtual bool isWater(float x, float y) = 0;                    ///< RW TerrainLogic vtbl+0x4c (plus the unresolved refinements, notes 13.3)
	// particles
	virtual bool particleSystemTemplateExists(const std::string &name) = 0;
	virtual std::unique_ptr<FXParticleSystemRef> createParticleSystem(const std::string &templateName) = 0; ///< RW 0x5f526a; nullptr when refused (cap/LOD)
	// drawables
	virtual bool boneWorldMatrix(const FXObject &obj, const std::string &boneName, Matrix3D &out) = 0; ///< RW 0x672b5b
	virtual std::vector<FXBoneTransform> boneWorldTransforms(const FXObject &obj, const std::string &boneName, int startIndex, int maxBones) = 0; ///< RW 0x672aee
	virtual void tintDrawable(const FXObject &obj, const RGBColor &color, unsigned preMs, unsigned postMs, unsigned sustainMs, float frequency, float amplitude) = 0;
	virtual void attachModel(const FXObject &obj, const std::string &model, bool randomRotate, int expireFrames) = 0;
	virtual void addBuff(const FXObject &obj, int buffType, const std::string *thingTemplate, int lifeFrames, const RGBColor &color, float extrusion) = 0;
	virtual void removeBuff(const FXObject &obj, int buffType) = 0;
	// misc
	virtual void createDecal(const FXDecalDesc &decal) = 0;
	virtual void createRayEffect(const Coord3D &a, const Coord3D &b, const std::string &thingTemplate) = 0;
	virtual void cursorParticles(const std::string &anim2D, unsigned burstCount, const GameClientRandomVariable &life, const GameClientRandomVariable &systemLife,
		const GameClientRandomVariable &driftX, const GameClientRandomVariable &driftY) = 0;
	virtual void laser(const std::string &laserTemplate, const Coord3D *a, const Coord3D *b, bool backwards, const FXObject *from, const FXObject *to) = 0;
	virtual int weather() const = 0; ///< RW TheWritableGlobalData +0x138 (0 NORMAL, 1 SNOWY)
};

// ---- parse-time services ----------------------------------------------------------------------------------------
// RW validates Sound and EvaEvent names while parsing; those registries are other lanes. Null members: not validated, and
// the unverified list says so.
class FXListParseServices
{
public:
	virtual ~FXListParseServices() = default;
	virtual bool audioEventExists(const std::string &name) = 0; ///< TheAudio->findAudioEventInfo
	virtual bool evaEventExists(const std::string &name) = 0;   ///< RW 0x5de0d8
};

// ---- nuggets ------------------------------------------------------------------------------------------------------
// Type tag = RW nugget +4. Types RW leaves at 0 (AttachedModel, Laser, TintDrawable, BuffNugget) get distinct values >= 100
// here; the original tags are only used by the ParticleSysBone check (9).
enum FXNuggetType
{
	FX_NUGGET_SOUND = 1,
	FX_NUGGET_RAY_EFFECT = 2,
	FX_NUGGET_LIGHT_PULSE = 4,
	FX_NUGGET_CAMERA_SHAKER_VOLUME = 5,
	FX_NUGGET_VIEW_SHAKE = 6,
	FX_NUGGET_TERRAIN_SCORCH = 7,
	FX_NUGGET_PARTICLE_SYSTEM = 8,
	FX_NUGGET_PARTICLE_SYS_BONE = 9,
	FX_NUGGET_FXLIST_AT_BONE_POS = 10,
	FX_NUGGET_CURSOR_PARTICLE_SYSTEM = 12,
	FX_NUGGET_DYNAMIC_DECAL = 13,
	FX_NUGGET_EVA_EVENT = 15,
	FX_NUGGET_ATTACHED_MODEL = 100,
	FX_NUGGET_LASER,
	FX_NUGGET_TINT_DRAWABLE,
	FX_NUGGET_BUFF
};

class FXNugget
{
public:
	explicit FXNugget(FXNuggetType t) : m_type(t) {}
	virtual ~FXNugget() = default;

	FXNuggetType m_type;
	std::shared_ptr<ObjectFilter> m_sourceObjectFilter; ///< RW +0x08 (applies to the primary object); null = unset (RW -1: accept all)
	std::shared_ptr<ObjectFilter> m_objectFilter;       ///< RW +0x0c (applies to the secondary object)
	ModelConditionFlags m_requiredSourceModelConditions; ///< RW +0x10
	ModelConditionFlags m_excludedSourceModelConditions; ///< RW +0x5c
	ModelConditionFlags m_requiredSecondaryModelConditions; ///< RW +0xa8
	ModelConditionFlags m_excludedSecondaryModelConditions; ///< RW +0xf4
	int m_weather = 2;                          ///< RW +0x140: 0 NORMAL, 1 SNOWY, 2 any
	bool m_stopIfNuggetPlayed = false;          ///< RW +0x144

	// RW slot 4 (0x5df671)
	bool passesFilters(const FXObject *primary, const FXObject *secondary, const FXServices &services) const;
	// RW slot 1: the position play (no-op for object-only nuggets)
	virtual void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const;
	// RW slot 2: default = doFXPos with the object's position and matrix
	virtual void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const;

	// the nugget's own field table (the base table follows it)
	virtual const FieldParse *fieldParse() const = 0;
};

struct SoundFXNugget : FXNugget // RW 0x14c
{
	SoundFXNugget() : FXNugget(FX_NUGGET_SOUND) {}
	std::string m_soundName; ///< empty for "NoSound" or an absent Name
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct EvaEventFXNugget : FXNugget // RW 0x154
{
	EvaEventFXNugget() : FXNugget(FX_NUGGET_EVA_EVENT) {}
	std::string m_owner, m_ally, m_enemy; ///< "" = none (-1)
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &, const Coord3D *, const Matrix3D *, float, const Coord3D *) const override {}
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct RayEffectFXNugget : FXNugget // RW 0x164
{
	RayEffectFXNugget() : FXNugget(FX_NUGGET_RAY_EFFECT) {}
	std::string m_templateName;
	Coord3D m_primaryOffset, m_secondaryOffset;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
};

struct LightPulseFXNugget : FXNugget // RW 0x164
{
	LightPulseFXNugget() : FXNugget(FX_NUGGET_LIGHT_PULSE) {}
	RGBColor m_color{ 0.0f, 0.0f, 0.0f };
	float m_radius = 0.0f;
	float m_boundingCirclePct = 0.0f; ///< RadiusAsPercentOfObjectSize (percent parsed to a fraction)
	unsigned m_increaseFrames = 0;    ///< IncreaseTime (ms -> logic frames, RW 0x73a429)
	unsigned m_decreaseFrames = 0;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct CameraShakerVolumeFXNugget : FXNugget // RW 0x160
{
	CameraShakerVolumeFXNugget() : FXNugget(FX_NUGGET_CAMERA_SHAKER_VOLUME) {}
	float m_radius = 0.0f, m_durationSeconds = 0.0f, m_amplitudeDegrees = 0.0f;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
};

struct ViewShakeFXNugget : FXNugget // RW 0x14c
{
	ViewShakeFXNugget() : FXNugget(FX_NUGGET_VIEW_SHAKE) {}
	int m_type = 1; ///< SUBTLE 0, NORMAL 1, STRONG 2, SEVERE 3, CINE_EXTREME 4, CINE_INSANE 5
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
};

struct AttachedModelFXNugget : FXNugget // RW 0x154
{
	AttachedModelFXNugget() : FXNugget(FX_NUGGET_ATTACHED_MODEL) {}
	std::string m_modelName;
	bool m_randomlyRotate = false;
	int m_expireTimer = 40; ///< raw frames; default 8 * LOGICFRAMES_PER_SECOND
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &, const Coord3D *, const Matrix3D *, float, const Coord3D *) const override {}
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct TerrainScorchFXNugget : FXNugget // RW 0x158
{
	TerrainScorchFXNugget() : FXNugget(FX_NUGGET_TERRAIN_SCORCH) {}
	int m_scorchType = -1; ///< SCORCH_1..SCORCH_9 = 0..8, RANDOM = -1
	float m_radius = 0.0f;
	ICoord2D m_randomRange{ 0, 3 };
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
};

struct ParticleSystemFXNugget : FXNugget // RW 0x1bc
{
	ParticleSystemFXNugget() : FXNugget(FX_NUGGET_PARTICLE_SYSTEM) {}
	std::string m_name;
	int m_count = 1;
	Coord3D m_offset;
	GameClientRandomVariable m_radius;                                        ///< CONSTANT 0 0
	GameClientRandomVariable m_height;                                        ///< CONSTANT 0 0
	GameClientRandomVariable m_initialDelay{ GameClientRandomVariable::CONSTANT, -1.0f, -1.0f }; ///< ms; < 0 = none
	float m_rotateX = 0.0f, m_rotateY = 0.0f, m_rotateZ = 0.0f;               ///< radians
	bool m_orientToObject = false;
	bool m_attachToObject = false;
	std::string m_attachToBone;
	bool m_createAtGroundHeight = false;
	bool m_ricochet = false;
	std::string m_createBoneOverride;
	std::string m_targetBoneOverride;
	bool m_createBoneAtTarget = false;
	float m_targetCoeff = 1.0f; ///< parsed and never read (RW 0x5e1270)
	int m_systemLife = -1;
	bool m_useTargetOffset = false;
	bool m_setTargetMatrix = false;
	bool m_onlyIfOnLand = false;
	bool m_onlyIfOnWater = false;
	Coord3D m_targetOffset;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
	// RW 0x5e1270
	void spawn(FXServices &s, const Coord3D &pos, const Matrix3D *mtx, const FXObject *primary, const FXObject *secondary) const;
};

struct ParticleSysBoneFXNugget : FXNugget // RW 0x1a0; single-line parse; both play slots are no-ops
{
	ParticleSysBoneFXNugget() : FXNugget(FX_NUGGET_PARTICLE_SYS_BONE) {}
	std::string m_boneName; ///< lower-cased
	bool m_followBone = false;
	int m_fxTrigger = 0; ///< NONE 0, CATAPULT_ROCK 1, TREBUCHET_ROCK 2
	std::string m_particleSystemName; ///< at most 63 characters (RW strncpy 0x3f)
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &, const Coord3D *, const Matrix3D *, float, const Coord3D *) const override {}
	void doFXObj(FXServices &, const FXObject *, const FXObject *) const override {}
};

struct FXListAtBonePosFXNugget : FXNugget // RW 0x150
{
	FXListAtBonePosFXNugget() : FXNugget(FX_NUGGET_FXLIST_AT_BONE_POS) {}
	const FXList *m_fx = nullptr; ///< resolved at parse time (parseFXList)
	std::string m_boneName;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &, const Coord3D *, const Matrix3D *, float, const Coord3D *) const override {}
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct CursorParticleSystemFXNugget : FXNugget // RW 0x180
{
	CursorParticleSystemFXNugget() : FXNugget(FX_NUGGET_CURSOR_PARTICLE_SYSTEM) {}
	std::string m_anim2DTemplateName;
	unsigned m_burstCount = 10;
	GameClientRandomVariable m_particleLife, m_systemLife, m_driftVelX, m_driftVelY;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct DynamicDecalFXNugget : FXNugget // RW 0x18c
{
	DynamicDecalFXNugget() : FXNugget(FX_NUGGET_DYNAMIC_DECAL) {}
	std::string m_decalName;
	int m_shader = 0; ///< ALPHA 0, ADDITIVE 1, SUBTRACT 2
	float m_size = 0.0f;
	RGBColor m_color{ 1.0f, 1.0f, 1.0f };
	Coord2D m_offset;
	bool m_orientToObject = true;
	unsigned m_opacityStart = 0;
	float m_opacityFadeTimeOne = 0.0f;
	unsigned m_opacityPeak = 0;
	float m_opacityPeakTime = 0.0f;
	float m_opacityFadeTimeTwo = 0.0f;
	unsigned m_opacityEnd = 0;
	float m_startingDelay = 0.0f;
	float m_lifetime = 0.0f;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct LaserFXNugget : FXNugget // RW 0x15c
{
	LaserFXNugget() : FXNugget(FX_NUGGET_LASER) {}
	std::string m_laserName;
	bool m_laserBackwards = false;
	Coord3D m_targetPositionOffsetFallback; ///< uninitialised in RW's constructor; zero here
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &s, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const override;
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct TintDrawableFXNugget : FXNugget // RW 0x168
{
	TintDrawableFXNugget() : FXNugget(FX_NUGGET_TINT_DRAWABLE) {}
	RGBColor m_color{ 1.0f, 1.0f, 1.0f };
	unsigned m_preColorTime = 2000, m_postColorTime = 2000, m_sustainedColorTime = 1000; ///< raw ms, no conversion
	float m_frequency = 1.0f, m_amplitude = 1.0f;
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &, const Coord3D *, const Matrix3D *, float, const Coord3D *) const override {}
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

struct BuffFXNugget : FXNugget // RW 0x190
{
	BuffFXNugget() : FXNugget(FX_NUGGET_BUFF) {}
	int m_buffType = 0;
	bool m_isComplexBuff = false;
	unsigned m_buffLifeTime = 0; ///< ms -> logic frames
	std::string m_buffThingTemplate = "INVALID_THING", m_buffOrcTemplate = "INVALID_THING", m_buffInfantryTemplate = "INVALID_THING",
		m_buffCavalryTemplate = "INVALID_THING", m_buffTrollTemplate = "INVALID_THING", m_buffMumakilTemplate = "INVALID_THING",
		m_buffShipTemplate = "INVALID_THING", m_buffMonsterTemplate = "INVALID_THING";
	float m_extrusion = 1.0f;
	RGBColor m_color{ 0.2f, 0.4f, 1.0f };
	const FieldParse *fieldParse() const override;
	void doFXPos(FXServices &, const Coord3D *, const Matrix3D *, float, const Coord3D *) const override {}
	void doFXObj(FXServices &s, const FXObject *primary, const FXObject *secondary) const override;
};

// ---- the FXList ---------------------------------------------------------------------------------------------------
class FXListStore;

class FXList
{
public:
	explicit FXList(const std::string &name) : m_name(name) {}

	const std::string &name() const { return m_name; }
	const std::vector<std::unique_ptr<FXNugget>> &nuggets() const { return m_nuggets; }
	bool hasParticleSysBone() const { return m_hasParticleSysBone; }
	bool playEvenIfShrouded() const { return m_playEvenIfShrouded; }
	bool superseded() const { return m_superseded; }
	int cullWindowFrames() const { return m_cullWindowFrames; }
	int cullAllAbove() const { return m_cullAllAbove; }
	int startCullingAbove() const { return m_startCullingAbove; }

	void addNugget(std::unique_ptr<FXNugget> n) { m_nuggets.push_back(std::move(n)); }

	// RW 0x5e21d8: no cull test (the static wrappers do that)
	void doFXPos(FXServices &s, const FXListStore &store, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary) const;
	// RW 0x5e2282
	void doFXObj(FXServices &s, const FXListStore &store, const FXObject *primary, const FXObject *secondary) const;

	// RW 0x5e275b FXList::shouldCull. Mutates the frame-stamp list; draws from the client stream only when the window is over StartCullingAbove.
	bool shouldCull(FXServices &s) const;

	// RW 0x494615 / 0x4b1b5a: the static wrappers the rest of the engine calls (a null FXList is a legal no-op).
	static void doFXPos(const FXList *fx, FXServices &s, const FXListStore &store, const Coord3D *pos, const Matrix3D *mtx, float speed, const Coord3D *secondary);
	static void doFXObj(const FXList *fx, FXServices &s, const FXListStore &store, const FXObject *primary, const FXObject *secondary);

	static const FieldParse *fieldParse();

private:
	friend class FXListStore;
	friend struct FXListParseAccess;
	std::string m_name;
	std::vector<std::unique_ptr<FXNugget>> m_nuggets;
	bool m_hasParticleSysBone = false;      // RW +0x08
	bool m_playEvenIfShrouded = false;      // RW +0x10
	int m_cullWindowFrames = 0;             // RW +0x14 (TrackingSeconds * 5, truncated); 0 = culling off
	mutable std::list<std::uint32_t> m_cullStamps; // RW +0x18
	int m_cullAllAbove = 30;                // RW +0x1c
	int m_startCullingAbove = 60;           // RW +0x20
	bool m_superseded = false;              // RW +0x24
};

class FXListStore
{
public:
	// RW findFXList 0x5e20a2: "None" (any case) and an unknown name give nullptr; the lookup is case sensitive.
	const FXList *findFXList(const std::string &name) const;
	// RW INI::parseFXList 0x73a302: throws INIException(3) for an unknown name that is not "None".
	const FXList *parseFXListRef(const std::string &token) const;
	// INI field parser (store is a `const FXList *`)
	static void parseFXList(INI *ini, void *instance, void *store, const void *userData);

	size_t listCount() const { return m_lists.size(); }
	const std::vector<std::string> &listNames() const { return m_order; }
	size_t supersededCount() const { return m_superseded.size(); }

	// RW 0x5e2518
	void parseDefinition(INI *ini);

	void setParseServices(FXListParseServices *services) { m_parseServices = services; }
	FXListParseServices *parseServices() const { return m_parseServices; }

	// Names whose parse-time validation RW does (Sound, EvaEvent) that was NOT done because no registry was injected, as lines for the
	// stop report (S-190). Empty when validation services are present.
	std::vector<std::string> unverified() const;
	size_t unvalidatedSoundNames() const { return m_unvalidatedSounds; }
	size_t unvalidatedEvaNames() const { return m_unvalidatedEva; }
	void noteUnvalidatedSound() { ++m_unvalidatedSounds; }
	void noteUnvalidatedEva() { ++m_unvalidatedEva; }

	void clear();

private:
	friend class FXList;
	std::map<std::string, std::unique_ptr<FXList>> m_lists;
	std::vector<std::unique_ptr<FXList>> m_superseded; ///< RW store +0x20: kept alive
	std::vector<std::string> m_order;
	FXListParseServices *m_parseServices = nullptr;
	size_t m_unvalidatedSounds = 0, m_unvalidatedEva = 0;
};

extern thread_local FXListStore *TheFXListStore; ///< RW 0xde367c
void ParseFXListDefinitionGlobal(INI *ini);
