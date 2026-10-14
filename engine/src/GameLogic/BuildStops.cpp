// OpenBFME. GPL-3.0.
// See GameLogic/BuildStops.h.

#include "GameLogic/BuildStops.h"

#include "GameLogic/BuildCommands.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Construction.h"
#include "GameLogic/FindPositionAround.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/WallSpan.h"

std::vector<std::string> BuildStops::lines()
{
	std::vector<std::string> out = {
		"[S-300] castle layouts: a CastleBehavior asks the CastleTemplateStore for the file Bases/<name>/<name>.bse when it unpacks (retail fills the store from a name list at start, RW 0x7311FB: same files, "
		"same parser RW 0x731010); a missing or unparsable file is an error. The maps-and-terrain spec's claim that no CastleTemplates chunk exists in the corpus is wrong: all 207 retail .bse files have one",
		"[S-303] castle: the instant unpack of RW 0x79C265 (the starting fortress, the camps and castles of the maps) is ported; the options 5 of the legality call RW 0x797A96 are taken as NO_OBJECT_OVERLAP "
		"(its clear-path half needs an AI the castle does not have); ported since: PreBuiltList and the castle after its unpack (CASTLE-1, S-951 / S-952), CastleUpgrade "
		"(FACT-B), the creation interface's site clearing RW 0x858C41 and push out RW 0x858CFC around every member (BACKLOG2); not ported: the creation interface "
		"RW 0x859198's own construct for a member (slot 7, RW 0x858701 with the castle as the plot: the port places the member itself), FactionDecal, the damage "
		"evacuation of a member, WallUpgradeUpdate",
		"[S-304] dozer (lane BUILD-3): RotWK's dozer machines are ported (DozerAIUpdate.h: the tasks and dock points, newTask RW 0x88CCFC with the dock position "
		"RW 0x88C2FE, the primary machine RW 0x88C06B, PickActionPos / MoveToActionPos / DoAction RW 0x88D7D2 / 0x88C5C7 / 0x88D993); INFERENCE: the structure is "
		"made at the order with every BUILD-1 effect (RW makes it PHANTOM and places it on arrival, RW 0x88D44F: the port keeps the placement's health of 1 and "
		"removes the unplaced structure with a refund when the task is cancelled before arrival, but does not repeat its legality test, flags 0x15)",
		"[S-306] placement mode (lane BUILD-5: RotWK's PlaceEventTranslator RW 0x83E486 and placement update RW 0x6A2AE5 are ported: the right button is tracked "
		"and passed on (right-drag scrolls), a right click cancels unless it was a drag held DragToleranceMS, the left press anchors, a 5 pixel drag turns the ghost, "
		"the left click places with the options 0x49F or shows the code's label and BadBuildingPlacementSound, no shift to keep placing, the ghost's legality "
		"on odd client frames with 0x48F; r2: a PORT's site and angle snap to the shore (getPortTransform RW 0x794C63) and a PORT's footprint must straddle water "
		"and land, ratio 6 at most (RW 0x796C0B)); not ported: the options' bits 0x80 and 0x400, the message 0x7EB of a refused click and the one RW 0x83EA10 appends "
		"after the construct, the PlacementProjection start point of getPlacementPoints, Escape (kept from BUILD-1: RotWK's translator has no key case); FINDING "
		"(BFME2 decomp BuildAssistant.cpp:1961, tier B for RW 0x796810): RotWK numbers the options 0x01 terrain restrictions, 0x02 clear path, 0x04 object "
		"overlap, 0x08 quick path, 0x20 enemy overlap, where LocationLegalFlags keeps ZH's order (0x01 clear path, 0x02 terrain): the castle unpack's options 5 "
		"(RW 0x7987EE) mean terrain + overlap in RotWK, clear path + overlap here (a simulation change, left to BUILD-1's S-301); a BASE_FOUNDATION builder sends "
		"MSG_FOUNDATION_CONSTRUCT (BUILD-1) where RotWK's click sends 0x41A; a plot's FOUNDATION_CONSTRUCT button builds at once on the plot with no ghost, as "
		"retail does (a retail 2.01 run on the owner's PC, BUILD-5 r2)",
		"[S-307] walls: BUILD-2 ported WallHubBehavior and the span a hub builds (MSG_WALL_HUB_CONSTRUCT_SPAN); what is still open is S-657. Repair is ported (S-656)",
	};
	for (const std::string &s : CastleBehavior::stopLines()) // lane CASTLE-1
	{
		out.push_back(s);
	}
	for (const std::string &s : Construction::stopLines())
	{
		out.push_back(s);
	}
	for (const std::string &s : WallSpan::stopLines())
	{
		out.push_back(s);
	}
	for (const std::string &s : BuildPlacement::stopLines())
	{
		out.push_back(s);
	}
	for (const std::string &s : FindPosition::stopLines()) // lane BUILD-3 (S-1280)
	{
		out.push_back(s);
	}
	out.push_back("[S-1281] contact points (lane BUILD-3): GeometryContactPoint rows (RW 0xAD3D30) and getBestContactPoint mode 0 with a preferred call (RW 0xAD30E0, "
	              "via Object::getWorldspaceBestContactPoint RW 0x690BD2 and AIUpdateInterface::findNearestLabeledContactPointOnTarget RW 0x667C76) are ported; the "
	              "shapes and points are the template's (retail: the object's own GeometryInfo, which a GeometryUpgrade can change); the other modes (1: the highest "
	              "point, 3: a random point) and a call without `preferred` (the overlap test) are refused; the label compare is taken as exact bytes (RW 0x406585)");
	out.push_back("[S-1282] dozer machine gaps (lane BUILD-3): not ported: the client side of a porter's stay inside the structure it builds (RW 0x88C51B / "
	              "0x88D6AE: the selection removed, the fade in over BuilderFadeInTime), the face command 0x26 before the work (AI state 36), the bridge tower branch of the dock position "
	              "(RW 0x88CC08), the bored wander of an AI player's idle dozer (RW 0x88E17F), the ActionManager's canRepairObject / canResumeConstructionOf (RW "
	              "0x82D7F2 / 0x82C84C: the owner's finished resp. unfinished structure stands for them), the script engine's note at the arrival (RW 0x756C87); the "
	              "idle worker list is the dozer's marked flag (TheInGameUI vslots 0x1A4 / 0x1A8); the primary machine reset after a PLAYER command runs before the "
	              "command (RW: after it); the AI player's 'dozer free' call (slot 0x7C) follows the completion tail RW 0x88DBB3 and is also made when a task is "
	              "cancelled (S-890); AIMover::locomotorDistanceToGoal measures a one-node path (RotWK: 100 only without a path, RW 0x6641A3) but keeps the port's 100 "
	              "for a longer path without a closest node");
	for (const std::string &s : BuildCommands::acceptanceStops())
	{
		out.push_back(s);
	}
	return out;
}
