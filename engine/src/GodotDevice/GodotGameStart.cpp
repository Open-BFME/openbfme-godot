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
	// lane RULES-1: the ten game rules (GameInfo + 0x60; all -1 = no rule block) and OpenBFME's Legendary Heroes factor
	Array rules;
	for (std::int32_t v : m.game.rules)
	{
		rules.push_back((int64_t)v);
	}
	d["rules"] = rules;
	d["legendary_heroes"] = m.game.legendaryHeroes;
	d["cheat_mode"] = m.game.cheatMode; // lane CHEAT-1: -1 off, else the cheat seat's slot
	d["simulation_profile"] = (int)m.game.simulationProfile; // lane UBER-1: 0 Faithful, 1 Uber (Common/SimulationProfile.h)
	// lane P16: the wire's extended GameInfo tag (NetPacket kExtendedGameInfoTag): a legacy game's dictionary stays exactly as it was
	// (eight slots, no capacity key); an extended map's carries map_start_count (9..16) and all 16 slot entries
	if (m.game.mapStartCount > LEGACY_MAX_SLOTS)
	{
		d["map_start_count"] = m.game.mapStartCount;
	}
	Array slots;
	for (int i = 0; i < m.game.serializedSlotCount(); ++i)
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
		sd["handicap"] = s.handicap; // lane OWNERFIX-2: GameSlot + 0x20
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
	// lane RULES-1: "rules" (ten ints) and "legendary_heroes" (1 .. 4); a dictionary without them (a script's own game) is a game with no rule block and the
	// rule off, the same meaning as a NewGameMessage built without the lobby
	if (d.has("rules"))
	{
		const Array rules = d["rules"];
		if (rules.size() != NUM_GAME_RULES)
		{
			return fail("rules has " + std::to_string((int)rules.size()) + " entries, expected " + std::to_string(NUM_GAME_RULES));
		}
		for (int i = 0; i < NUM_GAME_RULES; ++i)
		{
			m.game.rules[(size_t)i] = (std::int32_t)(int64_t)rules[i];
		}
	}
	m.game.legendaryHeroes = (int)(int64_t)d.get("legendary_heroes", (int64_t)LegendaryHeroes::kOff);
	if (!LegendaryHeroes::valid(m.game.legendaryHeroes))
	{
		return fail("legendary_heroes " + std::to_string(m.game.legendaryHeroes) + " (1 .. 4)");
	}
	// lane CHEAT-1: "cheat_mode" (absent: -1, off); the seat must be a human slot (checked once the slots are read, below)
	m.game.cheatMode = (int)(int64_t)d.get("cheat_mode", (int64_t)-1);
	// lane UBER-1: the simulation profile (absent: Faithful, the setups before UBER-1); a value this build does not know is refused
	const int64_t profile = (int64_t)d.get("simulation_profile", (int64_t)SimulationProfile::Faithful);
	if (!simulationProfileValid((std::int32_t)profile))
	{
		return fail("simulation_profile " + std::to_string((long long)profile) + " (0 faithful, 1 uber)");
	}
	m.game.simulationProfile = (SimulationProfile)profile;
	// lane P16: an absent "map_start_count" is a legacy 8-slot message (the form every build before the extension produced); present, it is
	// the extended tag's capacity, 9 .. MAX_SLOTS, and the message carries all 16 slot entries, those beyond the capacity closed (the wire's rule)
	if (d.has("map_start_count") && d["map_start_count"].get_type() != Variant::INT)
	{
		return fail("map_start_count must be an integer"); // P16-UI: "16" or 16.5 is refused, never converted
	}
	const int64_t mapStartCount = (int64_t)d.get("map_start_count", (int64_t)LEGACY_MAX_SLOTS);
	// ONLINE16-FIX: present means the extended tag, so an explicit 8 is refused like 7 or 17 (it was accepted as the legacy message)
	if (d.has("map_start_count") ? (mapStartCount <= LEGACY_MAX_SLOTS || mapStartCount > MAX_SLOTS) : false)
	{
		return fail("map_start_count " + std::to_string((long long)mapStartCount) + " (requires 9 .. " + std::to_string(MAX_SLOTS) + ")");
	}
	m.game.mapStartCount = (int)mapStartCount;
	const Array slots = d["slots"];
	if (slots.size() != m.game.serializedSlotCount())
	{
		return fail("slots has " + std::to_string((int)slots.size()) + " entries, expected " + std::to_string(m.game.serializedSlotCount()));
	}
	for (int i = 0; i < m.game.serializedSlotCount(); ++i)
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
		if (i >= m.game.mapStartCount && state != SLOT_CLOSED)
		{
			return fail("slot " + std::to_string(i) + " exceeds the map's start positions");
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
		// lane OWNERFIX-2: the slot's handicap (absent: 0, the setups before OWNERFIX-2); a value the lobby never offers is refused
		s.handicap = (int)(int64_t)sd.get("handicap", (int64_t)0);
		if (!SkirmishGameSlot::handicapValid(s.handicap))
		{
			return fail("slot " + std::to_string(i) + " handicap " + std::to_string(s.handicap) + " (0, -5 .. -95)");
		}
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
	// lane CHEAT-1: the cheat seat is a human slot (the host's), as NetPacket::readGameInfo holds a received setup to it
	if (m.game.cheatMode < -1 || m.game.cheatMode >= MAX_SLOTS || (m.game.cheatMode >= 0 && !m.game.slots[m.game.cheatMode].isHuman()))
	{
		return fail("cheat_mode " + std::to_string(m.game.cheatMode) + " is not a human slot");
	}
	out = m;
	return true;
}

bool loadScreenFromDictionary(const Dictionary &d, LoadScreenInfo &out, std::string *error)
{
	auto fail = [&](const std::string &why) {
		if (error) *error = why;
		return false;
	};
	for (const char *key : { "map_start_count", "loading_type", "local_card" })
	{
		if (d.has(key) && d[key].get_type() != Variant::INT) return fail(std::string(key) + " must be an integer");
	}
	LoadScreenInfo info;
	const int64_t capacity = d.get("map_start_count", (int64_t)LEGACY_MAX_SLOTS);
	if (capacity < LEGACY_MAX_SLOTS || capacity > MAX_LOAD_SLOTS) return fail("loading map capacity must be 8 .. 16");
	info.mapStartCount = (int)capacity;
	info.gameLoadingType = (int)(int64_t)d.get("loading_type", (int64_t)0);
	const int64_t local = d.get("local_card", (int64_t)0);
	if (local < 0 || local >= capacity) return fail("local loading card outside map capacity");
	info.localCard = (int)local;
	if (d.has("map") && d["map"].get_type() != Variant::STRING) return fail("loading map must be a string");
	info.mapName = toNativeUtf8(d.get("map", String()));
	if (d.has("cards") && d["cards"].get_type() != Variant::ARRAY) return fail("loading cards must be an array");
	const Array cards = d.get("cards", Array());
	if (cards.size() > capacity) return fail("loading cards exceed map capacity");
	for (int64_t i = 0; i < cards.size(); ++i)
	{
		if (cards[i].get_type() != Variant::DICTIONARY) return fail("loading card must be a dictionary");
		const Dictionary cd = cards[i];
		for (const char *key : { "rank", "team", "color" })
		{
			if (cd.has(key) && cd[key].get_type() != Variant::INT) return fail(std::string("loading card ") + key + " must be an integer");
		}
		for (const char *key : { "name", "army", "load_music" })
		{
			if (cd.has(key) && cd[key].get_type() != Variant::STRING) return fail(std::string("loading card ") + key + " must be a string");
		}
		if (cd.has("occupied") && cd["occupied"].get_type() != Variant::BOOL) return fail("loading card occupied must be a boolean");
		LoadScreenSlotInfo &s = info.cards[i];
		s.occupied = (bool)cd.get("occupied", true);
		s.playerName = godotToU16(cd.get("name", String()));
		s.armyName = godotToU16(cd.get("army", String()));
		s.rank = (int)(int64_t)cd.get("rank", (int64_t)0);
		s.teamNumber = (int)(int64_t)cd.get("team", (int64_t)-1);
		s.color = (std::uint32_t)(int64_t)cd.get("color", (int64_t)0);
		s.loadMusic = toNativeUtf8(cd.get("load_music", String()));
	}
	out = std::move(info);
	return true;
}
} // namespace godot
