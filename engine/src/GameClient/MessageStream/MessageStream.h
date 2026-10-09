// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The client message stream (ZH Include/Common/MessageStream.h, Source/Common/MessageStream.cpp), lane HUD-1: raw input becomes meta messages and
// logic messages by passing through the translators in priority order; what reaches the end of the stream is the commands of the frame.
//
// DONOR FACTS (ZH MessageStream::propagateMessages, MessageStream.cpp:1078): the translators are tried one after the other in ascending priority;
// each runs over the WHOLE message list (so a message a translator appends is seen by that translator later in the same pass and by every later
// translator, never by an earlier one); a DESTROY_MESSAGE disposition deletes the message; the messages left are appended to the command list.
// ZH GameClient.cpp:297-315 attaches WindowTranslator 10, MetaEventTranslator 20, HotKeyTranslator 25, PlaceEventTranslator 30,
// GUICommandTranslator 40, SelectionTranslator 50, LookAtTranslator 60, CommandTranslator 70, HintSpyTranslator 100 (RotWK 2.01 binary: not read,
// stop S-280).
//
// A logic message keeps the numbering of GameMessage.h (RotWK 1001 .. 1147: PLAN rule 4). The client-only types (raw input, clicks, hints, meta)
// are numbered from 2000 and never leave the client: toGameMessage() refuses them. The stream is per peer and not simulation state; everything it
// passes to the logic is a GameMessage appended to the CommandList (GameLogicDispatch.h), the lockstep path.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

enum ClientMessageType : int
{
	CMSG_INVALID = 0,
	// ---- raw input (ZH MSG_RAW_*) ----
	CMSG_RAW_MOUSE_BEGIN = 2000,
	CMSG_RAW_MOUSE_POSITION,          // pixel, modifiers
	CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN,  // pixel, modifiers, time ms
	CMSG_RAW_MOUSE_LEFT_DOUBLE_CLICK,
	CMSG_RAW_MOUSE_LEFT_BUTTON_UP,
	CMSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN,
	CMSG_RAW_MOUSE_MIDDLE_DOUBLE_CLICK,
	CMSG_RAW_MOUSE_MIDDLE_BUTTON_UP,
	CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN,
	CMSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK,
	CMSG_RAW_MOUSE_RIGHT_BUTTON_UP,
	CMSG_RAW_MOUSE_WHEEL,
	CMSG_RAW_MOUSE_END,
	CMSG_RAW_KEY_DOWN,                // key (ZH KeyDefs), key state flags
	CMSG_RAW_KEY_UP,
	// ---- clicks the MetaEventTranslator derives (ZH MSG_MOUSE_*_CLICK): pixel region, modifiers ----
	CMSG_MOUSE_LEFT_CLICK,
	CMSG_MOUSE_LEFT_DOUBLE_CLICK,
	CMSG_MOUSE_MIDDLE_CLICK,
	CMSG_MOUSE_MIDDLE_DOUBLE_CLICK,
	CMSG_MOUSE_RIGHT_CLICK,
	CMSG_MOUSE_RIGHT_DOUBLE_CLICK,
	// ---- hints (ZH MSG_*_HINT) ----
	CMSG_MOUSEOVER_DRAWABLE_HINT,     // object id
	CMSG_MOUSEOVER_LOCATION_HINT,     // location
	CMSG_AREA_SELECTION_HINT,         // pixel region
	// ---- meta messages (ZH MSG_META_*): the names of the RotWK registry (the CommandMap names, RW rdata 0xD...: the strings SAVE_VIEW1 .. ADD_TO_TEAM9 in
	// the order the binary's name table holds them), see ClientMessageMetaName ----
	CMSG_META_BEGIN,
#define CMSG_META_LIST(X) \
	X(SAVE_VIEW1) X(SAVE_VIEW2) X(SAVE_VIEW3) X(SAVE_VIEW4) X(SAVE_VIEW5) X(SAVE_VIEW6) X(SAVE_VIEW7) X(SAVE_VIEW8) \
	X(VIEW_VIEW1) X(VIEW_VIEW2) X(VIEW_VIEW3) X(VIEW_VIEW4) X(VIEW_VIEW5) X(VIEW_VIEW6) X(VIEW_VIEW7) X(VIEW_VIEW8) \
	X(CREATE_TEAM0) X(CREATE_TEAM1) X(CREATE_TEAM2) X(CREATE_TEAM3) X(CREATE_TEAM4) X(CREATE_TEAM5) X(CREATE_TEAM6) X(CREATE_TEAM7) X(CREATE_TEAM8) X(CREATE_TEAM9) \
	X(SELECT_TEAM0) X(SELECT_TEAM1) X(SELECT_TEAM2) X(SELECT_TEAM3) X(SELECT_TEAM4) X(SELECT_TEAM5) X(SELECT_TEAM6) X(SELECT_TEAM7) X(SELECT_TEAM8) X(SELECT_TEAM9) \
	X(ADD_TEAM0) X(ADD_TEAM1) X(ADD_TEAM2) X(ADD_TEAM3) X(ADD_TEAM4) X(ADD_TEAM5) X(ADD_TEAM6) X(ADD_TEAM7) X(ADD_TEAM8) X(ADD_TEAM9) \
	X(VIEW_TEAM0) X(VIEW_TEAM1) X(VIEW_TEAM2) X(VIEW_TEAM3) X(VIEW_TEAM4) X(VIEW_TEAM5) X(VIEW_TEAM6) X(VIEW_TEAM7) X(VIEW_TEAM8) X(VIEW_TEAM9) \
	X(SELECT_MATCHING_UNITS) X(SELECT_NEXT_UNIT) X(SELECT_PREV_UNIT) X(SELECT_NEXT_WORKER) X(SELECT_PREV_WORKER) X(SELECT_HERO) \
	X(VIEW_HOME_BASE) X(VIEW_LAST_RADAR_EVENT) X(SELECT_ALL) X(SCATTER) X(DEPLOY) X(CREATE_FORMATION) X(AUTO_SAVE) X(FOLLOW) X(STOP) \
	X(CHAT_PLAYERS) X(CHAT_BUDDIES) X(CHAT_ALLIES) X(CHAT_EVERYONE) X(DIPLOMACY) X(OPTIONS) X(TOGGLE_CONTROL_BAR) \
	X(BEGIN_PATH_BUILD) X(END_PATH_BUILD) X(BEGIN_FORCEATTACK) X(END_FORCEATTACK) X(BEGIN_FORCEMOVE) X(END_FORCEMOVE) X(BEGIN_WAYPOINTS) X(END_WAYPOINTS) \
	X(BEGIN_PREFER_SELECTION) X(END_PREFER_SELECTION) X(TAKE_SCREENSHOT) X(ALL_CHEER) X(TOGGLE_ATTACKMOVE) \
	X(BEGIN_CAMERA_ROTATE_LEFT) X(END_CAMERA_ROTATE_LEFT) X(BEGIN_CAMERA_ROTATE_RIGHT) X(END_CAMERA_ROTATE_RIGHT) \
	X(BEGIN_CAMERA_ZOOM_IN) X(END_CAMERA_ZOOM_IN) X(BEGIN_CAMERA_ZOOM_OUT) X(END_CAMERA_ZOOM_OUT) X(CAMERA_RESET) \
	X(BEGIN_CAMERA_SCROLL_LEFT) X(END_CAMERA_SCROLL_LEFT) X(BEGIN_CAMERA_SCROLL_RIGHT) X(END_CAMERA_SCROLL_RIGHT) \
	X(BEGIN_CAMERA_SCROLL_UP) X(END_CAMERA_SCROLL_UP) X(BEGIN_CAMERA_SCROLL_DOWN) X(END_CAMERA_SCROLL_DOWN) \
	X(PLACE_BEACON) X(DELETE_BEACON) X(SPELL_STORE) X(STANCE_AGGRESSIVE) X(STANCE_BATTLE) X(STANCE_HOLDGROUND) \
	X(ORDERMODE_IMMEDIATE) X(ORDERMODE_WAYPOINT) X(ORDER_SYNCHRONIZE) X(SELL) X(TOGGLE_PLANNING_MODE) \
	X(DEMO_PERFORM_STATISTICAL_DUMP) X(TOGGLE_FAST_FORWARD_MODE) X(RELOAD_RAPID_ITERATION_FEATURE) X(REFRESH_RAPID_ITERATION_FEATURE) \
	X(DEBUG_TOGGLE_SHOWVISIONEGGS) X(DEBUG_TOGGLE_STRINGTAGS) \
	X(ADD_TO_TEAM0) X(ADD_TO_TEAM1) X(ADD_TO_TEAM2) X(ADD_TO_TEAM3) X(ADD_TO_TEAM4) X(ADD_TO_TEAM5) X(ADD_TO_TEAM6) X(ADD_TO_TEAM7) X(ADD_TO_TEAM8) X(ADD_TO_TEAM9)
#define CMSG_META_ENUM(n) CMSG_META_##n,
	CMSG_META_LIST(CMSG_META_ENUM)
#undef CMSG_META_ENUM
	CMSG_META_END,
	CMSG_END
};

// the CommandMap.ini name of a meta message ("SAVE_VIEW1"), or "" when `type` is not one; the reverse lookup is case insensitive, CMSG_INVALID when unknown
const char *ClientMessageMetaName(int type);
int ClientMessageMetaType(const std::string &name);

enum class ClientArgKind
{
	Integer,
	Real,
	Boolean,
	ObjectID,
	Location,
	Pixel,
	PixelRegion
};

struct ClientArg
{
	ClientArgKind kind = ClientArgKind::Integer;
	int integer = 0;
	float real = 0.0f;
	bool boolean = false;
	ObjectID objectID = 0;
	Coord3D location;
	ICoord2D pixel;
	IRegion2D region{};
};

class ClientMessage
{
public:
	explicit ClientMessage(int type) : m_type(type) {}
	int type() const { return m_type; }
	bool isLogic() const { return m_type > MSG_BEGIN_NETWORK_MESSAGES && m_type < MSG_END_NETWORK_MESSAGES; }

	void appendInteger(int v) { ClientArg a; a.kind = ClientArgKind::Integer; a.integer = v; m_args.push_back(a); }
	void appendReal(float v) { ClientArg a; a.kind = ClientArgKind::Real; a.real = v; m_args.push_back(a); }
	void appendBoolean(bool v) { ClientArg a; a.kind = ClientArgKind::Boolean; a.boolean = v; m_args.push_back(a); }
	void appendObjectID(ObjectID v) { ClientArg a; a.kind = ClientArgKind::ObjectID; a.objectID = v; m_args.push_back(a); }
	void appendLocation(const Coord3D &v) { ClientArg a; a.kind = ClientArgKind::Location; a.location = v; m_args.push_back(a); }
	void appendPixel(const ICoord2D &v) { ClientArg a; a.kind = ClientArgKind::Pixel; a.pixel = v; m_args.push_back(a); }
	void appendPixelRegion(const IRegion2D &v) { ClientArg a; a.kind = ClientArgKind::PixelRegion; a.region = v; m_args.push_back(a); }
	size_t argumentCount() const { return m_args.size(); }
	// a missing argument is a programming error of the translator that built the message: out_of_range
	const ClientArg &arg(size_t i) const { return m_args.at(i); }

private:
	int m_type;
	std::vector<ClientArg> m_args;
};

enum class MessageDisposition
{
	Keep,
	Destroy
};

class MessageTranslator
{
public:
	virtual ~MessageTranslator() = default;
	virtual MessageDisposition translate(const ClientMessage &message) = 0;
};

class MessageStream
{
public:
	// the translators must outlive the stream; ascending priority, a tie keeps the order of attachment
	void attachTranslator(MessageTranslator *translator, int priority);
	void detachTranslator(MessageTranslator *translator);
	// ZH MessageStream::appendMessage: a message is added at the end of the stream (valid during a pass: the translator running sees it later)
	ClientMessage &append(int type);
	// ZH insertMessage: the message goes right after `after` (the click derived from a button up comes before the button up's later translators)
	ClientMessage &insertAfter(int type, const ClientMessage &after);
	size_t pending() const { return m_messages.size(); }

	// One pass (ZH propagateMessages). The messages that reach the end are returned in order; the stream is empty afterwards.
	std::vector<ClientMessage> propagate();
	// Every message that went by so far (the HUD acceptance tests compare two runs by this log): the logic messages that reached the end, as text
	// ("MSG_DO_MOVETO p0 loc(1,2,3) ..."), in order
	const std::vector<std::string> &log() const { return m_log; }
	static std::string describe(const ClientMessage &message);

private:
	struct Attached
	{
		MessageTranslator *translator;
		int priority;
	};
	std::vector<Attached> m_translators;
	std::list<std::unique_ptr<ClientMessage>> m_messages;
	std::vector<std::string> m_log;
};

// The logic message a client message of type 1001 .. 1147 becomes for `playerIndex`; std::logic_error for a client-only type.
GameMessage toGameMessage(const ClientMessage &message, int playerIndex);
