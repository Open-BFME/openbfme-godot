// OpenBFME unit tests (lane PROD-1): the SMCHelper timed model conditions (RW 0x8E2C0F / 0x8E2B72 / 0x8E2ACA), the Object model condition flags
// and the GameMessage numbering (PLAN rule 4: RotWK 1001 .. 1147). Expectations are from the RotWK disassembly (caveat S-001).

#include "doctest.h"
#include "LogicTestUtil.h"

#include "Common/ModelState.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/SMCHelper.h"

using namespace logictest;

TEST_CASE("prod: GameMessage numbers are the RotWK ones")
{
	CHECK(MSG_QUEUE_UNIT_CREATE == 1047);
	CHECK(MSG_CANCEL_UNIT_CREATE == 1048);
	CHECK(MSG_SET_RALLY_POINT == 1043);
	CHECK(MSG_QUEUE_UPGRADE == 1045);
	CHECK(MSG_LOGIC_CRC == 1098);
	CHECK(MSG_ADD_TO_TEAM9 == 1147);
	CHECK(std::string(GameMessageTypeName(1047)) == "MSG_QUEUE_UNIT_CREATE");
	CHECK(std::string(GameMessageTypeName(1147)) == "MSG_ADD_TO_TEAM9");
	CHECK(std::string(GameMessageTypeName(7)) == "");
}

TEST_CASE("prod: SMCHelper sets a model condition for N frames and clears it when it expires")
{
	LogicWorld lw;
	REQUIRE(lw.w.load("Object Plain\nEnd\n") == "");
	const int bit = ModelCondition::indexOf("JUST_BUILT");
	REQUIRE(bit == 218);
	Object *o = lw.make("Plain");
	REQUIRE(o != nullptr);
	CHECK_FALSE(o->testModelCondition(bit));
	o->setSpecialModelConditionState(bit, 3); // RW 0x8E2C0F: expiry = now + 3
	CHECK(o->testModelCondition(bit));
	const UnsignedInt start = lw.logic->getFrame();
	// a later request for the same bit keeps the later expiry; an earlier one does not shorten it (RW 0x8E2CA4: max)
	o->setSpecialModelConditionState(bit, 1);
	std::vector<bool> on;
	for (int i = 0; i < 5; ++i)
	{
		lw.logic->runLogicFrame();
		on.push_back(o->testModelCondition(bit));
	}
	// frames start+1, +2 still on, expired when frame >= start + 3 (RW 0x8E2B96: `now < expiry` keeps it)
	CHECK(lw.logic->getFrame() == start + 5);
	CHECK(on == std::vector<bool>{ true, true, false, false, false });
	// the helper went back to sleep: calc() of an empty list is UPDATE_SLEEP_FOREVER (RW 0x8E2AD3)
	SMCHelper *smc = static_cast<SMCHelper *>(o->findModule("SMCHelper"));
	REQUIRE(smc != nullptr);
	CHECK(smc->activeCount() == 0);
	CHECK(smc->friend_getNextCallFrame() == (UnsignedInt)UPDATE_SLEEP_FOREVER);
}

TEST_CASE("prod: clearAndSetModelConditionFlags clears first, then sets (RW 0x68D607)")
{
	LogicWorld lw;
	REQUIRE(lw.w.load("Object Plain\nEnd\n") == "");
	Object *o = lw.make("Plain");
	Object::ModelConditionBits a{}, b{};
	a[0] = 0x6; // bits 1, 2
	o->clearAndSetModelConditionFlags(Object::ModelConditionBits{}, a);
	CHECK(o->testModelCondition(1));
	CHECK(o->testModelCondition(2));
	b[0] = 0x2; // clear bit 1 and set it again in the same call: the set wins
	Object::ModelConditionBits c{};
	c[0] = 0x2;
	o->clearAndSetModelConditionFlags(b, c);
	CHECK(o->testModelCondition(1));
	Object::ModelConditionBits d{};
	d[0] = 0x4;
	o->clearAndSetModelConditionFlags(d, Object::ModelConditionBits{});
	CHECK_FALSE(o->testModelCondition(2));
}
