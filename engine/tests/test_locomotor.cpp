// OpenBFME unit tests: Locomotor block, LocomotorStore, LocomotorSet (GameLogic/Locomotor). GPL-3.0.
// Lane HORDE-1, spec horde-and-movement.md 1.5, 2.13 and checklist steps 1-2.
// Expected values: the RW 0x5E4326 constructor and 0x5E3143 post-parse (read from the disassembly), the
// FieldParse table and name lists dumped from game.dat into tests/data/horde1/*.tsv by
// tools/horde_oracle/dump_tables.py, and the INI conversions of PLAN rule 2.

#include "doctest.h"
#include "IniTestUtil.h"

#include "Common/GameCommon.h"
#include "GameLogic/Locomotor.h"
#include "RetailTestMount.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>

using namespace initest;

namespace
{
// ---- golden TSV ---------------------------------------------------------------------------------
struct TableRow
{
	int index;
	std::string token;
	std::uint32_t parse, user, offset;
};

std::vector<std::string> readLines(const std::string &name)
{
	std::vector<unsigned char> bytes;
	std::string error;
	REQUIRE_MESSAGE(retailtest::readLocalFile(retailtest::dataDir() + "/horde1/" + name, bytes, &error), error);
	std::vector<std::string> lines;
	std::string cur;
	for (unsigned char c : bytes)
	{
		if (c == '\n')
		{
			lines.push_back(cur);
			cur.clear();
		}
		else
		{
			cur.push_back((char)c);
		}
	}
	if (!cur.empty())
	{
		lines.push_back(cur);
	}
	return lines;
}

std::vector<TableRow> readTable(const std::string &name)
{
	std::vector<TableRow> rows;
	for (const std::string &line : readLines(name))
	{
		std::istringstream in(line);
		TableRow r;
		std::string parse, user, off;
		std::getline(in, parse, '\t');
		r.index = std::stoi(parse);
		std::getline(in, r.token, '\t');
		std::getline(in, parse, '\t');
		std::getline(in, user, '\t');
		std::getline(in, off, '\t');
		r.parse = (std::uint32_t)std::stoul(parse, nullptr, 16);
		r.user = (std::uint32_t)std::stoul(user, nullptr, 16);
		r.offset = (std::uint32_t)std::stoul(off, nullptr, 16);
		rows.push_back(r);
	}
	return rows;
}

std::vector<std::string> readList(const std::string &name)
{
	std::vector<std::string> out;
	for (const std::string &line : readLines(name))
	{
		out.push_back(line.substr(line.find('\t') + 1));
	}
	return out;
}

std::vector<std::string> namesOf(const char *const *list)
{
	std::vector<std::string> out;
	for (; *list; ++list)
	{
		out.push_back(*list);
	}
	return out;
}

// RW parse function address of each INI parser kind (identified by disassembly, see Locomotor.h).
std::map<INIFieldParseProc, std::uint32_t> rwParserAddresses()
{
	return {
		{ INI::parseBool, 0x42e558 },
		{ INI::parseReal, 0x42ed00 },
		{ INI::parsePercentToReal, 0x42eefa },
		{ INI::parseAngleReal, 0x42ee15 },
		{ INI::parseDurationUnsignedInt, 0x73a429 },
		{ INI::parseVelocityReal, 0x73a4b6 },
		{ INI::parseDurationReal, 0x73a403 },
		{ INI::parseIndexList, 0x42e956 },
		{ INI::parseBitString32, 0x42e840 },
		{ INI::parseAngularVelocityReal, 0x73a2d6 },
		{ INI::parseInt, 0x42ec5e },
		{ LocomotorTemplate::parseCanMoveBackwards, 0x5e3183 },
		{ INI::parseAsciiString, 0x42ee5e },
	};
}

// ---- parsing helpers -----------------------------------------------------------------------------
struct LocoWorld
{
	Fixture fx;
	LocomotorStore store;
	LocomotorStore *saved;

	LocoWorld()
	{
		saved = TheLocomotorStore;
		TheLocomotorStore = &store;
		fx.env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });
	}
	~LocoWorld() { TheLocomotorStore = saved; }

	std::string load(const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
	{
		return loadError(fx.env, "test.ini", text, type, code);
	}
};

const LocomotorTemplate *only(LocoWorld &w, const char *name)
{
	const LocomotorTemplate *t = w.store.findLocomotorTemplate(name);
	REQUIRE(t != nullptr);
	return t;
}

LocomotorTemplate parsedOne(const std::string &body)
{
	LocoWorld w;
	const std::string err = w.load("Locomotor T\n" + body + "\nEnd\n");
	REQUIRE_MESSAGE(err.empty(), err);
	return *only(w, "T")->getFinalOverride();
}

bool nearly(float a, double b, double eps = 1e-5)
{
	return std::fabs((double)a - b) <= eps;
}
}

TEST_CASE("Locomotor: the field table is the 88 rows of RW 0xBF4478, in order, with the retail parser kinds")
{
	const std::vector<TableRow> golden = readTable("table_locomotor.tsv");
	REQUIRE(golden.size() == 88);
	const FieldParse *mine = LocomotorTemplate::getFieldParse();
	const auto addresses = rwParserAddresses();
	const std::map<const void *, std::string> lists = {
		{ TheLocomotorSurfaceNames, "list_surface_names.tsv" },
		{ TheLocomotorZAxisNames, "list_zaxis_names.tsv" },
		{ TheLocomotorAppearanceNames, "list_appearance_names.tsv" },
		{ TheLocomotorFormationPriorityNames, "list_formation_priority_names.tsv" },
	};
	const std::map<std::string, std::uint32_t> listVa = {
		{ "list_surface_names.tsv", 0xd9e008 }, { "list_zaxis_names.tsv", 0xd9df1c }, { "list_appearance_names.tsv", 0xd9deec },
		{ "list_formation_priority_names.tsv", 0xd9dfcc } };
	size_t i = 0;
	for (; mine[i].token; ++i)
	{
		REQUIRE(i < golden.size());
		INFO("row " << i << " " << golden[i].token);
		CHECK(std::string(mine[i].token) == golden[i].token);
		const auto it = addresses.find(mine[i].parse);
		REQUIRE(it != addresses.end());
		CHECK(it->second == golden[i].parse);
		if (golden[i].user == 0)
		{
			CHECK(mine[i].userData == nullptr);
		}
		else
		{
			const auto l = lists.find(mine[i].userData);
			REQUIRE(l != lists.end());
			CHECK(listVa.at(l->second) == golden[i].user);
			CHECK(namesOf((const char *const *)mine[i].userData) == readList(l->second));
		}
	}
	CHECK(i == 88);
	CHECK(namesOf(TheLocomotorSetNames) == readList("list_locomotor_set_names.tsv"));
	CHECK(namesOf(TheLocomotorSetNames).size() == (size_t)LOCOMOTORSET_COUNT);
}

TEST_CASE("Locomotor: every table row stores into the member it names (behavioural check of all 88 rows)")
{
	struct Case
	{
		const char *token;
		const char *value;
		std::function<bool(const LocomotorTemplate &)> check;
	};
#define C(tok, val, expr) { tok, val, [](const LocomotorTemplate &t) { return (expr); } }
	const std::vector<Case> cases = {
		C("Surfaces", "GROUND WATER", t.m_surfaces == (LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_WATER)),
		C("Speed", "55", t.m_maxSpeed == 11.0f),
		C("LookAheadMult", "2.5", t.m_lookAheadMult == 2.5f),
		C("NonDirtyTransform", "Yes", t.m_nonDirtyTransform),
		C("SpeedDamaged", "50%", nearly(t.m_maxSpeedDamaged, 0.5)),
		C("TurnTime", "500", t.m_turnTime == 3),
		C("TurnTimeDamaged", "2000", t.m_turnTimeDamaged == 10),
		C("SlowTurnRadius", "7", t.m_slowTurnRadius == 7.0f),
		C("FastTurnRadius", "8", t.m_fastTurnRadius == 8.0f),
		C("TurnThreshold", "90", nearly(t.m_turnThreshold, 1.5707963)),
		C("TurnThresholdHS", "180", nearly(t.m_turnThresholdHighSpeed, 3.1415927)),
		C("Acceleration", "510", t.m_acceleration == 3),
		C("Lift", "25%", nearly(t.m_lift, 0.25)),
		C("LiftDamaged", "30%", nearly(t.m_liftDamaged, 0.3)),
		C("Braking", "1500", t.m_braking == 8),
		C("MinSpeed", "20%", nearly(t.m_minSpeed, 0.2)),
		C("MinTurnSpeed", "100%", nearly(t.m_minTurnSpeed, 1.0)),
		C("PreferredHeight", "30", t.m_preferredHeight == 30.0f),
		C("PreferredHeightDamping", "0.25", t.m_preferredHeightDamping == 0.25f),
		C("PreferredAttackHeight", "12", t.m_preferredAttackHeight == 12.0f),
		C("CirclingRadius", "-100", t.m_circlingRadius == -100.0f),
		C("SpeedLimitZ", "100", t.m_speedLimitZ == 20.0f),
		C("MaxThrustAngle", "45", nearly(t.m_maxThrustAngle, 0.7853982)),
		C("ZAxisBehavior", "FLOATING_Z", t.m_zAxisBehavior == Z_FLOATING_Z),
		C("Appearance", "HORDE", t.m_appearance == LOCO_HORDE),
		C("FormationPriority", "CAVALRY2", t.m_formationPriority == FORMATION_CAVALRY2),
		C("AccDecTrigger", "0.75", t.m_accDecTrigger == 0.75f),
		C("WalkDistance", "80", t.m_walkDistance == 80.0f),
		C("MaxOverlappedHeight", "33", t.m_maxOverlappedHeight == 33.0f),
		C("MaxTurnWithoutReform", "45", nearly(t.m_maxTurnWithoutReform, 0.7853982)),
		C("AccelerationPitchLimit", "10", nearly(t.m_accelerationPitchLimit, 0.17453292)),
		C("BounceAmount", "50", nearly(t.m_bounceAmount, 50 * 0.2 * 0.017453292, 1e-5)),
		C("PitchStiffness", "0.3", t.m_pitchStiffness == 0.3f),
		C("RollStiffness", "0.4", t.m_rollStiffness == 0.4f),
		C("PitchDamping", "0.5", t.m_pitchDamping == 0.5f),
		C("RollDamping", "0.6", t.m_rollDamping == 0.6f),
		C("PitchInDirectionOfZVelFactor", "0.7", t.m_pitchInDirectionOfZVelFactor == 0.7f),
		C("ForwardVelocityPitchFactor", "0.8", t.m_forwardVelocityPitchFactor == 0.8f),
		C("LateralVelocityRollFactor", "0.9", t.m_lateralVelocityRollFactor == 0.9f),
		C("ForwardAccelerationPitchFactor", "1.1", t.m_forwardAccelerationPitchFactor == 1.1f),
		C("LateralAccelerationRollFactor", "1.2", t.m_lateralAccelerationRollFactor == 1.2f),
		C("UniformAxialDamping", "1.3", t.m_uniformAxialDamping == 1.3f),
		C("TurnPivotOffset", "1", t.m_turnPivotOffset == 1.0f),
		C("Apply2DFrictionWhenAirborne", "Yes", t.m_apply2DFrictionWhenAirborne),
		C("DownhillOnly", "Yes", t.m_downhillOnly),
		C("AllowAirborneMotiveForce", "Yes", t.m_allowAirborneMotiveForce),
		C("LocomotorWorksWhenDead", "Yes", t.m_locomotorWorksWhenDead),
		C("AirborneTargetingHeight", "75", t.m_airborneTargetingHeight == 75),
		C("StickToGround", "Yes", t.m_stickToGround),
		C("CanMoveBackwards", "Yes", t.m_canMoveBackwards == 1),
		C("HasSuspension", "Yes", t.m_hasSuspension),
		C("FrontWheelTurnAngle", "20", nearly(t.m_frontWheelTurnAngle, 0.34906584)),
		C("MaximumWheelExtension", "1.4", t.m_maximumWheelExtension == 1.4f),
		C("MaximumWheelCompression", "1.5", t.m_maximumWheelCompression == 1.5f),
		C("CloseEnoughDist", "9", t.m_closeEnoughDist == 9.0f),
		C("CloseEnoughDist3D", "Yes", t.m_closeEnoughDist3D),
		C("SlideIntoPlaceTime", "500", t.m_slideIntoPlaceTime == 2.5f),
		C("CrewPowered", "Yes", t.m_crewPowered),
		C("UseTerrainSmoothing", "Yes", t.m_useTerrainSmoothing),
		C("WanderWidthFactor", "1.6", t.m_wanderWidthFactor == 1.6f),
		C("WanderLengthFactor", "1.7", t.m_wanderLengthFactor == 1.7f),
		C("WanderAboutPointRadius", "1.8", t.m_wanderAboutPointRadius == 1.8f),
		C("BurningDeathRadius", "1.9", t.m_burningDeathRadius == 1.9f),
		C("BurningDeathIsCavalry", "Yes", t.m_burningDeathIsCavalry),
		C("ChargeSpeed", "150%", nearly(t.m_chargeSpeed, 1.5)),
		C("ChargeAvailable", "Yes", t.m_chargeAvailable),
		C("ChargeIgnoresCondition", "Yes", t.m_chargeIgnoresCondition),
		C("EnableHighSpeedTurnModelconditions", "No", !t.m_enableHighSpeedTurnModelconditions),
		C("WaitForFormation", "Yes", t.m_waitForFormation),
		C("RudderCorrectionDegree", "2.1", t.m_rudderCorrectionDegree == 2.1f),
		C("RudderCorrectionRate", "2.2", t.m_rudderCorrectionRate == 2.2f),
		C("ElevatorCorrectionDegree", "2.3", t.m_elevatorCorrectionDegree == 2.3f),
		C("ElevatorCorrectionRate", "2.4", t.m_elevatorCorrectionRate == 2.4f),
		C("AeleronCorrectionDegree", "2.5", t.m_aeleronCorrectionDegree == 2.5f),
		C("AeleronCorrectionRate", "2.6", t.m_aeleronCorrectionRate == 2.6f),
		C("SwoopStandoffRadius", "2.7", t.m_swoopStandoffRadius == 2.7f),
		C("SwoopStandoffHeight", "2.8", t.m_swoopStandoffHeight == 2.8f),
		C("SwoopTerminalVelocity", "2.9", t.m_swoopTerminalVelocity == 2.9f),
		C("SwoopAccelerationRate", "3.1", t.m_swoopAccelerationRate == 3.1f),
		C("SwoopSpeedTuningFactor", "3.2", t.m_swoopSpeedTuningFactor == 3.2f),
		C("BackingUpSpeed", "33%", nearly(t.m_backingUpSpeed, 0.33)),
		C("BackingUpStopWhenTurning", "Yes", t.m_backingUpStopWhenTurning),
		C("BackingUpDistanceMin", "3.3", t.m_backingUpDistanceMin == 3.3f),
		C("BackingUpDistanceMax", "3.4", t.m_backingUpDistanceMax == 3.4f),
		C("BackingUpAngle", "0.25", t.m_backingUpAngle == 0.25f),
		C("RiverModifier", "60%", nearly(t.m_riverModifier, 0.6)),
		C("ScalesWalls", "Yes", t.m_scalesWalls),
		C("TurnWhileMoving", "No", !t.m_turnWhileMoving),
	};
#undef C
	const std::vector<TableRow> golden = readTable("table_locomotor.tsv");
	REQUIRE(cases.size() == golden.size());
	std::set<std::string> covered;
	for (size_t i = 0; i < cases.size(); ++i)
	{
		INFO("token " << cases[i].token);
		CHECK(std::string(cases[i].token) == golden[i].token); // one case per row, in table order
		covered.insert(cases[i].token);
		const LocomotorTemplate t = parsedOne(std::string("  ") + cases[i].token + " = " + cases[i].value);
		CHECK(cases[i].check(t));
	}
	CHECK(covered.size() == 88);
}

TEST_CASE("Locomotor: constructor defaults are RW 0x5E4326 (frames scale with LOGICFRAMES_PER_SECOND = 5)")
{
	const LocomotorTemplate t;
	CHECK(t.m_surfaces == 0);
	CHECK(t.m_maxSpeed == 0.0f);
	CHECK(t.m_lookAheadMult == 1.0f);
	CHECK(t.m_maxSpeedDamaged == 1.0f);
	CHECK(t.m_turnTime == 5);
	CHECK(t.m_turnTimeDamaged == 0);
	CHECK(t.m_fastTurnRadius == 20.0f);
	CHECK(t.m_slowTurnRadius == 0.0f);
	CHECK(t.m_turnThreshold == 0.261799395f); // RW 0xBF4A30 = 0x3E860A92 (15 degrees)
	CHECK(t.m_turnThresholdHighSpeed == 3.14159274f);
	CHECK(t.m_acceleration == 5);
	CHECK(t.m_braking == 5);
	CHECK(t.m_liftDamaged == -1.0f);
	CHECK(t.m_minTurnSpeed == 99999.0f);
	CHECK(t.m_speedLimitZ == 999999.0f);
	CHECK(t.m_zAxisBehavior == Z_NO_Z_MOTIVE_FORCE);
	CHECK(t.m_appearance == LOCO_WHEELS_FOUR);
	CHECK(t.m_formationPriority == FORMATION_RANGED1);
	CHECK(t.m_accDecTrigger == 0.5f);
	CHECK(t.m_maxTurnWithoutReform == 6.28318548f);
	CHECK(t.m_pitchStiffness == 0.1f);
	CHECK(t.m_rollStiffness == 0.1f);
	CHECK(t.m_pitchDamping == 0.9f);
	CHECK(t.m_rollDamping == 0.9f);
	CHECK(t.m_maxOverlappedHeight == 3.40282347e+38f);
	CHECK(t.m_uniformAxialDamping == 1.0f);
	CHECK(t.m_airborneTargetingHeight == 0x7fffffff);
	CHECK(t.m_closeEnoughDist == 1.0f);
	CHECK(t.m_wanderLengthFactor == 1.0f);
	CHECK(t.m_wanderWidthFactor == 0.0f);
	CHECK(t.m_enableHighSpeedTurnModelconditions);
	CHECK_FALSE(t.m_waitForFormation);
	CHECK(t.m_swoopStandoffRadius == 200.0f);
	CHECK(t.m_swoopStandoffHeight == 200.0f);
	CHECK(t.m_swoopTerminalVelocity == 0.07f);
	CHECK(t.m_swoopAccelerationRate == 0.003f);
	CHECK(t.m_swoopSpeedTuningFactor == 1.0f);
	CHECK(t.m_backingUpSpeed == 0.75f);
	CHECK(t.m_backingUpAngle == 0.5f);
	CHECK(t.m_riverModifier == 1.0f);
	CHECK_FALSE(t.m_scalesWalls);
	CHECK(t.m_turnWhileMoving);
	CHECK(t.m_canMoveBackwards == 0);
	CHECK_FALSE(t.m_stickToGround);
	CHECK(t.m_name.empty());
	CHECK_FALSE(t.m_isOverride);
}

TEST_CASE("Locomotor: golden conversions (spec checklist step 1): 500 ms is 3 frames, 2000 ms is 10, Speed 55 is 11 per frame")
{
	const LocomotorTemplate human = parsedOne("  TurnTime = 500\n  TurnTimeDamaged = 500\n  Acceleration = 510\n  Braking = 510\n  Speed = 55");
	CHECK(human.m_turnTime == 3);
	CHECK(human.m_turnTimeDamaged == 3);
	CHECK(human.m_acceleration == 3);
	CHECK(human.m_braking == 3);
	CHECK(human.m_maxSpeed == 11.0f);
	const LocomotorTemplate horde = parsedOne("  TurnTime = 2000\n  Acceleration = 500\n  Braking = 500");
	CHECK(horde.m_turnTime == 10);
	CHECK(horde.m_acceleration == 3);
	CHECK(horde.m_braking == 3);
	// a non-multiple rounds up (ceil), 1 ms is 1 frame, 0 stays 0
	CHECK(parsedOne("  Acceleration = 1").m_acceleration == 1);
	CHECK(parsedOne("  Acceleration = 200").m_acceleration == 1);
	CHECK(parsedOne("  Acceleration = 201").m_acceleration == 2);
	CHECK(parsedOne("  Acceleration = 0").m_acceleration == 0);
}

TEST_CASE("Locomotor: post-parse fixups (RW 0x5E3143)")
{
	// TurnTimeDamaged 0 -> TurnTime; LiftDamaged < 0 -> Lift
	const LocomotorTemplate a = parsedOne("  TurnTime = 1000\n  Lift = 40%");
	CHECK(a.m_turnTimeDamaged == 5);
	CHECK(nearly(a.m_liftDamaged, 0.4));
	const LocomotorTemplate b = parsedOne("  TurnTime = 1000\n  TurnTimeDamaged = 400\n  Lift = 40%\n  LiftDamaged = 10%");
	CHECK(b.m_turnTimeDamaged == 2);
	CHECK(nearly(b.m_liftDamaged, 0.1));
	// WINGS: MinSpeed and MinTurnSpeed <= 0 become 0.01; a positive value is kept
	const LocomotorTemplate w = parsedOne("  Appearance = WINGS\n  MinTurnSpeed = 0%");
	CHECK(w.m_minSpeed == 0.01f);
	CHECK(w.m_minTurnSpeed == 0.01f);
	const LocomotorTemplate w2 = parsedOne("  Appearance = WINGS\n  MinSpeed = 50%\n  MinTurnSpeed = 25%");
	CHECK(nearly(w2.m_minSpeed, 0.5));
	CHECK(nearly(w2.m_minTurnSpeed, 0.25));
	// not WINGS: left alone
	const LocomotorTemplate n = parsedOne("  Appearance = TWO_LEGS");
	CHECK(n.m_minSpeed == 0.0f);
	CHECK(n.m_minTurnSpeed == 99999.0f);
}

TEST_CASE("Locomotor: CanMoveBackwards is atoi in 1..4, else a bool (RW 0x5E3183)")
{
	CHECK(parsedOne("  CanMoveBackwards = Yes").m_canMoveBackwards == 1);
	CHECK(parsedOne("  CanMoveBackwards = No").m_canMoveBackwards == 0);
	CHECK(parsedOne("  CanMoveBackwards = 1").m_canMoveBackwards == 1);
	CHECK(parsedOne("  CanMoveBackwards = 3").m_canMoveBackwards == 3);
	CHECK(parsedOne("  CanMoveBackwards = 4").m_canMoveBackwards == 4);
	CHECK(parsedOne("  CanMoveBackwards = 2junk").m_canMoveBackwards == 2); // atoi stops at the first non-digit
	LocoWorld w;
	int code = 0;
	CHECK_FALSE(w.load("Locomotor T\n CanMoveBackwards = 5\nEnd\n", INI_LOAD_OVERWRITE, &code).empty()); // scanBool("5") fails
	CHECK(code == 3);
	// "0" is outside 1..4, so it goes to scanBool, which only knows Yes / No: retail rejects it
	CHECK_FALSE(w.load("Locomotor U\n CanMoveBackwards = 0\nEnd\n", INI_LOAD_OVERWRITE, &code).empty());
	CHECK(code == 3);
}

TEST_CASE("Locomotor: errors are retail's (unknown field, bad index name, missing End, missing store)")
{
	LocoWorld w;
	int code = 0;
	std::string e = w.load("Locomotor A\n  NoSuchField = 1\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
	CHECK(e.find("Unknown field 'NoSuchField' in block 'Locomotor'") != std::string::npos);
	e = w.load("Locomotor B\n  Appearance = BOGUS\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(e.find("Token 'BOGUS' is not a valid member of the index list") != std::string::npos);
	e = w.load("Locomotor C\n  Surfaces = GROUND +WATER\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	e = w.load("Locomotor D\n  Speed = 5\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 4); // Missing 'END' token
	// no store installed: RW 0x5E8290 "TheLocomotorStore==NULL"
	TheLocomotorStore = nullptr;
	e = w.load("Locomotor E\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(e.find("TheLocomotorStore==NULL") != std::string::npos);
	TheLocomotorStore = &w.store;
}

TEST_CASE("LocomotorStore: duplicate names follow the load type (RW 0x5E82E7-0x5E83EE)")
{
	LocoWorld w;
	REQUIRE(w.load("Locomotor A\n  TurnTime = 1000\nEnd\n").empty());
	REQUIRE(w.store.size() == 1);
	CHECK(only(w, "A")->m_turnTime == 5);
	CHECK_FALSE(only(w, "A")->m_isOverride);
	CHECK(only(w, "A")->m_name == "A");

	// OVERWRITE / MULTIFILE on an existing name: the block is NOT read, so its lines reach the dispatcher
	int code = 0;
	std::string e = w.load("Locomotor A\n  TurnTime = 2000\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
	CHECK(e.find("Unknown block 'TurnTime'") != std::string::npos);
	CHECK(only(w, "A")->m_turnTime == 5);
	e = w.load("Locomotor A\n  TurnTime = 2000\nEnd\n", INI_LOAD_MULTIFILE, &code);
	CHECK(code == 5);
	// an empty block is consumed by nothing: its End is the unknown block
	e = w.load("Locomotor A\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
	CHECK(e.find("Unknown block 'End'") != std::string::npos);

	// CREATE_OVERRIDES: a new override copied from the final override, linked behind the base
	REQUIRE(w.load("Locomotor A\n  TurnTime = 2000\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	const LocomotorTemplate *base = only(w, "A");
	CHECK(base->m_turnTime == 5); // the base is untouched
	REQUIRE(base->m_override != nullptr);
	CHECK(base->m_override->m_isOverride);
	CHECK(base->getFinalOverride()->m_turnTime == 10);
	CHECK(base->getFinalOverride()->m_name == "A");
	// a second override copies the first one (the FINAL override), so unset fields carry over
	REQUIRE(w.load("Locomotor A\n  Acceleration = 1000\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(base->getFinalOverride()->m_turnTime == 10);
	CHECK(base->getFinalOverride()->m_acceleration == 5);
	CHECK(base->m_override->m_override != nullptr);
	CHECK(w.store.size() == 1);

	// a new name under CREATE_OVERRIDES is marked as an override
	REQUIRE(w.load("Locomotor N\nEnd\n", INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(only(w, "N")->m_isOverride);

	// RELOAD replaces the template with a fresh one and parks the old
	REQUIRE(w.load("Locomotor A\n  Braking = 1000\nEnd\n", INI_LOAD_RELOAD).empty());
	CHECK(only(w, "A")->m_braking == 5);
	CHECK(only(w, "A")->m_turnTime == 5);
	CHECK(only(w, "A")->m_override == nullptr);
	CHECK(w.store.reloadedCount() == 1);
}

TEST_CASE("LocomotorStore: template names are case-sensitive (NameKey hash and strcmp, RW 0x548538)")
{
	LocoWorld w;
	REQUIRE(w.load("Locomotor Human\nEnd\n").empty());
	REQUIRE(w.load("Locomotor human\nEnd\n").empty()); // a different template, not a duplicate
	CHECK(w.store.size() == 2);
	CHECK(w.store.findLocomotorTemplate("HUMAN") == nullptr);
}

// ---- LocomotorSet -------------------------------------------------------------------------------------
namespace
{
struct FakeOwner : LocomotorSetOwner
{
	std::string name = "TestObject";
	bool ai = true;
	std::set<int> aiHas;
	LocomotorSetTemplate set;
	const std::string &locomotorSetObjectName() const override { return name; }
	bool hasAIUpdateModule() const override { return ai; }
	bool aiUpdateHasLocomotorsFor(int c) const override { return aiHas.count(c) != 0; }
	LocomotorSetTemplate &locomotorSet() override { return set; }
};

std::string loadSet(LocoWorld &w, FakeOwner &owner, const std::string &text, INILoadType type = INI_LOAD_OVERWRITE, int *code = nullptr)
{
	// one registration per world; the handler follows the owner of the latest call
	static FakeOwner *current = nullptr;
	current = &owner;
	if (!w.fx.env.blocks.contains("LocomotorSet"))
	{
		w.fx.env.blocks.registerBlock("LocomotorSet", [](INI *ini) { parseLocomotorSet(ini, static_cast<LocomotorSetOwner *>(current), nullptr, nullptr); });
	}
	const std::string err = loadError(w.fx.env, "set.ini", text, type, code);
	return err;
}
}

TEST_CASE("LocomotorSet: entries resolve the Locomotor name and the SET_* condition, speed is a plain Real")
{
	LocoWorld w;
	REQUIRE(w.load("Locomotor HumanLocomotor\nEnd\nLocomotor WanderLoco\nEnd\n").empty());
	FakeOwner owner;
	int code = 0;
	std::string e = loadSet(w, owner,
		"LocomotorSet\n  Locomotor = HumanLocomotor\n  Condition = SET_NORMAL\n  Speed = 55\nEnd\n"
		"LocomotorSet\n  Locomotor = WanderLoco\n  Condition = SET_WANDER\n  Speed = 22.5\nEnd\n", INI_LOAD_OVERWRITE, &code);
	REQUIRE_MESSAGE(e.empty(), e);
	REQUIRE(owner.set.conditionCount() == 2);
	const LocomotorSetTemplate::Slot *n = owner.set.find(LOCOMOTORSET_NORMAL);
	REQUIRE(n != nullptr);
	REQUIRE(n->locomotors.size() == 1);
	CHECK(n->locomotors[0] == w.store.findLocomotorTemplate("HumanLocomotor"));
	CHECK(n->speed == 55.0f); // parseReal: raw distance per second, NOT the per-frame velocity
	CHECK(owner.set.find(LOCOMOTORSET_WANDER)->speed == 22.5f);
	CHECK(owner.set.unresolvedLocomotors().empty());
}

TEST_CASE("LocomotorSet: Condition has no default, an unknown locomotor is stored as null (both retail behaviours)")
{
	LocoWorld w;
	FakeOwner owner;
	int code = 0;
	std::string e = loadSet(w, owner, "LocomotorSet\n  Locomotor = X\n  Speed = 5\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(e.find("Token '' is not a valid member of the index list") != std::string::npos);
	e = loadSet(w, owner, "LocomotorSet\n  Locomotor = Nobody\n  Condition = SET_PANIC\n  Speed = 5\nEnd\n", INI_LOAD_OVERWRITE, &code);
	REQUIRE_MESSAGE(e.empty(), e);
	const LocomotorSetTemplate::Slot *s = owner.set.find(LOCOMOTORSET_PANIC);
	REQUIRE(s != nullptr);
	REQUIRE(s->locomotors.size() == 1);
	CHECK(s->locomotors[0] == nullptr);
	CHECK(owner.set.unresolvedLocomotors() == std::vector<std::string>{ "Nobody" });
	e = loadSet(w, owner, "LocomotorSet\n  Locomotor = A\n  Condition = SET_BOGUS\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(e.find("Token 'SET_BOGUS' is not a valid member of the index list") != std::string::npos);
	e = loadSet(w, owner, "LocomotorSet\n  Bogus = A\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 5);
}

TEST_CASE("LocomotorSet: needs an AIUpdate, and re-specifying a condition is refused except under CREATE_OVERRIDES / CHILD_OBJECT")
{
	LocoWorld w;
	REQUIRE(w.load("Locomotor L\nEnd\n").empty());
	FakeOwner owner;
	owner.ai = false;
	int code = 0;
	std::string e = loadSet(w, owner, "LocomotorSet\n  Locomotor = L\n  Condition = SET_NORMAL\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(e.find("Attempted to specify a locomotor for object TestObject without an AIUpdate\tblock.") != std::string::npos);

	owner.ai = true;
	owner.aiHas.insert(LOCOMOTORSET_NORMAL);
	e = loadSet(w, owner, "LocomotorSet\n  Locomotor = L\n  Condition = SET_NORMAL\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(code == 3);
	CHECK(e.find("re-specifying a LocomotorSet\tis no longer allowed") != std::string::npos);
	e = loadSet(w, owner, "LocomotorSet\n  Locomotor = L\n  Condition = SET_NORMAL\nEnd\n", INI_LOAD_MULTIFILE, &code);
	CHECK(code == 3);
	e = loadSet(w, owner, "LocomotorSet\n  Locomotor = L\n  Condition = SET_NORMAL\nEnd\n", INI_LOAD_CREATE_OVERRIDES, &code);
	CHECK(e.empty());
	e = loadSet(w, owner, "LocomotorSet\n  Locomotor = L\n  Condition = SET_NORMAL\nEnd\n", INI_LOAD_CHILD_OBJECT, &code);
	CHECK(e.empty());
	// a condition the AIUpdate does not have yet is fine
	e = loadSet(w, owner, "LocomotorSet\n  Locomotor = L\n  Condition = SET_WANDER\nEnd\n", INI_LOAD_OVERWRITE, &code);
	CHECK(e.empty());
}

TEST_CASE("LocomotorSet: a second entry for one condition replaces the template list (RW 0x73FB3F clears, then pushes)")
{
	LocoWorld w;
	REQUIRE(w.load("Locomotor L1\nEnd\nLocomotor L2\nEnd\n").empty());
	FakeOwner owner;
	std::string e = loadSet(w, owner,
		"LocomotorSet\n  Locomotor = L1\n  Condition = SET_NORMAL\n  Speed = 10\nEnd\n"
		"LocomotorSet\n  Locomotor = L2\n  Condition = SET_NORMAL\n  Speed = 20\nEnd\n");
	REQUIRE_MESSAGE(e.empty(), e);
	const LocomotorSetTemplate::Slot *s = owner.set.find(LOCOMOTORSET_NORMAL);
	REQUIRE(s != nullptr);
	REQUIRE(s->locomotors.size() == 1);
	CHECK(s->locomotors[0] == w.store.findLocomotorTemplate("L2"));
	CHECK(s->speed == 20.0f);
}

// ---- retail corpus (SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset) --------------------------------------
namespace
{
std::string lowerCopy(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

std::vector<std::string> splitLines(const std::vector<std::uint8_t> &bytes)
{
	std::vector<std::string> lines;
	std::string cur;
	for (std::uint8_t c : bytes)
	{
		if (c == '\n')
		{
			if (!cur.empty() && cur.back() == '\r')
			{
				cur.pop_back();
			}
			lines.push_back(cur);
			cur.clear();
		}
		else
		{
			cur.push_back((char)c);
		}
	}
	lines.push_back(cur);
	return lines;
}

// first whitespace/'='-delimited token of a line with its ';' comment removed ("" for blank lines)
std::string firstTokenNoComment(const std::string &line)
{
	const std::string body = line.substr(0, line.find(';'));
	const size_t b = body.find_first_not_of(" \t=");
	if (b == std::string::npos)
	{
		return std::string();
	}
	const size_t e = body.find_first_of(" \t=", b);
	return body.substr(b, e == std::string::npos ? std::string::npos : e - b);
}
}

TEST_CASE("Locomotor retail: every Locomotor of the pure 2.01 INI set parses with zero errors")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("Locomotor retail");
		return;
	}
	REQUIRE_MESSAGE(mount->error.empty(), mount->error);
	Win32BIGFileSystem *fsys = mount->fs.get();
	REQUIRE(fsys != nullptr);

	INIEnvironment env;
	env.fileSystem = fsys;
	LocomotorStore store;
	LocomotorStore *saved = TheLocomotorStore;
	TheLocomotorStore = &store;
	env.blocks.registerBlock("Locomotor", [](INI *ini) { LocomotorStore::parseLocomotorTemplateDefinitionGlobal(ini); });

	// the global #define table first (every *.ini, as retail's INI::load does before parsing blocks)
	FilenameList all;
	fsys->getFileListInDirectory(std::string(), "Data\\", "*.ini", all, true);
	REQUIRE(all.size() > 600);
	INI pre(env);
	for (const std::string &f : all)
	{
		REQUIRE_NOTHROW(pre.preprocessFile(f, INI_LOAD_OVERWRITE));
	}

	// the two files the TheLocomotorStore subsystem loads (subsystem legend, spec 3.4)
	size_t expectedBlocks = 0;
	std::vector<std::string> errors;
	for (const char *file : { "Data\\INI\\Locomotor.ini", "Data\\INI\\Object\\Cinematic\\CinematicLocomotor.ini" })
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(fsys->readFile(file, bytes, &error), error);
		for (const std::string &line : splitLines(bytes))
		{
			// an independent line scan: a column-0 `Locomotor <name>` header (the python census of this lane says 120 + 5)
			if (line.size() > 10 && line.compare(0, 10, "Locomotor ") == 0)
			{
				++expectedBlocks;
			}
		}
		INI ini(env);
		try
		{
			ini.load(file, INI_LOAD_OVERWRITE);
		}
		catch (const INIException &e)
		{
			errors.push_back(std::string(file) + ": " + e.message());
		}
	}
	CHECK(errors.empty());
	for (const std::string &e : errors)
	{
		MESSAGE(e);
	}
	CHECK(expectedBlocks == 125);
	CHECK(store.size() == expectedBlocks);

	// values the spec lists (horde-and-movement.md 1.5, 2.13), at 5 frames per second
	const LocomotorTemplate *human = store.findLocomotorTemplate("HumanLocomotor");
	REQUIRE(human != nullptr);
	CHECK(human->m_appearance == LOCO_LEGS_TWO);
	CHECK(human->m_turnTime == 3);
	CHECK(human->m_acceleration == 3);
	CHECK(human->m_braking == 3);
	CHECK(human->m_formationPriority == FORMATION_MELEE1);
	CHECK(human->m_canMoveBackwards == 1);
	CHECK(human->m_stickToGround);
	CHECK(human->m_surfaces == (LOCOMOTORSURFACE_GROUND | LOCOMOTORSURFACE_RUBBLE));
	CHECK(nearly(human->m_minTurnSpeed, 0.0));
	const LocomotorTemplate *horde = store.findLocomotorTemplate("NormalMeleeHordeLocomotor");
	REQUIRE(horde != nullptr);
	CHECK(horde->m_appearance == LOCO_HORDE);
	CHECK(horde->m_turnTime == 10);
	CHECK(horde->m_acceleration == 3);
	CHECK(horde->m_braking == 3);
	CHECK(nearly(horde->m_maxTurnWithoutReform, 45.0 * 0.017453292, 1e-5));
	CHECK_FALSE(horde->m_turnWhileMoving);
	CHECK(horde->m_waitForFormation);
	CHECK(horde->m_formationPriority == FORMATION_MELEE1);
	CHECK(horde->m_canMoveBackwards == 0);
	const LocomotorTemplate *horse = store.findLocomotorTemplate("NormalHorseHordeMemberLocomotor");
	REQUIRE(horse != nullptr);
	CHECK(horse->m_appearance == LOCO_WHEELS_FOUR);
	CHECK(horse->m_hasSuspension);

	// every LocomotorSet block in the object INIs: scanned by the test (the Object parser is OBJ-1's), parsed by the port
	size_t setBlocks = 0;
	std::vector<std::string> setErrors;
	std::set<int> conditions;
	FakeOwner owner;
	size_t unresolved = 0;
	for (const std::string &file : all)
	{
		std::vector<std::uint8_t> bytes;
		std::string error;
		REQUIRE_MESSAGE(fsys->readFile(file, bytes, &error), error);
		std::string synthetic;
		size_t fileBlocks = 0;
		bool in = false;
		for (const std::string &line : splitLines(bytes))
		{
			const std::string tok = lowerCopy(firstTokenNoComment(line));
			if (!in && tok == "locomotorset")
			{
				in = true;
				++fileBlocks;
				synthetic += "LocomotorSet\n";
			}
			else if (in)
			{
				synthetic += line + "\n";
				if (tok == "end")
				{
					in = false;
				}
			}
		}
		if (fileBlocks == 0)
		{
			continue;
		}
		setBlocks += fileBlocks;
		if (!env.blocks.contains("LocomotorSet"))
		{
			static FakeOwner *current = nullptr;
			current = &owner;
			env.blocks.registerBlock("LocomotorSet", [](INI *ini) { parseLocomotorSet(ini, static_cast<LocomotorSetOwner *>(current), nullptr, nullptr); });
		}
		INI ini(env);
		try
		{
			ini.loadMemory(file + "#locomotorsets", toBytes(synthetic), INI_LOAD_OVERWRITE);
		}
		catch (const INIException &e)
		{
			setErrors.push_back(file + ": " + e.message());
		}
	}
	for (const auto &kv : owner.set.slots())
	{
		conditions.insert(kv.first);
	}
	unresolved = owner.set.unresolvedLocomotors().size();
	for (const std::string &e : setErrors)
	{
		MESSAGE(e);
	}
	CHECK(setErrors.empty());
	CHECK(setBlocks == 905); // independent python line scan over the extracted pure 2.01 INI set
	CHECK(unresolved == 0);  // every named Locomotor exists in the store
	CHECK(conditions.size() == 12);

	TheLocomotorStore = saved;
}
