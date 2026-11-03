// OpenBFME. GPL-3.0.
// See GameClient/GUI/AptMessageBox.h (lane UI-1).

#include "GameClient/GUI/AptMessageBox.h"

#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/GUI/WindowManager.h"
#include "Libraries/Source/Apt/AptValue.h"

namespace
{
std::string utf8(const std::u16string &s)
{
	std::string out;
	for (std::size_t i = 0; i < s.size(); ++i)
	{
		std::uint32_t c = s[i];
		if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
		{
			c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
			++i;
		}
		if (c < 0x80)
		{
			out.push_back((char)c);
		}
		else if (c < 0x800)
		{
			out.push_back((char)(0xC0 | (c >> 6)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
		else if (c < 0x10000)
		{
			out.push_back((char)(0xE0 | (c >> 12)));
			out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
		else
		{
			out.push_back((char)(0xF0 | (c >> 18)));
			out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
			out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
			out.push_back((char)(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

// RW 0x9532F3: the type names (RW 0xC81684 .. 0xC816AC)
const char *typeName(AptMessageBox::Type t)
{
	switch (t)
	{
		case AptMessageBox::TYPE_YES_NO: return "YesNo";
		case AptMessageBox::TYPE_OK_CANCEL: return "OkCancel";
		case AptMessageBox::TYPE_CANCEL: return "Cancel";
		case AptMessageBox::TYPE_NON_INTERACTIVE: return "NonInteractive";
		default: return "Ok";
	}
}
} // namespace

AptMessageBox::AptMessageBox(WindowManager &windows, Shell &shell, std::string clip) : m_windows(windows), m_clip(std::move(clip))
{
	// lane SAVE-2: a GuiFX.apt already loaded (the end screen's, loaded with a game) carries the box: RotWK's TheMessageBox is built on the one GuiFX level
	// (RW 0x81A3FB on _level13's clip); a second instance could not have its level
	const bool loaded = windows.findAptMovieIndex("GuiFX.apt") >= 0;
	m_guiFX = std::make_unique<AptGuiFXScreen>(windows, shell, loaded);
	registerCommands();
}

AptMessageBox::~AptMessageBox()
{
	for (const std::string &c : m_commands)
	{
		m_windows.unregisterCommand(c);
	}
}

int AptMessageBox::level() const
{
	return m_guiFX ? m_guiFX->level() : -1;
}

std::string AptMessageBox::prefix() const
{
	return "_level" + std::to_string(level()) + "." + m_clip; // RW 0x9534DE: "_level%u." + the clip path
}

void AptMessageBox::registerCommands()
{
	if (level() < 0)
	{
		m_windows.note("message-box-unavailable", "GuiFX.apt could not be loaded: no message box");
		return;
	}
	// RW 0x953141 .. 0x953197: the box is gone (type 5, nothing pending) and the button callback is told
	const char *buttons[4] = { "_OnButtonOk", "_OnButtonCancel", "_OnButtonYes", "_OnButtonNo" };
	for (int b = 0; b < 4; ++b)
	{
		const std::string name = prefix() + buttons[b];
		if (m_windows.registerCommand(name, [this, b](const std::string &) {
			    m_type = TYPE_NONE;
			    m_pending = 0;
			    m_calls.push_back(std::string("button ") + std::to_string(b));
			    m_trace.push_back(std::string("button ") + std::to_string(b));
			    if (m_onButton)
			    {
				    ButtonFn fn = m_onButton;
				    fn(b);
			    }
		    }))
		{
			m_commands.push_back(name);
		}
	}
	// RW 0x9531B4 ..: the state commands go to the state callback (_OnHidden: 3, then the callback is dropped)
	const char *states[4] = { "_OnShowing", "_OnShown", "_OnHiding", "_OnHidden" };
	for (int st = 0; st < 4; ++st)
	{
		const std::string name = prefix() + states[st];
		if (m_windows.registerCommand(name, [this, st](const std::string &) {
			    m_trace.push_back(std::string("state ") + std::to_string(st));
			    state(st);
		    }))
		{
			m_commands.push_back(name);
		}
	}
}

void AptMessageBox::state(int s)
{
	if (m_onState)
	{
		StateFn fn = m_onState;
		if (s == 3)
		{
			m_onState = StateFn();
		}
		fn(s);
	}
}

void AptMessageBox::show(Type type, const std::u16string &title, const std::u16string &text, ButtonFn onButton, StateFn onState)
{
	// RW 0x953861
	if (m_type != TYPE_NONE)
	{
		hide(false);
	}
	m_type = type;
	m_onButton = std::move(onButton);
	m_onState = std::move(onState); // replaced by every show (a notice pre-empted this way never hears its _OnHidden: retail's hazard, spec M 4)
	m_title = title;
	m_text = text;
	const std::string records = "APT:" + prefix() + "_"; // RW 0xC8171C "APT:_level%u.%s_"
	m_windows.setAptText(records + "Title", utf8(title));
	m_windows.setAptText(records + "Text", utf8(text));
	m_large = text.size() > 0x100; // RW 0x9539D4: the text's length > 256
	m_pending = 2;
	update();
}

void AptMessageBox::hide(bool instant)
{
	// RW 0x953243
	if (m_type == TYPE_NONE && m_pending != 2)
	{
		return;
	}
	m_type = TYPE_NONE;
	m_instant = instant;
	m_pending = 3;
	update();
	if (instant)
	{
		state(2); // RW 0x953243 with instant: hiding and hidden at once
		state(3);
	}
}

void AptMessageBox::update()
{
	// RW 0x9532F3: runs once the window manager is ready (+ 0x312 clear); here: once the movie is loaded
	if (m_pending == 0 || level() < 0 || !m_windows.isAptWindowLoaded(level()))
	{
		return;
	}
	std::string error;
	if (m_pending == 2)
	{
		if (!m_windows.invokeASAtValues(level(), m_clip, "Show", { AptValue::string(typeName(m_type)), AptValue::boolean(m_large) }, &error))
		{
			m_windows.note("invoke-failed", m_clip + ".Show: " + error);
		}
		m_calls.push_back(std::string("Show ") + typeName(m_type) + (m_large ? " true" : " false"));
	}
	else if (m_pending == 3)
	{
		if (!m_windows.invokeASAtValues(level(), m_clip, "Hide", { AptValue::boolean(m_instant) }, &error))
		{
			m_windows.note("invoke-failed", m_clip + ".Hide: " + error);
		}
		m_calls.push_back(std::string("Hide ") + (m_instant ? "true" : "false"));
	}
	m_pending = 0;
}
