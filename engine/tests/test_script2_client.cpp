// OpenBFME unit tests: the client side of the script presentation, lane SCRIPT-2 (S-1182): the script audio on the retail audio manager
// (PLAY_SOUND_EFFECT RW 0x7BE0EA, SPEECH_PLAY RW 0x7BE2EC, SOUND_PLAY_NAMED RW 0x7BE239) and the camera director's follow, zoom, look-toward,
// notification and objectives. GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/Audio/GameAudio.h"
#include "Common/Audio/SimulatedAudioDevice.h"
#include "GameClient/LiveGameAudio.h"
#include "GameClient/ScriptCameraDirector.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <cmath>
#include <string>

using namespace hudtest;

namespace
{

ScriptClientRequest request(const char *action, std::vector<ScriptParameter> params)
{
	ScriptClientRequest r;
	r.action = action;
	r.params = std::move(params);
	return r;
}

ScriptParameter str(int type, const std::string &s)
{
	ScriptParameter p;
	p.type = type;
	p.stringValue = s;
	return p;
}

ScriptParameter num(int type, int i, float f = 0.0f)
{
	ScriptParameter p;
	p.type = type;
	p.intValue = i;
	p.realValue = f;
	return p;
}

} // namespace

TEST_CASE("script2 client audio: the scripts' sound effects, speech and unit sounds reach the retail audio manager; other requests are not audio")
{
	if (!haveWorld("script2 client audio"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto scope = s.world->enterContext();
	Rig r(s);
	AudioAssetCache cache(s.mount->fs.get(), 16u << 20);
	SimulatedAudioDevice device(&cache);
	AudioManager audio(const_cast<AudioIniState &>(s.world->audio()), cache, device, RandomAlgorithm::ZH_CarryChain, 7);
	LiveGameAudio att(*r.game, audio);
	const std::uint64_t before = audio.report().eventsAdded;
	// MAP ANG Angmar's intro line (voice.ini)
	CHECK(att.applyScriptRequest(request("PLAY_SOUND_EFFECT", { str(12, "MAFound_Witchking01") })));
	CHECK(att.applyScriptRequest(request("SPEECH_PLAY", { str(21, "MAFound_Witchking02"), num(8, 0) })));
	Object *o = r.make("MordorLumberMill", 1200, 1200);
	REQUIRE(o);
	ScriptClientRequest named = request("SOUND_PLAY_NAMED", { str(12, "MAFound_Witchking03"), str(14, "Mill") });
	named.objectId = o->getID();
	CHECK(att.applyScriptRequest(named));
	CHECK(audio.report().eventsAdded == before + 3);
	CHECK(att.scriptAudioStats().played == 3);
	CHECK_FALSE(att.applyScriptRequest(request("MOVE_CAMERA_TO", { str(51, "x") })));
	// an object that is gone: no sound (RW 0x7BE239)
	named.objectId = 0xFFFFFFu;
	CHECK(att.applyScriptRequest(named));
	CHECK(att.scriptAudioStats().played == 3);
}

TEST_CASE("script2 camera director: CAMERA_FOLLOW_NAMED / STOP_FOLLOW, ZOOM_CAMERA with its ease, CAMERA_LOOK_TOWARD_WAYPOINT, the notification and the objectives")
{
	ScriptCameraDirector d;
	const Coord3D origin{ 0.0f, 0.0f, 0.0f };
	ScriptClientRequest follow = request("CAMERA_FOLLOW_NAMED", { str(14, "Witch King"), num(8, 1), num(1, 0) });
	follow.objectId = 42;
	d.apply(follow, origin, 0.0f);
	CHECK(d.followId() == 42u);
	d.apply(request("CAMERA_STOP_FOLLOW", {}), origin, 0.0f);
	CHECK(d.followId() == 0u);

	// ZOOM_CAMERA(0.85, 15 s, 1, 1): halfway in time the eased factor is halfway (the symmetric ParabolicEase), at the end exactly the target
	d.apply(request("ZOOM_CAMERA", { num(1, 0, 0.85f), num(1, 0, 15.0f), num(1, 0, 1.0f), num(1, 0, 1.0f) }), origin, 0.0f);
	d.update(7500.0);
	CHECK(d.zoom() == doctest::Approx(0.925f).epsilon(0.001));
	d.update(7600.0);
	CHECK(d.zoom() == 0.85f);

	// CAMERA_LOOK_TOWARD_WAYPOINT: from (0, 0) toward (100, 0) the view turns to angle pi / 2 (0 looks north)
	ScriptClientRequest look = request("CAMERA_LOOK_TOWARD_WAYPOINT", { str(7, "wp"), num(1, 0, 0.0f), num(1, 0, 0.0f), num(1, 0, 0.0f), num(8, 0) });
	look.hasPosition = true;
	look.position = Coord3D{ 100.0f, 0.0f, 0.0f };
	d.apply(look, origin, 0.0f);
	d.update(1.0);
	CHECK(d.angle() == doctest::Approx(1.5707963f));

	d.apply(request("DISPLAY_NOTIFICATION_BOX", { str(77, "NewObjective"), str(25, "SCRIPT:ANGAngmarNewObjectiveText_01"), num(0, 5) }), origin, 0.0f);
	CHECK(d.notificationLabel() == "SCRIPT:ANGAngmarNewObjectiveText_01");
	d.update(4999.0);
	CHECK_FALSE(d.notificationLabel().empty());
	d.update(2.0);
	CHECK(d.notificationLabel().empty());

	d.apply(request("SHOW_MISSION_OBJECTIVE", { num(0, 3) }), origin, 0.0f);
	d.apply(request("SHOW_MISSION_OBJECTIVE", { num(0, 5) }), origin, 0.0f);
	d.apply(request("MARK_MISSION_OBJECTIVE_COMPLETED", { num(0, 3) }), origin, 0.0f);
	d.apply(request("HIDE_MISSION_OBJECTIVE", { num(0, 5) }), origin, 0.0f);
	REQUIRE(d.objectives().size() == 1);
	CHECK(d.objectives()[0].first == 3);
	CHECK(d.objectives()[0].second);
}
