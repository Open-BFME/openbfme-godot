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
	m_names.erase(m_names.begin() + at);
	if (wasCurrent)
	{
		m_current = m_names.empty() ? std::u16string() : m_names.front();
	}
	return true;
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
