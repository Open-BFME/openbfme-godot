// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// MetaEvent (ZH Include/GameClient/MetaEvent.h, Source/GameClient/MessageStream/MetaEvent.cpp, KeyDefs.h), lane HUD-1: the CommandMap (`CommandMap <NAME>` blocks of
// Data\INI\CommandMap.ini: key, transition, modifiers, useable-in, category) and the translator that turns raw keys into meta messages and raw
// mouse button ups into the click messages the other translators work on.
//
// TARGET FACTS (RotWK game.dat strings, caveat S-001): the meta name registry is the strings SAVE_VIEW1 .. ADD_TO_TEAM9 of the binary's table (MessageStream.h);
// the key names KEY_NONE, KEY_A .. KEY_Z, KEY_0 .. KEY_9, KEY_F1 .. KEY_F12, KEY_KP0 .. KEY_KP9, KEY_KPSLASH and the punctuation, arrow and edit keys; the
// transitions DOWN, UP and DOUBLEDOWN; the modifier names NONE, SHIFT, CTRL, ALT and their combinations; the useable-in names GAME, SHELL, PLANNING; the
// categories CONTROL, INFORMATION, INTERFACE, SELECTION, TAUNT, TEAM, MISC, DEBUG.
// TARGET FACTS (lane INPUT-1): the CommandMap block (MetaMap::parseMetaMap RW 0x5DAD46, BFME2 decomp MetaMapParseMetaMap.cpp tier A) fills a record with the
// field table RW 0xBF0E70: Key (INI::parseLookupList RW 0x42E9B7 over the 86 {name, scan code} pairs RW 0xBF0B50, KEY_ESC .. KEY_KPSLASH, KEY_NONE last),
// Transition (lookup RW 0xBF0E08: DOWN 0, UP 1, DOUBLEDOWN 2), Modifiers (lookup RW 0xBF0E28: NONE 0, CTRL 4, ALT 0x40, SHIFT 0x10, CTRL_ALT 0x44,
// SHIFT_CTRL 0x14, SHIFT_ALT 0x50, SHIFT_ALT_CTRL 0x54), UseableIn (INI::parseBitString32 RW 0x42E840 with SHELL, GAME, PLANNING, RW 0xD9DCAC),
// Category (lookup RW 0xBF0B08) and Description / DisplayName (RW 0x73B192: the label). GameClient's subsystem table loads one entry "CommandMap.ini"
// (RW 0x63C4F6); which files that resolves to (here Data\INI\CommandMap.ini, then the language archive's CommandMap.ini) is the INI subsystem
// loader's (RW 0x63874B, not compared: stop S-281).
// The translator (RW 0x5DA83D, BFME2 decomp MetaEvent.cpp, tier B: the player field + 0x770 instead of + 0x750) is ZH's with three changes: with
// OurLanguage 2 (German) the Y and Z scan codes swap; a record applies when its UseableIn bits meet SHELL while the shell is up, else GAME, plus PLANNING
// while the local player's order mode (+ 0x770) is 2; an autorepeated key of a record is eaten unless a debug flag (the object at RW 0xDFEF18, + 0x18)
// is set. The German swap follows the keyboard layout as RotWK's (lane INPUT-1 r2, setGermanKeyboard).
// DONOR FACTS (ZH MetaEvent.cpp:380-615): a new map record is pushed at the HEAD of the list, so the translator tries the records in reverse order of first
// appearance; a key down matches when key, modifier state and transition agree, autorepeat is eaten without a meta message; a modifier-only record
// (KEY_NONE) fires when the modifier state changes to / from its state; a button up becomes a MOUSE_*_CLICK (or DOUBLE_CLICK when the
// raw double click came first) inserted after it, with the region from the button down to the up (a region smaller than the drag tolerance collapses
// to a point) and the modifier word.

#pragma once

#include "GameClient/HudContext.h"
#include "GameClient/MessageStream/MessageStream.h"

#include <cstdint>
#include <string>
#include <vector>

struct INIEnvironment;

// ZH KeyDefs.h KeyDefType (the DirectInput scan codes) for the names the CommandMap accepts
enum KeyCode : int
{
	KEY_NONE = 0x00,
	KEY_ESC = 0x01, KEY_1 = 0x02, KEY_2 = 0x03, KEY_3 = 0x04, KEY_4 = 0x05, KEY_5 = 0x06, KEY_6 = 0x07, KEY_7 = 0x08, KEY_8 = 0x09, KEY_9 = 0x0A, KEY_0 = 0x0B,
	KEY_MINUS = 0x0C, KEY_EQUAL = 0x0D, KEY_BACKSPACE = 0x0E, KEY_TAB = 0x0F,
	KEY_Q = 0x10, KEY_W = 0x11, KEY_E = 0x12, KEY_R = 0x13, KEY_T = 0x14, KEY_Y = 0x15, KEY_U = 0x16, KEY_I = 0x17, KEY_O = 0x18, KEY_P = 0x19,
	KEY_LBRACKET = 0x1A, KEY_RBRACKET = 0x1B, KEY_ENTER = 0x1C, KEY_LCTRL = 0x1D,
	KEY_A = 0x1E, KEY_S = 0x1F, KEY_D = 0x20, KEY_F = 0x21, KEY_G = 0x22, KEY_H = 0x23, KEY_J = 0x24, KEY_K = 0x25, KEY_L = 0x26,
	KEY_SEMICOLON = 0x27, KEY_APOSTROPHE = 0x28, KEY_TICK = 0x29, KEY_LSHIFT = 0x2A, KEY_BACKSLASH = 0x2B,
	KEY_Z = 0x2C, KEY_X = 0x2D, KEY_C = 0x2E, KEY_V = 0x2F, KEY_B = 0x30, KEY_N = 0x31, KEY_M = 0x32,
	KEY_COMMA = 0x33, KEY_PERIOD = 0x34, KEY_SLASH = 0x35, KEY_RSHIFT = 0x36, KEY_KPSTAR = 0x37, KEY_LALT = 0x38, KEY_SPACE = 0x39, KEY_CAPS = 0x3A,
	KEY_F1 = 0x3B, KEY_F2 = 0x3C, KEY_F3 = 0x3D, KEY_F4 = 0x3E, KEY_F5 = 0x3F, KEY_F6 = 0x40, KEY_F7 = 0x41, KEY_F8 = 0x42, KEY_F9 = 0x43, KEY_F10 = 0x44,
	KEY_KP7 = 0x47, KEY_KP8 = 0x48, KEY_KP9 = 0x49, KEY_KPMINUS = 0x4A, KEY_KP4 = 0x4B, KEY_KP5 = 0x4C, KEY_KP6 = 0x4D, KEY_KPPLUS = 0x4E,
	KEY_KP1 = 0x4F, KEY_KP2 = 0x50, KEY_KP3 = 0x51, KEY_KP0 = 0x52, KEY_KPDOT = 0x53, KEY_F11 = 0x57, KEY_F12 = 0x58,
	KEY_KPENTER = 0x9C, KEY_RCTRL = 0x9D, KEY_KPSLASH = 0xB5, KEY_RALT = 0xB8,
	KEY_HOME = 0xC7, KEY_UP = 0xC8, KEY_PGUP = 0xC9, KEY_LEFT = 0xCB, KEY_RIGHT = 0xCD, KEY_END = 0xCF, KEY_DOWN = 0xD0, KEY_PGDN = 0xD1, KEY_INS = 0xD2, KEY_DEL = 0xD3
};

// ZH KeyDefs.h KeyStateType
enum KeyState : int
{
	KEY_STATE_NONE = 0x0000,
	KEY_STATE_UP = 0x0001,
	KEY_STATE_DOWN = 0x0002,
	KEY_STATE_LCONTROL = 0x0004,
	KEY_STATE_RCONTROL = 0x0008,
	KEY_STATE_LSHIFT = 0x0010,
	KEY_STATE_RSHIFT = 0x0020,
	KEY_STATE_LALT = 0x0040,
	KEY_STATE_RALT = 0x0080,
	KEY_STATE_AUTOREPEAT = 0x0100,
	KEY_STATE_CAPSLOCK = 0x0200,
	KEY_STATE_SHIFT2 = 0x0400,
	KEY_STATE_CONTROL = KEY_STATE_LCONTROL | KEY_STATE_RCONTROL,
	KEY_STATE_SHIFT = KEY_STATE_LSHIFT | KEY_STATE_RSHIFT | KEY_STATE_SHIFT2,
	KEY_STATE_ALT = KEY_STATE_LALT | KEY_STATE_RALT
};

// ZH MetaEvent.h CommandUsableInType, CommandMapModifier, CommandMapTransition, CommandCategory
enum CommandUsableIn : unsigned
{
	COMMANDUSABLE_NONE = 0,
	COMMANDUSABLE_SHELL = 1u << 0,
	COMMANDUSABLE_GAME = 1u << 1,
	COMMANDUSABLE_PLANNING = 1u << 2
};
// the values are RotWK's (the Modifiers lookup list RW 0xBF0E28: CTRL 0x04, SHIFT 0x10, ALT 0x40, the KEY_STATE bits of the left keys)
enum MetaModifier : int
{
	MOD_NONE = 0,
	MOD_CTRL = 0x04,
	MOD_SHIFT = 0x10,
	MOD_ALT = 0x40,
	MOD_SHIFT_CTRL = MOD_SHIFT | MOD_CTRL,
	MOD_CTRL_ALT = MOD_CTRL | MOD_ALT,
	MOD_SHIFT_ALT = MOD_SHIFT | MOD_ALT,
	MOD_SHIFT_ALT_CTRL = MOD_SHIFT | MOD_ALT | MOD_CTRL
};
enum MetaTransition : int
{
	TRANSITION_DOWN = 0,
	TRANSITION_UP,
	TRANSITION_DOUBLEDOWN
};

struct MetaMapRec
{
	int meta = CMSG_INVALID;
	int key = KEY_NONE;
	int transition = TRANSITION_DOWN;
	int modState = MOD_NONE;
	unsigned usableIn = COMMANDUSABLE_NONE;
	int category = 0;
	std::string description, displayName; ///< game text labels (ZH translates them; the label is kept)
};

class MetaMap
{
public:
	// registers the `CommandMap` block handler on the environment's block registry (the parse fills this map)
	void registerBlocks(INIEnvironment &env);
	// loads one CommandMap file (the file names and their order are the caller's: the INI subsystem legend's); false + *error when the file throws
	bool load(INIEnvironment &env, const std::string &file, std::string *error);

	// the records in the order the translator tries them (reverse of first appearance)
	const std::vector<MetaMapRec> &records() const { return m_records; }
	const MetaMapRec *find(int meta) const;
	// a record of the map (any transition, any UseableIn) takes this key with exactly these modifiers (MOD_*): OpenBFME's own hotkeys stay off it
	bool isBound(int key, int modState) const;
	MetaMapRec &getMetaMapRec(int meta);
	static int keyByName(const std::string &name);       // -1 when unknown (case insensitive)
	static const char *keyName(int key);

private:
	std::vector<MetaMapRec> m_records; // front = the newest (ZH pushes at the head)
};

// lane INPUT-1 (review r1): the device's modifier keys, per side. DirectInput reports each Ctrl / Shift / Alt key separately (DIK_LCONTROL 0x1D,
// DIK_RCONTROL 0x9D, ...); the key state carries a bit per side (ZH KeyDefs: KEY_STATE_LCONTROL / _RCONTROL ...), so letting go of one Ctrl while the
// other is held keeps Ctrl down. The aggregate flags a device event carries (Godot's ctrl / shift / alt pressed, on keys, buttons and pointer motion)
// repair a release or a press the window never saw: sync() names the side key transitions that bring the state in line with them.
class ModifierTracker
{
public:
	struct Transition
	{
		int key;   ///< KEY_LCTRL, KEY_RCTRL, KEY_LSHIFT, KEY_RSHIFT, KEY_LALT, KEY_RALT
		bool down;
	};
	static bool isModifierKey(int key);
	// a modifier key went down / up: the state's side bit follows (other keys change nothing)
	void key(int key, bool down);
	// the aggregate state a device event reports; the transitions to inject (each also applied to the state): a kind reported up releases every
	// side still held, a kind reported down with no side held presses the left one
	std::vector<Transition> sync(bool ctrl, bool shift, bool alt);
	int state() const { return m_state; } ///< KEY_STATE_* side bits
private:
	int m_state = 0;
};

class MetaEventTranslator : public MessageTranslator
{
public:
	MetaEventTranslator(HudContext &ctx, const MetaMap &map) : m_ctx(ctx), m_map(map) {}
	MessageDisposition translate(const ClientMessage &message) override;
	// the shell is active: only SHELL commands apply (the HUD is the game: false)
	void setShellActive(bool on) { m_shellActive = on; }
	// ZH TheGameClient->getFrame() < 1: GAME-only commands are ignored until the client reached frame 1
	void setClientFrame(unsigned frame) { m_clientFrame = frame; }
	// lane INPUT-1 r2: RotWK's OurLanguage 2 (RW 0xDE789C), set by the keyboard setup RW 0x63F079 when the keyboard layout is German (LANGID 0x0407,
	// 0x0807, 0x0C07, 0x1007, 0x1407): the translator swaps the Y and Z scan codes (RW 0x5DA86C), so a CommandMap's KEY_Z is the key labelled Z
	void setGermanKeyboard(bool on) { m_german = on; }
	bool germanKeyboard() const { return m_german; }
	// lane INPUT-1: the raw keys seen and the meta messages they made (the HUD's key diagnosis)
	unsigned rawKeys() const { return m_rawKeys; }
	unsigned metasMade() const { return m_metasMade; }
	const std::string &lastMeta() const { return m_lastMeta; }
	const std::vector<std::string> &recentMetas() const { return m_recent; } ///< the last 32 meta messages made, oldest first

private:
	HudContext &m_ctx;
	const MetaMap &m_map;
	bool m_shellActive = false;
	unsigned m_clientFrame = 1;
	int m_lastModState = 0;
	int m_lastKeyDown = 0;
	unsigned m_rawKeys = 0, m_metasMade = 0;
	bool m_german = false;
	std::string m_lastMeta;
	std::vector<std::string> m_recent;
	void noteMeta(int meta);
	ICoord2D m_mouseDown[3]{};
	bool m_nextUpDouble[3] = { false, false, false };
};
