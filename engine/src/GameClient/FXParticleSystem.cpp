// OpenBFME. GPL-3.0. See FXParticleSystem.h for the target / donor facts.
//
// The field tables below are the RW tables transcribed in the lane notes (fx-ini-parse.md sections 3 and 7): the
// token strings, parse functions, index lists and object offsets come from game.dat; here an offset is the offset of the
// member inside the C++ module struct, and the row ORDER is RW's table order (which only matters for error text).

#include "GameClient/FXParticleSystem.h"

#include "Common/AsciiString.h"

#include <cstring>

namespace FXParticleSystem
{

const char *const ModuleCategoryKeys[] = { "Color", "Alpha", "Update", "Physics", "EmissionVelocity", "EmissionVolume", "Draw", "Wind", "Event", nullptr };

const char *const ParticlePriorityNames[] = { "NONE", "ULTRA_HIGH_ONLY", "HIGH_OR_ABOVE", "MEDIUM_OR_ABOVE", "LOW_OR_ABOVE", "VERY_LOW_OR_ABOVE", "ALWAYS_RENDER", nullptr };
const char *const ParticleShaderNames[] = { "NONE", "ADDITIVE", "ADDITIVE_ALPHA_TEST", "ALPHA", "ALPHA_TEST", "MULTIPLY", "ADDITIVE_NO_DEPTH_TEST", "ALPHA_NO_DEPTH_TEST",
	"W3D_DIFFUSE", "W3D_ALPHA", "W3D_EMISSIVE", nullptr };
const char *const ParticleTypeNames[] = { "NONE", "PARTICLE", "DRAWABLE", "STREAK", "VOLUME_PARTICLE", "SMUDGE", "TERRAIN_PARTICLE", "GPU_PARTICLE", "GPU_TERRAINFIRE", nullptr };
const char *const ParticleRotationNames[] = { "NONE", "ROTATION_OFF", "ROTATE_X", "ROTATE_Y", "ROTATE_Z", "ROTATE_V", nullptr };
const char *const WindMotionNames[] = { "NONE", "Unused", "PingPong", "Circular", nullptr };

namespace
{

// ---- field parsers --------------------------------------------------------------------------------------------------
// RW 0x969878: Alpha<N> = low high frame
void parseAlphaKey(INI *ini, void *, void *store, const void *)
{
	RandomAlphaKeyframe *key = (RandomAlphaKeyframe *)store;
	const float low = ini->scanReal(ini->getNextToken());
	const float high = ini->scanReal(ini->getNextToken());
	key->var.setRange(low, high, GameClientRandomVariable::UNIFORM);
	key->frame = ini->scanUnsignedInt(ini->getNextToken());
}

// RW 0x969f88 (0x42ef99 for the colour): Color<N> = R:r G:g B:b frame
void parseColorKey(INI *ini, void *instance, void *store, const void *userData)
{
	RGBColorKeyframe *key = (RGBColorKeyframe *)store;
	INI::parseRGBColor(ini, instance, &key->color, userData);
	key->frame = ini->scanUnsignedInt(ini->getNextToken());
}

// RW 0x96bd58: thin wrapper over parseAsciiString; the FXList name is not looked up here.
void parseEventFXListName(INI *ini, void *instance, void *store, const void *userData)
{
	INI::parseAsciiString(ini, instance, store, userData);
}

// offset of `member` inside `object`
template <class Object, class Member>
int offsetIn(const Object &object, const Member &member)
{
	return (int)((const char *)&member - (const char *)&object);
}

typedef std::vector<FieldParse> Table;
const FieldParse *finish(Table &table)
{
	table.push_back(FieldParse{ nullptr, nullptr, nullptr, 0 });
	return table.data();
}

const FieldParse RV = { nullptr, GameClientRandomVariable::parseRandomVariable, nullptr, 0 };

#define ROW(token, proc, ud, obj, member) FieldParse{ token, proc, ud, offsetIn(obj, (obj).member) }
#define RV_ROW(token, obj, member) ROW(token, GameClientRandomVariable::parseRandomVariable, nullptr, obj, member)

// ---- module bodies: construct with defaults, parse the End-terminated body --------------------------------------
template <class M>
std::shared_ptr<ModuleData> makeDefault()
{
	return std::make_shared<M>();
}

std::shared_ptr<ModuleData> makeEmptyDraw(ModuleClassId id)
{
	return std::make_shared<EmptyDrawModuleData>(id);
}

#define MODULE_PARSER(NAME, TYPE, TABLE_FN)                                                                              \
	std::shared_ptr<ModuleData> parse##NAME(INI *ini)                                                                    \
	{                                                                                                                    \
		std::shared_ptr<TYPE> m = std::make_shared<TYPE>();                                                              \
		ini->initFromINI(m.get(), TABLE_FN());                                                                           \
		return m;                                                                                                        \
	}

const FieldParse *colorTable()
{
	static Table t;
	if (t.empty())
	{
		static DefaultColorModuleData d;
		for (int i = 0; i < MAX_KEYFRAMES; ++i)
		{
			static std::string names[MAX_KEYFRAMES];
			names[i] = "Color" + std::to_string(i + 1);
			t.push_back(ROW(names[i].c_str(), parseColorKey, nullptr, d, m_colorKey[i]));
		}
		t.push_back(RV_ROW("ColorScale", d, m_colorScale));
		finish(t);
	}
	return t.data();
}

const FieldParse *alphaTable()
{
	static Table t;
	if (t.empty())
	{
		static DefaultAlphaModuleData d;
		static std::string names[MAX_KEYFRAMES];
		for (int i = 0; i < MAX_KEYFRAMES; ++i)
		{
			names[i] = "Alpha" + std::to_string(i + 1);
			t.push_back(ROW(names[i].c_str(), parseAlphaKey, nullptr, d, m_alphaKey[i]));
		}
		finish(t);
	}
	return t.data();
}

const FieldParse *updateTable()
{
	static Table t;
	if (t.empty())
	{
		static DefaultUpdateModuleData d;
		t.push_back(RV_ROW("SizeRate", d, m_sizeRate));
		t.push_back(RV_ROW("SizeRateDamping", d, m_sizeRateDamping));
		t.push_back(RV_ROW("AngleZ", d, m_angleZ));
		t.push_back(RV_ROW("AngularRateZ", d, m_angularRateZ));
		t.push_back(RV_ROW("AngularDamping", d, m_angularDamping));
		t.push_back(ROW("Rotation", INI::parseIndexList, ParticleRotationNames, d, m_rotation));
		t.push_back(RV_ROW("AngleXY", d, m_angleXY));
		t.push_back(RV_ROW("AngularRateXY", d, m_angularRateXY));
		t.push_back(RV_ROW("AngularDampingXY", d, m_angularDampingXY));
		finish(t);
	}
	return t.data();
}

const FieldParse *renderObjectUpdateTable()
{
	static Table t;
	if (t.empty())
	{
		static RenderObjectUpdateModuleData d;
		t.push_back(RV_ROW("StartSizeX", d, m_startSizeX));
		t.push_back(RV_ROW("StartSizeY", d, m_startSizeY));
		t.push_back(RV_ROW("StartSizeZ", d, m_startSizeZ));
		t.push_back(RV_ROW("SizeRateX", d, m_sizeRateX));
		t.push_back(RV_ROW("SizeRateY", d, m_sizeRateY));
		t.push_back(RV_ROW("SizeRateZ", d, m_sizeRateZ));
		t.push_back(RV_ROW("SizeDampingX", d, m_sizeDampingX));
		t.push_back(RV_ROW("SizeDampingY", d, m_sizeDampingY));
		t.push_back(RV_ROW("SizeDampingZ", d, m_sizeDampingZ));
		t.push_back(RV_ROW("AngleZ", d, m_angleZ));
		t.push_back(RV_ROW("AngularRateZ", d, m_angularRateZ));
		t.push_back(RV_ROW("AngularDamping", d, m_angularDamping));
		t.push_back(ROW("Rotation", INI::parseIndexList, ParticleRotationNames, d, m_rotation));
		finish(t);
	}
	return t.data();
}

const FieldParse *physicsTable()
{
	static Table t;
	if (t.empty())
	{
		static DefaultPhysicsModuleData d;
		t.push_back(ROW("Gravity", INI::parseReal, nullptr, d, m_gravity));
		t.push_back(RV_ROW("VelocityDamping", d, m_velocityDamping));
		t.push_back(ROW("DriftVelocity", INI::parseCoord3D, nullptr, d, m_driftVelocity));
		t.push_back(ROW("Swirly", INI::parseBool, nullptr, d, m_swirly));
		t.push_back(ROW("ParticlesAttachToBone", INI::parseBool, nullptr, d, m_particlesAttachToBone));
		finish(t);
	}
	return t.data();
}

const FieldParse *orthoTable()
{
	static Table t;
	if (t.empty())
	{
		static OrthoEmissionVelocityModuleData d;
		t.push_back(RV_ROW("X", d, m_x));
		t.push_back(RV_ROW("Y", d, m_y));
		t.push_back(RV_ROW("Z", d, m_z));
		finish(t);
	}
	return t.data();
}

const FieldParse *sphericalTable()
{
	static Table t;
	if (t.empty())
	{
		static SphericalEmissionVelocityModuleData d;
		t.push_back(RV_ROW("Speed", d, m_speed));
		finish(t);
	}
	return t.data();
}

const FieldParse *cylindricalTable()
{
	static Table t;
	if (t.empty())
	{
		static CylindricalEmissionVelocityModuleData d;
		t.push_back(RV_ROW("Radial", d, m_radial));
		t.push_back(RV_ROW("Normal", d, m_normal));
		finish(t);
	}
	return t.data();
}

const FieldParse *outwardTable()
{
	static Table t;
	if (t.empty())
	{
		static OutwardEmissionVelocityModuleData d;
		t.push_back(RV_ROW("Speed", d, m_speed));
		t.push_back(RV_ROW("OtherSpeed", d, m_otherSpeed));
		finish(t);
	}
	return t.data();
}

const FieldParse *pointTable()
{
	static Table t;
	if (t.empty())
	{
		static PointEmissionVolumeModuleData d;
		t.push_back(ROW("IsHollow", INI::parseBool, nullptr, d, m_isHollow));
		finish(t);
	}
	return t.data();
}

const FieldParse *lineTable()
{
	static Table t;
	if (t.empty())
	{
		static LineEmissionVolumeModuleData d;
		t.push_back(ROW("IsHollow", INI::parseBool, nullptr, d, m_isHollow));
		t.push_back(ROW("StartPoint", INI::parseCoord3D, nullptr, d, m_startPoint));
		t.push_back(ROW("EndPoint", INI::parseCoord3D, nullptr, d, m_endPoint));
		finish(t);
	}
	return t.data();
}

const FieldParse *boxTable()
{
	static Table t;
	if (t.empty())
	{
		static BoxEmissionVolumeModuleData d;
		t.push_back(ROW("IsHollow", INI::parseBool, nullptr, d, m_isHollow));
		t.push_back(ROW("HalfSize", INI::parseCoord3D, nullptr, d, m_halfSize));
		finish(t);
	}
	return t.data();
}

const FieldParse *sphereTable()
{
	static Table t;
	if (t.empty())
	{
		static SphereEmissionVolumeModuleData d;
		t.push_back(ROW("IsHollow", INI::parseBool, nullptr, d, m_isHollow));
		t.push_back(ROW("Radius", INI::parseReal, nullptr, d, m_radius));
		finish(t);
	}
	return t.data();
}

const FieldParse *cylinderTable()
{
	static Table t;
	if (t.empty())
	{
		static CylinderEmissionVolumeModuleData d;
		t.push_back(ROW("IsHollow", INI::parseBool, nullptr, d, m_isHollow));
		t.push_back(ROW("Radius", INI::parseReal, nullptr, d, m_radius));
		t.push_back(ROW("RadiusRate", INI::parseReal, nullptr, d, m_radiusRate));
		t.push_back(ROW("Length", INI::parseReal, nullptr, d, m_length));
		t.push_back(ROW("Offset", INI::parseCoord3D, nullptr, d, m_offset));
		finish(t);
	}
	return t.data();
}

const FieldParse *lightningEmissionTable()
{
	static Table t;
	if (t.empty())
	{
		static LightningEmissionModuleData d;
		t.push_back(ROW("StartPoint", INI::parseCoord3D, nullptr, d, m_startPoint));
		t.push_back(ROW("EndPoint", INI::parseCoord3D, nullptr, d, m_endPoint));
		t.push_back(RV_ROW("Amplitude1", d, m_amplitude[0]));
		t.push_back(RV_ROW("Frequency1", d, m_frequency[0]));
		t.push_back(RV_ROW("Phase1", d, m_phase[0]));
		t.push_back(RV_ROW("Amplitude2", d, m_amplitude[1]));
		t.push_back(RV_ROW("Frequency2", d, m_frequency[1]));
		t.push_back(RV_ROW("Phase2", d, m_phase[1]));
		t.push_back(RV_ROW("Amplitude3", d, m_amplitude[2]));
		t.push_back(RV_ROW("Frequency3", d, m_frequency[2]));
		t.push_back(RV_ROW("Phase3", d, m_phase[2]));
		finish(t);
	}
	return t.data();
}

const FieldParse *terrainFireTable()
{
	static Table t;
	if (t.empty())
	{
		static TerrainFireEmissionModuleData d;
		t.push_back(RV_ROW("Xoffset", d, m_xOffset));
		t.push_back(RV_ROW("Yoffset", d, m_yOffset));
		t.push_back(RV_ROW("Zoffset", d, m_zOffset));
		t.push_back(ROW("CellEmissionChance", INI::parseReal, nullptr, d, m_cellEmissionChance));
		finish(t);
	}
	return t.data();
}

const FieldParse *emptyTable()
{
	static const FieldParse t[1] = { { nullptr, nullptr, nullptr, 0 } }; // RW 0xc84858: the all-zero first row
	return t;
}

const FieldParse *renderObjectDrawTable()
{
	static Table t;
	if (t.empty())
	{
		static RenderObjectDrawModuleData d;
		t.push_back(ROW("MultiRenderObjects", INI::parseBool, nullptr, d, m_multiRenderObjects));
		static std::string names[3][4];
		for (int i = 0; i < 3; ++i)
		{
			const std::string n = std::to_string(i + 1);
			names[i][0] = "RenderGroup" + n;
			names[i][1] = "NumObjects" + n;
			names[i][2] = "Percent" + n;
			names[i][3] = "Shader" + n;
			t.push_back(ROW(names[i][0].c_str(), INI::parseAsciiString, nullptr, d, m_renderGroup[i]));
			t.push_back(ROW(names[i][1].c_str(), INI::parseInt, nullptr, d, m_numObjects[i]));
			t.push_back(ROW(names[i][2].c_str(), INI::parseReal, nullptr, d, m_percent[i]));
			t.push_back(ROW(names[i][3].c_str(), INI::parseIndexList, ParticleShaderNames, d, m_shader[i]));
		}
		t.push_back(ROW("SinkOnTerrainCollision", INI::parseBool, nullptr, d, m_sinkOnTerrainCollision));
		t.push_back(ROW("SinkRate", INI::parseReal, nullptr, d, m_sinkRate));
		finish(t);
	}
	return t.data();
}

const FieldParse *lightningDrawTable()
{
	static Table t;
	if (t.empty())
	{
		static LightningDrawModuleData d;
		t.push_back(RV_ROW("OffsetX", d, m_offsetX));
		t.push_back(RV_ROW("OffsetY", d, m_offsetY));
		t.push_back(RV_ROW("OffsetZ", d, m_offsetZ));
		t.push_back(ROW("MultiChance", INI::parseReal, nullptr, d, m_multiChance));
		t.push_back(ROW("TileTexture", INI::parseBool, nullptr, d, m_tileTexture));
		finish(t);
	}
	return t.data();
}

const FieldParse *gpuDrawTable()
{
	static Table t;
	if (t.empty())
	{
		static GpuDrawModuleData d;
		t.push_back(ROW("FramesPerRow", INI::parseInt, nullptr, d, m_framesPerRow));
		t.push_back(ROW("TotalFrames", INI::parseInt, nullptr, d, m_totalFrames));
		t.push_back(ROW("DetailTexture", INI::parseAsciiString, nullptr, d, m_detailTexture));
		t.push_back(ROW("SpeedMultiplier", INI::parseReal, nullptr, d, m_speedMultiplier));
		finish(t);
	}
	return t.data();
}

const FieldParse *windTable()
{
	static Table t;
	if (t.empty())
	{
		static DefaultWindModuleData d;
		t.push_back(ROW("WindMotion", INI::parseIndexList, WindMotionNames, d, m_windMotion));
		t.push_back(ROW("WindStrength", INI::parseReal, nullptr, d, m_windStrength));
		t.push_back(ROW("WindFullStrengthDist", INI::parseReal, nullptr, d, m_windFullStrengthDist));
		t.push_back(ROW("WindZeroStrengthDist", INI::parseReal, nullptr, d, m_windZeroStrengthDist));
		t.push_back(ROW("WindAngleChangeMin", INI::parseReal, nullptr, d, m_windAngleChangeMin));
		t.push_back(ROW("WindAngleChangeMax", INI::parseReal, nullptr, d, m_windAngleChangeMax));
		t.push_back(ROW("WindPingPongStartAngleMin", INI::parseReal, nullptr, d, m_windPingPongStartAngleMin));
		t.push_back(ROW("WindPingPongStartAngleMax", INI::parseReal, nullptr, d, m_windPingPongStartAngleMax));
		t.push_back(ROW("WindPingPongEndAngleMin", INI::parseReal, nullptr, d, m_windPingPongEndAngleMin));
		t.push_back(ROW("WindPingPongEndAngleMax", INI::parseReal, nullptr, d, m_windPingPongEndAngleMax));
		t.push_back(ROW("TurbulenceAmplitude", INI::parseReal, nullptr, d, m_turbulenceAmplitude));
		t.push_back(ROW("TurbulenceFrequency", INI::parseReal, nullptr, d, m_turbulenceFrequency));
		finish(t);
	}
	return t.data();
}

const FieldParse *lifeEventTable()
{
	static Table t;
	if (t.empty())
	{
		static LifeEventModuleData d;
		t.push_back(RV_ROW("EventTime", d, m_eventTime));
		t.push_back(ROW("EventFX", parseEventFXListName, nullptr, d, m_eventFX));
		t.push_back(ROW("PerParticle", INI::parseBool, nullptr, d, m_perParticle));
		t.push_back(ROW("KillAfterEvent", INI::parseBool, nullptr, d, m_killAfterEvent));
		finish(t);
	}
	return t.data();
}

const FieldParse *terrainCollisionTable()
{
	static Table t;
	if (t.empty())
	{
		static TerrainCollisionModuleData d;
		t.push_back(RV_ROW("HeightOffset", d, m_heightOffset));
		t.push_back(ROW("EventFX", parseEventFXListName, nullptr, d, m_eventFX));
		t.push_back(ROW("OrientFXToTerrain", INI::parseBool, nullptr, d, m_orientFXToTerrain));
		t.push_back(ROW("PerParticle", INI::parseBool, nullptr, d, m_perParticle));
		t.push_back(ROW("KillAfterEvent", INI::parseBool, nullptr, d, m_killAfterEvent));
		finish(t);
	}
	return t.data();
}

MODULE_PARSER(DefaultColor, DefaultColorModuleData, colorTable)
MODULE_PARSER(DefaultAlpha, DefaultAlphaModuleData, alphaTable)
MODULE_PARSER(DefaultUpdate, DefaultUpdateModuleData, updateTable)
MODULE_PARSER(RenderObjectUpdate, RenderObjectUpdateModuleData, renderObjectUpdateTable)
MODULE_PARSER(DefaultPhysics, DefaultPhysicsModuleData, physicsTable)
MODULE_PARSER(OrthoVelocity, OrthoEmissionVelocityModuleData, orthoTable)
MODULE_PARSER(CylindricalVelocity, CylindricalEmissionVelocityModuleData, cylindricalTable)
MODULE_PARSER(OutwardVelocity, OutwardEmissionVelocityModuleData, outwardTable)
MODULE_PARSER(PointVolume, PointEmissionVolumeModuleData, pointTable)
MODULE_PARSER(LineVolume, LineEmissionVolumeModuleData, lineTable)
MODULE_PARSER(BoxVolume, BoxEmissionVolumeModuleData, boxTable)
MODULE_PARSER(SphereVolume, SphereEmissionVolumeModuleData, sphereTable)
MODULE_PARSER(CylinderVolume, CylinderEmissionVolumeModuleData, cylinderTable)
MODULE_PARSER(LightningEmission, LightningEmissionModuleData, lightningEmissionTable)
MODULE_PARSER(TerrainFire, TerrainFireEmissionModuleData, terrainFireTable)
MODULE_PARSER(RenderObjectDraw, RenderObjectDrawModuleData, renderObjectDrawTable)
MODULE_PARSER(LightningDraw, LightningDrawModuleData, lightningDrawTable)
MODULE_PARSER(GpuDraw, GpuDrawModuleData, gpuDrawTable)
MODULE_PARSER(DefaultWind, DefaultWindModuleData, windTable)
MODULE_PARSER(LifeEvent, LifeEventModuleData, lifeEventTable)
MODULE_PARSER(TerrainCollision, TerrainCollisionModuleData, terrainCollisionTable)

std::shared_ptr<ModuleData> parseSpherical(INI *ini)
{
	std::shared_ptr<SphericalEmissionVelocityModuleData> m = std::make_shared<SphericalEmissionVelocityModuleData>(MODULE_SPHERICAL_EMISSION_VELOCITY);
	ini->initFromINI(m.get(), sphericalTable());
	return m;
}

std::shared_ptr<ModuleData> parseHemispherical(INI *ini)
{
	std::shared_ptr<SphericalEmissionVelocityModuleData> m = std::make_shared<SphericalEmissionVelocityModuleData>(MODULE_HEMISPHERICAL_EMISSION_VELOCITY);
	ini->initFromINI(m.get(), sphericalTable()); // RW: the same table and parse function (0x9676d7)
	return m;
}

std::shared_ptr<ModuleData> makeSpherical() { return std::make_shared<SphericalEmissionVelocityModuleData>(MODULE_SPHERICAL_EMISSION_VELOCITY); }
std::shared_ptr<ModuleData> makeHemispherical() { return std::make_shared<SphericalEmissionVelocityModuleData>(MODULE_HEMISPHERICAL_EMISSION_VELOCITY); }

// The four draw classes with no fields: every non-End line is code 5 through the empty table.
#define EMPTY_DRAW(NAME, ID)                                                                                             \
	std::shared_ptr<ModuleData> parse##NAME(INI *ini)                                                                    \
	{                                                                                                                    \
		std::shared_ptr<EmptyDrawModuleData> m = std::make_shared<EmptyDrawModuleData>(ID);                              \
		ini->initFromINI(m.get(), emptyTable());                                                                         \
		return m;                                                                                                        \
	}                                                                                                                    \
	std::shared_ptr<ModuleData> make##NAME() { return makeEmptyDraw(ID); }
EMPTY_DRAW(DefaultDraw, MODULE_DEFAULT_DRAW)
EMPTY_DRAW(StreakDraw, MODULE_STREAK_DRAW)
EMPTY_DRAW(QuadDraw, MODULE_QUAD_DRAW)
EMPTY_DRAW(ButterflyDraw, MODULE_BUTTERFLY_DRAW)

// ---- the registry (RW registration order 0x7a8c69-0x7a8dd3; display names are RW node +8) ----------------------
std::vector<ModuleClassInfo> buildRegistry()
{
	std::vector<ModuleClassInfo> r;
#define CLS(ID, CAT, TOKEN, DISPLAY, ISDEFAULT, PARSE, MAKE) r.push_back(ModuleClassInfo{ ID, CAT, TOKEN, DISPLAY, ISDEFAULT, PARSE, MAKE })
	CLS(MODULE_DEFAULT_COLOR, MODULE_CATEGORY_COLOR, "DefaultColor", "Default Color", true, parseDefaultColor, makeDefault<DefaultColorModuleData>);
	CLS(MODULE_DEFAULT_ALPHA, MODULE_CATEGORY_ALPHA, "DefaultAlpha", "Default Alpha", true, parseDefaultAlpha, makeDefault<DefaultAlphaModuleData>);
	CLS(MODULE_DEFAULT_UPDATE, MODULE_CATEGORY_UPDATE, "DefaultUpdate", "Default Update", true, parseDefaultUpdate, makeDefault<DefaultUpdateModuleData>);
	CLS(MODULE_RENDER_OBJECT_UPDATE, MODULE_CATEGORY_UPDATE, "RenderObjectUpdate", "Render Object Update", false, parseRenderObjectUpdate, makeDefault<RenderObjectUpdateModuleData>);
	CLS(MODULE_DEFAULT_PHYSICS, MODULE_CATEGORY_PHYSICS, "DefaultPhysics", "Default Physics", true, parseDefaultPhysics, makeDefault<DefaultPhysicsModuleData>);
	CLS(MODULE_ORTHO_EMISSION_VELOCITY, MODULE_CATEGORY_EMISSION_VELOCITY, "OrthoEmissionVelocity", "Ortho Emission Velocity", true, parseOrthoVelocity, makeDefault<OrthoEmissionVelocityModuleData>);
	CLS(MODULE_SPHERICAL_EMISSION_VELOCITY, MODULE_CATEGORY_EMISSION_VELOCITY, "SphericalEmissionVelocity", "Spherical Emission Velocity", false, parseSpherical, makeSpherical);
	CLS(MODULE_HEMISPHERICAL_EMISSION_VELOCITY, MODULE_CATEGORY_EMISSION_VELOCITY, "HemisphericalEmissionVelocity", "Hemispherical Emission Velocity", false, parseHemispherical, makeHemispherical);
	CLS(MODULE_CYLINDRICAL_EMISSION_VELOCITY, MODULE_CATEGORY_EMISSION_VELOCITY, "CylindricalEmissionVelocity", "Cylindrical Emission Velocity", false, parseCylindricalVelocity, makeDefault<CylindricalEmissionVelocityModuleData>);
	CLS(MODULE_OUTWARD_EMISSION_VELOCITY, MODULE_CATEGORY_EMISSION_VELOCITY, "OutwardEmissionVelocity", "Outward Emission Velocity", false, parseOutwardVelocity, makeDefault<OutwardEmissionVelocityModuleData>);
	CLS(MODULE_POINT_EMISSION_VOLUME, MODULE_CATEGORY_EMISSION_VOLUME, "PointEmissionVolume", "Point Emission Volume", true, parsePointVolume, makeDefault<PointEmissionVolumeModuleData>);
	CLS(MODULE_LINE_EMISSION_VOLUME, MODULE_CATEGORY_EMISSION_VOLUME, "LineEmissionVolume", "Line Emission Volume", false, parseLineVolume, makeDefault<LineEmissionVolumeModuleData>);
	CLS(MODULE_BOX_EMISSION_VOLUME, MODULE_CATEGORY_EMISSION_VOLUME, "BoxEmissionVolume", "Box Emission Volume", false, parseBoxVolume, makeDefault<BoxEmissionVolumeModuleData>);
	CLS(MODULE_SPHERE_EMISSION_VOLUME, MODULE_CATEGORY_EMISSION_VOLUME, "SphereEmissionVolume", "Sphere Emission Volume", false, parseSphereVolume, makeDefault<SphereEmissionVolumeModuleData>);
	CLS(MODULE_CYLINDER_EMISSION_VOLUME, MODULE_CATEGORY_EMISSION_VOLUME, "CylinderEmissionVolume", "Cylinder Emission Volume", false, parseCylinderVolume, makeDefault<CylinderEmissionVolumeModuleData>);
	CLS(MODULE_LIGHTNING_EMISSION, MODULE_CATEGORY_EMISSION_VOLUME, "LightningEmission", "Lightning Emission", false, parseLightningEmission, makeDefault<LightningEmissionModuleData>);
	CLS(MODULE_TERRAIN_FIRE_EMISSION, MODULE_CATEGORY_EMISSION_VOLUME, "TerrainFireEmission", "Terrain Fire Emission", false, parseTerrainFire, makeDefault<TerrainFireEmissionModuleData>);
	CLS(MODULE_DEFAULT_DRAW, MODULE_CATEGORY_DRAW, "DefaultDraw", "Default Draw", true, parseDefaultDraw, makeDefaultDraw);
	CLS(MODULE_STREAK_DRAW, MODULE_CATEGORY_DRAW, "StreakDraw", "Streak Draw", false, parseStreakDraw, makeStreakDraw);
	CLS(MODULE_QUAD_DRAW, MODULE_CATEGORY_DRAW, "QuadDraw", "Quad Draw", false, parseQuadDraw, makeQuadDraw);
	CLS(MODULE_BUTTERFLY_DRAW, MODULE_CATEGORY_DRAW, "ButterflyDraw", "Butterfly Draw", false, parseButterflyDraw, makeButterflyDraw);
	CLS(MODULE_RENDER_OBJECT_DRAW, MODULE_CATEGORY_DRAW, "RenderObjectDraw", "Render Object Draw", false, parseRenderObjectDraw, makeDefault<RenderObjectDrawModuleData>);
	CLS(MODULE_LIGHTNING_DRAW, MODULE_CATEGORY_DRAW, "LightningDraw", "Lightning Draw", false, parseLightningDraw, makeDefault<LightningDrawModuleData>);
	CLS(MODULE_GPU_DRAW, MODULE_CATEGORY_DRAW, "GpuDraw", "Gpu Draw", false, parseGpuDraw, makeDefault<GpuDrawModuleData>);
	CLS(MODULE_DEFAULT_WIND, MODULE_CATEGORY_WIND, "DefaultWind", "Default Wind", true, parseDefaultWind, makeDefault<DefaultWindModuleData>);
	CLS(MODULE_LIFE_EVENT, MODULE_CATEGORY_EVENT, "LifeEvent", "Life Event", false, parseLifeEvent, makeDefault<LifeEventModuleData>);
	CLS(MODULE_TERRAIN_COLLISION, MODULE_CATEGORY_EVENT, "TerrainCollision", "Terrain Collision", false, parseTerrainCollision, makeDefault<TerrainCollisionModuleData>);
#undef CLS
	return r;
}

// ---- template-level parse ---------------------------------------------------------------------------------------
// RW 0x5f7bdb ... 0x5fa052: one token = the class name; exact lookup in the category's registry; the class parses its body.
void parseCategory(INI *ini, void *instance, void *, const void *userData)
{
	ParticleSystemTemplate *tmpl = (ParticleSystemTemplate *)instance;
	const ModuleCategory category = (ModuleCategory)(intptr_t)userData;
	const std::string className = ini->getNextToken(); // a missing token is code 3 (RW 0x42dc9f)
	const ModuleClassInfo *cls = FindModuleClass(category, className);
	if (!cls)
	{
		// RW walks off the end of the registry list (access violation at address 4, no INIException: notes 2.3). Rule 10: an
		// error that reaches the report.
		throw INIException(5, "Unknown FXParticleSystem module class '%s' for category '%s'", className.c_str(), ModuleCategoryKeys[category]);
	}
	tmpl->setModule(category, cls->parse(ini));
}

// RW 0x5f33c2: `System` has no `=`; its body is the 21-row table applied to the template's SystemInfo.
void parseSystemBlock(INI *ini, void *instance, void *, const void *)
{
	ParticleSystemTemplate *tmpl = (ParticleSystemTemplate *)instance;
	ini->initFromINI(&tmpl->info(), ParticleSystemTemplate::systemFieldParse());
}

} // namespace

const std::vector<ModuleClassInfo> &ModuleClassRegistry()
{
	static const std::vector<ModuleClassInfo> registry = buildRegistry();
	return registry;
}

const ModuleClassInfo *FindModuleClass(ModuleCategory category, const std::string &token)
{
	for (const ModuleClassInfo &c : ModuleClassRegistry())
	{
		if (c.category == category && token == c.token) // exact (RW StringBase::compare, 0x406585)
		{
			return &c;
		}
	}
	return nullptr;
}

// ---- ParticleSystemTemplate -------------------------------------------------------------------------------------
ParticleSystemTemplate::ParticleSystemTemplate(const std::string &name) : m_name(name) {}

void ParticleSystemTemplate::setModule(ModuleCategory category, std::shared_ptr<ModuleData> module)
{
	if (category == MODULE_CATEGORY_EVENT)
	{
		m_events.push_back(std::move(module));
	}
	else
	{
		m_modules[category] = std::move(module);
	}
}

// RW table 0xde3a58: Color, Alpha, Update, Physics, EmissionVelocity, EmissionVolume, Draw, Wind, Event, System.
const FieldParse *ParticleSystemTemplate::templateFieldParse()
{
	static Table t;
	if (t.empty())
	{
		for (int c = 0; c < MODULE_CATEGORY_COUNT; ++c)
		{
			t.push_back(FieldParse{ ModuleCategoryKeys[c], parseCategory, (const void *)(intptr_t)c, 0 });
		}
		t.push_back(FieldParse{ "System", parseSystemBlock, nullptr, 0 });
		finish(t);
	}
	return t.data();
}

// RW 0x5f33c2 stack table (21 rows, RW order).
const FieldParse *ParticleSystemTemplate::systemFieldParse()
{
	static Table t;
	if (t.empty())
	{
		static ParticleSystemInfo d;
		t.push_back(ROW("Priority", INI::parseIndexList, ParticlePriorityNames, d, m_priority));
		t.push_back(ROW("IsOneShot", INI::parseBool, nullptr, d, m_isOneShot));
		t.push_back(ROW("Shader", INI::parseIndexList, ParticleShaderNames, d, m_shaderType));
		t.push_back(ROW("Type", INI::parseIndexList, ParticleTypeNames, d, m_particleType));
		t.push_back(ROW("ParticleName", INI::parseAsciiString, nullptr, d, m_particleTypeName));
		t.push_back(ROW("SlaveSystem", INI::parseAsciiString, nullptr, d, m_slaveSystemName));
		t.push_back(ROW("SlavePosOffset", INI::parseCoord3D, nullptr, d, m_slavePosOffset));
		t.push_back(ROW("PerParticleAttachedSystem", INI::parseAsciiString, nullptr, d, m_attachedSystemName));
		t.push_back(RV_ROW("Lifetime", d, m_lifetime));
		t.push_back(ROW("SystemLifetime", INI::parseUnsignedInt, nullptr, d, m_systemLifetime)); // RW parseUnsignedIntMax with userData 0
		t.push_back(ROW("SortLevel", INI::parseUnsignedInt, nullptr, d, m_sortLevel));
		t.push_back(RV_ROW("Size", d, m_size));
		t.push_back(RV_ROW("StartSizeRate", d, m_startSizeRate));
		t.push_back(RV_ROW("BurstDelay", d, m_burstDelay));
		t.push_back(RV_ROW("BurstCount", d, m_burstCount));
		t.push_back(RV_ROW("InitialDelay", d, m_initialDelay));
		t.push_back(ROW("IsGroundAligned", INI::parseBool, nullptr, d, m_isGroundAligned));
		t.push_back(ROW("IsEmitAboveGroundOnly", INI::parseBool, nullptr, d, m_isEmitAboveGroundOnly));
		t.push_back(ROW("IsParticleUpTowardsEmitter", INI::parseBool, nullptr, d, m_isParticleUpTowardsEmitter));
		t.push_back(ROW("UseMaximumHeight", INI::parseBool, nullptr, d, m_useMaximumHeight));
		t.push_back(ROW("ShroudEmitter", INI::parseBool, nullptr, d, m_shroudEmitter));
		finish(t);
	}
	return t.data();
}

void ParticleSystemTemplate::parseBody(INI *ini)
{
	ini->initFromINI(this, templateFieldParse());
}

// ---- store ------------------------------------------------------------------------------------------------------
const ParticleSystemTemplate *FXParticleSystemTemplateStore::findTemplate(const std::string &name) const
{
	auto it = m_templates.find(name);
	return it == m_templates.end() ? nullptr : it->second.get();
}

ParticleSystemTemplate *FXParticleSystemTemplateStore::findTemplate(const std::string &name)
{
	auto it = m_templates.find(name);
	return it == m_templates.end() ? nullptr : it->second.get();
}

void FXParticleSystemTemplateStore::parseTemplateDefinition(INI *ini)
{
	const std::string name = ini->getNextToken(); // RW 0x5fc7f6
	auto it = m_templates.find(name);
	if (it == m_templates.end())
	{
		it = m_templates.emplace(name, std::make_unique<ParticleSystemTemplate>(name)).first;
		m_order.push_back(name);
	}
	else
	{
		// RW 0x5fc82f-0x5fc848: destroyed and re-constructed in place for every load type; no merge
		it->second = std::make_unique<ParticleSystemTemplate>(name);
	}
	it->second->parseBody(ini);
	if (ini->getLoadType() == INI_LOAD_CREATE_OVERRIDES)
	{
		m_overrideLoaded = true; // RW 0x5fc896: manager byte +0x84
	}
}

void FXParticleSystemTemplateStore::clear()
{
	m_templates.clear();
	m_order.clear();
	m_overrideLoaded = false;
}

FXParticleSystemTemplateStore *TheFXParticleSystemTemplateStore = nullptr;

void ParseFXParticleSystemDefinitionGlobal(INI *ini)
{
	if (!TheFXParticleSystemTemplateStore)
	{
		throw INIException(3, "TheFXParticleSystemManager==NULL");
	}
	TheFXParticleSystemTemplateStore->parseTemplateDefinition(ini);
}

} // namespace FXParticleSystem
