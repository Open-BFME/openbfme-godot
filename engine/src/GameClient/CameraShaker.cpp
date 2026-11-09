// OpenBFME. GPL-3.0. See CameraShaker.h.

#include "GameClient/CameraShaker.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
const float kMinOmega = 12.5f * 360.0f * kPi / 180.0f; // cameraShakesystem_shaker.cpp MIN_OMEGA (DEG_TO_RADF)
const float kMaxOmega = 15.0f * 360.0f * kPi / 180.0f; // MAX_OMEGA
const float kEndOmega = 360.0f * kPi / 180.0f;         // END_OMEGA
const float kAxisRotation[3] = { 7.5f * kPi / 180.0f, 15.0f * kPi / 180.0f, 5.0f * kPi / 180.0f }; // AXIS_ROTATION
} // namespace

float CameraShakerQueue::randomFloat() const
{
	m_rng = m_rng * 1664525u + 1013904223u;
	return (float)(m_rng >> 8) * (1.0f / 16777216.0f);
}

void CameraShakerQueue::add(const Coord3D &pos, float radius, float durationSeconds, float powerDegrees)
{
	addRadians(pos, radius, durationSeconds, powerDegrees * kPi / 180.0f); // DONOR BFME2 0x65DAD: power * PI / 180
}

void CameraShakerQueue::addRadians(const Coord3D &pos, float radius, float durationSeconds, float amplitudeRadians)
{
	CameraShakerFx s;
	s.position = pos;
	s.radius = radius;
	s.durationSeconds = durationSeconds;
	s.amplitudeRadians = amplitudeRadians;
	for (int i = 0; i < 3; ++i)
	{
		s.omega[i] = kMinOmega + (kMaxOmega - kMinOmega) * randomFloat(); // DONOR BFME2 0x65A39: Random_Float(MIN_OMEGA, MAX_OMEGA)
		s.phi[i] = 2.0f * kPi * randomFloat();                            // DONOR BFME2 0x65A39: Random_Float(0, 2 pi)
	}
	++m_additions;
	if (m_shakers.size() >= kMaxShakers)
	{
		m_shakers.erase(m_shakers.begin());
		++m_dropped;
	}
	m_shakers.push_back(s);
}

void CameraShakerQueue::anglesAt(const Coord3D &cameraPos, float out[3]) const
{
	out[0] = out[1] = out[2] = 0.0f;
	for (const CameraShakerFx &s : m_shakers)
	{
		const float dx = cameraPos.x - s.position.x, dy = cameraPos.y - s.position.y, dz = cameraPos.z - s.position.z;
		const float len2 = dx * dx + dy * dy + dz * dz;
		if (s.radius <= 0.0f || s.durationSeconds <= 0.0f || len2 > s.radius * s.radius)
		{
			continue;
		}
		// DONOR BFME2 0x65B09: intensity * (1 - distance / radius) * (1 - elapsed / duration)
		const float intensity = s.amplitudeRadians * (1.0f - std::sqrt(len2) / s.radius) * (1.0f - s.elapsed / s.durationSeconds);
		for (int i = 0; i < 3; ++i)
		{
			const float omega = s.omega[i] + (kEndOmega - s.omega[i]) * s.elapsed;
			out[i] += kAxisRotation[i] * intensity * std::sin(omega * s.elapsed + s.phi[i]);
			// the donor's WST 11/14/2002 fudge: three deltas of +-intensity / 2 per axis
			out[0] += (randomFloat() * 2.0f - 1.0f) * intensity * 0.5f;
			out[1] += (randomFloat() * 2.0f - 1.0f) * intensity * 0.5f;
			out[2] += (randomFloat() * 2.0f - 1.0f) * intensity * 0.5f;
		}
	}
}

size_t CameraShakerQueue::step(float dtSeconds)
{
	for (CameraShakerFx &s : m_shakers)
	{
		s.elapsed += dtSeconds;
	}
	m_shakers.erase(std::remove_if(m_shakers.begin(), m_shakers.end(), [](const CameraShakerFx &s) { return s.expired(); }), m_shakers.end());
	return m_shakers.size();
}

void CameraShakerQueue::clear()
{
	m_shakers.clear();
}
