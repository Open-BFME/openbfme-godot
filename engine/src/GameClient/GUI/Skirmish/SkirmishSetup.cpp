// OpenBFME. GPL-3.0.
// See GameClient/GUI/Skirmish/SkirmishSetup.h.

#include "GameClient/GUI/Skirmish/SkirmishSetup.h"

#include <algorithm>

namespace
{
char16_t lowerU16(char16_t c)
{
	return (c >= u'A' && c <= u'Z') ? (char16_t)(c - u'A' + u'a') : c;
}
bool equalNoCase(const std::u16string &a, const std::u16string &b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (lowerU16(a[i]) != lowerU16(b[i]))
		{
			return false;
		}
	}
	return true;
}
} // namespace

int MemorySkirmishProfiles::find(const std::u16string &name) const
{
	for (std::size_t i = 0; i < m_names.size(); ++i)
	{
		if (equalNoCase(m_names[i], name))
		{
			return (int)i;
		}
	}
	return -1;
}

bool MemorySkirmishProfiles::add(const std::u16string &name)
{
	if (name.empty() || find(name) >= 0)
	{
		return false;
	}
	m_names.push_back(name);
	return true;
}

bool MemorySkirmishProfiles::remove(const std::u16string &name)
{
	const int at = find(name);
	if (at < 0)
	{
		return false;
	}
	const bool wasCurrent = equalNoCase(m_current, name);
	for (auto it = m_values.begin(); it != m_values.end();)
	{
		it = it->first.first == m_names[(std::size_t)at] ? m_values.erase(it) : std::next(it); // lane UI-6: the profile's entries go with it
	}
	m_names.erase(m_names.begin() + at);
	if (wasCurrent)
	{
		m_current = m_names.empty() ? std::u16string() : m_names.front();
	}
	return true;
}

std::string MemorySkirmishProfiles::profileValue(int mode, const std::string &name) const
{
	auto it = m_values.find({ m_current, ":" + std::to_string(mode) + ":" + name });
	return m_current.empty() || it == m_values.end() ? std::string() : it->second;
}

void MemorySkirmishProfiles::setProfileValue(int mode, const std::string &name, const std::string &value)
{
	if (!m_current.empty())
	{
		m_values[{ m_current, ":" + std::to_string(mode) + ":" + name }] = value;
	}
}

bool MemorySkirmishProfiles::setCurrent(const std::u16string &name)
{
	const int at = find(name);
	if (at < 0)
	{
		return false;
	}
	m_current = m_names[(std::size_t)at];
	return true;
}
