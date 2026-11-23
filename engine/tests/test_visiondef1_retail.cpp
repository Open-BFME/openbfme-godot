// OpenBFME. GPL-3.0.
// Lane VISIONDEF-1: the vision height bonus's ground reference (Object + 0x1C4) as RotWK 2.01 keeps it, on real data (map mp fall back 4p).
//   * getVisionRange RW 0x68E43B / getShroudClearingRange RW 0x68E4E2 (BFME2 1.06 0x68DDE0 / 0x68DE87): range * (1 + clamp((z - ref) * PerFoot, Min, Max)).
//   * The ThingTemplate constructor (RW 0x73FB7A, BFME2 0x73E1B8): segments 8, radius 100, max +0.5, min -0.5 for a template without the rows. The
//     shipped INI sets VisionBonusTestRadius 200 and MaxVisionBonusPercent 300% on most units, never the segments or the minimum.
//   * ref: 0 in the constructor (RW 0x699A77); Object::reactToTransformChange (RW 0x69355D, BFME2 0x692D49) sets the object's own z while it is 0; RW
//     0x69264D (BFME2 0x691EB1) samples the ring of `segments` ground heights at `radius` again only when the pathfinder's updatePos (RW 0x8E26B7) gave
//     the unit a new position cell, with the angle step 6.282 / segments (RW 0xC12150).
// Before the lane the port sampled the ring on every integer-position change and had 0 for all four defaults (no height bonus, no penalty). SKIPs without
// the game files.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/AI/AIPathfind.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ShroudManager.h"
#include "GameLogic/Object/Object.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <string>

namespace
{
// the ring mean RW 0x69264D computes, written out again here from the retail listing (RW 0x692715 .. 0x6927F9)
float ringMean(LiveGame &game, const Object &o, const ShroudManager::TemplateVision &tv)
{
	const Pathfinder &pf = game.ai().pathfinder();
	const PathfindLayerEnum layer = const_cast<Object &>(o).getAIUpdateInterface()->adapter().getLayer();
	const float step = SimMath::sseDiv(6.282f, SimMath::sseFromInt32(tv.bonusTestSegments));
	float angle = 0.0f, sum = 0.0f;
	for (int i = 0; i < tv.bonusTestSegments; ++i)
	{
		double s, c;
		SimMath::sinCosDet((double)angle, s, c);
		const float px = (float)SimMath::addD(SimMath::mulD(c, (double)tv.bonusTestRadius), (double)o.getPosition()->x);
		const float py = (float)SimMath::addD(SimMath::mulD(s, (double)tv.bonusTestRadius), (double)o.getPosition()->y);
		sum = SimMath::pc24Add(pf.getLayerHeight(px, py, layer), sum);
		angle = SimMath::sseAdd(angle, step);
	}
	return SimMath::sseDiv(sum, SimMath::sseFromInt32(tv.bonusTestSegments));
}
} // namespace

TEST_CASE("visiondef1 retail: the vision ground reference is the unit's own z until the pathfinder gives it a new cell, then the ring mean")
{
	OPENBFME_REQUIRE_START(s);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 1;
	o.slots.players.push_back({ "Player_1", "FactionDwarves", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	Player *p1 = game.players().findPlayerWithName("Player_1");
	REQUIRE(p1);
	const Waypoint *w = game.logic().terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(w);
	Coord3D at = w->location;
	at.x += 300.0f;
	at.z = game.logic().getGroundHeight(at.x, at.y);
	Object *cat = game.createObject("DwarvenCatapult", p1->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(cat, err);
	const ThingTemplate *tt = static_cast<const ThingTemplate *>(cat->getTemplate())->getFinalOverride();
	const ShroudManager::TemplateVision tv = ShroudManager::templateVision(*tt);
	// the INI's rows (catapult.ini: VisionBonusPercentPerFoot 1%, VisionBonusTestRadius 200, MaxVisionBonusPercent 300%), the constructor's segments and minimum
	CHECK(tv.bonusPerFoot == 0.01f);
	CHECK(tv.bonusTestRadius == 200.0f);
	CHECK(tv.maxBonus == 3.0f);
	CHECK(tv.bonusTestSegments == 8);
	CHECK(tv.minBonus == -0.5f);
	// the creation's first position change set + 0x1C4 from 0 to the unit's z (RW 0x6935EC), and the pathfinder registered the unit's first cell there, so
	// the ring was sampled at once (RW 0x69264D)
	const float z0 = cat->visionGroundReference();
	CHECK(z0 == ringMean(game, *cat, tv));
	MESSAGE("created at ground z " << at.z << ": z " << cat->getPosition()->z << ", vision ground reference " << z0);
	game.advance(0.2);
	ShroudManager *sm = game.logic().shroud();
	REQUIRE(sm);
	const float base = ShroudManager::templateVision(*tt).shroudClearingRange;
	const auto expectedRange = [&]() {
		float bonus = SimMath::pc24Mul(SimMath::pc24Sub(cat->getPosition()->z, cat->visionGroundReference()), tv.bonusPerFoot);
		bonus = bonus > tv.maxBonus ? tv.maxBonus : bonus;
		bonus = tv.minBonus > bonus ? tv.minBonus : bonus;
		return SimMath::pc24Mul(SimMath::pc24Add(bonus, 1.0f), base);
	};
	CHECK(sm->visionRange(*cat) == expectedRange());

	// a move inside the same pathfind cell (10 ft cells: from the cell's centre by 2 ft): the integer position changes, the cell does not, ref stays
	Coord3D c = *cat->getPosition();
	c.x = SimMath::addf32(SimMath::mulf32(std::floor(c.x / 10.0f), 10.0f), 5.0f);
	c.z = game.logic().getGroundHeight(c.x, c.y);
	cat->setPosition(&c);
	const float afterCentre = cat->visionGroundReference();
	Coord3D d = c;
	d.x = SimMath::addf32(c.x, 2.0f);
	d.z = game.logic().getGroundHeight(d.x, d.y);
	cat->setPosition(&d);
	CHECK(cat->visionGroundReference() == afterCentre);

	// a move to a cell 30 ft east: the pathfinder takes a new cell, the ring is sampled at the new position
	Coord3D e = d;
	e.x = SimMath::addf32(d.x, 30.0f);
	e.z = game.logic().getGroundHeight(e.x, e.y);
	cat->setPosition(&e);
	CHECK(cat->visionGroundReference() == ringMean(game, *cat, tv));

	// the range follows: 1 + clamp((z - ref) * PerFoot, -0.5, Max)
	CHECK(sm->visionRange(*cat) == expectedRange());
	MESSAGE("after the moves: z " << cat->getPosition()->z << ", vision ground reference " << cat->visionGroundReference() << ", range " << sm->visionRange(*cat));
}
