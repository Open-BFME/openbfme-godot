// OpenBFME unit tests. GPL-3.0.
// Port 202 (fork issue 469): the locomotor's wading updater (RW 0x5E5785) and the reveal of a disguised climber (RW 0x5E7C0A). Synthetic INI only.
// Every expectation is an instruction of RotWK 2.01's game.dat (the address stands next to it), not a value read back from the port:
//   RW 0x5E5785  the updater: ground layer (0x5E579A), TheTerrainLogic vslot 0x4C (0x5E57CB), depth = waterZ - terrainZ (0x5E57DA), depth > 0.25 x AIData
//                WadeWaterDepth (0x5E57E8, 0.25 at RW 0xBD1904) -> wading, depth > WadeWaterDepth (0x5E57F9) -> SWIMMING and WADING set, else SWIMMING
//                cleared and WADING set (0x5E581C); otherwise both cleared (0x5E584B)
//   RW 0x5E7787  NO_Z_MOTIVE_FORCE calls it and, on true, takes the ground height the water query left (0x5E778E -> 0x5E77F0)
//   RW 0x5E773E  FLOATING_Z calls it and drops the answer (0x5E7743)
//   RW 0x5E7C08  the CLIMBING step of SURFACE_RELATIVE_HEIGHT / ABSOLUTE_HEIGHT marks a disguised object detected, on every frame above 0.5 (RW 0xBD869C)
//   AIData WadeWaterDepth's constructor value is 5.0 (RW 0x6FFBEF, the constant RW 0xBDAE58)

#include "doctest.h"
#include "IniTestUtil.h"
#include "MoveTestUtil.h"

#include "GameLogic/BitFlags.h"
#include "GameLogic/Locomotor.h"

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace
{
int conditionIndex(const char *name)
{
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (std::string(TheModelConditionNames[i]) == name)
		{
			return i;
		}
	}
	FAIL("unknown model condition " << name);
	return -1;
}

// A host that answers the locomotor's questions from plain fields; the water facts are the three values RW 0x5E5785 reads.
struct WadeHost : LocomotorHost
{
	Coord3D pos{ 0, 0, 0 };
	float angle = 0.0f;
	LocomotorMatrix transform = LocomotorMatrix::identity();
	bool pendingValid = false;
	Coord3D pending{ 0, 0, 0 };
	bool zSuppressed = false;
	unsigned frame = 100;
	float layerZ = 0.0f;         // TheTerrainLogic vslot 0x1C on the object's layer
	bool rampOrRaised = false;
	std::set<int> mc;
	int conditionCalls = 0;      // clearAndSetModelConditions calls
	int conditionChanges = 0;    // those that changed a bit (RW 0x68D607 tells the drawable only then)
	// the wading updater's reads
	bool groundLayer = true;     // Object::getLayer (RW 0x68BBE0) == 1
	bool water = false;          // TheTerrainLogic vslot 0x4C's answer
	float waterZ = 0.0f;
	float bedZ = 0.0f;           // the ground under the water (the query's fourth argument)
	float wade = 5.0f;           // AIData WadeWaterDepth, the constructor's value (RW 0x6FFBEF)
	mutable int waterQueries = 0;
	int reveals = 0;             // revealDisguisedClimber calls

	WadeHost() { syncTransform(); }
	void place(float x, float y, float z)
	{
		pos = Coord3D{ x, y, z };
		syncTransform();
	}
	void syncTransform()
	{
		transform = LocomotorMatrix::identity();
		transform.m[0][3] = pos.x;
		transform.m[1][3] = pos.y;
		transform.m[2][3] = pos.z;
	}
	bool has(const char *name) const { return mc.count(conditionIndex(name)) != 0; }

	Coord3D getPosition() const override { return pos; }
	float getAngle() const override { return angle; }
	Coord2D getUnitDirectionVector2D() const override { return Coord2D{ std::cos(angle), std::sin(angle) }; }
	float getBoundingRadius() const override { return 10.0f; }
	LocomotorMatrix getTransform() const override { return transform; }
	void setTransform(const LocomotorMatrix &m) override
	{
		transform = m;
		pos = Coord3D{ m.m[0][3], m.m[1][3], m.m[2][3] };
		angle = std::atan2(m.m[1][0], m.m[0][0]);
	}
	bool hasPendingPosition() const override { return pendingValid; }
	Coord3D getPendingPosition() const override { return pending; }
	void setPendingPosition(const Coord3D &p) override
	{
		pending = p;
		pendingValid = true;
	}
	float locomotorSetSpeed() const override { return 55.0f; }
	int damageState() const override { return 0; }
	int movementPenaltyDamageState() const override { return 3; }
	float crewPowerMultiplier() const override { return 1.0f; }
	bool speedAttributeModifier(float &out) const override
	{
		out = 1.0f;
		return false;
	}
	bool isInRiver(float, float) const override { return false; }
	bool isTurnLimited() const override { return false; }
	bool isChargeOrdered() const override { return false; }
	bool physicsMotionDisabled() const override { return false; }
	bool hasPhysicsModule() const override { return true; }
	bool zMotionSuppressed() const override { return zSuppressed; }
	unsigned logicFrame() const override { return frame; }
	bool containerAllowsBackingUp() const override { return false; }
	float groundHeightAt(float, float) const override { return layerZ; }
	bool isOnRampOrRaisedLayer() const override { return rampOrRaised; }
	LocomotorPath *getPath() override { return nullptr; }
	bool testObjectStatus(int) const override { return false; }
	bool testModelCondition(int bit) const override { return mc.count(bit) != 0; }
	void setModelCondition(int bit, bool value) override
	{
		if (value)
		{
			mc.insert(bit);
		}
		else
		{
			mc.erase(bit);
		}
	}
	void clearAndSetModelConditions(const std::vector<int> &clear, const std::vector<int> &set) override
	{
		const std::set<int> before = mc;
		for (int c : clear)
		{
			mc.erase(c);
		}
		for (int s : set)
		{
			mc.insert(s);
		}
		++conditionCalls;
		conditionChanges += before != mc ? 1 : 0;
	}
	void notifyWheelsStopped() override {}
	bool hasHordeContain() const override { return false; }
	void hordeBeginReform() override {}
	void hordeEndReform() override {}
	bool hordeFormationReady(float) const override { return true; }
	bool thingWaitsForFormation() const override { return false; }
	bool isUnderwater(float, float, float *z) const override
	{
		if (water && z)
		{
			*z = waterZ;
		}
		return water;
	}
	bool isDisabledHeld() const override { return false; }
	bool isKindOfSubmarine() const override { return false; }
	void revealDisguisedClimber() override { ++reveals; }
	bool wadingWaterAt(float, float, WadingWater &out) const override
	{
		++waterQueries;
		if (!groundLayer || !water)
		{
			return false;
		}
		out.waterZ = waterZ;
		out.terrainZ = bedZ;
		out.wadeWaterDepth = wade;
		return true;
	}
};

struct LocoFixture
{
	initest::Fixture fx;
	LocomotorStore store;
	LocomotorStore *saved;
	explicit LocoFixture(const std::string &body)
	{
		saved = TheLocomotorStore;
		TheLocomotorStore = &store;
		fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
		const std::string err = initest::loadError(fx.env, "k.ini", "Locomotor K\n" + body + "\nEnd\n");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	~LocoFixture() { TheLocomotorStore = saved; }
	const LocomotorTemplate *tmpl() { return store.findLocomotorTemplate("K"); }
};

const char kWalkerBody[] = "  Surfaces = GROUND RUBBLE\n  TurnTime = 500\n  TurnTimeDamaged = 500\n  Acceleration = 510\n  Braking = 510\n  Appearance = TWO_LEGS\n"
						   "  StickToGround = Yes\n";

void step(Locomotor &loco, WadeHost &h)
{
	loco.locomotorMoveTowardsPosition(h, Coord3D{ h.pos.x + 100.0f, h.pos.y, 0.0f }, 100.0f, 11.0f);
}
} // namespace

TEST_CASE("PORT-202 wading: a NO_Z_MOTIVE_FORCE walker gets WADING above a quarter of WadeWaterDepth, SWIMMING above all of it, and loses both on the bank (RW 0x5E5785)")
{
	LocoFixture f(std::string(kWalkerBody) + "  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
	Locomotor loco(f.tmpl());
	WadeHost h;
	h.place(0, 0, 0);
	h.layerZ = 7.0f; // the layer query's answer (RW 0x5E77AC); the bed under the water is 3
	h.bedZ = 3.0f;

	// dry land: neither condition, nothing told to the drawable, the height is the layer's (RW 0x5E7790)
	step(loco, h);
	CHECK_FALSE(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.conditionCalls == 0);
	CHECK(h.pos.z == 7.0f);

	// water 1.25 deep: 0.25 x 5 is not exceeded (RW 0x5E57F4 comiss, 0x5E57F7 jbe): still dry, the layer's height
	h.water = true;
	h.waterZ = 4.25f;
	step(loco, h);
	CHECK_FALSE(h.has("WADING"));
	CHECK(h.conditionCalls == 0);
	CHECK(h.pos.z == 7.0f);

	// 1.5 deep: WADING on this frame, told once; the height is the ground's under the water (RW 0x5E778E -> 0x5E77F0), not the layer query's
	h.waterZ = 4.5f;
	step(loco, h);
	CHECK(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.conditionChanges == 1);
	CHECK(h.pos.z == 3.0f);

	// the next frame in the same water: WADING set and SWIMMING clear skips the write (RW 0x5E5831 jne 0x5E5847)
	step(loco, h);
	CHECK(h.has("WADING"));
	CHECK(h.conditionChanges == 1);

	// exactly WadeWaterDepth deep: not above it (RW 0x5E57F9 comiss, 0x5E57FC jbe): WADING alone
	h.waterZ = 8.0f;
	step(loco, h);
	CHECK(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.conditionChanges == 1);

	// 5.5 deep: SWIMMING and WADING both set (RW 0x5E57FE: the flags { 0xF0, 0xEF } are set, none cleared)
	h.waterZ = 8.5f;
	step(loco, h);
	CHECK(h.has("WADING"));
	CHECK(h.has("SWIMMING"));
	CHECK(h.conditionChanges == 2);
	CHECK(h.pos.z == 3.0f);

	// back into 2 deep water: SWIMMING cleared, WADING kept, one notification (RW 0x5E5833 .. 0x5E5842)
	h.waterZ = 5.0f;
	step(loco, h);
	CHECK(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.conditionChanges == 3);

	// the bank: both cleared at once (RW 0x5E584B), the layer's height again; then nothing more is told
	h.water = false;
	step(loco, h);
	CHECK_FALSE(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.conditionChanges == 4);
	CHECK(h.pos.z == 7.0f);
	step(loco, h);
	CHECK(h.conditionChanges == 4);
	CHECK(h.conditionCalls == h.conditionChanges); // an unchanged set never reaches the host (RW 0x68D632 .. 0x68D63F)

	// swimming straight onto the bank clears both in one notification
	h.water = true;
	h.waterZ = 20.0f;
	step(loco, h);
	CHECK(h.has("SWIMMING"));
	h.water = false;
	step(loco, h);
	CHECK_FALSE(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.conditionChanges == 6);

	// off the ground layer (RW 0x5E579F: getLayer != 1) the water is not looked at: a wading object that steps onto a bridge loses WADING
	h.water = true;
	h.waterZ = 5.0f;
	step(loco, h);
	CHECK(h.has("WADING"));
	h.groundLayer = false;
	step(loco, h);
	CHECK_FALSE(h.has("WADING"));
	CHECK(h.pos.z == 7.0f);

	// AIData WadeWaterDepth is the threshold (RW 0x5E57D2 .. 0x5E57E3): with 20 the same 2 deep water is below its quarter
	h.groundLayer = true;
	h.wade = 20.0f;
	step(loco, h);
	CHECK_FALSE(h.has("WADING"));
	h.waterZ = 8.5f; // 5.5 deep: above 5, below 20
	step(loco, h);
	CHECK(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
}

TEST_CASE("PORT-202 wading: FLOATING_Z runs the updater on the mover's position and keeps the mover's height (RW 0x5E773E)")
{
	LocoFixture f(std::string(kWalkerBody) + "  ZAxisBehavior = FLOATING_Z\n");
	Locomotor loco(f.tmpl());
	WadeHost h;
	h.place(0, 0, 40.0f);
	h.layerZ = 7.0f;
	h.bedZ = 3.0f;
	h.water = true;
	h.waterZ = 5.0f; // 2 deep
	step(loco, h);
	CHECK(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.pos.z == 40.0f); // the answer is dropped (RW 0x5E7743): no snap to the bed or the layer
	CHECK(h.pendingValid);
	CHECK(h.pending.x == h.pos.x);
	h.waterZ = 9.0f; // 6 deep
	step(loco, h);
	CHECK(h.has("SWIMMING"));
	h.water = false;
	step(loco, h);
	CHECK_FALSE(h.has("WADING"));
	CHECK_FALSE(h.has("SWIMMING"));
	CHECK(h.pos.z == 40.0f);
}

TEST_CASE("PORT-202 neighbours: a held or thrown object, a SEA_LEVEL ship and the walkway height hold do not go through the updater (RW 0x5E7751, 0x5E781F, 0x5E77C2)")
{
	{
		// RW 0x5E774A .. 0x5E7765: DISABLED_HELD or THROWN_PROJECTILE jumps to the tail before the updater: the water is not asked, the conditions stay
		LocoFixture f(std::string(kWalkerBody) + "  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
		Locomotor loco(f.tmpl());
		WadeHost h;
		h.place(0, 0, 30.0f);
		h.zSuppressed = true;
		h.water = true;
		h.waterZ = 20.0f;
		h.mc.insert(conditionIndex("WADING"));
		step(loco, h);
		CHECK(h.waterQueries == 0);
		CHECK(h.has("WADING"));
		CHECK_FALSE(h.has("SWIMMING"));
		CHECK(h.pos.z == 30.0f);
	}
	{
		// RW 0x5E781F (SEA_LEVEL, case 1 of the table RW 0x5E7CA3) holds no call of RW 0x5E5785: a ship in deep water floats at the water's height unmarked
		LocoFixture f(std::string(kWalkerBody) + "  ZAxisBehavior = SEA_LEVEL\n");
		Locomotor loco(f.tmpl());
		WadeHost h;
		h.place(0, 0, 0);
		h.water = true;
		h.waterZ = 12.5f;
		h.bedZ = -30.0f;
		step(loco, h);
		CHECK(h.waterQueries == 0);
		CHECK_FALSE(h.has("WADING"));
		CHECK_FALSE(h.has("SWIMMING"));
		CHECK(h.pos.z == 12.5f);
	}
	{
		// lane WALL-4's hold (RW 0x5E77C2 .. 0x5E77EE) is the branch the updater's false leads to: on a walkway above water the object keeps its height
		LocoFixture f(std::string(kWalkerBody) + "  ZAxisBehavior = NO_Z_MOTIVE_FORCE\n");
		Locomotor loco(f.tmpl());
		WadeHost h;
		h.place(0, 0, 50.0f);
		h.groundLayer = false;
		h.rampOrRaised = true;
		h.layerZ = 10.0f;
		h.water = true;
		h.waterZ = 9.0f;
		h.bedZ = 0.0f;
		step(loco, h);
		CHECK_FALSE(h.has("WADING"));
		CHECK(h.pos.z == 50.0f); // more than 20 (RW 0xBDBC6C) above the layer's height after the first second
	}
}

TEST_CASE("PORT-202 climb: a climbing SURFACE_RELATIVE_HEIGHT object asks for the disguise reveal on every frame its step is above 0.5, and not at 0.5 (RW 0x5E7C0A)")
{
	// the height fields of the kinematics suite's SURFACE_RELATIVE_HEIGHT case: aim = PreferredHeight + surface, step = (aim - z) / 10
	LocoFixture f("  Surfaces = AIR\n  TurnTime = 500\n  Acceleration = 2000\n  Braking = 2000\n  PreferredHeight = 50\n  PreferredAttackHeight = 25\n"
				  "  ZAxisBehavior = SURFACE_RELATIVE_HEIGHT\n  Appearance = HOVER\n");
	Locomotor loco(f.tmpl());
	WadeHost h;
	h.place(0, 0, 0);
	h.layerZ = 10.0f;
	step(loco, h); // step 6 (RW 0x5E7BE8: above 0.5, RW 0xBD869C)
	CHECK(h.pos.z == 6.0f);
	CHECK(h.has("CLIMBING"));
	CHECK(h.reveals == 1);
	step(loco, h); // step 5.4: CLIMBING is already set, which skips only its write (RW 0x5E7BF8 jne 0x5E7C08), not the reveal
	CHECK(h.has("CLIMBING"));
	CHECK(h.reveals == 2);
	h.place(h.pos.x, h.pos.y, 55.0f);
	step(loco, h); // step exactly 0.5: not above (RW 0x5E7BEF jbe 0x5E7C29): CLIMBING cleared, no reveal
	CHECK(h.pos.z == 55.5f);
	CHECK_FALSE(h.has("CLIMBING"));
	CHECK(h.reveals == 2);
	CHECK(h.waterQueries == 0); // this z behaviour holds no call of RW 0x5E5785
	CHECK_FALSE(h.has("WADING"));
}

TEST_CASE("PORT-202 world: a walker ordered across a ford wades on every frame it stands in water deeper than a quarter of AIData WadeWaterDepth and stops on the bank")
{
	using namespace movetest;
	const int wadingBit = conditionIndex("WADING");
	const int swimmingBit = conditionIndex("SWIMMING");
	// flat ground at 0; a ford 3 deep over x 300 .. 400 and a puddle 1 deep over x 500 .. 560 (WadeWaterDepth 5: a quarter is 1.25)
	const auto cross = [&](float wadeWaterDepth, int &wadingFrames, int &puddleFrames, int &mismatches, bool &swam, Coord3D &end) {
		MoveWorld mw;
		mw.terrain.water.push_back({ 300.0f, 0.0f, 400.0f, 1000.0f, 3.0f });
		mw.terrain.water.push_back({ 500.0f, 0.0f, 560.0f, 1000.0f, 1.0f });
		mw.logic->settings().wadeWaterDepth = wadeWaterDepth;
		mw.buildMap();
		Object *u = mw.spawn("Walker", 105.0f, 505.0f);
		AIUpdateInterface *ai = mw.aiOf(u);
		REQUIRE(ai != nullptr);
		mw.frames(2);
		CHECK_FALSE(u->testModelCondition(wadingBit));
		ai->aiMoveToPosition(Coord3D{ 705.0f, 505.0f, 0.0f }, CMD_FROM_PLAYER);
		wadingFrames = puddleFrames = mismatches = 0;
		swam = false;
		for (int i = 0; i < 200; ++i)
		{
			mw.frames(1);
			const Coord3D p = *u->getPosition();
			const bool inFord = p.x >= 300.0f && p.x < 400.0f;
			const bool inPuddle = p.x >= 500.0f && p.x < 560.0f;
			const float depth = inFord ? 3.0f : inPuddle ? 1.0f : 0.0f;
			const bool expected = depth > wadeWaterDepth * 0.25f; // RW 0x5E57E8
			const bool wading = u->testModelCondition(wadingBit);
			mismatches += wading != expected ? 1 : 0;
			wadingFrames += wading ? 1 : 0;
			puddleFrames += inPuddle ? 1 : 0;
			swam = swam || u->testModelCondition(swimmingBit);
		}
		end = *u->getPosition();
		CHECK(ai->isIdle());
		CHECK_FALSE(u->testModelCondition(wadingBit));
	};
	int wadingFrames = 0, puddleFrames = 0, mismatches = 0;
	bool swam = false;
	Coord3D end{ 0, 0, 0 };
	cross(5.0f, wadingFrames, puddleFrames, mismatches, swam, end); // the AIData constructor's WadeWaterDepth (RW 0x6FFBEF)
	CHECK(end.x > 690.0f);     // tolerance: the goal is x 705 and the destination's cell snap may end the walk a few units short; the far bank starts at 560
	CHECK(mismatches == 0);    // WADING on exactly the frames the walker stands in the ford: set on the frame it enters, cleared on the frame it leaves
	CHECK(wadingFrames >= 5);  // tolerance: the ford is 100 wide and the walker makes at most 11 a frame (9 frames at full speed); `mismatches` holds the exact frames
	CHECK(puddleFrames >= 3);  // tolerance: the puddle (1 deep, below the quarter) is 60 wide: 5 frames at 11 a frame, fewer are impossible; it was crossed without WADING
	CHECK_FALSE(swam);         // 3 is not above WadeWaterDepth (RW 0x5E57F9)

	cross(20.0f, wadingFrames, puddleFrames, mismatches, swam, end); // AIData WadeWaterDepth = 20: the ford's 3 is below its quarter (5)
	CHECK(end.x > 690.0f); // tolerance: as above, the far bank
	CHECK(mismatches == 0);
	CHECK(wadingFrames == 0);
}
