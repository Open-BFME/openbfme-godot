// OpenBFME. GPL-3.0.
//
// Device layer, lane START-1: the Godot-side form of the new-game message (SkirmishGameInfo) and of the load screen's data, so a GDScript scene can take the
// message the lobby's StartGame queued from the shell (AptMenuPlayer.take_new_game) and give it to the live game (GameWorld.start_new_game) without a
// C++ pointer crossing the script, and so tests can read every field.
//
// Dictionary form of a NewGameMessage:
//   { mode: "skirmish"|"single_player", difficulty, rank_points, map: String (the map cache key), map_crc, map_size, map_mask, seed, starting_cash,
//     superweapon_restriction, in_progress: bool, map_start_count: int (optional; absent = legacy 8, extended maps = 9 .. 16),
//     slots: [8 x { state: int (SlotState: 0 open, 1 closed, 2..5 easy..brutal AI, 6 human), name: String, accepted, color, start_pos, player_template, team,
//     create_a_hero: PackedByteArray (optional, lane HERO-2: the slot's Create-a-Hero record in its .cah form) }] }
// Extended maps carry 16 serialized slots; slots beyond map_start_count are closed. Legacy dictionaries stay eight-wide.

#pragma once

#include "GameNetwork/GameInfo.h"

#include <godot_cpp/variant/dictionary.hpp>

#include <string>

struct LoadScreenInfo;

namespace godot
{
Dictionary newGameToDictionary(const NewGameMessage &message);
// false + *error when a field is missing or out of range (never a default)
bool newGameFromDictionary(const Dictionary &d, NewGameMessage &out, std::string *error);
// { cards: [{ occupied, name, army, rank, team, color, load_music }...], local_card: int, loading_type: int, map: String }
bool loadScreenFromDictionary(const Dictionary &d, LoadScreenInfo &out, std::string *error = nullptr);
} // namespace godot
