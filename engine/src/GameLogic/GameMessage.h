// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// GameMessage (ZH Include/Common/GameMessage.h, Source/Common/MessageStream/GameMessage.cpp), lane PROD-1: the player command as the lockstep
// command list carries it: a type, the issuing player and typed arguments. Only the logic-side message types (1001 .. 1147, PLAN rule 4) are here;
// the raw input and meta messages of ZH's translators belong to the UI lanes.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the numbering is the one GameMessage::getCommandAsAsciiString (a jump table over 1002 .. 1147 with
// 1001 special cased) switches on; tools/replay/rotwk_replay.py read it out of the binary: MSG_BEGIN_NETWORK_MESSAGES = 1000, the first logic message
// MSG_CREATE_SELECTED_GROUP = 1001, MSG_LOGIC_CRC = 1098, the last MSG_ADD_TO_TEAM9 = 1147. The argument type enum is the recorder's readArgument
// order (same as ZH): int, real, bool, object id, drawable id, team id, location, pixel, pixel region, timestamp, wide char.
//
// Determinism: a message is plain data; the dispatcher turns it into logic state changes in a defined order (GameLogicDispatch.h).

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <string>
#include <vector>

enum GameMessageType : int
{
	MSG_BEGIN_NETWORK_MESSAGES = 1000,
	MSG_CREATE_SELECTED_GROUP = 1001,
	MSG_CREATE_SELECTED_GROUP_NO_SOUND = 1002,
	MSG_CREATE_SELECTED_GROUP_IDLE_WORKER_VOICE = 1003,
	MSG_DESTROY_SELECTED_GROUP = 1004,
	MSG_REMOVE_FROM_SELECTED_GROUP = 1005,
	MSG_CREATE_TEAM0 = 1006,
	MSG_CREATE_TEAM1 = 1007,
	MSG_CREATE_TEAM2 = 1008,
	MSG_CREATE_TEAM3 = 1009,
	MSG_CREATE_TEAM4 = 1010,
	MSG_CREATE_TEAM5 = 1011,
	MSG_CREATE_TEAM6 = 1012,
	MSG_CREATE_TEAM7 = 1013,
	MSG_CREATE_TEAM8 = 1014,
	MSG_CREATE_TEAM9 = 1015,
	MSG_SELECT_TEAM0 = 1016,
	MSG_SELECT_TEAM1 = 1017,
	MSG_SELECT_TEAM2 = 1018,
	MSG_SELECT_TEAM3 = 1019,
	MSG_SELECT_TEAM4 = 1020,
	MSG_SELECT_TEAM5 = 1021,
	MSG_SELECT_TEAM6 = 1022,
	MSG_SELECT_TEAM7 = 1023,
	MSG_SELECT_TEAM8 = 1024,
	MSG_SELECT_TEAM9 = 1025,
	MSG_ADD_TEAM0 = 1026,
	MSG_ADD_TEAM1 = 1027,
	MSG_ADD_TEAM2 = 1028,
	MSG_ADD_TEAM3 = 1029,
	MSG_ADD_TEAM4 = 1030,
	MSG_ADD_TEAM5 = 1031,
	MSG_ADD_TEAM6 = 1032,
	MSG_ADD_TEAM7 = 1033,
	MSG_ADD_TEAM8 = 1034,
	MSG_ADD_TEAM9 = 1035,
	MSG_DO_ATTACKSQUAD = 1036,
	MSG_DO_WEAPON = 1037,
	MSG_DO_WEAPON_AT_LOCATION = 1038,
	MSG_DO_WEAPON_AT_OBJECT = 1039,
	MSG_DO_SPECIAL_POWER = 1040,
	MSG_DO_SPECIAL_POWER_AT_LOCATION = 1041,
	MSG_DO_SPECIAL_POWER_AT_OBJECT = 1042,
	MSG_SET_RALLY_POINT = 1043,
	MSG_PURCHASE_SCIENCE = 1044,
	MSG_QUEUE_UPGRADE = 1045,
	MSG_CANCEL_UPGRADE = 1046,
	MSG_QUEUE_UNIT_CREATE = 1047,
	MSG_CANCEL_UNIT_CREATE = 1048,
	MSG_FOUNDATION_CONSTRUCT = 1049,
	MSG_DOZER_CONSTRUCT = 1050,
	MSG_DOZER_CANCEL_CONSTRUCT = 1051,
	MSG_SELL = 1052,
	MSG_EXIT = 1053,
	MSG_EVACUATE = 1054,
	MSG_EVACUATE_CONTESTERS = 1055,
	MSG_SACRIFICE = 1056,
	MSG_COMBATDROP_AT_LOCATION = 1057,
	MSG_COMBATDROP_AT_OBJECT = 1058,
	MSG_COMBINE_HORDES_WITH_OBJECT = 1059,
	MSG_AREA_SELECTION = 1060,
	MSG_DO_ATTACK_OBJECT = 1061,
	MSG_DO_FORCE_ATTACK_OBJECT = 1062,
	MSG_DO_FORCE_ATTACK_GROUND = 1063,
	MSG_GET_REPAIRED = 1064,
	MSG_GET_HEALED = 1065,
	MSG_DO_REPAIR = 1066,
	MSG_RESUME_CONSTRUCTION = 1067,
	MSG_ENTER = 1068,
	MSG_DOCK = 1069,
	MSG_HARVEST = 1070,
	MSG_DO_MOVETO = 1071,
	MSG_DO_ATTACKMOVETO = 1072,
	MSG_DO_FORCEMOVETO = 1073,
	MSG_ADD_WAYPOINT = 1074,
	MSG_DO_GUARD_POSITION = 1075,
	MSG_DO_GUARD_OBJECT = 1076,
	MSG_DO_STOP = 1077,
	MSG_DO_SCATTER = 1078,
	MSG_OPEN_GATE = 1079,
	MSG_DO_CHEER = 1080,
	MSG_CLOSE_GATE = 1081,
	MSG_SWITCH_WEAPONS = 1082,
	MSG_CONVERT_TO_CARBOMB = 1083,
	MSG_CAPTUREBUILDING = 1084,
	MSG_CASTLE_UNPACK = 1085,
	MSG_CASTLE_PACK = 1086,
	MSG_CASTLE_UNPACK_EXPLICIT_OBJECT = 1087,
	MSG_SNIPE_VEHICLE = 1088,
	MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION = 1089,
	MSG_DO_SALVAGE = 1090,
	MSG_CLEAR_INGAME_POPUP_MESSAGE = 1091,
	MSG_PLACE_BEACON = 1092,
	MSG_REMOVE_BEACON = 1093,
	MSG_SET_BEACON_TEXT = 1094,
	MSG_SET_REPLAY_CAMERA = 1095,
	MSG_SELF_DESTRUCT = 1096,
	MSG_CREATE_FORMATION = 1097,
	MSG_LOGIC_CRC = 1098,
	MSG_SET_MINE_CLEARING_DETAIL = 1099,
	MSG_DO_USER1 = 1100,
	MSG_DO_USER2 = 1101,
	MSG_DO_USER3 = 1102,
	MSG_DO_USER4 = 1103,
	MSG_MOVE_ARMY_TO_POSITION = 1104,
	MSG_AUTO_SAVE = 1105,
	MSG_CHANGE_CAMERA_ARRIVED_AT_WAYPOINTID = 1106,
	MSG_HORDE_TOGGLE_FORMATION = 1107,
	MSG_ONE_RING = 1108,
	MSG_CREW_EVACUATE = 1109,
	MSG_DO_SPELLBOOK_SPECIAL_POWER = 1110,
	MSG_WEAPONSET_TOGGLE = 1111,
	MSG_DO_AUTO_ABILITY = 1112,
	MSG_DO_AUTO_ABILITY_WEAPON = 1113,
	MSG_REVIVE = 1114,
	MSG_TOGGLE_NO_AUTO_ACQUIRE = 1115,
	MSG_WAKE_AUTO_PICKUP = 1116,
	MSG_START_SELF_REPAIR = 1117,
	MSG_SUMMON_REINFORCEMENTS = 1118,
	MSG_CALL_IN_REINFORCEMENTS = 1119,
	MSG_HORDE_SET_FORMATION = 1120,
	MSG_CREATE_SELECT_ALL_GROUP = 1121,
	MSG_ENABLE_RETALIATION_MODE = 1122,
	MSG_WALL_HUB_CONSTRUCT_SPAN = 1123,
	MSG_DO_MOVETO_FORMATION = 1124,
	MSG_DO_MOVE_AND_ORIENTATE_OBJECTTO = 1125,
	MSG_GIVE_MONEY = 1126,
	MSG_DO_ROTATE_FIRINGARC = 1127,
	MSG_CHANGE_STANCE = 1128,
	MSG_CHANGE_ORDERMODE = 1129,
	MSG_ACTIONQUEUE_EXECUTE_PLANNED = 1130,
	MSG_ACTIONQUEUE_CLEAR_ALL = 1131,
	MSG_ACTIONQUEUE_CLEAR_PLANNED = 1132,
	MSG_ACTIONQUEUE_CLEAR_LAST = 1133,
	MSG_START_NEIGHBORHOOD_REPAIR = 1134,
	MSG_CANCEL_NEIGHBORHOOD = 1135,
	MSG_ORDER_SYNCHRONIZE = 1136,
	MSG_ADD_ALL_FACTION_UPGRADE = 1137,
	MSG_ADD_TO_TEAM0 = 1138,
	MSG_ADD_TO_TEAM1 = 1139,
	MSG_ADD_TO_TEAM2 = 1140,
	MSG_ADD_TO_TEAM3 = 1141,
	MSG_ADD_TO_TEAM4 = 1142,
	MSG_ADD_TO_TEAM5 = 1143,
	MSG_ADD_TO_TEAM6 = 1144,
	MSG_ADD_TO_TEAM7 = 1145,
	MSG_ADD_TO_TEAM8 = 1146,
	MSG_ADD_TO_TEAM9 = 1147,
	MSG_END_NETWORK_MESSAGES = 1148
};

// RW recorder readArgument order
enum GameMessageArgumentDataType
{
	ARGUMENTDATATYPE_INTEGER = 0,
	ARGUMENTDATATYPE_REAL,
	ARGUMENTDATATYPE_BOOLEAN,
	ARGUMENTDATATYPE_OBJECTID,
	ARGUMENTDATATYPE_DRAWABLEID,
	ARGUMENTDATATYPE_TEAMID,
	ARGUMENTDATATYPE_LOCATION,
	ARGUMENTDATATYPE_PIXEL,
	ARGUMENTDATATYPE_PIXELREGION,
	ARGUMENTDATATYPE_TIMESTAMP,
	ARGUMENTDATATYPE_WIDECHAR
};

struct GameMessageArgument
{
	GameMessageArgumentDataType type = ARGUMENTDATATYPE_INTEGER;
	int integer = 0;
	float real = 0.0f;
	bool boolean = false;
	ObjectID objectID = 0;
	Coord3D location;
};

// the name of a logic message type ("MSG_QUEUE_UNIT_CREATE"), or "" for a number outside 1000 .. 1148
const char *GameMessageTypeName(int type);

class GameMessage
{
public:
	GameMessage(int type, int playerIndex)
		: m_type(type)
		, m_player(playerIndex)
	{
	}
	int getType() const { return m_type; }
	// ZH GameMessage::getPlayerIndex: the player the message was issued by (the command list is per player)
	int getPlayerIndex() const { return m_player; }
	// ZH GameMessage::friend_setPlayerIndex: the network sets the sender's player on a received command (lane MP-1, GameNetwork/Network.cpp)
	void friend_setPlayerIndex(int playerIndex) { m_player = playerIndex; }

	void appendIntegerArgument(int v)
	{
		GameMessageArgument a;
		a.type = ARGUMENTDATATYPE_INTEGER;
		a.integer = v;
		m_args.push_back(a);
	}
	void appendRealArgument(float v)
	{
		GameMessageArgument a;
		a.type = ARGUMENTDATATYPE_REAL;
		a.real = v;
		m_args.push_back(a);
	}
	void appendBooleanArgument(bool v)
	{
		GameMessageArgument a;
		a.type = ARGUMENTDATATYPE_BOOLEAN;
		a.boolean = v;
		m_args.push_back(a);
	}
	void appendObjectIDArgument(ObjectID v)
	{
		GameMessageArgument a;
		a.type = ARGUMENTDATATYPE_OBJECTID;
		a.objectID = v;
		m_args.push_back(a);
	}
	void appendLocationArgument(const Coord3D &v)
	{
		GameMessageArgument a;
		a.type = ARGUMENTDATATYPE_LOCATION;
		a.location = v;
		m_args.push_back(a);
	}
	size_t getArgumentCount() const { return m_args.size(); }
	const GameMessageArgument *getArgument(size_t i) const { return i < m_args.size() ? &m_args[i] : nullptr; }

private:
	int m_type;
	int m_player;
	std::vector<GameMessageArgument> m_args;
};
