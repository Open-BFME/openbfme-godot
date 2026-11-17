// OpenBFME unit tests: PORT-439, the script action FOCAL_LENGTH_CAMERA. The action is ordinal 389 of the dispatch switch RW 0x7CAFD5 (slot
// RW 0x7CF857 + 389 * 4 = RW 0x7CB6FB) and it did nothing in the port. The retail chain:
//   * RW 0x7CB6FB: four reals (RW 0x602EFB, the parameter's + 0xC) - focal length (mm), seconds, ease-in seconds, ease-out seconds - pushed and
//     the view called: RW 0x7BB627(focal, seconds * 1000.0f, easeIn * 1000.0f, easeOut * 1000.0f), the factor the pooled 1000.0 at RW 0xBD4388;
//   * RW 0x7BB627: TheTacticalView (RW 0xDE447C) vslot 0xF0 -> RW 0x48885E;
//   * RW 0x48885E View::setFocalLength: + 0x254 = 1, frames + 0x22C = max(1, max(1, msec) / TheW3DFrameLengthInMsec RW 0xDC7A8C), + 0x230 = 0,
//     + 0x23C = View + 0x6C * 57.295776 (RW 0xBD18FC, degrees), + 0x240 = focalLengthToFovDegrees(focal) (vslot 0x21C, RW 0x65EB68 ->
//     RW 0x8DAF6C), + 0x234 = fovToFocalLength(View + 0x6C) (vslot 0x218, RW 0x65EB57 -> RW 0x8DAEEE), + 0x238 = focal, the ease + 0x24C =
//     RW 0x70FCC0(easeInMs, easeOutMs, (float)msec); frames == 1 steps at once (RW 0x488911);
//   * the step RW 0x4866A7, called from the view's per-frame update RW 0x48A953 (+ 0x254 branch, the display RW 0x44B8D3 / RW 0x48C09A):
//     ++ + 0x230, the ease of frame / frames (RW 0x70FD72), View + 0x6C = (+ 0x23C + (+ 0x240 - + 0x23C) * e) * 0.017453292 (RW 0xBD1900);
//     the last frame writes + 0x240 * 0.017453292 and clears + 0x254; GlobalData + 0x9A4 (DisableCameraMovements, the row RW 0xC000C0,
//     parseBool RW 0x42E558, the constructor RW 0x642DA6 stores 0) writes no angle at all that frame (RW 0x4866C7).
// The conversions: RW 0x8DAEEE 12 / tan(radians / 2) (a zero tangent -> the pooled 1e10, RW 0xBF7328), RW 0x8DAF22 2 * atan(12 / mm) (mm <= 0 ->
// the pooled pi / 2, RW 0xBD89D0), RW 0x8DAF6C the degrees form. GPL-3.0.

#include "doctest.h"

#include "ScriptTestUtil.h"

#include "GameClient/CameraSettings.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameClient/TacticalCamera.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <string>
#include <vector>

using namespace scripttest;

namespace
{

float degrees(float radians)
{
	return radians * 57.2957763671875f;
}

// a camera on the logic fixture's flat ground (the synthetic map: 128 x 128 cells, every sample 0)
struct CameraRig
{
	logictest::LogicWorld lw;
	WorldHeightMap heights;
	CameraSettings settings;
	std::unique_ptr<TacticalCamera> cam;

	explicit CameraRig(bool disableCameraMovements = false)
	{
		settings.loaded = true;
		settings.defaultMinHeight = 120.0f;
		settings.defaultMaxHeight = 300.0f;
		settings.defaultPitchAngle = 37.5f;
		settings.defaultScrollSpeedScalar = 1.0f;
		settings.horizontalScrollSpeedFactor = 0.4f;
		settings.verticalScrollSpeedFactor = 0.5f;
		settings.screenEdgeScrollSpeedFactor = 1.0f;
		settings.screenEdgeScrollRampTimeMs = 250;
		settings.keyboardScrollSpeedFactor = 1.0f;
		settings.partitionCellSize = 40.0f;
		settings.disableCameraMovements = disableCameraMovements;
		heights.m_width = 128;
		heights.m_height = 128;
		heights.m_borderSize = 1;
		heights.m_data.assign(128 * 128, 0);
		heights.m_dataSize = 128 * 128;
		cam = std::make_unique<TacticalCamera>(settings);
		cam->setViewport(1024, 768);
		cam->startMap(*lw.logic, heights, nullptr, Coord3D{ 512, 384, 0 });
	}
	// one client frame of the view (the display's RW 0x44B8D3 -> RW 0x48A953)
	void frame(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			cam->update(false);
		}
	}
};

// the script engine's request for `focalMm` / `seconds` / eases (what ScriptActions::clientRequest records, RW 0x7CB6FB's four reals)
ScriptClientRequest focalRequest(float focalMm, float seconds, float easeIn, float easeOut)
{
	ScriptClientRequest r;
	r.action = "FOCAL_LENGTH_CAMERA";
	ScriptParameter p;
	p.type = 1;
	p.realValue = focalMm;
	r.params.push_back(p);
	p.realValue = seconds;
	r.params.push_back(p);
	p.realValue = easeIn;
	r.params.push_back(p);
	p.realValue = easeOut;
	r.params.push_back(p);
	r.focalMm = focalMm;
	return r;
}

} // namespace

TEST_CASE("PORT-439 focal length: the template's dispatch case and the census line (RW 0x7CAFA5 case 0x185, RW 0x7CF857 + 389 * 4 = 0x7CB6FB)")
{
	// the registry row: ordinal 389 "FOCAL_LENGTH_CAMERA", four real parameters (RW 0x602EFB reads each parameter's + 0xC), an ordinary-game case
	const int ordinal = ScriptTemplates::findAction("FOCAL_LENGTH_CAMERA");
	REQUIRE(ordinal == 389);
	const ScriptTemplate *t = ScriptTemplates::action(ordinal);
	REQUIRE(t != nullptr);
	CHECK(t->name == "FOCAL_LENGTH_CAMERA");
	CHECK(t->retailCase); // the dispatch switch has a case (the jump table entry is not the default RW 0x7CF846)
	REQUIRE(t->parameterTypes.size() == 4);
	for (int type : t->parameterTypes)
	{
		CHECK(type == 1); // PARAM_TYPE_REAL
	}
}

TEST_CASE("PORT-439 focal length: the script action reaches the client as a request with its focal length, and is no longer counted as unported")
{
	ScriptGame g;
	// the action with the mod's parameter shape: focal 35 mm over 2 s, 0.5 s of ease-in and out (the census line is FOCAL_LENGTH_CAMERA)
	g.lists[1].items.push_back(item(script("focal", { { cond("CONDITION_TRUE", {}) } },
		{ act("FOCAL_LENGTH_CAMERA", { rp(35.0f), rp(2.0f), rp(0.5f), rp(0.5f) }) }, {}, true)));
	g.start();
	g.frame();
	CHECK(g.engine().stats().unportedActions.count("FOCAL_LENGTH_CAMERA") == 0);
	REQUIRE(g.engine().stats().clientRequests.count("FOCAL_LENGTH_CAMERA") == 1);
	const std::vector<ScriptClientRequest> reqs = g.engine().takeClientRequests();
	REQUIRE(reqs.size() == 1);
	CHECK(reqs[0].action == "FOCAL_LENGTH_CAMERA");
	CHECK(reqs[0].focalMm == 35.0f);
	REQUIRE(reqs[0].params.size() == 4);
	CHECK(reqs[0].params[1].realValue == 2.0f);
	CHECK(reqs[0].params[2].realValue == 0.5f);
	CHECK(reqs[0].params[3].realValue == 0.5f);
}

TEST_CASE("PORT-439 focal length: the view's conversions (RW 0x8DAEEE, RW 0x8DAF22, RW 0x8DAF6C) and the set (RW 0x48885E)")
{
	// RW 0x8DAF22: 12 / tan(angle / 2), and a zero tangent answers the pooled 1e10 (RW 0xBF7328)
	CHECK(TacticalCamera::fovToFocalLength(0.0f) == 1.0e10f);
	CHECK(TacticalCamera::fovToFocalLength(2.0f * std::atan(12.0f / 50.0f)) == doctest::Approx(50.0f).epsilon(1e-4)); // tolerance: the float32 round trip through the double tan / atan (RW 0x8DAEEE / 0x8DAF22)
	// RW 0x8DAEEE: 2 * atan(12 / mm), and mm <= 0 answers pi / 2 (RW 0xBD89D0 = 1.5707964)
	CHECK(TacticalCamera::focalLengthToFov(0.0f) == 1.5707964f);
	CHECK(TacticalCamera::focalLengthToFov(-5.0f) == 1.5707964f);
	CHECK(TacticalCamera::focalLengthToFov(50.0f) == doctest::Approx(0.4713475f).epsilon(1e-5)); // tolerance: 2 * atan(0.24) at double precision, stored as float32 (the retail value is not pinned to a bit)
	// the defaults: the view is 50 degrees = 2 * atan(12 / 28.8991..) (View + 0x6C, RW 0xBDD410)
	CHECK(degrees(TacticalCamera::focalLengthToFov(TacticalCamera::fovToFocalLength(0.8726646f))) == doctest::Approx(50.0f).epsilon(1e-4)); // tolerance: the round trip through the float32 mm value (View + 0x6C is the pooled 50 degrees)

	CameraRig rig;
	// RW 0x488866: msec < 1 is 1; frames = msec / 33 (RW 0xDC7A8C). 1000 ms over 1000 ms with no ease: after 33 frames the angle is the target's
	rig.cam->setFocalLength(35.0f, 1000, 0.0f, 0.0f);
	CHECK(rig.cam->focalLengthAnimating());
	CHECK(degrees(rig.cam->horizontalFov()) == doctest::Approx(50.0f).epsilon(1e-4)); // tolerance: the store multiplies the float32 + 0x6C by the pooled 57.295776 (RW 0x488888); the first store is the start angle, no step yet
	rig.frame(33);
	CHECK_FALSE(rig.cam->focalLengthAnimating());
	CHECK(degrees(rig.cam->horizontalFov()) == doctest::Approx(degrees(TacticalCamera::focalLengthToFov(35.0f))).epsilon(1e-4)); // tolerance: float32 degrees -> radians (RW 0x486739) and back
	// 2 frames: the step is 2 / 2 at the second frame (the ease of the fraction), the last frame exact
	CameraRig two;
	two.cam->setFocalLength(35.0f, 66, 0.0f, 0.0f); // 66 / 33 = 2 frames
	CHECK(two.cam->focalLengthAnimating());
	two.frame(1);
	// at frame 1 of 2 with no ease the angle is halfway between the start and the target angle (RW 0x486701 in degrees)
	const float start = 50.0f, target = degrees(TacticalCamera::focalLengthToFov(35.0f));
	CHECK(degrees(two.cam->horizontalFov()) == doctest::Approx((start + target) * 0.5f).epsilon(1e-3)); // tolerance: the ease runs in float32 over the degrees; a half of two rounded angles
	two.frame(1);
	CHECK_FALSE(two.cam->focalLengthAnimating());
	CHECK(degrees(two.cam->horizontalFov()) == doctest::Approx(target).epsilon(1e-4)); // tolerance: the last frame's degrees -> radians store (RW 0x486739) and back
	// 33 ms is one frame: RW 0x488911 runs the step in the call (a longer focal length narrows the angle)
	CameraRig instant;
	instant.cam->setFocalLength(100.0f, 33, 0.0f, 0.0f);
	CHECK_FALSE(instant.cam->focalLengthAnimating());
	CHECK(degrees(instant.cam->horizontalFov()) == doctest::Approx(degrees(TacticalCamera::focalLengthToFov(100.0f))).epsilon(1e-4)); // tolerance: float32 degrees -> radians (RW 0x486739) and back
}

TEST_CASE("PORT-439 focal length: the ramp keeps the player's zoom (one reaction, RW 0x4866A7 touches only View + 0x6C) and the ease shortens it")
{
	CameraRig rig;
	const float zoomBefore = rig.cam->getZoom();
	const Coord3D eyeBefore = rig.cam->eye();
	// 100 mm over 1 s with 1 s of ease-in and 1 s of ease-out: the fractions are the milliseconds over the milliseconds (RW 0x488905's divisor is
	// the action's msec, so both are 1.0 and clamp to a full ramp: m_in = m_out = ... the ease's setEaseTimes of (1, 0))
	rig.cam->setFocalLength(100.0f, 1000, 1000.0f, 1000.0f);
	rig.frame(17);
	CHECK(rig.cam->focalLengthAnimating());
	// the camera's own zoom, height and eye are untouched by the animation (the script action is the view's field of view only)
	CHECK(rig.cam->getZoom() == zoomBefore);
	CHECK(rig.cam->eye().x == eyeBefore.x);
	CHECK(rig.cam->eye().y == eyeBefore.y);
	CHECK(rig.cam->eye().z == eyeBefore.z);
	rig.frame(20);
	CHECK_FALSE(rig.cam->focalLengthAnimating());
	CHECK(degrees(rig.cam->horizontalFov()) == doctest::Approx(degrees(TacticalCamera::focalLengthToFov(100.0f))).epsilon(1e-4)); // tolerance: float32 degrees -> radians (RW 0x486739) and back
	// the converted focal length of the new angle is the action's (View + 0x234 is recomputed from + 0x6C)
	CHECK(TacticalCamera::fovToFocalLength(rig.cam->horizontalFov()) == doctest::Approx(100.0f).epsilon(1e-3)); // tolerance: the mm -> angle -> mm round trip through the float32 stored angle

	// DisableCameraMovements (GlobalData + 0x9A4): the frame count grows, no angle is written (RW 0x4866C7), the animation ends at its frames
	CameraRig frozen(true);
	frozen.cam->setFocalLength(35.0f, 990, 0.0f, 0.0f); // 30 frames
	frozen.frame(10);
	CHECK(degrees(frozen.cam->horizontalFov()) == doctest::Approx(50.0f).epsilon(1e-4)); // tolerance: the degrees form of the untouched float32 + 0x6C (RW 0x488888)
	CHECK(frozen.cam->focalLengthAnimating());
	frozen.frame(20);
	CHECK_FALSE(frozen.cam->focalLengthAnimating());
	CHECK(degrees(frozen.cam->horizontalFov()) == doctest::Approx(50.0f).epsilon(1e-4)); // tolerance: the degrees form of the untouched float32 + 0x6C (RW 0x488888)
}

TEST_CASE("PORT-439 focal length: the director keeps the viewers' angle (the same animation in degrees) so the request is not dropped")
{
	ScriptCameraDirector d;
	const Coord3D origin{ 0.0f, 0.0f, 0.0f };
	CHECK(d.focalDegrees() == 0.0f);
	d.apply(focalRequest(35.0f, 1.0f, 0.0f, 0.0f), origin, 0.0f);
	REQUIRE(d.focalAnimating());
	// the target angle is the focal length's (RW 0x8DAF6C), reached over the action's second without ease
	d.update(500.0);
	CHECK(d.focalDegrees() == doctest::Approx(degrees(TacticalCamera::focalLengthToFov(35.0f)) * 0.5f).epsilon(1e-3)); // tolerance: the director's float32 degree lerp, half of two rounded angles
	d.update(500.0);
	CHECK_FALSE(d.focalAnimating());
	CHECK(d.focalDegrees() == doctest::Approx(degrees(TacticalCamera::focalLengthToFov(35.0f))).epsilon(1e-4)); // tolerance: float32 angles over the ease, the value is the exact target at the end
	// the request list still reports it as unhandled by nothing (a second, unknown action is what lands in `unhandled`)
	d.apply(focalRequest(35.0f, 0.0f, 0.0f, 0.0f), origin, 0.0f); // seconds 0: the millisecond floor (1 ms) is a step at once
	CHECK(d.focalDegrees() == doctest::Approx(degrees(TacticalCamera::focalLengthToFov(35.0f))).epsilon(1e-4)); // tolerance: float32 angles over the ease, the value is the exact target at the end
}
