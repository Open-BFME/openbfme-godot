// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (WWMath Matrix3D::Look_At, Build_Matrix3D, Slerp).
//
// CameraAnimation (lane CAMANIM-1): the map's CameraAnimationList played by the tactical view (MOVE_CAMERA_BY_ANIMATION, script action 550).
// Client state only: the logic records the action as a client request (ScriptActions.cpp, RW 0x7BBB9D) and never reads any of this.
//
// TARGET FACTS (RotWK 2.01; the camera-animation code is tier A to the BFME2 decomp at RW = BFME2 + 0x406xxx, see each function):
//   * the tracks (CameraAnimationTrack<T>, BFME2 decomp GameClient/CameraAnimation/CameraAnimationTrackLookAtCamera.cpp): keys sorted by frame; a
//     key with an existing frame replaces it, a negative frame is dropped (insert RW 0x94890D look camera, RW 0x9489EE look-at); getFrameData(t) (free RW
//     0x946E76, look RW 0x9482A5, look-at its twin): t < 0 -> the first key; else the last key whose frame <= floor(t) (the first key when t is
//     before it: locators RW 0x946C67 / 0x947EC9 / 0x947F91); the last key, or a key whose successor has the same frame, is returned as is; else
//     the four keys around it (clamped at both ends) are interpolated at (t - k0) / (k1 - k0) through the CURRENT key's interpolation type
//   * the interpolators: scalar RW 0x9D665F, Coord3D RW 0x9D6731 (0 STEP: the current key; 1 LINE: (1 - t) c + t f; 2 CATM: the outer control
//     points rebuilt from the frame spacing, then the Catmull-Rom RW 0x909D11 per component), quaternion RW 0x9D661C (1 and 2: Slerp, else the key)
//   * the frames: free RW 0x94674E (position, orientation, FOV through the current key's type; the frame ctor RW 0x9466D0: identity orientation),
//     look camera RW 0x9476D2 (position, roll, FOV; the default ctor RW 0x94766F: mode 2, FOV 0.87266463), look-at RW 0x9477C5 (the point)
//   * free animation transform RW 0x9471A5: Build_Matrix3D(orientation) + the position, the frame's FOV; look animation RW 0x9488B3: the camera
//     track's position, roll and FOV and the look-at track's point: Matrix3D::Look_At(position, target, roll)
//   * the player (vtable 0xC1FCF0, ctor RW 0x710BA9): time 0, started by RW 0x710B10; advance (RW 0x710B64): dt >= 0 and started: time += dt; past
//     the animation's frame count it finishes (RW 0x710B2D) and the time stays at the count; getTransform (RW 0x710A70) = the animation's at time
//   * the view (W3DView, vtable 0xBDD490): slot 0x68 (RW 0x4864F9) frees the previous player, makes a new one, starts it, stores the client frame
//     (TheGameClient vslot 0x7C) and sets the camera mode (+ 0x2354) to 4. Each view update in mode 4 (RW 0x48AA25) advances the player by
//     (frame now - stored frame) * 30 / client fps (0xD9F60C, 30) and stores the frame; a finished player is freed and the mode goes to 0. The
//     camera build (RW 0x502858) in mode 4 takes the player's transform as the camera's and its FOV as the view plane's horizontal angle
//     (RW 0x48B919: Set_View_Plane(fov, -1)); the view's own position, angle, pitch and zoom are untouched, so when the animation ends the camera
//     is the view's again.
//   * what ends mode 4 early: any view call that writes the mode: MOVE_CAMERA_TO and SETUP_CAMERA (slot 0x60 -> RW 0x489570: 0 / 1),
//     MOVE_CAMERA_ALONG_SPLINE_PATH / MOVE_CAMERA_LOCATOR_ALONG_SPLINE_PATH (slot 0x64 RW 0x48976D: 2), RESET_CAMERA (slot 0xC8 RW 0x48D26B: 3),
//     CAMERA_STOP_FOLLOW / CAMERA_STOP_TETHER_NAMED (slot 0x1A0 RW 0x48862B) and CAMERA_SET_DEFAULT (slot 0xE4 RW 0x488630) through RW 0x486303 (0);
//     the player stays allocated until the next MOVE_CAMERA_BY_ANIMATION frees it. ROTATE_CAMERA, the zoom / pitch / roll actions, the follow and
//     the look-toward actions (slots 0xCC .. 0x190) do not write the mode: they change the view underneath a playing animation.
//   * an unknown animation name: RW 0x7320BA (TheCameraAnimationList's name lookup, exact compare) returns null and the action does nothing.
//
// INFERENCES (presentation only, client state):
//   * the time between two client frames: retail steps the time by whole client frames (30 a second); the port draws the camera at the fraction of
//     the client frame the render frame shows (docs/PLAN.md presentation rule: the client may be smoother than retail); the steps are retail's.
//   * view slot 0x278 (RW 0x48D45E, the view's camera-input update) also writes mode 0 (RW 0x48D6DB); the port's HUD camera input does not end an
//     animation (the retail maps disable input while one plays). MOVE_CAMERA_ALONG_SPLINE_PATH ends one here also when retail's slot 0x64 would
//     refuse the path (an object whose type is not 6).

#pragma once

#include "GameClient/MapChunks.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CameraAnimationMath
{
// RW 0x909D11 (BFME2 0x503D8E): the Catmull-Rom scalar
float catmullRom(float p0, float p1, float p2, float p3, float t);
// RW 0x9D665F: the scalar channel through the current key's type (a at ta, c at tc: the current key, f at tf: the next one, d at td)
float interpolate(int type, float t, float a, int ta, float c, int tc, float f, int tf, float d, int td);
// RW 0x9D6731: the same for a Coord3D
void interpolate3(int type, float t, const float *a, int ta, const float *c, int tc, const float *f, int tf, const float *d, int td, float *out);
// RW 0x9D661C: the orientation (x, y, z, w): Slerp(c, f, t) for LINE and CATM, else c
void interpolateQuat(int type, float t, const float *c, const float *f, float *out);
// ZH WWMath quat.cpp Slerp (the row RotWK calls from RW 0x9D661C)
void slerp(const float *p, const float *q, float alpha, float *out);
// ZH WWMath matrix3d.cpp Matrix3D::Look_At(p, t, roll): the camera at p looking down its -Z at t, rolled about its Z
Matrix3D lookAt(const float *p, const float *t, float roll);
} // namespace CameraAnimationMath

template <class T> class CameraAnimationTrack
{
public:
	struct Key
	{
		int frame = 0;
		T data;
	};
	// the retail insert (BFME2 0x541FCC): a negative frame is dropped, an existing frame is replaced, else the key goes in sorted
	void setKey(int frame, const T &data);
	// the index the locators leave: the last key whose frame <= `frame`, 0 when `frame` is before the first key (keys must not be empty)
	size_t locate(int frame) const;
	const std::vector<Key> &keys() const { return m_keys; }
	bool empty() const { return m_keys.empty(); }

	// getFrameData: `interp(t, a, ta, b, tb, c, tc, d, td)` builds the frame from the four keys around the time (b the current key)
	template <class Interp> T frameData(float frameNum, Interp interp) const;

private:
	std::vector<Key> m_keys;
};

struct FreeCameraFrame
{
	int type = CAMERA_INTERP_CATM;
	float position[3] = {};
	float orientation[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // ctor RW 0x9466D0: identity
	float fov = 0.0f;
};

struct LookCameraFrame
{
	int type = CAMERA_INTERP_CATM;
	float position[3] = {};
	float roll = 0.0f;
	float fov = 0.87266463f; // ctor RW 0x94766F: 50 degrees
};

struct LookAtFrame
{
	int type = CAMERA_INTERP_CATM;
	float target[3] = {};
};

// one animation of the list: a free animation (one track) or a look animation (camera and look-at tracks)
class CameraAnimationClip
{
public:
	// errors: an empty track (retail would index an empty vector) or an unknown type; the map's record as parsed
	static std::unique_ptr<CameraAnimationClip> build(const CameraAnimation &record, std::string *err);

	const std::string &name() const { return m_name; }
	bool isFree() const { return m_free; }
	int numFrames() const { return m_numFrames; }
	// the transform at `time` (frames): the camera's world matrix (W3D: the camera looks down its -Z, Y up) and the horizontal FOV (radians)
	void getTransform(float time, Matrix3D &transform, float &fov) const;

	const CameraAnimationTrack<FreeCameraFrame> &freeTrack() const { return m_freeTrack; }
	const CameraAnimationTrack<LookCameraFrame> &cameraTrack() const { return m_cameraTrack; }
	const CameraAnimationTrack<LookAtFrame> &targetTrack() const { return m_targetTrack; }

	FreeCameraFrame freeFrame(float time) const;
	LookCameraFrame lookFrame(float time) const;
	LookAtFrame lookAtFrame(float time) const;

private:
	std::string m_name;
	bool m_free = true;
	int m_numFrames = 0;
	CameraAnimationTrack<FreeCameraFrame> m_freeTrack;
	CameraAnimationTrack<LookCameraFrame> m_cameraTrack;
	CameraAnimationTrack<LookAtFrame> m_targetTrack;
};

// TheCameraAnimationList for one map
class CameraAnimationList
{
public:
	// every record of the map; a record that cannot play is an error (listed in `errors`, left out of the list)
	void build(const std::vector<CameraAnimation> &records, std::vector<std::string> &errors);
	// RW 0x7320BA: the first animation whose name equals `name` exactly, null for none
	const CameraAnimationClip *find(const std::string &name) const;
	size_t size() const { return m_clips.size(); }

private:
	std::vector<std::unique_ptr<CameraAnimationClip>> m_clips;
};

// the tactical view's animation state (W3DView + 0x2354 mode 4, + 0x235C the player, + 0x2360 the start frame, + 0x2364 the FOV)
class CameraAnimationPlayback
{
public:
	// a script request in the client's order: MOVE_CAMERA_BY_ANIMATION starts `name` (RW 0x7BBB9D -> slot 0x68), the mode-writing camera actions
	// end the animation (see the header); returns true when the request changed the playback
	bool applyRequest(const CameraAnimationList &list, const std::string &action, const std::string &name);
	// slot 0x68: the animation from its first frame; false (and nothing changes) when the list has no such animation
	bool start(const CameraAnimationList &list, const std::string &name);
	// a view call that writes the camera mode
	void stop();
	// one client frame of the view update (RW 0x48AA25): time + 1, past the frame count the animation ends
	void clientFrame();

	bool active() const { return m_clip != nullptr; }
	const std::string &name() const { return m_name; }
	float time() const { return m_time; }
	// the camera of the frame drawn, `alpha` (0..1) of the way to the next client frame (header: INFERENCES); false when no animation plays
	bool transform(double alpha, Matrix3D &transform, float &fov) const;

	static bool cancelsAnimation(const std::string &action);

private:
	const CameraAnimationClip *m_clip = nullptr;
	std::string m_name;
	float m_time = 0.0f;
};
