// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (WWMath Matrix3D::Look_At, Slerp).
//
// Lane CAMANIM-1: see CameraAnimation.h for the RotWK evidence.

#include "GameClient/CameraAnimation.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace CameraAnimationMath
{

float catmullRom(float p0, float p1, float p2, float p3, float t)
{
	// RW 0x909D11 (BFME2 decomp Common/calc.cpp:23, the operation order kept)
	return ((((p1 * 3.0f - p0 - p2 * 3.0f + p3) * t + (p0 + p0 - p1 * 5.0f + p2 * 4.0f - p3)) * t + (p2 - p0)) * t + (p1 + p1)) * 0.5f;
}

float interpolate(int type, float t, float a, int ta, float c, int tc, float f, int tf, float d, int td)
{
	// RW 0x9D665F (BFME2 decomp CameraAnimationFrameData_doInterpolate.cpp)
	switch (type)
	{
	case CAMERA_INTERP_LINE:
		return (1.0f - t) * c + t * f;
	case CAMERA_INTERP_CATM:
	{
		const float d0 = (float)(tc - ta);
		const float d1 = (float)(tf - tc);
		const float d2 = (float)(td - tf);
		const float v0 = d0 > 0.0f ? c + ((a - c) / d0) * d1 : a;
		const float v1 = d2 > 0.0f ? f + ((d - f) / d2) * d1 : d;
		return catmullRom(v0, c, f, v1, t);
	}
	default:
		return c;
	}
}

void interpolate3(int type, float t, const float *a, int ta, const float *c, int tc, const float *f, int tf, const float *d, int td, float *out)
{
	// RW 0x9D6731 (BFME2 decomp Rva005C71AD.cpp): the vector form; the outer points are (a - c) * d1 / d0 + c (division as a reciprocal product)
	float r[3];
	switch (type)
	{
	case CAMERA_INTERP_LINE:
		for (int i = 0; i < 3; ++i)
		{
			r[i] = c[i] * (1.0f - t) + f[i] * t;
		}
		break;
	case CAMERA_INTERP_CATM:
	{
		const float d0 = (float)(tc - ta);
		const float d1 = (float)(tf - tc);
		const float d2 = (float)(td - tf);
		float v0[3], v1[3];
		for (int i = 0; i < 3; ++i)
		{
			v0[i] = d0 > 0.0f ? (a[i] - c[i]) * d1 * (1.0f / d0) + c[i] : a[i];
			v1[i] = d2 > 0.0f ? (d[i] - f[i]) * d1 * (1.0f / d2) + f[i] : d[i];
		}
		for (int i = 0; i < 3; ++i)
		{
			r[i] = catmullRom(v0[i], c[i], f[i], v1[i], t);
		}
		break;
	}
	default:
		for (int i = 0; i < 3; ++i)
		{
			r[i] = c[i];
		}
		break;
	}
	for (int i = 0; i < 3; ++i)
	{
		out[i] = r[i];
	}
}

void slerp(const float *p, const float *q, float alpha, float *out)
{
	// ZH WWMath quat.cpp Slerp (WWMATH_EPSILON 0.0001)
	float cosT = p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3];
	bool flip = false;
	if (cosT < 0.0f)
	{
		cosT = -cosT;
		flip = true;
	}
	float beta;
	if (1.0f - cosT < 0.0001f * 0.0001f)
	{
		beta = 1.0f - alpha;
	}
	else
	{
		const float theta = std::acos(cosT);
		const float sinT = std::sin(theta);
		const float ooSinT = 1.0f / sinT;
		beta = std::sin(theta - alpha * theta) * ooSinT;
		alpha = std::sin(alpha * theta) * ooSinT;
	}
	if (flip)
	{
		alpha = -alpha;
	}
	for (int i = 0; i < 4; ++i)
	{
		out[i] = beta * p[i] + alpha * q[i];
	}
}

void interpolateQuat(int type, float t, const float *c, const float *f, float *out)
{
	// RW 0x9D661C (BFME2 decomp Rva005C7098.cpp): LINE and CATM slerp between the current and the next key, STEP keeps the current one
	if (type > 0 && type <= 2)
	{
		float r[4];
		slerp(c, f, t, r);
		std::copy(r, r + 4, out);
	}
	else
	{
		std::copy(c, c + 4, out);
	}
}

Matrix3D lookAt(const float *p, const float *t, float roll)
{
	// ZH WWMath matrix3d.cpp Matrix3D::Look_At: -Z down +X, +Y up, then Rotate_Y(yaw), Rotate_X(pitch), Rotate_Z(-roll) (post-multiplied)
	const float dx = t[0] - p[0], dy = t[1] - p[1], dz = t[2] - p[2];
	const float len1 = std::sqrt(dx * dx + dy * dy + dz * dz);
	const float len2 = std::sqrt(dx * dx + dy * dy);
	float sinp = 0.0f, cosp = 1.0f, siny = 0.0f, cosy = 1.0f;
	if (len1 != 0.0f)
	{
		sinp = dz / len1;
		cosp = len2 / len1;
	}
	if (len2 != 0.0f)
	{
		siny = dy / len2;
		cosy = dx / len2;
	}
	Matrix3D m;
	m.Row[0][0] = 0.0f;
	m.Row[0][1] = 0.0f;
	m.Row[0][2] = -1.0f;
	m.Row[1][0] = -1.0f;
	m.Row[1][1] = 0.0f;
	m.Row[1][2] = 0.0f;
	m.Row[2][0] = 0.0f;
	m.Row[2][1] = 1.0f;
	m.Row[2][2] = 0.0f;
	m.Row[0][3] = p[0];
	m.Row[1][3] = p[1];
	m.Row[2][3] = p[2];
	for (int r = 0; r < 3; ++r) // Rotate_Y(s, c)
	{
		const float t1 = m.Row[r][0], t2 = m.Row[r][2];
		m.Row[r][0] = cosy * t1 - siny * t2;
		m.Row[r][2] = siny * t1 + cosy * t2;
	}
	for (int r = 0; r < 3; ++r) // Rotate_X(s, c)
	{
		const float t1 = m.Row[r][1], t2 = m.Row[r][2];
		m.Row[r][1] = cosp * t1 + sinp * t2;
		m.Row[r][2] = -sinp * t1 + cosp * t2;
	}
	const float cz = std::cos(-roll), sz = std::sin(-roll);
	for (int r = 0; r < 3; ++r) // Rotate_Z(theta)
	{
		const float t1 = m.Row[r][0], t2 = m.Row[r][1];
		m.Row[r][0] = cz * t1 + sz * t2;
		m.Row[r][1] = -sz * t1 + cz * t2;
	}
	return m;
}

} // namespace CameraAnimationMath

template <class T> void CameraAnimationTrack<T>::setKey(int frame, const T &data)
{
	if (frame < 0)
	{
		return;
	}
	auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame, [](const Key &k, int f) { return k.frame < f; });
	if (it != m_keys.end() && it->frame == frame)
	{
		it->data = data;
		return;
	}
	Key k;
	k.frame = frame;
	k.data = data;
	m_keys.insert(it, k);
}

template <class T> size_t CameraAnimationTrack<T>::locate(int frame) const
{
	// RW 0x946C67 / 0x947EC9 / 0x947F91: lower_bound, then one back unless it hit the frame exactly (or is the first key)
	auto it = std::lower_bound(m_keys.begin(), m_keys.end(), frame, [](const Key &k, int f) { return k.frame < f; });
	size_t i = (size_t)(it - m_keys.begin());
	if (i >= m_keys.size() || (i > 0 && m_keys[i].frame != frame))
	{
		--i;
	}
	return i;
}

template <class T> template <class Interp> T CameraAnimationTrack<T>::frameData(float frameNum, Interp interp) const
{
	// RW 0x946E76 / 0x9482A5 (BFME2 decomp CameraAnimationTrackLookAtCamera.cpp: getFrameData)
	if (frameNum < 0.0f)
	{
		return m_keys[0].data;
	}
	const size_t i = locate((int)std::floor(frameNum));
	if (i >= m_keys.size() - 1)
	{
		return m_keys[i].data;
	}
	const Key &k0 = m_keys[i];
	const Key &k1 = m_keys[i + 1];
	if (k0.frame == k1.frame)
	{
		return k0.data;
	}
	const float t = (frameNum - (float)k0.frame) / (float)(k1.frame - k0.frame);
	const Key &kPrev = i > 0 ? m_keys[i - 1] : k0;
	const Key &kNext = i + 2 < m_keys.size() ? m_keys[i + 2] : k1;
	return interp(t, kPrev.data, kPrev.frame, k0.data, k0.frame, k1.data, k1.frame, kNext.data, kNext.frame);
}

template class CameraAnimationTrack<FreeCameraFrame>;
template class CameraAnimationTrack<LookCameraFrame>;
template class CameraAnimationTrack<LookAtFrame>;

std::unique_ptr<CameraAnimationClip> CameraAnimationClip::build(const CameraAnimation &record, std::string *err)
{
	auto fail = [&](const std::string &why) -> std::unique_ptr<CameraAnimationClip> {
		if (err)
		{
			*err = "camera animation '" + record.name + "': " + why;
		}
		return nullptr;
	};
	std::unique_ptr<CameraAnimationClip> c(new CameraAnimationClip);
	c->m_name = record.name;
	c->m_numFrames = record.numFrames;
	if (record.type == "free")
	{
		c->m_free = true;
		for (const CameraAnimationFrame &k : record.keys)
		{
			FreeCameraFrame f;
			f.type = k.interp;
			std::copy(k.pos, k.pos + 3, f.position);
			std::copy(k.quat, k.quat + 4, f.orientation);
			f.fov = k.fov;
			c->m_freeTrack.setKey(k.frame, f);
		}
		if (c->m_freeTrack.empty())
		{
			return fail("no camera key (retail would read past an empty track)");
		}
	}
	else if (record.type == "look")
	{
		c->m_free = false;
		for (const CameraAnimationFrame &k : record.keys)
		{
			LookCameraFrame f;
			f.type = k.interp;
			std::copy(k.pos, k.pos + 3, f.position);
			f.roll = k.roll;
			f.fov = k.fov;
			c->m_cameraTrack.setKey(k.frame, f);
		}
		for (const CameraLookAtKey &k : record.lookAtKeys)
		{
			LookAtFrame f;
			f.type = k.interp;
			std::copy(k.lookAt, k.lookAt + 3, f.target);
			c->m_targetTrack.setKey(k.frame, f);
		}
		if (c->m_cameraTrack.empty() || c->m_targetTrack.empty())
		{
			return fail("an empty camera or look-at track (retail would read past an empty track)");
		}
	}
	else
	{
		return fail("unknown type '" + record.type + "'");
	}
	return c;
}

FreeCameraFrame CameraAnimationClip::freeFrame(float time) const
{
	// RW 0x94674E: each channel through the current key's type; the frame keeps that type
	return m_freeTrack.frameData(time, [](float t, const FreeCameraFrame &a, int ta, const FreeCameraFrame &b, int tb, const FreeCameraFrame &c, int tc,
		const FreeCameraFrame &d, int td) {
		FreeCameraFrame r;
		CameraAnimationMath::interpolate3(b.type, t, a.position, ta, b.position, tb, c.position, tc, d.position, td, r.position);
		CameraAnimationMath::interpolateQuat(b.type, t, b.orientation, c.orientation, r.orientation);
		r.fov = CameraAnimationMath::interpolate(b.type, t, a.fov, ta, b.fov, tb, c.fov, tc, d.fov, td);
		r.type = b.type;
		return r;
	});
}

LookCameraFrame CameraAnimationClip::lookFrame(float time) const
{
	// RW 0x9476D2: the position, the roll (+ 0x10) and the FOV (+ 0x14)
	return m_cameraTrack.frameData(time, [](float t, const LookCameraFrame &a, int ta, const LookCameraFrame &b, int tb, const LookCameraFrame &c, int tc,
		const LookCameraFrame &d, int td) {
		LookCameraFrame r;
		CameraAnimationMath::interpolate3(b.type, t, a.position, ta, b.position, tb, c.position, tc, d.position, td, r.position);
		r.roll = CameraAnimationMath::interpolate(b.type, t, a.roll, ta, b.roll, tb, c.roll, tc, d.roll, td);
		r.fov = CameraAnimationMath::interpolate(b.type, t, a.fov, ta, b.fov, tb, c.fov, tc, d.fov, td);
		r.type = b.type;
		return r;
	});
}

LookAtFrame CameraAnimationClip::lookAtFrame(float time) const
{
	// RW 0x9477C5: the point through RW 0x9D6731, the current key's type
	return m_targetTrack.frameData(time, [](float t, const LookAtFrame &a, int ta, const LookAtFrame &b, int tb, const LookAtFrame &c, int tc,
		const LookAtFrame &d, int td) {
		LookAtFrame r;
		CameraAnimationMath::interpolate3(b.type, t, a.target, ta, b.target, tb, c.target, tc, d.target, td, r.target);
		r.type = b.type;
		return r;
	});
}

void CameraAnimationClip::getTransform(float time, Matrix3D &transform, float &fov) const
{
	if (m_free)
	{
		// RW 0x9471A5: Build_Matrix3D(orientation), the position as the translation
		const FreeCameraFrame f = freeFrame(time);
		transform = Build_Matrix3D(f.orientation[0], f.orientation[1], f.orientation[2], f.orientation[3]);
		transform.Set_Translation(Vector3(f.position[0], f.position[1], f.position[2]));
		fov = f.fov;
		return;
	}
	// RW 0x9488B3: Look_At(camera position, target, camera roll)
	const LookCameraFrame camera = lookFrame(time);
	const LookAtFrame target = lookAtFrame(time);
	transform = CameraAnimationMath::lookAt(camera.position, target.target, camera.roll);
	fov = camera.fov;
}

void CameraAnimationList::build(const std::vector<CameraAnimation> &records, std::vector<std::string> &errors)
{
	m_clips.clear();
	for (const CameraAnimation &r : records)
	{
		std::string err;
		std::unique_ptr<CameraAnimationClip> c = CameraAnimationClip::build(r, &err);
		if (!c)
		{
			errors.push_back(err);
			continue;
		}
		m_clips.push_back(std::move(c));
	}
}

const CameraAnimationClip *CameraAnimationList::find(const std::string &name) const
{
	for (const auto &c : m_clips)
	{
		if (c->name() == name)
		{
			return c.get();
		}
	}
	return nullptr;
}

bool CameraAnimationPlayback::cancelsAnimation(const std::string &action)
{
	// the actions whose view call writes the camera mode (CameraAnimation.h)
	static const std::set<std::string> s = { "MOVE_CAMERA_TO", "SETUP_CAMERA", "MOVE_CAMERA_ALONG_SPLINE_PATH", "MOVE_CAMERA_LOCATOR_ALONG_SPLINE_PATH",
		"RESET_CAMERA", "CAMERA_STOP_FOLLOW", "CAMERA_STOP_TETHER_NAMED", "CAMERA_SET_DEFAULT" };
	return s.count(action) != 0;
}

bool CameraAnimationPlayback::applyRequest(const CameraAnimationList &list, const std::string &action, const std::string &name)
{
	if (action == "MOVE_CAMERA_BY_ANIMATION")
	{
		return start(list, name);
	}
	if (cancelsAnimation(action) && active())
	{
		stop();
		return true;
	}
	return false;
}

bool CameraAnimationPlayback::start(const CameraAnimationList &list, const std::string &name)
{
	const CameraAnimationClip *c = list.find(name);
	if (!c)
	{
		return false; // RW 0x7BBB9D: no animation, no view call
	}
	m_clip = c;
	m_name = name;
	m_time = 0.0f;
	return true;
}

void CameraAnimationPlayback::stop()
{
	m_clip = nullptr;
	m_name.clear();
	m_time = 0.0f;
}

void CameraAnimationPlayback::clientFrame()
{
	if (!m_clip)
	{
		return;
	}
	// RW 0x710B64: past the frame count the player finishes at the count; RW 0x48AA83: a finished player is freed, mode 0
	const float t = m_time + 1.0f;
	const float end = (float)m_clip->numFrames();
	if (end < t)
	{
		stop();
		return;
	}
	m_time = t;
}

bool CameraAnimationPlayback::transform(double alpha, Matrix3D &tm, float &fov) const
{
	if (!m_clip)
	{
		return false;
	}
	float t = m_time + (float)std::min(std::max(alpha, 0.0), 1.0);
	const float end = (float)m_clip->numFrames();
	if (t > end)
	{
		t = end;
	}
	m_clip->getTransform(t, tm, fov);
	return true;
}
