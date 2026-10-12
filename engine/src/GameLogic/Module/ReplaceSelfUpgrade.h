// OpenBFME. GPL-3.0.
//
// ReplaceSelfUpgrade (lane FACT-A): an upgrade module that replaces its object with other objects in a line (the castle walls: a segment becomes a gate,
// a postern gate, a tower, a hub, a trebuchet or catapult emplacement). Without it the segment is not affected by its upgrade and the purchase is refused
// (UpgradeCommands, RW 0x76FBFB, Object::affectedByUpgrade).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; constructor RW 0x8BB174: the UpgradeModule base RW 0x863B8F, module vtable RW 0xC6F770, mux vtable RW 0xC6F728):
//   * data (createData via RW 0x8BBA3A, field table RW 0xC6F6E8 after the UpgradeModule table): ReplaceWith and AndThenAddA both append to one name list
//     (parseAsciiStringVectorAppend RW 0x42E59E, + 0x138): the pieces in order;
//   * wouldUpgrade (mux slot 2, RW 0x8BB23F, overriding RW 0x8D26F3): the base test; an object UNDER_CONSTRUCTION (status 2) that is not UNDERGOING_REPAIR
//     (status 0x14) refuses (RW 0x44DDEC); an empty list refuses; the pieces' half lengths (template + 0xC8, WallSpan::halfExtentY) are summed (a name the
//     thing factory does not know refuses); the line runs across the object's direction (RW 0x70B9E0: the basis' first column d; across = (-d.y, d.x, d.z)),
//     from the object's position + across * total; for each piece: the step = -half * across (SSE, the binary's order) is added, then the objects within
//     the piece's bounding circle radius (template + 0xB0) * 3.0 (RW 0xBDD42C) of the OBJECT's position (FROM_BOUNDINGSPHERE_3D, RW 0xA39340 on
//     ThePartitionManager) that are any of WALL_UPGRADE / WALL_HUB (RW 0x46E72F(0, 0x96, 0x9C) -> 0x797E8D) and whose geometry overlaps the piece's at the
//     object's position and angle (RW 0x67C5F3 -> RW 0xAD2CE0) refuse, unless the piece is a CASTLE_KEEP and the found one is not (template + 0x118 bit 28);
//     every geometry shape of the piece (template + 0xA0, shapes of 0x24 bytes) placed at the piece's point (RW 0xAD2380 -> 0xAD22F0, x87 PC24: see
//     shapePoint) must stand on ground (TheTerrainLogic vslot 0x18) within AllowedHeightVariationForBuilding (GlobalData + 0xA70, x87 fsub and the CRT
//     fabs) of the object's height; then the step is added again (the shape's point is written into the step's three slots, so the second add uses the
//     last shape's point: kept as the binary has it);
//   * upgradeImplementation (mux slot 10, RW 0x8BB5FA): the list (none: nothing); the object's position, angle and name (+ 0x88); the object leaves the world
//     (RW 0x68C18F), its AI stops (RW 0x68C6EE), its drawable hides, the pathfinder forgets it (RW 0x6E85FB) and TheGameLogic destroys it (RW 0x62BBAB): the
//     port's GameLogic::destroyObject does these; the total and the start as above (a piece whose template is unknown ends it all, after the destroy);
//     per piece: the step is added; every WALL_SEGMENT (RW 0x444D39(0, 0xBD)) within the piece's bounding circle radius * 0.5 (RW 0xBD869C) of the old
//     position (FROM_CENTER_3D) whose geometry overlaps the piece's there is killed (RW 0x698EC3(8, 0)); BuildAssistant slot 0x38 (buildObjectNow, RW
//     0xDE8200) builds the piece for the old object's controlling player with the old object as the builder (none made: the rest is skipped); the piece is
//     put at the point (RW 0x696E63) and gets the old name; the step is added again; the piece's paid cost (+ 0x33C) is the BuildCost (+ 0x34, int ->
//     float) of the first upgrade of the module's TriggeredBy mask (RW 0x66F468); then the wall links are carried over (RW 0x8BB96D .. 0x8BB9DF, stop S-1986).
// NOT PORTED (stop S-1986): the wall links: for every object of KindOf CASTLE_KEEP, BRIDGE or DEFENSIVE_WALL, the interface the first module answers through
// behaviour slot 0x80 (RW 0x68C3E6) is asked (vslot 0x5C) whether it holds the old object and then given the piece (vslot 0x48), and the old object's
// interface's holdings go to the piece's: no ported module answers that slot. The x87 fsin / fcos of the shape's point are SimMath's deterministic sin / cos
// (S-081).

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class ThingTemplate;

class ReplaceSelfUpgradeModuleData : public UpgradeModuleData
{
public:
	std::vector<std::string> m_pieces; ///< + 0x138 ReplaceWith / AndThenAddA, appended in order
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ReplaceSelfUpgrade : public UpgradeModule
{
public:
	ReplaceSelfUpgrade(Thing *thing, const ReplaceSelfUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	bool wouldUpgrade(const UpgradeMaskType &mask) const override; // RW 0x8BB23F
	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

protected:
	void upgradeImplementation() override; // RW 0x8BB5FA

private:
	// the pieces' templates (empty when one is unknown) and the sum of their half lengths
	bool pieces(std::vector<const ThingTemplate *> &out, float &total) const;
	const ReplaceSelfUpgradeModuleData *m_data;
};
