// OpenBFME. GPL-3.0.
//
// The aerial path of the GIANT_BIRD flyers (lane FLY-1): the spline a GiantBirdAIUpdate flies along (RW AI + 0x58C) and TheAerialPathfinder's queries that
// build and test it. No Zero Hour counterpart; BFME2 1.06 has the same code (the decomp names are address names: Rva00312C95 for the spline,
// AerialPathfinder::rva00375A73 ..., Code/GameEngine/Source/Common/System and GameLogic/AI/AerialPathfinder*.cpp; donor only).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; every function below is tier A or B same-shape against BFME2, read in Ghidra):
//   * the spline (ctor RW 0x714563, vtable RW 0xBDD438): a vector of 0xB8 byte records at + 0x2C (duration, ten sample lengths, ten sample points, the
//     control point at + 0xA4, two tail words); the time + 0x18, the segment start + 0x1C, the segment + 0x20, the valid byte + 0x24, the total length + 0x38,
//     the sample start + 0x3C, the sample + 0x40, the evaluated point + 0x48, the end point + 0x54. vtable + 8 prepare (RW 0x712D75), + 0xC clear (RW 0x71468A),
//     + 0x10 insert after the segment (RW 0x7148D6: at index segment + 2), + 0x18 append (RW 0x7149A1);
//   * build (RW 0x7139E6): a Catmull-Rom segment between the control points i + 1 and i + 2 for every i < count - 3, sampled at t = 0.1 .. 1.0 (SSE binary32,
//     0.5 * ((P2 - P0) t + 2 P1 + (2 P0 - 5 P1 + 4 P2 - P3) t^2 + (3 P1 - P0 - 3 P2 + P3) t^3)), each sample's chord (x87 fsqrt of the binary32 sum of
//     squares) is its length, the record's duration their sum; the end point is the control point count - 2;
//   * evaluate (RW 0x713242): the point at arc length `time` (linear between the samples); false past the last segment (segment + 4 > count);
//   * fixEndpoints (RW 0x71343E): the first and the last control point are put on the line through their neighbour, at the distance of the next one;
//   * query (RW 0x777DAE): prepare (1, 4000, 1000, 1000, 0, 0), time = d, evaluate; false for d < 0 or an invalid spline;
//   * the flight ground height (RW 0x777ED7): max(terrain height, the no-fly-zone height RW 0x777E54), raised to AIData MinFlightHeight (+ 0xB0) unless that
//     is FLT_MAX;
//   * the probe (RW 0x77824E): five ground samples (the centre at z - h / 2, the four corners of the bounding sphere radius at z - h / 2 * 0.777) give the worst
//     ground intrusion as a push up, the separation (RW 0x777F9F) adds the overlap of the objects around; 2 without an AI, 1 when anything intrudes, else 0;
//   * the separation (RW 0x777F9F, BFME2 decomp AerialPathfinder.cpp rva00375C28): the objects within the bounding sphere radius (FROM_BOUNDINGSPHERE_3D)
//     whose geometry would collide (PartitionFilterWouldCollide), not the flyer, not its victim (AI vslot 0x184 -> + 0x584), not INFANTRY / CAVALRY / HORDE;
//     a mobile one only within 1.2 radius; the push is the sum of the unit directions scaled by the overlap (2 r_other + r_self - distance);
//   * the path check (RW 0x778176): the point `d` along the path, lowered by half the clearance, is above the terrain and clear of the separation;
//   * computeAerialPath (RW 0x778413, "computeAerialPath"): the control points (a point behind the flyer, the flyer, the goal, a terminal point beyond the
//     goal, see the .cpp), then up to five corrective points where the sampled path hits something.
//
// NOT PORTED / INFERENCE (stop S-4041, AerialPathfinder::stopLines): the no-fly zones (the map INI block AerialPathfindNoFlyZone is a recording stub: the list is
// empty, RW 0x777E54 then answers 0); AIData MinFlightHeight is not read (absent from the retail INI: the retail default FLT_MAX is used); the would-collide
// filter of the separation tests the 2D footprints (BuildPlacement::footprintsCollide) because the shape overlap test RW 0xAD2CE0 is not ported (S-1020);
// the x87 parts (the probe's products, the terminal radius) run through the NumericState PC24 operations (S-081).
//
// Determinism: binary32 SSE operations through SimMath in the retail order, the PC24 sqrt where retail uses fsqrt, no allocation per evaluation.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <string>
#include <vector>

class GameLogic;
class Object;
class StateHasher;

class AerialSpline
{
public:
	static constexpr int kSamples = 10;
	struct Record
	{
		float duration = 0.0f;           // + 0x00
		float steps[kSamples] = {};      // + 0x04
		Coord3D samples[kSamples] = {};  // + 0x2C
		Coord3D point{ 0.0f, 0.0f, 0.0f }; // + 0xA4: the control point
	};

	// RW 0x71468A (vtable + 0xC): the control points go
	void clear() { m_records.clear(); }
	// RW 0x7149A1 (vtable + 0x18)
	void append(const Coord3D &p);
	// RW 0x7148D6 (vtable + 0x10): a control point inserted at index segment + 2
	void insertAfterSegment(const Coord3D &p);
	// RW 0x712D75 (vtable + 8) with the aerial path's arguments (1, 4000, 1000.0, 1000.0, 0, 0): the walk starts again, the spline is valid
	void prepare();
	// RW 0x7139E6
	void build();
	// RW 0x713242(spline, 0, null): the point at the current time; false past the end
	bool evaluate();
	// RW 0x71343E
	void fixEndpoints();
	// RW 0x713814 (RotWK only): an interior control point (index 2 .. count - 3) lies within `radius` of `p` in the ground plane
	bool nearInteriorPoint(const Coord3D &p, float radius) const;

	float time() const { return m_time; }
	void setTime(float t) { m_time = t; }
	bool valid() const { return m_valid; }
	const Coord3D &result() const { return m_result; }
	const Coord3D &endPoint() const { return m_endPoint; }
	size_t controlPointCount() const { return m_records.size(); }
	const Coord3D &controlPoint(size_t i) const { return m_records[i].point; }
	const std::vector<Record> &records() const { return m_records; }

	void crc(StateHasher &h) const;

private:
	std::vector<Record> m_records; // + 0x2C
	float m_time = 0.0f;           // + 0x18
	float m_segmentBegin = 0.0f;   // + 0x1C
	unsigned m_segment = 0;        // + 0x20
	bool m_valid = false;          // + 0x24
	float m_totalLength = 0.0f;    // + 0x38 (summed by build, never reset by the aerial prepare)
	float m_sampleBegin = 0.0f;    // + 0x3C
	int m_sample = 0;              // + 0x40
	Coord3D m_result{ 0.0f, 0.0f, 0.0f };   // + 0x48
	Coord3D m_endPoint{ 0.0f, 0.0f, 0.0f }; // + 0x54
};

namespace AerialPathfinder
{
// the path masks of GiantBirdAIUpdate's constructor (RW 0x76D40F ORs them into the globals RW 0xDE7908 .. 0xDE7918)
enum : unsigned
{
	MASK_A = 0x28,              // RW 0xDE7908
	MASK_A_LOWER_START = 0x2C,  // RW 0xDE790C (bit 2: a flyer above the terrain starts below its behind point)
	MASK_A_LOWER_END = 0x2A,    // RW 0xDE7910 (bit 1: the terminal point lowered)
	MASK_A_BOTH = 0x2E,         // RW 0xDE7914
	MASK_DIRECT = 0x88          // RW 0xDE7918 (bit 7: the behind point at 1000 over the flyer; bit 3)
};

// the flyer's view the queries need (GiantBirdAIUpdate supplies it)
struct Flyer
{
	Object *object = nullptr;
	bool hasAI = true;            // RW obj + 0x260 != 0
	float preferredHeight = 0.0f; // the current locomotor's + 0x48 (its template's PreferredHeight)
	ObjectID partner = INVALID_ID; // AI vslot 0x184 -> + 0x584: the current victim, ignored by the separation
	unsigned surfaces = 0; // AI + 0x1CC (LocomotorSurfaceType bits), for the goal snap
};

// RW 0x777DAE
bool query(AerialSpline &spline, float distance, Coord3D &out);
// RW 0x777ED7
float flightGroundHeight(const GameLogic &logic, float x, float y);
// RW 0x777F9F: true when nothing is in the way; `worst` the largest overlap, `push` the summed push
bool separation(const Flyer &flyer, const Coord3D &pos, float &worst, Coord3D &push);
// RW 0x77824E: 0 clear, 1 something intrudes (push set), 2 no AI
int probe(const Flyer &flyer, const Coord3D &pos, Coord3D &push);
// RW 0x778176
bool pathClear(const Flyer &flyer, AerialSpline &spline, float distance, float clearance);
// RW 0x778413: false only when `snap` is set and the flyer is not in the logic (RW 0x77848B)
bool computeAerialPath(const Flyer &flyer, const Coord3D &goal, AerialSpline &spline, unsigned mask, const Coord3D *lookAhead, bool snap);

std::vector<std::string> stopLines();
} // namespace AerialPathfinder
