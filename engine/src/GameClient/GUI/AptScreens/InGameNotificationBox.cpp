// OpenBFME. GPL-3.0.
// See InGameNotificationBox.h (lane CAMPUI-1).

#include "GameClient/GUI/AptScreens/InGameNotificationBox.h"

#include "Common/ArchiveFileSystem.h"
#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameClient/CommandButtonHelp.h"
#include "GameClient/GUI/GadgetDrawList.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameClient/GUI/GameWindowManager.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/AptValue.h"

#include <cmath>
#include <cstddef>

// ---------------------------------------------------------------------------------------------------------------------------------
// NotificationBoxSettings (RW 0x621FFA / 0x621F44)
// ---------------------------------------------------------------------------------------------------------------------------------
namespace
{
struct ParseState
{
	NotificationBoxSettings *out = nullptr;
	bool block = false;
};

// UTF-8 of a game text (the window manager's text records are UTF-8)
std::string utf8(const UnicodeString &in)
{
	std::string s;
	for (size_t i = 0; i < in.size(); ++i)
	{
		std::uint32_t ch = in[i];
		if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < in.size() && in[i + 1] >= 0xDC00 && in[i + 1] < 0xE000)
		{
			ch = 0x10000u + ((ch - 0xD800u) << 10) + (std::uint32_t)(in[++i] - 0xDC00u);
		}
		if (ch < 0x80)
		{
			s += (char)ch;
		}
		else if (ch < 0x800)
		{
			s += (char)(0xC0 | (ch >> 6));
			s += (char)(0x80 | (ch & 0x3F));
		}
		else if (ch < 0x10000)
		{
			s += (char)(0xE0 | (ch >> 12));
			s += (char)(0x80 | ((ch >> 6) & 0x3F));
			s += (char)(0x80 | (ch & 0x3F));
		}
		else
		{
			s += (char)(0xF0 | (ch >> 18));
			s += (char)(0x80 | ((ch >> 12) & 0x3F));
			s += (char)(0x80 | ((ch >> 6) & 0x3F));
			s += (char)(0x80 | (ch & 0x3F));
		}
	}
	return s;
}

// the type's own table RW 0xBFCF48 (offsets into NotificationBoxType)
const FieldParse kTypeFields[] = {
	{ "Title", INI::parseAsciiString, nullptr, (int)offsetof(NotificationBoxType, title) },
	{ "OpenAudio", INI::parseAsciiString, nullptr, (int)offsetof(NotificationBoxType, openAudio) },
	{ "Icon", INI::parseAsciiString, nullptr, (int)offsetof(NotificationBoxType, icon) },
	{ "MessageFont", GlobalLanguage::parseFontDesc, nullptr, (int)offsetof(NotificationBoxType, font) },
	{ "MessageColor", INI::parseColorInt, nullptr, (int)offsetof(NotificationBoxType, color) },
	{ nullptr, nullptr, nullptr, 0 },
};

// RW 0x621F44: NotificationType <name>: a copy of the defaults, then the type's fields; the first of a name stays
void parseType(INI *ini, void *instance, void *, const void *)
{
	auto *s = static_cast<NotificationBoxSettings *>(instance);
	NotificationBoxType t = s->defaults;
	t.name = ini->getNextToken();
	ini->initFromINI(&t, kTypeFields);
	if (!s->find(t.name))
	{
		s->types.push_back(t);
	}
}

// the box's table RW 0xBFD050 (offsets into NotificationBoxSettings::defaults)
const FieldParse kBoxFields[] = {
	{ "DefaultOpenAudio", INI::parseAsciiString, nullptr, (int)(offsetof(NotificationBoxSettings, defaults) + offsetof(NotificationBoxType, openAudio)) },
	{ "DefaultMessageFont", GlobalLanguage::parseFontDesc, nullptr, (int)(offsetof(NotificationBoxSettings, defaults) + offsetof(NotificationBoxType, font)) },
	{ "DefaultMessageColor", INI::parseColorInt, nullptr, (int)(offsetof(NotificationBoxSettings, defaults) + offsetof(NotificationBoxType, color)) },
	{ "NotificationType", parseType, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};

bool run(ArchiveFileSystem *fs, const std::string *memory, NotificationBoxSettings &out, std::string *error)
{
	const char *file = "data\\ini\\ingamenotificationbox.ini";
	INIEnvironment env;
	env.fileSystem = fs;
	ParseState st;
	st.out = &out;
	env.blocks.registerBlock("InGameNotificationBox", [&st](INI *ini) {
		st.block = true;
		ini->initFromINI(st.out, kBoxFields);
	});
	try
	{
		INI ini(env);
		if (memory)
		{
			ini.loadMemory(file, std::vector<std::uint8_t>(memory->begin(), memory->end()), INI_LOAD_OVERWRITE);
		}
		else
		{
			if (!fs || !fs->doesFileExist(file))
			{
				if (error)
				{
					*error = std::string(file) + ": file not found in any mounted archive";
				}
				return false;
			}
			ini.load(file, INI_LOAD_OVERWRITE);
		}
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = std::string(file) + ": " + e.what();
		}
		return false;
	}
	if (!st.block)
	{
		if (error)
		{
			*error = std::string(file) + ": no InGameNotificationBox block";
		}
		return false;
	}
	return true;
}
} // namespace

const NotificationBoxType *NotificationBoxSettings::find(const std::string &name) const
{
	for (const NotificationBoxType &t : types)
	{
		if (t.name == name)
		{
			return &t;
		}
	}
	return nullptr;
}

bool NotificationBoxSettings::load(ArchiveFileSystem &fs, NotificationBoxSettings &out, std::string *error)
{
	out = NotificationBoxSettings();
	return run(&fs, nullptr, out, error);
}

bool NotificationBoxSettings::scan(const std::string &ini, NotificationBoxSettings &out, std::string *error)
{
	out = NotificationBoxSettings();
	return run(nullptr, &ini, out, error);
}

// ---------------------------------------------------------------------------------------------------------------------------------
// InGameNotificationBoxMovieClip (ctor RW 0x8EB371)
// ---------------------------------------------------------------------------------------------------------------------------------
InGameNotificationBoxMovieClip::InGameNotificationBoxMovieClip(WindowManager &windows, Shell &shell, FontMetricsSource &metrics)
	: AptScreen(windows, shell, "InGameNotificationBox.apt", "InGameNotificationBox"), m_metrics(metrics)
{
	if (level() < 0)
	{
		return;
	}
	const std::string p = prefix();
	registerCommand(p + "_OnInitialized", [this](const std::string &) { // RW 0x8EAED0
		if (m_state == STATE_LOADING)
		{
			m_state = STATE_IDLE;
		}
	});
	registerCommand(p + "_OnOpen", [this](const std::string &) { // RW 0x8EAEE0
		if (m_state == STATE_OPENING)
		{
			m_state = STATE_SHOWN;
			m_openedAt = m_now;
		}
	});
	registerCommand(p + "_OnClosed", [this](const std::string &) { // RW 0x8EB30F
		if (m_state == STATE_CLOSING)
		{
			m_state = STATE_IDLE;
			this->windows().setAptImage(prefix() + "_Icon", std::string());
			m_hasActive = false;
		}
	});
	registerCommand(p + "_OnCloseButtonClicked", [this](const std::string &) { // RW 0x8EAEFD
		if (m_state == STATE_OPENING || m_state == STATE_SHOWN)
		{
			call("Close", {});
			m_state = STATE_CLOSING;
		}
	});
	// RW 0x8EAF2E: the movie writes the message clip's width (stage units) at its first frame
	registerProvider(p + "_MessageWidth", [this](const std::string &, std::string &value, bool setting) {
		if (setting)
		{
			m_width = (float)std::atof(value.c_str());
		}
		else
		{
			value = std::to_string(m_width);
		}
		return true;
	});
	// RW 0x8EAFC5: the message, centred in the clip's rectangle, each line at the font's height
	windows.setRenderCallback(p + "_Message", [this](float x, float y, float w, float h, GadgetDrawList &out) {
		if (!m_hasActive || m_lines.empty())
		{
			return;
		}
		const GameFont &font = m_active.font;
		const int lineHeight = m_metrics.fontHeight(font);
		const float total = (float)lineHeight * (float)m_lines.size();
		float top = y + (h - total) * 0.5f;
		for (const UnicodeString &line : m_lines)
		{
			GadgetDrawCommand c;
			c.kind = GadgetDrawCommand::Kind::Text;
			c.text = line;
			c.font = font;
			c.color = m_active.color;
			const float lw = (float)m_metrics.textWidth(font, line);
			c.x0 = (int)std::floor(x + (w - lw) * 0.5f + 0.5f);
			c.y0 = (int)std::floor(top + 0.5f);
			out.commands.push_back(c);
			top += (float)lineHeight;
		}
	});
}

InGameNotificationBoxMovieClip::~InGameNotificationBoxMovieClip()
{
	if (level() >= 0)
	{
		windows().clearRenderCallback(prefix() + "_Message");
		windows().setAptImage(prefix() + "_Icon", std::string());
	}
}

bool InGameNotificationBoxMovieClip::call(const std::string &fn, const std::vector<std::string> &args)
{
	std::vector<AptValue> values;
	for (const std::string &a : args)
	{
		values.push_back(AptValue::string(a));
	}
	std::string error;
	if (!windows().invokeASAtValues(level(), "", fn, values, &error))
	{
		if (m_callErrors.size() < 64)
		{
			m_callErrors.push_back(fn + ": " + error);
		}
		return false;
	}
	return true;
}

bool InGameNotificationBoxMovieClip::makeNotice(const NotificationBoxSettings &settings, const GameTextSource *text, const std::string &type,
	const std::string &label, int seconds, bool overrideIcon, const std::string &overrideImage, Notice &out)
{
	const NotificationBoxType *t = settings.find(type); // RW 0x621E3A: an unknown type shows nothing
	if (!t)
	{
		return false;
	}
	out = Notice();
	if (!t->title.empty() && text) // RW 0x62161D: the title's game text
	{
		text->fetch(t->title, out.title);
	}
	out.audio = t->openAudio;
	out.icon = overrideIcon ? overrideImage : t->icon; // RW 0x6D1305 / 0x99344A: the template's ButtonImage replaces the icon
	out.font.name = t->font.name;
	out.font.pointSize = t->font.size; // the stage's units: the renderer scales the callback's text by min(aptScaleX, aptScaleY) as RW 0x62161D does
	out.font.bold = t->font.bold;
	out.color = t->color;
	if (text && !text->fetch(label, out.message))
	{
		const std::string missing = "MISSING: '" + label + "'"; // GameText::fetch of an unknown label
		out.message.assign(missing.begin(), missing.end());
	}
	out.timeoutMs = seconds > 0 ? (unsigned)seconds * 1000u : 0u;
	return true;
}

void InGameNotificationBoxMovieClip::open(const Notice &notice)
{
	if (!m_enabled) // RW 0x8EB66D: + 0x50
	{
		return;
	}
	m_pending = notice; // the one pending slot: a newer notice replaces an unshown one
	m_hasPending = true;
}

void InGameNotificationBoxMovieClip::updateNotice(unsigned, bool letterboxed)
{
	if (m_width <= 0.0f || m_pending.font.name.empty() || letterboxed) // RW 0x8EB75B
	{
		closeImmediately();
		return;
	}
	const std::string p = prefix();
	windows().setAptText("APT:" + p + "_Title", utf8(m_pending.title)); // RW 0x8EB7BA: "APT:_level%u_Title"
	const bool icon = !m_pending.icon.empty();
	if (icon != m_iconVisible)
	{
		std::string error;
		if (!windows().invokeASAtValues(level(), "", "SetIconVisibility", { AptValue::boolean(icon) }, &error) && m_callErrors.size() < 64)
		{
			m_callErrors.push_back("SetIconVisibility: " + error);
		}
		m_iconVisible = icon;
	}
	if (icon)
	{
		windows().setAptImage(p + "_Icon", m_pending.icon);
	}
	// the message's display string: centred, wrapped to the clip's width
	m_lines = CommandButtonHelp::wrap(m_pending.message, m_pending.font, (int)std::floor(m_width + 0.5f), m_metrics, nullptr);
	m_openAudio = m_pending.audio;
	const float height = (float)m_metrics.fontHeight(m_pending.font) * (float)m_lines.size();
	static const char *const kLocations[] = { "main", "spellStore", "objectives" }; // RW 0xC791BC
	const int loc = m_pending.location >= 0 && m_pending.location <= 2 ? m_pending.location : 0;
	std::string error;
	if (!windows().invokeASAtValues(level(), "", "Open", { AptValue::number(height), AptValue::boolean(m_pending.opaque), AptValue::string(kLocations[loc]) },
			&error) &&
		m_callErrors.size() < 64)
	{
		m_callErrors.push_back("Open: " + error);
	}
	m_state = STATE_OPENING;
	m_active = m_pending;
	m_hasActive = true;
	m_hasPending = false;
}

std::string InGameNotificationBoxMovieClip::update(unsigned nowMs, bool letterboxed)
{
	m_now = nowMs;
	m_openAudio.clear();
	if (m_state == STATE_LOADING)
	{
		return std::string();
	}
	if (m_hasPending)
	{
		updateNotice(nowMs, letterboxed);
	}
	if (m_state == STATE_SHOWN && m_hasActive && m_active.timeoutMs != 0 && nowMs - m_openedAt >= m_active.timeoutMs) // RW 0x8EAF73
	{
		call("Close", {});
		m_state = STATE_CLOSING;
	}
	return m_openAudio;
}

void InGameNotificationBoxMovieClip::closeImmediately()
{
	if (m_state != STATE_LOADING && m_state != STATE_IDLE)
	{
		windows().setAptImage(prefix() + "_Icon", std::string());
		call("Hide", {});
		m_state = STATE_IDLE;
	}
	m_hasPending = false;
	m_hasActive = false;
}

void InGameNotificationBoxMovieClip::doClose()
{
	m_hasPending = false;
	if (m_state == STATE_OPENING || m_state == STATE_SHOWN)
	{
		call("Close", {});
		m_state = STATE_CLOSING;
	}
}

void InGameNotificationBoxMovieClip::setEnabled(bool enabled)
{
	m_enabled = enabled;
	if (!enabled)
	{
		closeImmediately();
	}
}
