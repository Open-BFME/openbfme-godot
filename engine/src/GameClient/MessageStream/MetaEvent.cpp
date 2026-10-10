// OpenBFME. GPL-3.0.
// See MetaEvent.h.

#include "GameClient/MessageStream/MetaEvent.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "Common/INIException.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace
{
struct KeyName
{
	const char *name;
	int code;
};
// the key names of the binary's table (RW rdata, the strings KEY_NONE .. KEY_KPSLASH read in order)
const KeyName kKeyNames[] = {
	{ "KEY_NONE", KEY_NONE }, { "KEY_ESC", KEY_ESC }, { "KEY_BACKSPACE", KEY_BACKSPACE }, { "KEY_ENTER", KEY_ENTER }, { "KEY_SPACE", KEY_SPACE }, { "KEY_TAB", KEY_TAB },
	{ "KEY_F1", KEY_F1 }, { "KEY_F2", KEY_F2 }, { "KEY_F3", KEY_F3 }, { "KEY_F4", KEY_F4 }, { "KEY_F5", KEY_F5 }, { "KEY_F6", KEY_F6 }, { "KEY_F7", KEY_F7 }, { "KEY_F8", KEY_F8 },
	{ "KEY_F9", KEY_F9 }, { "KEY_F10", KEY_F10 }, { "KEY_F11", KEY_F11 }, { "KEY_F12", KEY_F12 },
	{ "KEY_A", KEY_A }, { "KEY_B", KEY_B }, { "KEY_C", KEY_C }, { "KEY_D", KEY_D }, { "KEY_E", KEY_E }, { "KEY_F", KEY_F }, { "KEY_G", KEY_G }, { "KEY_H", KEY_H }, { "KEY_I", KEY_I },
	{ "KEY_J", KEY_J }, { "KEY_K", KEY_K }, { "KEY_L", KEY_L }, { "KEY_M", KEY_M }, { "KEY_N", KEY_N }, { "KEY_O", KEY_O }, { "KEY_P", KEY_P }, { "KEY_Q", KEY_Q }, { "KEY_R", KEY_R },
	{ "KEY_S", KEY_S }, { "KEY_T", KEY_T }, { "KEY_U", KEY_U }, { "KEY_V", KEY_V }, { "KEY_W", KEY_W }, { "KEY_X", KEY_X }, { "KEY_Y", KEY_Y }, { "KEY_Z", KEY_Z },
	{ "KEY_1", KEY_1 }, { "KEY_2", KEY_2 }, { "KEY_3", KEY_3 }, { "KEY_4", KEY_4 }, { "KEY_5", KEY_5 }, { "KEY_6", KEY_6 }, { "KEY_7", KEY_7 }, { "KEY_8", KEY_8 }, { "KEY_9", KEY_9 },
	{ "KEY_0", KEY_0 },
	{ "KEY_KP1", KEY_KP1 }, { "KEY_KP2", KEY_KP2 }, { "KEY_KP3", KEY_KP3 }, { "KEY_KP4", KEY_KP4 }, { "KEY_KP5", KEY_KP5 }, { "KEY_KP6", KEY_KP6 }, { "KEY_KP7", KEY_KP7 }, { "KEY_KP8", KEY_KP8 },
	{ "KEY_KP9", KEY_KP9 }, { "KEY_KP0", KEY_KP0 },
	{ "KEY_MINUS", KEY_MINUS }, { "KEY_EQUAL", KEY_EQUAL }, { "KEY_LBRACKET", KEY_LBRACKET }, { "KEY_RBRACKET", KEY_RBRACKET }, { "KEY_SEMICOLON", KEY_SEMICOLON },
	{ "KEY_APOSTROPHE", KEY_APOSTROPHE }, { "KEY_TICK", KEY_TICK }, { "KEY_BACKSLASH", KEY_BACKSLASH }, { "KEY_COMMA", KEY_COMMA }, { "KEY_PERIOD", KEY_PERIOD }, { "KEY_SLASH", KEY_SLASH },
	{ "KEY_UP", KEY_UP }, { "KEY_DOWN", KEY_DOWN }, { "KEY_LEFT", KEY_LEFT }, { "KEY_RIGHT", KEY_RIGHT }, { "KEY_HOME", KEY_HOME }, { "KEY_END", KEY_END }, { "KEY_PGUP", KEY_PGUP },
	{ "KEY_PGDN", KEY_PGDN }, { "KEY_INS", KEY_INS }, { "KEY_DEL", KEY_DEL }, { "KEY_KPSLASH", KEY_KPSLASH }
};

const LookupListRec kTransitionNames[] = { { "DOWN", TRANSITION_DOWN }, { "UP", TRANSITION_UP }, { "DOUBLEDOWN", TRANSITION_DOUBLEDOWN }, { nullptr, 0 } };
// RW 0xBF0E28, in the binary's order
const LookupListRec kModifierNames[] = { { "NONE", MOD_NONE }, { "CTRL", MOD_CTRL }, { "ALT", MOD_ALT }, { "SHIFT", MOD_SHIFT }, { "CTRL_ALT", MOD_CTRL_ALT },
	{ "SHIFT_CTRL", MOD_SHIFT_CTRL }, { "SHIFT_ALT", MOD_SHIFT_ALT }, { "SHIFT_ALT_CTRL", MOD_SHIFT_ALT_CTRL }, { nullptr, 0 } };
const LookupListRec kCategoryNames[] = { { "CONTROL", 0 }, { "INFORMATION", 1 }, { "INTERFACE", 2 }, { "SELECTION", 3 }, { "TAUNT", 4 }, { "TEAM", 5 }, { "MISC", 6 }, { "DEBUG", 7 }, { nullptr, 0 } };

bool equalsNoCase(const char *a, const char *b)
{
	for (; *a && *b; ++a, ++b)
	{
		if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
		{
			return false;
		}
	}
	return *a == *b;
}

void parseKey(INI *ini, void *, void *store, const void *)
{
	const char *token = ini->getNextToken();
	const int key = MetaMap::keyByName(token);
	if (key < 0)
	{
		throw INIException(3, "Invalid key name '%s'", token);
	}
	*static_cast<int *>(store) = key;
}

// RW 0xD9DCAC: the UseableIn names of the field table's INI::parseBitString32 (RW 0x42E840): bit 0 SHELL, bit 1 GAME, bit 2 PLANNING
const char *const kUsableInNames[] = { "SHELL", "GAME", "PLANNING", nullptr };

void parseLabel(INI *ini, void *, void *store, const void *)
{
	// ZH parseAndTranslateLabel: the label is a game text key; kept as written
	*static_cast<std::string *>(store) = ini->getNextToken();
}

#define MM_OFF(m) (int)offsetof(MetaMapRec, m)
const FieldParse kMetaMapFields[] = {
	{ "Key", parseKey, nullptr, MM_OFF(key) },
	{ "Transition", INI::parseLookupList, kTransitionNames, MM_OFF(transition) },
	{ "Modifiers", INI::parseLookupList, kModifierNames, MM_OFF(modState) },
	{ "UseableIn", INI::parseBitString32, kUsableInNames, MM_OFF(usableIn) },
	{ "Category", INI::parseLookupList, kCategoryNames, MM_OFF(category) },
	{ "Description", parseLabel, nullptr, MM_OFF(description) },
	{ "DisplayName", parseLabel, nullptr, MM_OFF(displayName) },
	{ nullptr, nullptr, nullptr, 0 }
};

} // namespace

int MetaMap::keyByName(const std::string &name)
{
	for (const KeyName &k : kKeyNames)
	{
		if (equalsNoCase(name.c_str(), k.name))
		{
			return k.code;
		}
	}
	return -1;
}

const char *MetaMap::keyName(int key)
{
	for (const KeyName &k : kKeyNames)
	{
		if (k.code == key)
		{
			return k.name;
		}
	}
	return "";
}

MetaMapRec &MetaMap::getMetaMapRec(int meta)
{
	for (MetaMapRec &r : m_records)
	{
		if (r.meta == meta)
		{
			return r;
		}
	}
	MetaMapRec rec;
	rec.meta = meta;
	m_records.insert(m_records.begin(), rec); // ZH pushes the new record at the head of the list
	return m_records.front();
}

const MetaMapRec *MetaMap::find(int meta) const
{
	for (const MetaMapRec &r : m_records)
	{
		if (r.meta == meta)
		{
			return &r;
		}
	}
	return nullptr;
}

bool MetaMap::isBound(int key, int modState) const
{
	for (const MetaMapRec &r : m_records)
	{
		if (r.key == key && r.modState == modState)
		{
			return true;
		}
	}
	return false;
}

void MetaMap::registerBlocks(INIEnvironment &env)
{
	env.blocks.registerBlock("CommandMap", [this](INI *ini) {
		// ZH MetaMap::parseMetaMap: the first token is the meta message name; an unknown one is INI_INVALID_DATA
		const std::string name = ini->getNextToken();
		const int type = ClientMessageMetaType(name);
		if (type == CMSG_INVALID)
		{
			throw INIException(3, "Game message meta type for '%s' not found", name.c_str()); // RW string (the table walk's message)
		}
		MetaMapRec &rec = getMetaMapRec(type);
		ini->initFromINI(&rec, kMetaMapFields);
	});
}

bool MetaMap::load(INIEnvironment &env, const std::string &file, std::string *error)
{
	INI ini(env);
	try
	{
		ini.load(file, INI_LOAD_OVERWRITE);
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = file + ": " + e.what();
		}
		return false;
	}
	return true;
}

namespace
{
int sideFlag(int key)
{
	switch (key)
	{
		case KEY_LCTRL: return KEY_STATE_LCONTROL;
		case KEY_RCTRL: return KEY_STATE_RCONTROL;
		case KEY_LSHIFT: return KEY_STATE_LSHIFT;
		case KEY_RSHIFT: return KEY_STATE_RSHIFT;
		case KEY_LALT: return KEY_STATE_LALT;
		case KEY_RALT: return KEY_STATE_RALT;
		default: return 0;
	}
}
} // namespace

bool ModifierTracker::isModifierKey(int key)
{
	return sideFlag(key) != 0;
}

void ModifierTracker::key(int key, bool down)
{
	const int flag = sideFlag(key);
	if (flag)
	{
		m_state = down ? (m_state | flag) : (m_state & ~flag);
	}
}

std::vector<ModifierTracker::Transition> ModifierTracker::sync(bool ctrl, bool shift, bool alt)
{
	std::vector<Transition> out;
	const struct
	{
		bool down;
		int left, right;
	} kinds[] = { { ctrl, KEY_LCTRL, KEY_RCTRL }, { shift, KEY_LSHIFT, KEY_RSHIFT }, { alt, KEY_LALT, KEY_RALT } };
	for (const auto &k : kinds)
	{
		const bool leftHeld = (m_state & sideFlag(k.left)) != 0, rightHeld = (m_state & sideFlag(k.right)) != 0;
		if (!k.down)
		{
			if (leftHeld)
			{
				out.push_back({ k.left, false });
			}
			if (rightHeld)
			{
				out.push_back({ k.right, false });
			}
		}
		else if (!leftHeld && !rightHeld)
		{
			out.push_back({ k.left, true });
		}
	}
	for (const Transition &t : out)
	{
		key(t.key, t.down);
	}
	return out;
}

void MetaEventTranslator::noteMeta(int meta)
{
	++m_metasMade;
	m_lastMeta = ClientMessageMetaName(meta);
	m_recent.push_back(m_lastMeta);
	if (m_recent.size() > 32)
	{
		m_recent.erase(m_recent.begin());
	}
}

MessageDisposition MetaEventTranslator::translate(const ClientMessage &msg)
{
	MessageDisposition disp = MessageDisposition::Keep;
	const int t = msg.type();
	if (t == CMSG_RAW_KEY_DOWN || t == CMSG_RAW_KEY_UP)
	{
		int key = msg.arg(0).integer;
		if (m_german)
		{
			key = key == KEY_Z ? KEY_Y : key == KEY_Y ? KEY_Z : key; // RW 0x5DA86C
		}
		const int keyState = msg.arg(1).integer;
		++m_rawKeys;
		int newModState = 0;
		if (keyState & KEY_STATE_CONTROL)
		{
			newModState |= MOD_CTRL;
		}
		if (keyState & KEY_STATE_SHIFT)
		{
			newModState |= MOD_SHIFT;
		}
		if (keyState & KEY_STATE_ALT)
		{
			newModState |= MOD_ALT;
		}
		for (const MetaMapRec &map : m_map.records())
		{
			if (map.usableIn == COMMANDUSABLE_GAME && m_clientFrame < 1)
			{
				continue;
			}
			if (m_shellActive && !(map.usableIn & COMMANDUSABLE_SHELL))
			{
				continue;
			}
			if (!m_shellActive && !(map.usableIn & COMMANDUSABLE_GAME))
			{
				continue;
			}
			// a modifier-only change
			if (map.key == KEY_NONE && newModState != m_lastModState
				&& ((map.transition == TRANSITION_UP && map.modState == m_lastModState) || (map.transition == TRANSITION_DOWN && map.modState == newModState)))
			{
				m_ctx.stream.append(map.meta);
				noteMeta(map.meta);
				disp = MessageDisposition::Destroy;
				break;
			}
			if (map.key == key && map.modState == newModState
				&& ((map.transition == TRANSITION_UP && (keyState & KEY_STATE_UP)) || (map.transition == TRANSITION_DOWN && (keyState & KEY_STATE_DOWN))))
			{
				if (!(keyState & KEY_STATE_AUTOREPEAT))
				{
					m_ctx.stream.append(map.meta); // an autorepeat of a known key is eaten without a meta message
					noteMeta(map.meta);
				}
				disp = MessageDisposition::Destroy;
				break;
			}
		}
		if (t == CMSG_RAW_KEY_DOWN)
		{
			m_lastKeyDown = key;
		}
		m_lastModState = newModState;
		return disp;
	}
	if (t > CMSG_RAW_MOUSE_BEGIN && t < CMSG_RAW_MOUSE_END)
	{
		int index = 0;
		switch (t)
		{
			case CMSG_RAW_MOUSE_LEFT_BUTTON_DOWN:
			case CMSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN:
			case CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN:
				index = t == CMSG_RAW_MOUSE_MIDDLE_BUTTON_DOWN ? 1 : (t == CMSG_RAW_MOUSE_RIGHT_BUTTON_DOWN ? 2 : 0);
				m_mouseDown[index] = msg.arg(0).pixel;
				m_nextUpDouble[index] = false;
				break;
			case CMSG_RAW_MOUSE_LEFT_DOUBLE_CLICK:
			case CMSG_RAW_MOUSE_MIDDLE_DOUBLE_CLICK:
			case CMSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK:
				index = t == CMSG_RAW_MOUSE_MIDDLE_DOUBLE_CLICK ? 1 : (t == CMSG_RAW_MOUSE_RIGHT_DOUBLE_CLICK ? 2 : 0);
				m_nextUpDouble[index] = true;
				break;
			case CMSG_RAW_MOUSE_LEFT_BUTTON_UP:
			case CMSG_RAW_MOUSE_MIDDLE_BUTTON_UP:
			case CMSG_RAW_MOUSE_RIGHT_BUTTON_UP:
			{
				const ICoord2D location = msg.arg(0).pixel;
				index = t == CMSG_RAW_MOUSE_MIDDLE_BUTTON_UP ? 1 : (t == CMSG_RAW_MOUSE_RIGHT_BUTTON_UP ? 2 : 0);
				int clickType;
				if (index == 0)
				{
					clickType = m_nextUpDouble[0] ? CMSG_MOUSE_LEFT_DOUBLE_CLICK : CMSG_MOUSE_LEFT_CLICK;
				}
				else if (index == 1)
				{
					clickType = m_nextUpDouble[1] ? CMSG_MOUSE_MIDDLE_DOUBLE_CLICK : CMSG_MOUSE_MIDDLE_CLICK;
				}
				else
				{
					clickType = m_nextUpDouble[2] ? CMSG_MOUSE_RIGHT_DOUBLE_CLICK : CMSG_MOUSE_RIGHT_CLICK;
				}
				m_nextUpDouble[index] = false;
				ClientMessage &click = m_ctx.stream.insertAfter(clickType, msg);
				IRegion2D region;
				region.lo.x = std::min(m_mouseDown[index].x, location.x);
				region.hi.x = std::max(m_mouseDown[index].x, location.x);
				region.lo.y = std::min(m_mouseDown[index].y, location.y);
				region.hi.y = std::max(m_mouseDown[index].y, location.y);
				if (std::abs(region.hi.x - region.lo.x) < m_ctx.mouse.dragTolerance && std::abs(region.hi.y - region.lo.y) < m_ctx.mouse.dragTolerance)
				{
					region.hi = region.lo;
				}
				click.appendPixelRegion(region);
				click.appendInteger(msg.arg(1).integer);
				break;
			}
			default:
				break;
		}
	}
	return disp;
}
