// OpenBFME. GPL-3.0.
// See InGameCommandButtonSubMenu.h (RW 0x9D2E0F .. 0x9D3550).

#include "GameClient/GUI/AptScreens/InGameCommandButtonSubMenu.h"

#include <cstdlib>

const char *const InGameCommandButtonSubMenu::kFrameLoaded = "_OnButtonFrameLoaded";     // RW 0xC8C490
const char *const InGameCommandButtonSubMenu::kFrameUnloaded = "_OnButtonFrameUnloaded"; // RW 0xC8C478
const char *const InGameCommandButtonSubMenu::kClosed = "_OnClosed";                     // RW 0xC79170

InGameCommandButtonSubMenu::InGameCommandButtonSubMenu(int level, std::string path, CallFn call) : m_level(level), m_path(std::move(path)), m_call(std::move(call)) {}

std::string InGameCommandButtonSubMenu::commandName(const char *suffix) const
{
	return "_level" + std::to_string(m_level) + "." + m_path + suffix; // RW 0x9D3380: "_level%u." (RW 0xC7C4A4) + path + suffix
}

std::string InGameCommandButtonSubMenu::queryValue(const std::string &arg, const std::string &key)
{
	const std::string needle = key + "=";
	size_t p = 0;
	while ((p = arg.find(needle, p)) != std::string::npos)
	{
		if (p == 0 || arg[p - 1] == '&')
		{
			const size_t b = p + needle.size();
			const size_t e = arg.find('&', b);
			return arg.substr(b, e == std::string::npos ? std::string::npos : e - b);
		}
		p += needle.size();
	}
	return std::string();
}

std::string InGameCommandButtonSubMenu::stripLevel(const std::string &path)
{
	if (path.rfind("_level", 0) != 0)
	{
		return path;
	}
	const size_t dot = path.find('.');
	return dot == std::string::npos ? std::string() : path.substr(dot + 1);
}

bool InGameCommandButtonSubMenu::call(const char *fn, const std::vector<std::string> &args)
{
	const bool ok = m_call && m_call(m_path, fn, args);
	std::string line = std::string(fn) + "(";
	for (size_t i = 0; i < args.size(); ++i)
	{
		line += (i ? "," : "") + args[i];
	}
	line += ok ? ")" : ") FAILED";
	if (m_calls.size() >= 64)
	{
		m_calls.erase(m_calls.begin());
	}
	m_calls.push_back(line);
	return ok;
}

bool InGameCommandButtonSubMenu::addItem(int value)
{
	if (m_count >= kMaxItems)
	{
		return false;
	}
	m_items[m_count] = Item();
	m_items[m_count].value = value; // RW 0x9D2EDA: the record at + 0x1C, then + 0x1C + 1
	++m_count;
	return true;
}

bool InGameCommandButtonSubMenu::open()
{
	const bool ok = call("Show", { std::to_string(m_count) }); // RW 0x9D32F9 -> 0x92BC2A: "Show" with "%d" of + 0x1C
	m_open = true;                                               // set whatever the call returned (RW 0x9D3325)
	return ok;
}

void InGameCommandButtonSubMenu::dropItems()
{
	// RW 0x9D2F21 / 0x9D3235: count down to 0, each record's item, frame and content released (the content's destructor: DeleteContent, RW 0x9D2640)
	while (m_count > 0)
	{
		--m_count;
		Item &it = m_items[m_count];
		if (it.content && m_dropContent)
		{
			m_dropContent(it);
		}
		it = Item();
	}
}

void InGameCommandButtonSubMenu::update(const std::function<bool(int)> &createItemContent, const std::function<void(int)> &updateItemContent)
{
	if (m_closeRequested) // RW 0x9D2F92
	{
		if (m_open && !m_fading)
		{
			call("FadeOut", {}); // RW 0xBF6CF4
			m_fading = true;
		}
		m_closeRequested = false;
	}
	for (int i = 0; i < m_count; ++i)
	{
		Item &it = m_items[i];
		if (it.value >= 0 && !it.frame.empty() && !it.content && createItemContent)
		{
			it.content = createItemContent(i); // RW 0x9D2E25: the item's vslot 8 makes the content in the frame
		}
		if (it.content && updateItemContent)
		{
			updateItemContent(i); // the content's vslot 4
		}
	}
}

void InGameCommandButtonSubMenu::hide()
{
	if (!m_open) // RW 0x9D2F21
	{
		return;
	}
	call("Hide", {}); // RW 0xC79134
	dropItems();
	m_fading = false;
	m_open = false;
}

void InGameCommandButtonSubMenu::onButtonFrameLoaded(const std::string &arg)
{
	const std::string index = queryValue(arg, "index"); // RW 0x9D305F: "index" (RW 0xC7C3FC)
	if (index.empty())
	{
		return;
	}
	const int i = std::atoi(index.c_str());
	if (i < 0 || i >= m_count || !m_items[i].frame.empty() || m_items[i].value < 0)
	{
		return;
	}
	const std::string name = queryValue(arg, "name"); // RW 0xC300E0
	if (name.empty())
	{
		return;
	}
	m_items[i].frame = stripLevel(name); // RW 0x815563
}

void InGameCommandButtonSubMenu::onButtonFrameUnloaded(const std::string &arg)
{
	const std::string index = queryValue(arg, "index"); // RW 0x9D31B3
	if (index.empty())
	{
		return;
	}
	const int i = std::atoi(index.c_str());
	if (i < 0 || i >= kMaxItems)
	{
		return;
	}
	// the frame binding goes; its content went with the clip (INFERENCE: the record's content pointer is not released here, RW 0x9D31B3 releases one
	// reference only; with the frame gone, no DeleteContent can reach a clip, so the content is forgotten without a call)
	m_items[i].frame.clear();
	m_items[i].content = false;
	m_items[i].initialized = false;
	m_items[i].sig.clear();
}

void InGameCommandButtonSubMenu::onClosed()
{
	if (!m_fading) // RW 0x9D3235
	{
		return;
	}
	m_fading = false;
	if (m_open)
	{
		dropItems();
		m_open = false;
	}
}
