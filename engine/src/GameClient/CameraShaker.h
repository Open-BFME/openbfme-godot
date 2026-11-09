// OpenBFME. GPL-3.0.
//
// CameraShakerVolume's client state (TARGET call RW 0x5DFE90 / 0x5DFECE; DONOR BFME2 0x65DAD .. 0x65B09, FXNuggetSlotBodies.cpp and camerashakesystem_shaker.cpp).
// Kept out of ClientEffects.h so TacticalCamera can include it without the FXList header (whose ModelConditionFlags typedef cannot share a
// translation unit with the object model's).
//
// TARGET FACTS (RotWK game.dat, caveat S-001) and DONOR facts (BFME2 1.06 decomp bodies)
//   * CameraShakerVolume -> TheTacticalView vslot 0xA8 = View::Add_Camera_Shake (TARGET RW 0x5DFE90 / 0x5DFECE; DONOR BFME2 0x65DAD): power is in degrees of amplitude, converted by
//     power * PI / 180, and a shaker joins the list.
//   * CameraShakerClass's constructor (DONOR BFME2 0x65A39) draws Omega[i] = Random_Float(12.5 * 360 deg, 15 * 360 deg) and Phi[i] = Random_Float(0, 2 pi)
//     for the three axes.
//   * Compute_Rotations (DONOR BFME2 0x65B09): for a camera inside Radius, intensity = amplitude * (1 - distance / Radius) * (1 - elapsed / Duration); for
//     each axis, angle += AXIS_ROTATION[i] * intensity * sin((Omega[i] + (END_OMEGA - Omega[i]) * elapsed) * elapsed + Phi[i]) with AXIS_ROTATION
//     (7.5, 15, 5) degrees, then a fudge of three random values in +-intensity / 2 (the donor's WST 11/14/2002 addition).
//
// INFERENCE (stop S-687): retail's WWMath::Random_Float is the client's own generator, whose state is not the game client stream's and is not
// reproduced; this queue uses a private fixed-seed LCG so a replay's presentation is repeatable. Presentation only: no simulation state.

#pragma once

#include "Common/INIDataTypes.h"

#include <cstdint>
#include <vector>

struct CameraShakerFx
{
	Coord3D position;
	float radius = 0.0f;
	float durationSeconds = 0.0f;
	float amplitudeRadians = 0.0f;
	float elapsed = 0.0f;
	float omega[3] = { 0.0f, 0.0f, 0.0f };
	float phi[3] = { 0.0f, 0.0f, 0.0f };
	bool expired() const { return elapsed >= durationSeconds; }
};

class CameraShakerQueue
{
public:
	static constexpr size_t kMaxShakers = 32;
	// DONOR BFME2 0x65DAD (via TARGET RW 0x5DFE90): power is in degrees of amplitude; the constructor draws the sinusoid frequencies and phases (DONOR BFME2 0x65A39)
	void add(const Coord3D &pos, float radius, float durationSeconds, float powerDegrees);
	// the same with the amplitude already in radians (the FXList nugget's value goes through the playback with it)
	void addRadians(const Coord3D &pos, float radius, float durationSeconds, float amplitudeRadians);
	// the rotations the active shakers add at the camera position (DONOR BFME2 0x65B09), in radians, presentation only
	void anglesAt(const Coord3D &cameraPos, float out[3]) const;
	// one 1/30 s client frame: the elapsed time of every shaker, then the expired ones go; returns how many remain
	size_t step(float dtSeconds);
	const std::vector<CameraShakerFx> &shakers() const { return m_shakers; }
	unsigned long long additions() const { return m_additions; }
	unsigned long long dropped() const { return m_dropped; }
	void clear();

private:
	float randomFloat() const; // [0, 1): the client's own generator (INFERENCE, S-687: not TheGameClient's stream)
	std::vector<CameraShakerFx> m_shakers;
	mutable std::uint32_t m_rng = 0x9E3779B9u;
	unsigned long long m_additions = 0, m_dropped = 0;
};
