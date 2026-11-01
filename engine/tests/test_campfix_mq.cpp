// OpenBFME. GPL-3.0.
// Lane CAMPFIX-MQ (from lane BFME2CAMP1): a horde's new team reaches its members (Object::setTeam RW 0x69954A -> horde interface slot 0x170, RW 0x870667),
// and a quoted GeometryName loses its quotes (INI::parseAsciiString -> getNextAsciiString), so the gates' "Closed" / "OpenLeft" / "OpenRight" shapes match.

#include "doctest.h"
#include "CombatTestUtil.h"
#include "ScriptTestUtil.h"

#include "Common/Player.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectGeometry.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ScriptEngine/ScriptActions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include <string>
#include <vector>

using namespace scripttest;

namespace
{
void startEngine(GameLogic &logic)
{
	logic.partition().setRegion(0.0f, 0.0f, 1000.0f, 1000.0f);
	ScriptEngine::SideScripts side;
	side.sideName = "";
	static ScriptList empty;
	side.lists.push_back(&empty);
	logic.scriptEngine().newGame({ side }, nullptr, nullptr, false);
}

std::vector<Object *> membersOf(GameLogic &logic, const Object *horde)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getContainedBy() == horde)
		{
			out.push_back(o);
		}
	}
	return out;
}

// RivGate's geometry rows (rivendellbuildings.ini), the names quoted as the retail file writes them
const char kQuotedGate[] = "Object QuotedGate\n"
						   "  KindOf = STRUCTURE IMMOBILE\n"
						   "  Geometry = BOX\n"
						   "  GeometryMajorRadius = 45.0\n"
						   "  GeometryMinorRadius = 40.0\n"
						   "  GeometryHeight = 65.0\n"
						   "  AdditionalGeometry = BOX\n"
						   "  GeometryName = \"Closed\"\n"
						   "  GeometryMajorRadius = 5.0\n"
						   "  GeometryMinorRadius = 60.0\n"
						   "  GeometryHeight = 65.0\n"
						   "  AdditionalGeometry = BOX\n"
						   "  GeometryName = \"OpenLeft\"\n"
						   "  GeometryMajorRadius = 45.0\n"
						   "  GeometryMinorRadius = 10.0\n"
						   "  GeometryHeight = 65.0\n"
						   "  AdditionalGeometry = BOX\n"
						   "  GeometryName = OpenRight\n"
						   "  GeometryMajorRadius = 45.0\n"
						   "  GeometryMinorRadius = 10.0\n"
						   "  GeometryHeight = 65.0\n"
						   "End\n";
} // namespace

TEST_CASE("campfix-mq Object::setTeam (RW 0x69954A) hands a horde's new team to its members (horde interface slot 0x170, RW 0x870667): UNIT_SET_TEAM on a "
		  "horde gives its members to the new owner; a null team changes none of them")
{
	combattest::CombatWorld w;
	startEngine(*w.logic);
	ScriptEngine &e = w.logic->scriptEngine();
	Object *h = w.unit("SwordHorde", 'B', 500.0f, 500.0f);
	w.frames(2);
	h->setName("H");
	const std::vector<Object *> before = membersOf(*w.logic, h);
	REQUIRE_FALSE(before.empty());
	for (Object *m : before)
	{
		REQUIRE(m->getTeam() == w.teamB());
	}
	const std::string teamA = w.teamA()->getControllingPlayer()->getPlayerName() + "/" + w.teamA()->getName();
	ScriptActions::execute(e, act("UNIT_SET_TEAM", { sp(14, "H"), sp(3, teamA) }));
	CHECK(h->getTeam() == w.teamA());
	const std::vector<Object *> after = membersOf(*w.logic, h);
	CHECK(after.size() == before.size());
	for (Object *m : after)
	{
		CHECK(m->getTeam() == w.teamA());
		CHECK(m->getControllingPlayer() == w.teamA()->getControllingPlayer());
	}
	h->setTeam(nullptr);
	for (Object *m : after)
	{
		CHECK(m->getTeam() == w.teamA());
	}
}

TEST_CASE("campfix-mq a quoted GeometryName is read as INI::parseAsciiString reads it (getNextAsciiString drops the quotes): RivGate's and the castle wall "
		  "gates' \"Closed\" / \"OpenLeft\" are the names GateOpenAndCloseBehavior's defaults toggle")
{
	combattest::CombatWorld w(kQuotedGate);
	const ThingTemplate *tt = w.logic->things().findTemplate("QuotedGate");
	REQUIRE(tt != nullptr);
	const std::vector<ObjectGeometry::Shape> shapes = ObjectGeometry::shapesOf(*tt);
	REQUIRE(shapes.size() == 4);
	CHECK(shapes[0].name.empty());
	CHECK(shapes[1].name == "Closed");
	CHECK(shapes[2].name == "OpenLeft");
	CHECK(shapes[3].name == "OpenRight"); // unquoted rows are unchanged
}
