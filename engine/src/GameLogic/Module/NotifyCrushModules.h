// OpenBFME. GPL-3.0.
//
// NotifyTargetsOfImminentProbableCrushingUpdate and HordeNotifyTargetsOfImminentProbableCrushingUpdate (lane MODULES-3): a crusher about to run units over warns
// them (they brace: EMOTION_BRACE_FOR_BEING_CRUSHED) and cheers itself (EMOTION_CHEER_FOR_ABOUT_TO_CRUSH). RotWK only, ported from the binary (caveat S-001).
//
// TARGET FACTS (RotWK game.dat):
//   * module data (RW 0x653AF3 -> 0x8D2ADB, table RW 0xC76C48 at data + 8): TimeBetweenUpdatesMS (+ 8) and ScanAheadTimeMS (+ 0xC) parseDurationUnsignedInt
//     (RW 0x73A429, default 2 * LOGICFRAMES_PER_SECOND), ScanHeight (+ 0x10) and ScanWidth (+ 0x14) parseReal (RW 0x42ED00, default -1).
//   * the mux RW 0x8D2B21(object, data, &sleep), true when it built the scan box: the next update is RandomInt(delay, (3 * delay) >> 1)
//     ("NotifyTargetsOfImminentProbableCrushingMux.cpp" line 0x3F), drawn first; then the object needs a current locomotor (RW 0x68B31D) whose speed (RW 0x5E36F7) is
//     not 0, crusherLevel > 0 (RW 0x695070), canCrush (RW 0x68D524) and ScanAheadTime != 0. The velocity (RW 0x68EF58: the pending position - the position while one is
//     pending (+ 0x1A6); else (position - the recorded position) / (now - the recorded frame) after a later frame; else (position - the previous position) /
//     (now - recorded frame + 1); else 0) times ScanAheadTime (cvtsi2ss, mulss) must be at least 1 long (Coord3D::GetLengthEstimate2D RW 0x403720: |the larger| +
//     0.25 |the smaller|). The box (a BOX geometry, RW 0x450429): centre the midpoint of the position and position + scan, lowered by a third of its height, turned
//     by atan2(scan.y, scan.x) (RW 0x441BD2); height ScanHeight, else 3 * the object's geometry height (RW 0xAD1920); half length (estimate + the geometry's x extent)
//     / 2, half width ScanWidth / 2, else the geometry's z extent / 2 (RW 0xAD2040: hi.z - lo.z, as written). ThePartitionManager's region query (RW 0xA393C0 ->
//     0xA3C4E0) over the box's 2D bounds (RW 0xAD1D60), unsorted, with the filters crushPolicy(object, other, 2) (RW 0xC76CD8 -> 0x69519A), the box's geometry
//     overlap with the other's (RW 0xC112C8 -> RW 0xAD2CE0) and "alive" (RW 0xC10E20: Object + 0x458 bit 0 clear). Any hit: the object requests
//     CHEER_FOR_ABOUT_TO_CRUSH (11, no source, delay 1: RW 0x68F37F); every hit requests BRACE_FOR_BEING_CRUSHED (10) from the object (RW 0x69035C).
//   * NotifyTargetsOfImminentProbableCrushingUpdate::update RW 0x8D3225: the mux on the object, the sleep it drew.
//   * HordeNotifyTargetsOfImminentProbableCrushingUpdate::update RW 0x8D3167: without a horde contain interface sleep 0x3FFFFFFF; else the members (contain slot 0x10C)
//     sorted nearest to the horde first (std::list::sort, stable, RW 0x8D2F92 with RW 0x8D2EC6), the mux on each until one builds its box; the sleep is the last draw
//     (TimeBetweenUpdates when there is no member).
//   * RW 0xAD2CE0 (the shape overlap): every active shape pair (the shape's world position RW 0xAD22F0: position + the offset turned by the angle, z + offset z) whose
//     heights meet (other.z - (a sphere's radius + offset z) <= this shape's z + (sphere radius / cylinder or box height) + offset z, and this shape's z <= the other's z +
//     its height + offset z); then circle / circle (RW 0xAD48F0: centre distance^2 <= (r1 + r2)^2), box / circle (RW 0xAD4870 -> 0xAD45A0: the circle's centre's
//     squared distance from the box, axes (cos, sin) / (-sin, cos) stored as floats), box / box (RW 0xAD4790 -> 0xAD4660: the four axes' separation test).
// INFERENCE (stop S-1029): crushPolicy is PHYS-1's reading of RW 0x69519A (the line test RW 0x6F5BB0 not ported); the x87 sin / cos are SimMath::sinCosDet;
// the members of the horde's map + 0x170 are not ported (the horde's contained list only).

#pragma once

#include "Common/GameCommon.h"
#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Object/ObjectGeometry.h"

#include <string>
#include <vector>

class ModuleFactory;
class StateHasher;

class NotifyCrushModuleData : public ModuleData
{
public:
	unsigned m_timeBetweenUpdates = 2u * (unsigned)LOGICFRAMES_PER_SECOND; ///< + 8 (frames)
	unsigned m_scanAheadTime = 2u * (unsigned)LOGICFRAMES_PER_SECOND;      ///< + 0xC (frames)
	float m_scanHeight = -1.0f;                                            ///< + 0x10
	float m_scanWidth = -1.0f;                                             ///< + 0x14
	static void buildFieldParse(MultiIniFieldParse &p);
};

class NotifyCrushUpdate : public UpdateModule
{
public:
	NotifyCrushUpdate(Thing *thing, const NotifyCrushModuleData *data, bool horde);
	UpdateSleepTime update() override; ///< RW 0x8D3225 / 0x8D3167
	void crc(StateHasher &h) const override;
	unsigned scans() const { return m_scans; }     ///< boxes built
	unsigned warned() const { return m_warned; }   ///< brace requests sent
	static std::vector<std::string> stopLines();

private:
	bool mux(Object &obj, unsigned &sleep); ///< RW 0x8D2B21
	const NotifyCrushModuleData *m_data;
	bool m_horde;
	unsigned m_scans = 0;
	unsigned m_warned = 0;
};

namespace NotifyCrushModules
{
void registerAll(ModuleFactory &modules);
// RW 0xAD2CE0: a shape of `a` (at `posA`, turned by `angleA`) overlaps a shape of `b`
bool geometriesOverlap(const std::vector<ObjectGeometry::Shape> &a, const Coord3D &posA, float angleA, const std::vector<ObjectGeometry::Shape> &b, const Coord3D &posB,
                       float angleB);
} // namespace NotifyCrushModules
