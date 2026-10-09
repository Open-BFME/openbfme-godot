// OpenBFME tests (lane PROD-1): the world state hash covers every field production adds. One mutation test per field group: two identical worlds
// hash alike, one field is changed in one of them, the hash must change. A field the hash misses would let two peers desync silently.

#include "doctest.h"
#include "ProdTestUtil.h"

#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/Module/ExitInterface.h"

using namespace prodtest;

namespace
{
struct HashWorld : ProdWorld
{
	Object *barracks = nullptr;
	HashWorld()
	{
		load(kBarracksObjects);
		load(kBarracksCommands);
		barracks = make("Barracks", teamOf("Alice"));
		REQUIRE(barracks != nullptr);
		Coord3D pos = { 1000.0f, 2000.0f, 0.0f };
		barracks->setPosition(&pos);
		frames(1);
	}
	std::uint32_t hash() const { return logic->computeStateHash(); }
	void frames(int n)
	{
		activate();
		ProdWorld::frames(n);
	}
	bool queue(const char *unit, const std::string &name = std::string(), bool flag44 = false)
	{
		activate();
		ProductionUpdateInterface *pu = barracks->getProductionUpdate();
		return pu->queueCreateUnit(w.get(unit), -1, pu->requestUniqueUnitID(), -1, false, name, flag44);
	}
};

template <class F>
void mutates(const char *what, F &&mutate)
{
	HashWorld a, b;
	REQUIRE(a.hash() == b.hash());
	mutate(a);
	CHECK_MESSAGE(a.hash() != b.hash(), what);
}
} // namespace

TEST_CASE("production hash: two identical production worlds hash alike, also after the same commands and frames")
{
	HashWorld a, b;
	a.queue("Soldier");
	b.queue("Soldier");
	a.frames(60);
	b.frames(60);
	CHECK(a.hash() == b.hash());
}

TEST_CASE("production hash: the queue (entries, ids, cost, progress) is hashed")
{
	mutates("a queued unit", [](HashWorld &w) { REQUIRE(w.queue("Soldier")); });
	{
		HashWorld a, b;
		a.queue("Soldier");
		b.queue("Archer"); // another template (an archer needs the upgrade: give it to both)
		(void)b;
		CHECK(a.hash() != b.hash());
	}
	mutates("progress", [](HashWorld &w) { w.queue("Soldier"); w.frames(1); });
	mutates("an entry's name", [](HashWorld &w) { w.queue("Soldier", "x"); });
	mutates("an entry's flag44", [](HashWorld &w) { w.queue("Soldier", std::string(), true); });
	mutates("the unique production id counter", [](HashWorld &w) { (void)w.barracks->getProductionUpdate()->requestUniqueUnitID(); });
	mutates("a cancelled entry", [](HashWorld &w) {
		w.queue("Soldier");
		w.barracks->getProductionUpdate()->cancelUnitCreateByType(w.w.get("Soldier"), false);
		w.queue("Soldier"); // the id counter moved on
	});
}

TEST_CASE("production hash: the factory state, door holds, construction complete frame and the exit state are hashed")
{
	mutates("factory disabled", [](HashWorld &w) { w.barracks->getProductionUpdate()->setFactoryDisabled(true); });
	mutates("a door held open", [](HashWorld &w) { w.barracks->getProductionUpdate()->setHoldDoorOpen(0, true); });
	mutates("the rally point", [](HashWorld &w) {
		Coord3D p = { 1100.0f, 2100.0f, 0.0f };
		w.barracks->getObjectExitInterface()->setRallyPoint(&p);
	});
	{
		// the door timeline and the exit delay move with the frames: one frame apart
		HashWorld a, b;
		a.queue("Soldier");
		b.queue("Soldier");
		a.frames(51);
		b.frames(52);
		CHECK(a.hash() != b.hash());
		a.frames(1);
		CHECK(a.hash() == b.hash());
	}
}

TEST_CASE("production hash: a produced object's producer id, timed model conditions and the player's production state are hashed")
{
	mutates("the SMC helper's timed model condition", [](HashWorld &w) { w.barracks->setSpecialModelConditionState(218 /* JUST_BUILT (RW name table; test_prod_object checks the index) */, 10); });
	{
		HashWorld a, b;
		a.logic->setPendingProducer(a.barracks->getID());
		a.make("Soldier", a.teamOf("Alice"));
		b.make("Soldier", b.teamOf("Alice"));
		CHECK(a.hash() != b.hash()); // the producer id of the new object (the unit's own id is the same in both worlds)
	}
	mutates("the player's unit building permission", [](HashWorld &w) { w.alice()->setCanBuildUnits(false); });
	mutates("the player's structure building permission", [](HashWorld &w) { w.alice()->setCanBuildBase(false); });
	mutates("a template the scripts forbade", [](HashWorld &w) { w.alice()->setTemplateBuildable(w.w.get("Soldier")->getTemplateID(), false); });
	mutates("a completed player upgrade", [](HashWorld &w) { w.alice()->addCompletedUpgrade("Upgrade_Basic"); });
	mutates("the command points' usage", [](HashWorld &w) { w.alice()->commandPoints().addUsage(1); });
	mutates("the command points' base", [](HashWorld &w) { w.alice()->commandPoints().setFromScript(w.alice()->commandPoints().getBase() + 1, w.alice()->commandPoints().getCap()); });
	mutates("the command points' bonus", [](HashWorld &w) { w.alice()->commandPoints().addBonus(1); });
	mutates("the command points' cap", [](HashWorld &w) { w.alice()->commandPoints().setFromScript(w.alice()->commandPoints().getBase(), w.alice()->commandPoints().getCap() + 1); });
	mutates("an object upgrade", [](HashWorld &w) { w.barracks->giveUpgrade("Upgrade_Barracks2"); });
	mutates("a command set override", [](HashWorld &w) { w.barracks->setCommandSetOverride("BarracksSet"); });
}

TEST_CASE("production hash: the player's selection (ordered ids) is hashed, by selection messages alone")
{
	// two selections of the same two barracks in a different order, nothing else changed: the first selected object is the producer
	auto run = [](bool reversed) {
		HashWorld w;
		Object *second = w.make("Barracks", w.teamOf("Alice"));
		REQUIRE(second != nullptr);
		CommandList list;
		GameLogicDispatch dispatch(*w.logic);
		dispatch.attach(list);
		GameMessage sel(MSG_CREATE_SELECTED_GROUP, w.alice()->getPlayerIndex());
		sel.appendBooleanArgument(true);
		sel.appendObjectIDArgument(reversed ? second->getID() : w.barracks->getID());
		sel.appendObjectIDArgument(reversed ? w.barracks->getID() : second->getID());
		list.append(sel);
		w.frames(1);
		return w.hash();
	};
	CHECK(run(false) != run(true));
	CHECK(run(false) == run(false));
	// an empty selection against a destroyed-and-deselected one
	HashWorld a, b;
	CommandList la, lb;
	GameLogicDispatch da(*a.logic), db(*b.logic);
	da.attach(la);
	db.attach(lb);
	GameMessage sel(MSG_CREATE_SELECTED_GROUP, a.alice()->getPlayerIndex());
	sel.appendBooleanArgument(true);
	sel.appendObjectIDArgument(a.barracks->getID());
	la.append(sel);
	a.frames(1);
	b.frames(1);
	CHECK(a.hash() != b.hash());
	GameMessage clear(MSG_DESTROY_SELECTED_GROUP, a.alice()->getPlayerIndex());
	la.append(clear);
	a.frames(1);
	b.frames(1);
	CHECK(a.hash() == b.hash());
}
