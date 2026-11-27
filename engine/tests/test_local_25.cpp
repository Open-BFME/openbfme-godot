// OpenBFME unit tests: LOCAL-25 - Locomotor ground motion as retail:
// MinTurnSpeed, StickToGround, suspension, pitch and roll.
// GPL-3.0.
//
// Retail RotWK 2.01 (and BFME2 1.06):
//   1. Dead fields in simulation (Stop S-7221):
//      MinTurnSpeed (+0x54), StickToGround (+0xD4), AllowAirborneMotiveForce (+0xD1),
//      Apply2DFrictionWhenAirborne (+0xD2), FrontWheelTurnAngle (+0xE8), MaximumWheelCompression (+0xE4),
//      LateralVelocityRollFactor (+0xAC), PitchInDirectionOfZVelFactor (+0xA0), MaxOverlappedHeight (+0xA4),
//      SlideIntoPlaceTime (+0xCC), WanderAboutPointRadius (+0xF8), WanderLengthFactor (+0xF4).
//   2. Client presentation physics (Drawable::calcPhysicsXform RW 0x67BDC0, applyPhysicsXform RW 0x67BE90):
//      - PitchStiffness (+0x90), RollStiffness (+0x94), PitchDamping (+0x98), RollDamping (+0x9C)
//      - AccelerationPitchLimit (+0x88), BounceAmount (+0x8C), UniformAxialDamping (+0xB8)
//      - HasSuspension (+0xDC), MaximumWheelExtension (+0xE0)
//      - UseTerrainSmoothing (+0xED): calcPitchRoll (RW 0x670570) 3-corner plane vs terrain normal fallback
//      - ForwardVelocityPitchFactor (+0xA8), ForwardAccelerationPitchFactor (+0xB0), LateralAccelerationRollFactor (+0xB4)
//      - Retail transform order: local Z translation, local pitch Rotate_Y, local roll Rotate_X(-roll), local yaw Rotate_Z.
//      - Separation of simulation pose (Thing::getPosition / getBasis) and presentation pose (getPresentationPose).

#include "doctest.h"
#include "ObjectTestUtil.h"
#include "IniTestUtil.h"
#include "W3dSynth.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cstring>
#include <cfloat>
#include <cmath>
#include <vector>

using namespace initest;
using namespace objtest;
using namespace w3dsynth;

namespace
{
struct LocomotorFixture
{
	Fixture fx;
	LocomotorStore store;
	LocomotorStore *savedStore;

	LocomotorFixture()
	{
		savedStore = TheLocomotorStore;
		TheLocomotorStore = &store;
		fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
	}

	~LocomotorFixture()
	{
		TheLocomotorStore = savedStore;
	}

	std::string load(const std::string &iniText)
	{
		return loadError(fx.env, "loco_local_25.ini", iniText);
	}
};
} // namespace

TEST_CASE("LOCAL-25: stop S-7221 - 11 dead simulation locomotor fields stay parsed into LocomotorTemplate")
{
	LocomotorFixture fix;
	const std::string ini =
		"Locomotor TestDeadFieldsLoco\n"
		"  Surfaces = GROUND\n"
		"  Speed = 60\n"
		"  MinTurnSpeed = 25%\n"
		"  StickToGround = Yes\n"
		"  AllowAirborneMotiveForce = Yes\n"
		"  Apply2DFrictionWhenAirborne = Yes\n"
		"  FrontWheelTurnAngle = 45d\n"
		"  MaximumWheelCompression = 6.0\n"
		"  LateralVelocityRollFactor = 1.25\n"
		"  PitchInDirectionOfZVelFactor = 0.5\n"
		"  MaxOverlappedHeight = 12.0\n"
		"  SlideIntoPlaceTime = 250\n"
		"  WanderAboutPointRadius = 35.0\n"
		"  WanderLengthFactor = 1.75\n"
		"End\n";

	REQUIRE(fix.load(ini).empty());
	const LocomotorTemplate *tmpl = fix.store.findLocomotorTemplate("TestDeadFieldsLoco");
	REQUIRE(tmpl != nullptr);

	// Pinned parsed values
	CHECK(tmpl->m_minTurnSpeed == 0.25f);
	CHECK(tmpl->m_stickToGround == true);
	CHECK(tmpl->m_allowAirborneMotiveForce == true);
	CHECK(tmpl->m_apply2DFrictionWhenAirborne == true);
	// tolerance: degree to radian angle conversion
	CHECK(tmpl->m_frontWheelTurnAngle == doctest::Approx(0.785398f));
	CHECK(tmpl->m_maximumWheelCompression == 6.0f);
	CHECK(tmpl->m_lateralVelocityRollFactor == 1.25f);
	CHECK(tmpl->m_pitchInDirectionOfZVelFactor == 0.5f);
	CHECK(tmpl->m_maxOverlappedHeight == 12.0f);
	CHECK(tmpl->m_slideIntoPlaceTime == 1.25f); // 250 ms = 1.25 frames
	CHECK(tmpl->m_wanderAboutPointRadius == 35.0f);
	CHECK(tmpl->m_wanderLengthFactor == 1.75f);

	// Default template values match retail RW 0x5E4326 ctor
	LocomotorTemplate def;
	CHECK(def.m_minTurnSpeed == 99999.0f);
	CHECK(def.m_stickToGround == false);
	CHECK(def.m_allowAirborneMotiveForce == false);
	CHECK(def.m_apply2DFrictionWhenAirborne == false);
	CHECK(def.m_frontWheelTurnAngle == 0.0f);
	CHECK(def.m_maximumWheelCompression == 0.0f);
	CHECK(def.m_lateralVelocityRollFactor == 0.0f);
	CHECK(def.m_pitchInDirectionOfZVelFactor == 0.0f);
	CHECK(def.m_maxOverlappedHeight == FLT_MAX);
	CHECK(def.m_slideIntoPlaceTime == 0.0f);
	CHECK(def.m_wanderAboutPointRadius == 0.0f);
	CHECK(def.m_wanderLengthFactor == 1.0f);
}

TEST_CASE("LOCAL-25: Matrix3D rotations Rotate_X, Rotate_Y, Rotate_Z (RW 0x00A3CEF0/CF90/D030)")
{
	constexpr float kPi = 3.14159265358979323846f;

	// Rotate_Z by +pi/2 rotates (1, 0, 0) to (0, 1, 0)
	Matrix3D mz;
	mz.Rotate_Z(kPi * 0.5f);
	Vector3 pz = mz.Transform_Point(Vector3(1.0f, 0.0f, 0.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(pz.X == doctest::Approx(0.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(pz.Y == doctest::Approx(1.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(pz.Z == doctest::Approx(0.0f));

	// Rotate_X by +pi/2 rotates (0, 1, 0) to (0, 0, 1)
	Matrix3D mx;
	mx.Rotate_X(kPi * 0.5f);
	Vector3 px = mx.Transform_Point(Vector3(0.0f, 1.0f, 0.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(px.X == doctest::Approx(0.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(px.Y == doctest::Approx(0.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(px.Z == doctest::Approx(1.0f));

	// Rotate_Y by +pi/2 rotates (0, 0, 1) to (1, 0, 0)
	Matrix3D my;
	my.Rotate_Y(kPi * 0.5f);
	Vector3 py = my.Transform_Point(Vector3(0.0f, 0.0f, 1.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(py.X == doctest::Approx(1.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(py.Y == doctest::Approx(0.0f));
	// tolerance: floating-point trigonometry rounding
	CHECK(py.Z == doctest::Approx(0.0f));
}

TEST_CASE("LOCAL-25: calcPitchRoll - RW 0x670570 terrain smoothing (m_useTerrainSmoothing) and normal fallback")
{
	objtest::World w;
	const std::string objIni =
		"Object TestVehicle\n"
		"  Geometry = BOX\n"
		"  GeometryMajorRadius = 20.0\n"
		"  GeometryMinorRadius = 10.0\n"
		"  GeometryHeight = 10.0\n"
		"End\n";
	REQUIRE(w.load(objIni).empty());
	const ThingTemplate *tt = w.get("TestVehicle");
	REQUIRE(tt != nullptr);

	MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpt;
	DrawServices services(assets, w.modules, hostOpt);

	// Terrain with normal tilted along Y: (0, 0.2, 0.979796)
	services.groundHeight = [](float /*x*/, float /*y*/, Coord3D *normal) -> float {
		if (normal)
		{
			*normal = Coord3D{ 0.0f, 0.2f, 0.979796f };
		}
		return 0.0f;
	};

	std::vector<std::string> problems;
	Drawable d(services, tt, (DrawableID)1, (ObjectID)10, {}, problems);
	REQUIRE(problems.empty());

	Coord3D pos{ 100.0f, 100.0f, 0.0f };
	Coord3D dir{ 1.0f, 0.0f, 0.0f }; // pointing +X

	// Test 1: without terrain smoothing (normal fallback)
	LocomotorTemplate locoNoSmooth;
	locoNoSmooth.m_useTerrainSmoothing = false;
	float pitch = 0.0f, roll = 0.0f;
	REQUIRE(d.calcPitchRoll(pos, dir, &locoNoSmooth, pitch, roll));
	CHECK(pitch == 0.0f);
	// tolerance: CRT std::asin precision
	CHECK(roll == doctest::Approx(std::asin(0.2f)));

	// Test 2: with terrain smoothing on a smooth slope z = 0.1 * x
	LocomotorTemplate locoSmooth;
	locoSmooth.m_useTerrainSmoothing = true;
	services.groundHeight = [](float x, float /*y*/, Coord3D *normal) -> float {
		if (normal)
		{
			*normal = Coord3D{ -0.0995f, 0.0f, 0.995f };
		}
		return 0.1f * x;
	};

	REQUIRE(d.calcPitchRoll(pos, dir, &locoSmooth, pitch, roll));
	// tolerance: negative slope angle along positive X
	CHECK(pitch < 0.0f);
	CHECK(roll == 0.0f);

	// Test 3: too-steep terrain falls back to position normal
	services.groundHeight = [](float x, float /*y*/, Coord3D *normal) -> float {
		if (normal)
		{
			*normal = Coord3D{ 0.0f, 0.0f, 1.0f };
		}
		// Cliff: jump of 50 units (exceeds (20+10)*0.5 = 15)
		return (x > 100.0f) ? 50.0f : 0.0f;
	};
	REQUIRE(d.calcPitchRoll(pos, dir, &locoSmooth, pitch, roll));
	// Falls back to normal (0, 0, 1), giving 0 pitch and roll
	CHECK(pitch == 0.0f);
	CHECK(roll == 0.0f);
}

TEST_CASE("LOCAL-25: calcPhysicsXform and applyPhysicsXform - wheels and suspension (RW 0x677026, RW 0x67BE90)")
{
	objtest::World w;
	const std::string objIni =
		"Object TestWheeledVehicle\n"
		"  Geometry = BOX\n"
		"  GeometryMajorRadius = 15.0\n"
		"  GeometryMinorRadius = 8.0\n"
		"  GeometryHeight = 8.0\n"
		"End\n";
	REQUIRE(w.load(objIni).empty());
	const ThingTemplate *tt = w.get("TestWheeledVehicle");
	REQUIRE(tt != nullptr);

	MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpt;
	DrawServices services(assets, w.modules, hostOpt);

	services.groundHeight = [](float /*x*/, float /*y*/, Coord3D *normal) -> float {
		if (normal)
		{
			*normal = Coord3D{ 0.0f, 0.0f, 1.0f };
		}
		return 0.0f;
	};

	std::vector<std::string> problems;
	Drawable d(services, tt, (DrawableID)2, (ObjectID)20, {}, problems);
	REQUIRE(problems.empty());

	LocomotorTemplate wheelLoco;
	wheelLoco.m_appearance = LOCO_WHEELS_FOUR;
	wheelLoco.m_hasSuspension = true;
	wheelLoco.m_pitchStiffness = 0.2f;
	wheelLoco.m_rollStiffness = 0.2f;
	wheelLoco.m_pitchDamping = 0.1f;
	wheelLoco.m_rollDamping = 0.1f;
	wheelLoco.m_uniformAxialDamping = 0.8f;
	wheelLoco.m_accelerationPitchLimit = 0.35f;
	wheelLoco.m_maximumWheelExtension = -5.0f;
	wheelLoco.m_bounceAmount = 0.1f;
	wheelLoco.m_maxSpeed = 50.0f;

	ObjectSnapshot snap;
	snap.id = 20;
	snap.tmpl = tt;
	snap.position = Coord3D{ 50.0f, 50.0f, 0.0f };
	snap.previousPos = Coord3D{ 45.0f, 50.0f, 0.0f };
	snap.angle = 0.0f;
	snap.moveSpeed = 30.0f;
	snap.aboveTerrain = false;
	snap.locomotorTemplate = &wheelLoco;

	Drawable::SyncPrep prep;
	d.prepareSync(snap, 1, 0.0, false, prep);
	d.commitSync(prep);
	CHECK_FALSE(d.hasPhysicsXform()); // the parallel half makes no step (it would draw the shared client random)
	d.applyPhysicsXform(snap, prep.pose); // the ordered pass of DrawableManager
	CHECK(d.hasPhysicsXform());

	// RW 0x67BE04: one step per client frame. With the manager's frame bound, further pictures of the same client frame keep the step's state
	unsigned clientFrame = 7;
	services.clientFrame = &clientFrame;
	d.applyPhysicsXform(snap, prep.pose); // a new client frame: a step
	const DrawableLocoInfo afterStep = *d.locoInfo();
	for (int i = 0; i < 5; ++i)
	{
		d.applyPhysicsXform(snap, prep.pose); // the same client frame: no step
	}
	CHECK(std::memcmp(&afterStep, d.locoInfo(), sizeof(DrawableLocoInfo)) == 0);
	CHECK(d.hasPhysicsXform());
	clientFrame = 8;
	d.applyPhysicsXform(snap, prep.pose);
	const bool unchangedAfterNextFrame = std::memcmp(&afterStep, d.locoInfo(), sizeof(DrawableLocoInfo)) == 0;
	CHECK_FALSE(unchangedAfterNextFrame); // the next client frame steps again
	services.clientFrame = nullptr;

	// Separation of simulation pose and presentation pose:
	// Simulation position is untouched
	CHECK(d.getPosition()->x == 50.0f);
	CHECK(d.getPosition()->y == 50.0f);
	CHECK(d.getPosition()->z == 0.0f);

	// Presentation pose is available
	float presBasis[9];
	Coord3D presPos;
	d.getPresentationPose(presBasis, presPos);
	CHECK(presPos.x == 50.0f);
	CHECK(presPos.y == 50.0f);

	// Suspension info is updated
	const DrawableLocoInfo *info = d.locoInfo();
	REQUIRE(info != nullptr);
	// tolerance: suspension extension clamp limit
	CHECK(info->m_frontLeftHeightOffset >= wheelLoco.m_maximumWheelExtension);
}

TEST_CASE("LOCAL-25: calcPhysicsXform - appearance cases (treads, ship, giant bird, unhandled)")
{
	objtest::World w;
	const std::string objIni =
		"Object TestMultiAppearance\n"
		"  Geometry = BOX\n"
		"  GeometryMajorRadius = 15.0\n"
		"  GeometryMinorRadius = 8.0\n"
		"  GeometryHeight = 8.0\n"
		"End\n";
	REQUIRE(w.load(objIni).empty());
	const ThingTemplate *tt = w.get("TestMultiAppearance");
	REQUIRE(tt != nullptr);

	MemoryFileSource fs;
	WW3DAssetManager assets(fs);
	W3DLuaDrawScriptHost::Options hostOpt;
	DrawServices services(assets, w.modules, hostOpt);
	services.groundHeight = [](float /*x*/, float /*y*/, Coord3D *normal) -> float {
		if (normal) *normal = Coord3D{ 0.0f, 0.0f, 1.0f };
		return 0.0f;
	};

	std::vector<std::string> problems;
	Drawable d(services, tt, (DrawableID)3, (ObjectID)30, {}, problems);
	REQUIRE(problems.empty());

	RenderInterpolation::Pose pose;
	pose.position = Coord3D{ 10.0f, 20.0f, 0.0f };
	pose.angle = 0.0f;
	pose.basis[0] = 1.0f; pose.basis[4] = 1.0f; pose.basis[8] = 1.0f;

	// Case 1: Treads (LOCO_TREADS / LOCO_HOVER)
	LocomotorTemplate treadLoco;
	treadLoco.m_appearance = LOCO_HOVER;
	treadLoco.m_pitchStiffness = 0.15f;
	treadLoco.m_rollStiffness = 0.15f;
	treadLoco.m_pitchDamping = 0.05f;
	treadLoco.m_rollDamping = 0.05f;
	treadLoco.m_uniformAxialDamping = 0.9f;
	treadLoco.m_accelerationPitchLimit = 0.3f;

	ObjectSnapshot snap;
	snap.locomotorTemplate = &treadLoco;
	snap.position = pose.position;
	snap.previousPos = pose.position;

	PhysicsXformInfo xfInfo;
	CHECK(d.calcPhysicsXform(snap, pose, xfInfo));
	CHECK(xfInfo.m_totalZ == 0.0f);

	// Case 2: Ship (LOCO_SHIP)
	LocomotorTemplate shipLoco;
	shipLoco.m_appearance = LOCO_SHIP;
	shipLoco.m_pitchStiffness = 0.1f;
	shipLoco.m_rollStiffness = 0.1f;
	shipLoco.m_pitchDamping = 0.05f;
	shipLoco.m_rollDamping = 0.05f;
	shipLoco.m_uniformAxialDamping = 0.8f;
	shipLoco.m_accelerationPitchLimit = 0.4f;
	shipLoco.m_forwardVelocityPitchFactor = 0.02f;
	shipLoco.m_forwardAccelerationPitchFactor = 0.01f;
	shipLoco.m_lateralAccelerationRollFactor = 0.01f;

	snap.locomotorTemplate = &shipLoco;
	CHECK(d.calcPhysicsXform(snap, pose, xfInfo));

	// Case 3: Giant Bird (LOCO_GIANT_BIRD)
	LocomotorTemplate birdLoco;
	birdLoco.m_appearance = LOCO_GIANT_BIRD;
	snap.locomotorTemplate = &birdLoco;
	CHECK(d.calcPhysicsXform(snap, pose, xfInfo));
	CHECK(xfInfo.m_totalPitch == 0.0f);

	// Case 4: Default two-legged infantry (LOCO_LEGS_TWO) -> returns false (no physics transform, 1:1 retail)
	LocomotorTemplate infLoco;
	infLoco.m_appearance = LOCO_LEGS_TWO;
	snap.locomotorTemplate = &infLoco;
	CHECK_FALSE(d.calcPhysicsXform(snap, pose, xfInfo));
}
