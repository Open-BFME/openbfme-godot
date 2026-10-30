// OpenBFME. GPL-3.0.
//
// Lane CAMANIM-1: MOVE_CAMERA_BY_ANIMATION. The CameraAnimationList as RotWK reads it (the version gates of RW 0x94622C / RW 0x9D65B1), the tracks and
// their interpolation (GameClient/CameraAnimation.h), the view's playback (RW 0x4864F9 / 0x48AA25 / 0x710B64) and the request the logic records
// (RW 0x7BBB9D). The retail case reads the two Angmar maps whose intros use it (SKIPs without ROTWK_INSTALL / BFME2_INSTALL).

#include "doctest.h"
#include "LogicTestUtil.h"
#include "MapTestUtil.h"
#include "RetailTestMount.h"
#include "ScriptTestUtil.h"

#include "GameClient/CameraAnimation.h"
#include "GameClient/MapUtil.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <string>

using namespace maptest;

namespace
{

Bytes rev(const std::string &s)
{
	return Bytes{ (std::uint8_t)s[3], (std::uint8_t)s[2], (std::uint8_t)s[1], (std::uint8_t)s[0] };
}

bool loadMap(const Bytes &data, LoadedMap &out, std::string *err)
{
	MapReadOptions opt;
	opt.strict = true;
	return MapReader::load(data, "synthetic.map", opt, out, err);
}

// one free key: frame, tag (empty: none, the version < 2 layout), position, orientation, FOV
void freeKey(W &w, int frame, const std::string &tag, float x, float y, float z, const float *q, float fov)
{
	w.i32(frame);
	if (!tag.empty())
	{
		w.raw(rev(tag));
	}
	w.f32(x).f32(y).f32(z).f32(q[0]).f32(q[1]).f32(q[2]).f32(q[3]).f32(fov);
}

const float kIdentity[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

} // namespace

TEST_CASE("camanim1 parse: the CameraAnimationList versions 1 / 2 / 3 as RotWK reads them (RW 0x94622C start offset from 3, RW 0x9D65B1 tags from 2)")
{
	for (int version : { 1, 2, 3 })
	{
		CAPTURE(version);
		Toc t;
		W a;
		a.i32(1).raw(rev("free")).astr("Fly").i32(40);
		if (version >= 3)
		{
			a.i32(7);
		}
		a.i32(2);
		freeKey(a, 0, version >= 2 ? "step" : "", 1, 2, 3, kIdentity, 0.5f);
		freeKey(a, 10, version >= 2 ? "zzzz" : "", 4, 5, 6, kIdentity, 0.6f); // an unknown code keeps catm
		LoadedMap m;
		std::string err;
		REQUIRE_MESSAGE(loadMap(file(t, chunk(t, "CameraAnimationList", version, a.b)), m, &err), err);
		REQUIRE(m.chunks.cameraAnimations.size() == 1);
		const CameraAnimation &c = m.chunks.cameraAnimations[0];
		CHECK(c.numFrames == 40);
		if (version >= 3)
		{
			CHECK(c.startOffset == 7);
		}
		else
		{
			CHECK(c.startOffset == 0);
		}
		REQUIRE(c.keys.size() == 2);
		if (version >= 2)
		{
			CHECK(c.keys[0].interp == CAMERA_INTERP_STEP);
		}
		else
		{
			CHECK(c.keys[0].interp == CAMERA_INTERP_CATM);
		}
		CHECK(c.keys[1].interp == CAMERA_INTERP_CATM);
		CHECK(c.keys[1].pos[2] == 6.0f);
		CHECK(c.keys[1].fov == 0.6f);
	}
}

TEST_CASE("camanim1 interpolate: a free track from a synthetic chunk: key order, LINE / CATM / STEP, the ends, the orientation and the FOV")
{
	// keys written out of order, one repeated frame (the later replaces it) and one negative frame (dropped): the retail insert (RW 0x94890D family)
	const float rot90[4] = { 0.0f, 0.0f, std::sqrt(0.5f), std::sqrt(0.5f) }; // 90 degrees about Z
	Toc t;
	W a;
	a.i32(1).raw(rev("free")).astr("Fly").i32(30).i32(0).i32(5);
	freeKey(a, 10, "line", 100, 0, 100, rot90, 1.0f);
	freeKey(a, 0, "line", 0, 0, 100, kIdentity, 0.5f);
	freeKey(a, 20, "step", 999, 999, 999, kIdentity, 0.9f); // replaced below
	freeKey(a, -3, "line", 5, 5, 5, kIdentity, 0.1f);       // dropped
	freeKey(a, 20, "catm", 100, 100, 100, rot90, 0.8f);
	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, chunk(t, "CameraAnimationList", 3, a.b)), m, &err), err);
	REQUIRE(m.chunks.cameraAnimations.size() == 1);
	std::unique_ptr<CameraAnimationClip> clip = CameraAnimationClip::build(m.chunks.cameraAnimations[0], &err);
	REQUIRE_MESSAGE(clip != nullptr, err);
	REQUIRE(clip->freeTrack().keys().size() == 3);
	CHECK(clip->freeTrack().keys()[0].frame == 0);
	CHECK(clip->freeTrack().keys()[1].frame == 10);
	CHECK(clip->freeTrack().keys()[2].frame == 20);
	CHECK(clip->freeTrack().keys()[2].data.position[0] == 100.0f);
	CHECK(clip->freeTrack().locate(15) == 1);
	CHECK(clip->freeTrack().locate(20) == 2);
	CHECK(clip->freeTrack().locate(99) == 2);

	// LINE between frames 0 and 10 (the current key's type), at frame 5: halfway, FOV 0.75, orientation 45 degrees about Z
	FreeCameraFrame f = clip->freeFrame(5.0f);
	CHECK(f.position[0] == doctest::Approx(50.0f)); // tolerance: float rounding of the arithmetic
	CHECK(f.position[2] == doctest::Approx(100.0f)); // tolerance: float rounding of the arithmetic
	CHECK(f.fov == doctest::Approx(0.75f)); // tolerance: float rounding of the arithmetic
	Matrix3D tm;
	float fov = 0.0f;
	clip->getTransform(5.0f, tm, fov);
	CHECK(fov == doctest::Approx(0.75f)); // tolerance: float rounding of the arithmetic
	CHECK(tm.Row[0][0] == doctest::Approx(std::cos(0.7853982f)).epsilon(1e-5)); // tolerance: float rounding of the arithmetic
	CHECK(tm.Row[1][0] == doctest::Approx(std::sin(0.7853982f)).epsilon(1e-5)); // tolerance: float rounding of the arithmetic
	CHECK(tm.Row[0][3] == doctest::Approx(50.0f)); // tolerance: float rounding of the arithmetic
	// before the first frame (t < 0) and past the last key: the end keys as they are
	CHECK(clip->freeFrame(-1.0f).position[0] == 0.0f);
	CHECK(clip->freeFrame(25.0f).fov == 0.8f);
	CHECK(clip->freeFrame(20.0f).position[1] == 100.0f);

	// CATM (the current key at frame 10 switched to catm): the outer points from the frame spacing, then Catmull-Rom per component
	CameraAnimation r = m.chunks.cameraAnimations[0];
	for (CameraAnimationFrame &k : r.keys)
	{
		k.interp = CAMERA_INTERP_CATM;
	}
	clip = CameraAnimationClip::build(r, &err);
	REQUIRE(clip != nullptr);
	f = clip->freeFrame(15.0f);
	// keys x: 0 @ 0, 100 @ 10, 100 @ 20 (the last key repeats as the next): v0 = 100 + (0 - 100) / 10 * 10 = 0, v1 = d = 100
	CHECK(f.position[0] == doctest::Approx(CameraAnimationMath::catmullRom(0.0f, 100.0f, 100.0f, 100.0f, 0.5f))); // tolerance: float rounding of the arithmetic
	CHECK(f.position[1] == doctest::Approx(CameraAnimationMath::catmullRom(0.0f, 0.0f, 100.0f, 100.0f, 0.5f))); // tolerance: float rounding of the arithmetic
	CHECK(f.fov == doctest::Approx(CameraAnimationMath::catmullRom(0.5f, 1.0f, 0.8f, 0.8f, 0.5f))); // tolerance: float rounding of the arithmetic
	// a straight, evenly spaced line stays straight under CATM
	CHECK(CameraAnimationMath::interpolate(CAMERA_INTERP_CATM, 0.25f, 0.0f, 0, 10.0f, 10, 20.0f, 20, 30.0f, 30) == doctest::Approx(12.5f)); // tolerance: float rounding of the arithmetic
	// uneven spacing: the outer point is rebuilt to the current segment's length
	CHECK(CameraAnimationMath::interpolate(CAMERA_INTERP_CATM, 0.5f, 0.0f, 0, 10.0f, 20, 20.0f, 30, 20.0f, 30) ==
		doctest::Approx(CameraAnimationMath::catmullRom(10.0f + (0.0f - 10.0f) / 20.0f * 10.0f, 10.0f, 20.0f, 20.0f, 0.5f))); // tolerance: float rounding of the arithmetic
	CHECK(CameraAnimationMath::interpolate(CAMERA_INTERP_STEP, 0.9f, 0.0f, 0, 1.0f, 1, 2.0f, 2, 3.0f, 3) == 1.0f);
}

TEST_CASE("camanim1 look: the camera track looks at the look-at track (Matrix3D::Look_At), roll and FOV from the camera keys")
{
	Toc t;
	W a;
	a.i32(1).raw(rev("look")).astr("Orbit").i32(10).i32(0);
	a.i32(2);
	a.i32(0).raw(rev("line")).f32(0).f32(0).f32(10).f32(0).f32(0.6f);
	a.i32(10).raw(rev("line")).f32(0).f32(0).f32(10).f32(0).f32(1.0f);
	a.i32(2);
	a.i32(0).raw(rev("line")).f32(10).f32(0).f32(10);
	a.i32(10).raw(rev("line")).f32(0).f32(10).f32(10);
	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, chunk(t, "CameraAnimationList", 3, a.b)), m, &err), err);
	std::unique_ptr<CameraAnimationClip> clip = CameraAnimationClip::build(m.chunks.cameraAnimations.at(0), &err);
	REQUIRE_MESSAGE(clip != nullptr, err);
	CHECK_FALSE(clip->isFree());
	Matrix3D tm;
	float fov = 0.0f;
	clip->getTransform(0.0f, tm, fov);
	CHECK(fov == doctest::Approx(0.6f)); // tolerance: float rounding of the arithmetic
	// the camera looks down its -Z: at frame 0 toward +X; Y up is world +Z; the camera stands at (0, 0, 10)
	CHECK(-tm.Row[0][2] == doctest::Approx(1.0f)); // tolerance: float rounding of the arithmetic
	CHECK(tm.Row[1][2] == doctest::Approx(0.0f)); // tolerance: float rounding of the arithmetic
	CHECK(tm.Row[2][1] == doctest::Approx(1.0f)); // tolerance: float rounding of the arithmetic
	CHECK(tm.Row[2][3] == doctest::Approx(10.0f)); // tolerance: float rounding of the arithmetic
	// at the end toward +Y
	clip->getTransform(10.0f, tm, fov);
	CHECK(-tm.Row[1][2] == doctest::Approx(1.0f)); // tolerance: float rounding of the arithmetic
	CHECK(fov == doctest::Approx(1.0f)); // tolerance: float rounding of the arithmetic
	// an empty look-at track cannot play (retail would read an empty vector): an error, not a default
	CameraAnimation bad = m.chunks.cameraAnimations[0];
	bad.lookAtKeys.clear();
	CHECK(CameraAnimationClip::build(bad, &err) == nullptr);
	CHECK(err.find("Orbit") != std::string::npos);
	CameraAnimationList list;
	std::vector<std::string> errors;
	list.build({ m.chunks.cameraAnimations[0], bad }, errors);
	CHECK(list.size() == 1);
	CHECK(errors.size() == 1);
}

TEST_CASE("camanim1 playback: the view's mode 4 (RW 0x4864F9): one client frame a step, the end past the frame count, the actions that end it")
{
	CameraAnimation r;
	r.type = "free";
	r.name = "Look at Army";
	r.numFrames = 3;
	r.keys.resize(2);
	r.keys[0].frame = 0;
	r.keys[0].quat[3] = 1.0f;
	r.keys[0].fov = 0.5f;
	r.keys[1].frame = 3;
	r.keys[1].pos[0] = 30.0f;
	r.keys[1].quat[3] = 1.0f;
	r.keys[1].fov = 0.5f;
	r.keys[0].interp = r.keys[1].interp = CAMERA_INTERP_LINE;
	CameraAnimationList list;
	std::vector<std::string> errors;
	list.build({ r }, errors);
	REQUIRE(errors.empty());

	CameraAnimationPlayback p;
	Matrix3D tm;
	float fov = 0.0f;
	CHECK_FALSE(p.transform(0.0, tm, fov));
	// RW 0x7320BA compares names exactly: no animation, no view call
	CHECK_FALSE(p.applyRequest(list, "MOVE_CAMERA_BY_ANIMATION", "look at army"));
	CHECK_FALSE(p.active());
	CHECK(p.applyRequest(list, "MOVE_CAMERA_BY_ANIMATION", "Look at Army"));
	CHECK(p.active());
	CHECK(p.time() == 0.0f);
	REQUIRE(p.transform(0.5, tm, fov));
	CHECK(tm.Row[0][3] == doctest::Approx(5.0f)); // tolerance: float rounding (drawn half a client frame on: presentation)
	CHECK(fov == doctest::Approx(0.5f)); // tolerance: float rounding of the arithmetic
	p.clientFrame();
	p.clientFrame();
	p.clientFrame();
	CHECK(p.active()); // time 3 = the frame count: still playing (RW 0x710B64 ends only past it)
	CHECK(p.time() == 3.0f);
	REQUIRE(p.transform(0.9, tm, fov));
	CHECK(tm.Row[0][3] == doctest::Approx(30.0f)); // tolerance: float rounding of the arithmetic
	p.clientFrame();
	CHECK_FALSE(p.active()); // finished: the view's own camera again (mode 0)

	// the actions that write the view's mode end it (RESET_CAMERA then the animation, in that order, as Carn Dum's "Move camera to glori")
	CHECK(p.applyRequest(list, "MOVE_CAMERA_BY_ANIMATION", "Look at Army"));
	CHECK_FALSE(p.applyRequest(list, "ROTATE_CAMERA", ""));
	CHECK_FALSE(p.applyRequest(list, "CAMERA_LOOK_TOWARD_OBJECT", ""));
	CHECK(p.active());
	CHECK(p.applyRequest(list, "MOVE_CAMERA_TO", ""));
	CHECK_FALSE(p.active());
	CHECK_FALSE(p.applyRequest(list, "RESET_CAMERA", "")); // nothing playing: no change
	CHECK(p.applyRequest(list, "MOVE_CAMERA_BY_ANIMATION", "Look at Army"));
	CHECK(p.active());
	for (const char *a : { "SETUP_CAMERA", "MOVE_CAMERA_ALONG_SPLINE_PATH", "MOVE_CAMERA_LOCATOR_ALONG_SPLINE_PATH", "RESET_CAMERA", "CAMERA_STOP_FOLLOW",
			 "CAMERA_STOP_TETHER_NAMED", "CAMERA_SET_DEFAULT" })
	{
		CAPTURE(a);
		CHECK(CameraAnimationPlayback::cancelsAnimation(a));
	}
	// a second animation request restarts from the first frame (slot 0x68 frees the old player)
	p.clientFrame();
	CHECK(p.applyRequest(list, "MOVE_CAMERA_BY_ANIMATION", "Look at Army"));
	CHECK(p.time() == 0.0f);
}

TEST_CASE("camanim1 script: MOVE_CAMERA_BY_ANIMATION (action 550, RW 0x7BBB9D) is recorded as a client request with its name, not counted unported")
{
	using namespace scripttest;
	ScriptGame g;
	g.lists[1].items.push_back(item(script("intro", { { cond("CONDITION_TRUE", {}) } },
		{ act("MOVE_CAMERA_BY_ANIMATION", { sp(64, "Look at Army") }), act("DISABLE_INPUT", {}) }, {}, true)));
	g.start();
	g.frame();
	std::vector<ScriptClientRequest> reqs = g.engine().takeClientRequests();
	REQUIRE(reqs.size() == 2);
	CHECK(reqs[0].action == "MOVE_CAMERA_BY_ANIMATION");
	REQUIRE(reqs[0].params.size() == 1);
	CHECK(reqs[0].params[0].stringValue == "Look at Army");
	CHECK(reqs[0].script == "intro");
	CHECK(reqs[1].action == "DISABLE_INPUT");
	CHECK(g.engine().stats().unportedActions.count("MOVE_CAMERA_BY_ANIMATION") == 0);
}

TEST_CASE("camanim1 retail: the Angmar intros' animations (map ang carn dum, map ang fornost) read and build")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("camanim1 retail");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	struct Anim
	{
		std::string name;
		bool free;
		int numFrames;
		size_t keys;
	};
	struct Case
	{
		const char *path;
		std::vector<Anim> anims;
	};
	// the stored records (camera keys): Look at Army flies for 300 frames; Glori's Army Cam and Citadel Look are one-key stills of length 1, which
	// RotWK shows for about two client frames before the view's own camera returns (RW 0x710B64 / 0x48AA83)
	const Case cases[] = {
		{ "maps\\map ang carn dum\\map ang carn dum.map", { { "Look at Army", false, 300, 2 }, { "Glori's Army Cam", false, 1, 1 } } },
		{ "maps\\map ang fornost\\map ang fornost.map", { { "Citadel Look", true, 1, 1 } } },
	};
	for (const Case &c : cases)
	{
		CAPTURE(c.path);
		std::vector<std::uint8_t> data;
		std::string err;
		REQUIRE_MESSAGE(mount->fs->readFile(c.path, data, &err), err);
		LoadedMap m;
		MapReadOptions opt;
		REQUIRE_MESSAGE(MapReader::load(data, c.path, opt, m, &err), err);
		CameraAnimationList list;
		std::vector<std::string> errors;
		list.build(m.chunks.cameraAnimations, errors);
		CHECK_MESSAGE(errors.empty(), (errors.empty() ? std::string() : errors[0]));
		for (const Anim &a : c.anims)
		{
			const std::string &n = a.name;
			CAPTURE(n);
			const CameraAnimationClip *clip = list.find(n);
			REQUIRE(clip != nullptr);
			CHECK(clip->isFree() == a.free);
			CHECK(clip->numFrames() == a.numFrames);
			CHECK((a.free ? clip->freeTrack().keys().size() : clip->cameraTrack().keys().size()) == a.keys);
			Matrix3D tm;
			float fov = 0.0f;
			clip->getTransform((float)clip->numFrames() * 0.5f, tm, fov);
			MESSAGE(c.path << " '" << n << "': " << (clip->isFree() ? "free" : "look") << ", " << clip->numFrames() << " frames, FOV " << fov
							<< ", camera at (" << tm.Row[0][3] << ", " << tm.Row[1][3] << ", " << tm.Row[2][3] << ")");
		}
	}
}
