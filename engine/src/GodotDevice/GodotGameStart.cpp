// OpenBFME. GPL-3.0.
// See GodotDevice/GodotGameStart.h.

#include "GodotDevice/GodotGameStart.h"

#include "GameClient/GUI/LoadScreenInfo.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot
{
namespace
{
String u16ToGodot(const std::u16string &s)
{
	String out;
	for (char16_t c : s)
	{
		out += String::chr((char32_t)c);
	}
	return out;
}
std::u16string godotToU16(const String &s)
{
	std::u16string out;
	for (int64_t i = 0; i < s.length(); ++i)
	{
		const char32_t c = s[i];
		if (c >= 0x10000)
		{
			const char32_t v = c - 0x10000;
			out.push_back((char16_t)(0xD800 + (v >> 10)));
			out.push_back((char16_t)(0xDC00 + (v & 0x3FF)));
		}
		else
		{
			out.push_back((char16_t)c);
		}
	}
	return out;
}
std::string toNativeUtf8(const String &s)
{
	CharString u = s.utf8();
	return std::string(u.get_data(), (size_t)u.length());
}
} // namespace

Dictionary newGameToDictionary(const NewGameMessage &m)
{
	Dictionary d;
	d["mode"] = m.mode == NewGameMode::Skirmish ? "skirmish" : "single_player";
	d["difficulty"] = m.difficulty;
	d["rank_points"] = m.rankPoints;
	d["map"] = String::utf8(m.game.mapName.c_str(), (int64_t)m.game.mapName.size());
	d["map_crc"] = (int64_t)m.game.mapCRC;
	d["map_size"] = (int64_t)m.game.mapSize;
	d["map_mask"] = m.game.mapMask;
	d["seed"] = (int64_t)m.game.seed;
	d["starting_cash"] = m.game.startingCash;
	d["superweapon_restriction"] = m.game.superweaponRestriction;
	d["in_progress"] = m.game.inProgress;
	Array slots;
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const SkirmishGameSlot &s = m.game.slots[i];
		Dictionary sd;
		sd["state"] = (int)s.state;
		sd["name"] = u16ToGodot(s.name);
		sd["accepted"] = s.accepted;
		sd["has_map"] = s.hasMap;
		sd["color"] = s.color;
		sd["start_pos"] = s.startPos;
		sd["player_template"] = s.playerTemplate;
		sd["team"] = s.teamNumber;
		if (s.hasCreateAHero)
		{
			const std::vector<std::uint8_t> bytes = s.createAHero.save();
			PackedByteArray b;
			b.resize((int64_t)bytes.size());
			for (size_t k = 0; k < bytes.size(); ++k)
			{
				b.set((int64_t)k, bytes[k]);
			}
			sd["create_a_hero"] = b;
		}
		slots.push_back(sd);
	}
	d["slots"] = slots;
	return d;
}

bool newGameFromDictionary(const Dictionary &d, NewGameMessage &out, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error)
		{
			*error = "new game message: " + why;
		}
		return false;
	};
	for (const char *key : { "mode", "map", "map_crc", "map_size", "seed", "starting_cash", "slots" })
	{
		if (!d.has(key))
		{
			return fail(std::string("missing '") + key + "'");
		}
	}
	NewGameMessage m;
	const String mode = d["mode"];
	if (mode == "skirmish")
	{
		m.mode = NewGameMode::Skirmish;
	}
	else if (mode == "single_player")
	{
		m.mode = NewGameMode::SinglePlayer;
	}
	else
	{
		return fail("mode '" + toNativeUtf8(mode) + "'");
	}
	m.difficulty = (int)(int64_t)d.get("difficulty", (int64_t)kDifficultyNormal);
	m.rankPoints = (int)(int64_t)d.get("rank_points", (int64_t)0);
	m.game.mapName = toNativeUtf8(d["map"]);
	m.game.mapCRC = (std::uint32_t)(int64_t)d["map_crc"];
	m.game.mapSize = (std::uint32_t)(int64_t)d["map_size"];
	m.game.mapMask = (int)(int64_t)d.get("map_mask", (int64_t)0);
	m.game.seed = (std::uint32_t)(int64_t)d["seed"];
	m.game.startingCash = (int)(int64_t)d["starting_cash"];
	m.game.superweaponRestriction = (int)(int64_t)d.get("superweapon_restriction", (int64_t)0);
	m.game.inProgress = (bool)d.get("in_progress", false);
	const Array slots = d["slots"];
	if (slots.size() != MAX_SLOTS)
	{
		return fail("slots has " + std::to_string((int)slots.size()) + " entries, expected " + std::to_string(MAX_SLOTS));
	}
	for (int i = 0; i < MAX_SLOTS; ++i)
	{
		const Dictionary sd = slots[i];
		for (const char *key : { "state", "name", "color", "start_pos", "player_template", "team" })
		{
			if (!sd.has(key))
			{
				return fail("slot " + std::to_string(i) + " lacks '" + key + "'");
			}
		}
		const int state = (int)(int64_t)sd["state"];
		if (state < SLOT_OPEN || state > SLOT_PLAYER)
		{
			return fail("slot " + std::to_string(i) + " state " + std::to_string(state));
		}
		SkirmishGameSlot &s = m.game.slots[i];
		s.state = (SlotState)state;
		s.name = godotToU16(sd["name"]);
		s.accepted = (bool)sd.get("accepted", false);
		s.hasMap = (bool)sd.get("has_map", true);
		s.color = (int)(int64_t)sd["color"];
		s.startPos = (int)(int64_t)sd["start_pos"];
		s.playerTemplate = (int)(int64_t)sd["player_template"];
		s.teamNumber = (int)(int64_t)sd["team"];
		if (sd.has("create_a_hero"))
		{
			// lane HERO-2: the slot's Create-a-Hero record in its .cah form (GameWorld.get_create_a_hero_record at the setup)
			if (sd["create_a_hero"].get_type() != Variant::PACKED_BYTE_ARRAY)
			{
				return fail("slot " + std::to_string(i) + " create_a_hero is not a PackedByteArray");
			}
			const PackedByteArray b = sd["create_a_hero"];
			std::vector<std::uint8_t> bytes((size_t)b.size());
			for (int64_t k = 0; k < b.size(); ++k)
			{
				bytes[(size_t)k] = b[k];
			}
			std::string why;
			if (!s.setCreateAHeroBytes(bytes, &why))
			{
				return fail("slot " + std::to_string(i) + " " + why);
			}
			if (s.state != SLOT_PLAYER)
			{
				return fail("slot " + std::to_string(i) + " has a Create-a-Hero but no human player");
			}
		}
	}
	out = m;
	return true;
}

void loadScreenFromDictionary(const Dictionary &d, LoadScreenInfo &out)
{
	out = LoadScreenInfo();
	out.gameLoadingType = (int)(int64_t)d.get("loading_type", (int64_t)0);
	out.localCard = (int)(int64_t)d.get("local_card", (int64_t)0);
	out.mapName = toNativeUtf8(d.get("map", String()));
	const Array cards = d.get("cards", Array());
	for (int64_t i = 0; i < cards.size() && i < MAX_LOAD_SLOTS; ++i)
	{
		const Dictionary cd = cards[i];
		LoadScreenSlotInfo &s = out.cards[i];
		s.occupied = (bool)cd.get("occupied", true);
		s.playerName = godotToU16(cd.get("name", String()));
		s.armyName = godotToU16(cd.get("army", String()));
		s.rank = (int)(int64_t)cd.get("rank", (int64_t)0);
		s.teamNumber = (int)(int64_t)cd.get("team", (int64_t)-1);
		s.color = (std::uint32_t)(int64_t)cd.get("color", (int64_t)0);
		s.loadMusic = toNativeUtf8(cd.get("load_music", String()));
	}
}
} // namespace godot
