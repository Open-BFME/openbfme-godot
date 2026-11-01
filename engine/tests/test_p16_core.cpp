// OpenBFME. GPL-3.0. The 16-slot extension is not retail behavior.
#include "doctest.h"

#include "GameClient/GUI/Skirmish/SkirmishGameRecord.h"
#include "GameLogic/NewGame/SkirmishRandom.h"
#include "GameLogic/ProductionSettings.h"
#include "Common/BuildVersion.h"
#include "Common/Recorder.h"

#include <limits>

TEST_CASE("p16 core: legacy eight-slot preferences stay unchanged")
{
	SkirmishGameInfo game;
	CHECK(game.mapStartCount == 8);
	CHECK(game.serializedSlotCount() == 8);
	const std::string text = SkirmishGameInfoToString(game, 0);
	CHECK(text.substr(text.rfind(";S=") + 1) == "S=X:X:X:X:X:X:X:X:;");
	CHECK(text.find("SC=") == std::string::npos);
	CHECK(SkirmishHeroIndexesString() == ":-1:-1:-1:-1:-1:-1:-1:-1:");
}

TEST_CASE("p16 core: slot 8 9 15 16 preference round trip and slot 17 refused")
{
	for (int number : { 8, 9, 15, 16 })
	{
		INFO(number);
		SkirmishGameInfo game;
		game.mapName = "maps/p16/p16.map";
		game.mapStartCount = number;
		game.slots[number - 1].state = SLOT_BRUTAL_AI;
		game.slots[number - 1].startPos = number - 1;
		CHECK(game.serializedSlotCount() == (number == 8 ? 8 : 16));
		SkirmishGameRecordLimits limits;
		limits.numColors = 16;
		limits.numTemplates = 16;
		const std::string text = SkirmishGameInfoToString(game, 0);
		SkirmishGameInfo decoded;
		std::string error;
		REQUIRE_MESSAGE(ParseSkirmishGameInfo(text, limits, decoded, nullptr, &error), error);
		CHECK(decoded.mapStartCount == number);
		CHECK(decoded.slots[number - 1].state == SLOT_BRUTAL_AI);
		CHECK(decoded.slots[number - 1].startPos == number - 1);
		const size_t end = text.rfind(';');
		std::string tooMany = text;
		tooMany.insert(end, "X:");
		CHECK_FALSE(ParseSkirmishGameInfo(tooMany, limits, decoded, nullptr, &error));
		game.mapStartCount = 17;
		CHECK_FALSE(ParseSkirmishGameInfo(SkirmishGameInfoToString(game, 0), limits, decoded, nullptr, &error));
	}
}

TEST_CASE("p16 core: extra occupied slots require matching map capacity")
{
	for (int number : { 8, 9, 15, 16 })
	{
		MapCacheEntry map;
		map.numPlayers = number;
		for (int i = 1; i <= number; ++i)
		{
			map.startPositions[i] = Coord3D{ (float)i * 100.0f, 100.0f, 0.0f };
		}
		SkirmishGameInfo game;
		game.slots[number - 1].state = SLOT_EASY_AI;
		game.slots[number - 1].startPos = number - 1;
		game.slots[number - 1].playerTemplate = 0;
		SkirmishResolveInput input;
		input.map = &map;
		GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
		rng.initGameLogicRandom(1, -1);
		std::string error;
		REQUIRE_MESSAGE(SkirmishRandom::populateRandomStartPosition(game, input, rng, &error), error);
		if (number > 8)
		{
			map.numPlayers = 8;
			CHECK_FALSE(SkirmishRandom::populateRandomStartPosition(game, input, rng, &error));
		}
		map.numPlayers = 17;
		CHECK_FALSE(SkirmishRandom::populateRandomStartPosition(game, input, rng, &error));
	}
}

TEST_CASE("p16 core: startup rejects incomplete unknown and nonfinite starts")
{
	MapCacheEntry map;
	map.numPlayers = 16;
	for (int i = 1; i <= 16; ++i) map.startPositions[i] = Coord3D{ (float)i * 100, 100, 0 };
	std::string error;
	REQUIRE(MapCache::validateStartPositions(map, &error));
	map.startPositions.erase(9);
	CHECK_FALSE(MapCache::validateStartPositions(map, &error));
	CHECK(error.find("Player_9_Start") != std::string::npos);
	map.startPositions[9] = Coord3D{ 900, 100, 0 };
	map.startPositions[17] = Coord3D{};
	CHECK_FALSE(MapCache::validateStartPositions(map, &error));
	map.startPositions.erase(17);
	map.startPositions[16].x = std::numeric_limits<float>::quiet_NaN();
	CHECK_FALSE(MapCache::validateStartPositions(map, &error));
	map.startPositions[16].x = 1600;
	map.numPlayers = 17;
	CHECK_FALSE(MapCache::validateStartPositions(map, &error));
}

TEST_CASE("p16 core: replay network indices preserve legacy width and extended high slots")
{
	for (int count : { 8, 9, 15, 16 })
	{
		INFO(count);
		ReplayHeader header;
		header.game.game.mapName = "maps/p16/p16.map";
		header.game.game.mapStartCount = count;
		header.game.game.slots[count - 1].state = SLOT_PLAYER;
		header.recordingSlot = count - 1;
		header.network.slotPlayerIndex[count - 1] = count + 1;
		ReplayWriter writer;
		writer.setKeepCopy(true);
		std::string error;
		REQUIRE_MESSAGE(writer.open("", header, &error), error);
		writer.recordHash(2, 123);
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(writer.logUpTo(2, 123, bytes, &error), error);
		writer.close(2, 123);
		ReplayFile decoded;
		REQUIRE_MESSAGE(ReplayFile::parse(bytes, decoded, &error), error);
		CHECK(decoded.complete);
		CHECK(decoded.header.game.game.mapStartCount == count);
		CHECK(decoded.header.recordingSlot == count - 1);
		CHECK(decoded.header.network.slotPlayerIndex == header.network.slotPlayerIndex);
		CHECK(decoded.hashes.at(2) == 123);
		if (count == 8)
		{
			// Independently spell the pre-P16 replay header, including exactly eight network indices.
			NetByteWriter expected;
			expected.u32(11); // format 11 (9: the recording build, 10: its date, 11: CHEAT-1's Cheat Mode i32 in the GameInfo); 8 was ANTICHEAT-1's VIEW record (an eight-slot replay without views has none)
			expected.str(header.profile.text);
			expected.str(BuildVersion::kVersion);
			expected.str(BuildVersion::kCommit);
			expected.str(BuildVersion::kBuildDate);
			NetPacket::writeNewGameMessage(expected, header.game);
			expected.i32(header.recordingSlot);
			expected.u32((std::uint32_t)header.algorithm);
			for (int i = 0; i < 8; ++i) expected.i32(header.network.slotPlayerIndex[i]);
			expected.i32(header.network.runAhead);
			expected.i32(header.network.crcInterval);
			expected.u8(2); expected.u32(2); expected.u32(123);
			expected.u8(3); expected.u32(2); expected.u32(123);
			std::vector<std::uint8_t> legacy{ 'O', 'B', 'F', 'M', 'E', 'R', 'E', 'P' };
			legacy.insert(legacy.end(), expected.data().begin(), expected.data().end());
			CHECK(bytes == legacy);
		}
	}
}

TEST_CASE("p16 core: production tables cannot fake complete MP1 through MP8 coverage")
{
	const std::string all = "MP1:1 MP2:1 MP3:1 MP4:1 MP5:1 MP6:1 MP7:1 MP8:1";
	auto data = [&](const std::string &units, const std::string &buildings) {
		return "GameData\nMinLowEnergyProductionSpeed = 1\nMaxLowEnergyProductionSpeed = 1\nLowEnergyPenaltyModifier = 1\nMultipleFactory = 1\n"
			"MultiPlayUnitSpeedMult = " + units + "\nMultiPlayBuildingSpeedMult = " + buildings + "\nEnd\n";
	};
	ProductionSettings settings;
	std::string error;
	REQUIRE_MESSAGE(ProductionSettings::scan(data(all, all), settings, &error), error);
	CHECK(settings.multiPlayEntries == 8);
	for (const std::string &bad : { "MP1:1 MP2:1 MP3:1 MP4:1 MP5:1 MP6:1 MP7:1 MP7:1",
		"MP1:1 MP3:1", "MP1junk:1", "MP01:1", "MP1:nan", "MP9:1" })
	{
		INFO(bad);
		CHECK_FALSE(ProductionSettings::scan(data(bad, all), settings, &error));
		CHECK_FALSE(settings.loaded);
		CHECK_FALSE(error.empty());
	}
	CHECK_FALSE(ProductionSettings::scan(data(all, "MP1:1"), settings, &error));
	CHECK(error.find("different MP entry counts") != std::string::npos);
}
