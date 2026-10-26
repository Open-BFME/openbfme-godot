// OpenBFME. GPL-3.0.
// See GameClient/GUI/Skirmish/SkirmishPreferences.h.

#include "GameClient/GUI/Skirmish/SkirmishPreferences.h"
#include "Common/PathIO.h"

#include <filesystem>
#include <system_error>

namespace
{
bool isSpace(char16_t c)
{
	return c == u' ' || c == u'\t' || c == u'\r' || c == u'\n';
}

std::u16string trim(const std::u16string &s)
{
	std::size_t a = 0, b = s.size();
	while (a < b && isSpace(s[a]))
	{
		++a;
	}
	while (b > a && isSpace(s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}

char16_t lowerAscii(char16_t c)
{
	return c >= u'A' && c <= u'Z' ? (char16_t)(c - u'A' + u'a') : c;
}

bool equalNoCase(const std::u16string &a, const std::u16string &b)
{
	if (a.size() != b.size())
	{
		return false;
	}
	for (std::size_t i = 0; i < a.size(); ++i)
	{
		if (lowerAscii(a[i]) != lowerAscii(b[i]))
		{
			return false;
		}
	}
	return true;
}

bool isAlnumByte(unsigned char b)
{
	return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z');
}

char hexDigit(int v)
{
	// RW 0x93BD39
	return (char)(v < 10 ? '0' + v : 'A' + v - 10);
}

int hexValue(char c)
{
	// RW 0x93BD52: 0-9, a-f, A-F; anything else 0
	if (c >= '0' && c <= '9')
	{
		return c - '0';
	}
	if (c >= 'a' && c <= 'f')
	{
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F')
	{
		return c - 'A' + 10;
	}
	return 0;
}
} // namespace

std::string SkirmishPreferences::unicodeToQuotedPrintable(const std::u16string &text)
{
	// RW 0x93BD82: low byte, then high byte of every unit, into a 1024-byte buffer (it stops before index 0x3FD)
	std::string out;
	for (char16_t unit : text)
	{
		if (unit == 0 || out.size() >= 0x3FD)
		{
			break;
		}
		for (unsigned char b : { (unsigned char)(unit & 0xFF), (unsigned char)(unit >> 8) })
		{
			if (isAlnumByte(b))
			{
				out += (char)b;
			}
			else
			{
				out += '_';
				out += hexDigit(b >> 4);
				out += hexDigit(b & 0xF);
			}
		}
	}
	return out;
}

std::u16string SkirmishPreferences::quotedPrintableToUnicode(const std::string &text)
{
	// RW 0x93BF28: '_' takes the next one or two characters as hex digits; the bytes are read back as UTF-16 units (little-endian)
	std::string bytes;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (c != '_')
		{
			bytes += c;
			continue;
		}
		if (i + 1 >= text.size())
		{
			break;
		}
		int v = hexValue(text[++i]);
		if (i + 1 < text.size())
		{
			v = (v << 4) | hexValue(text[++i]);
		}
		bytes += (char)v;
	}
	std::u16string out;
	for (std::size_t i = 0; i + 1 < bytes.size() + 1; i += 2)
	{
		const unsigned char lo = (unsigned char)bytes[i];
		const unsigned char hi = i + 1 < bytes.size() ? (unsigned char)bytes[i + 1] : 0;
		const char16_t unit = (char16_t)(lo | (hi << 8));
		if (unit == 0)
		{
			break;
		}
		out += unit;
	}
	return out;
}

SkirmishPreferences::SkirmishPreferences(std::string path) : m_path(std::move(path))
{
	// RW 0x8221F5
	std::error_code ec;
	const bool exists = std::filesystem::exists(PathIO::native(m_path), ec);
	if (ec)
	{
		// lane MENU-1 review: a folder that cannot be inspected is an error, never taken for a first run
		m_errors.push_back("Skirmish.ini: cannot inspect " + m_path + ": " + ec.message());
		return;
	}
	if (!exists)
	{
		// the first run (no file yet) only when its folder is a folder or not made yet; a path through a file is an error
		const std::filesystem::path folder = PathIO::native(m_path).parent_path();
		std::error_code fec;
		if (!folder.empty() && std::filesystem::exists(folder, fec) && !std::filesystem::is_directory(folder, fec))
		{
			m_errors.push_back("Skirmish.ini: " + folder.u8string() + " is not a folder");
			return;
		}
		if (fec)
		{
			m_errors.push_back("Skirmish.ini: cannot inspect " + folder.u8string() + ": " + fec.message());
		}
		return;
	}
	std::string error;
	if (!m_file.load(m_path, &error))
	{
		m_errors.push_back("Skirmish.ini: " + error);
		return;
	}
	if (!m_file.has("UserNames"))
	{
		return;
	}
	const std::u16string all = trim(quotedPrintableToUnicode(m_file.get("UserNames")));
	std::size_t at = 0;
	while (at < all.size())
	{
		// ZH nextToken: separators skipped, the token runs to the next one
		while (at < all.size() && all[at] == u',')
		{
			++at;
		}
		const std::size_t end = all.find(u',', at);
		const std::size_t stop = end == std::u16string::npos ? all.size() : end;
		if (stop > at)
		{
			m_names.push_back(all.substr(at, stop - at));
		}
		at = stop;
	}
	if (m_file.has("CurrentUserName"))
	{
		m_currentUserName = m_file.get("CurrentUserName");
	}
}

std::u16string SkirmishPreferences::current() const
{
	// BFME2 0x43B9F5: the decoded, trimmed CurrentUserName
	return trim(quotedPrintableToUnicode(m_currentUserName));
}

bool SkirmishPreferences::add(const std::u16string &name)
{
	// BFME2 0x521B56 (the add-profile handler's test) and 0x43C612
	if (name.empty())
	{
		return false;
	}
	for (const std::u16string &n : m_names)
	{
		if (equalNoCase(n, name))
		{
			return false;
		}
	}
	m_names.push_back(name);
	write();
	return true;
}

bool SkirmishPreferences::remove(const std::u16string &name)
{
	// BFME2 0x43C2EB drops the exact name; 0x522833 makes the first name current when the current one went
	bool found = false;
	for (std::size_t i = m_names.size(); i-- > 0;)
	{
		if (m_names[i] == name)
		{
			m_names.erase(m_names.begin() + (std::ptrdiff_t)i);
			found = true;
		}
	}
	if (!found)
	{
		return false;
	}
	// lane UI-6: BFME2 0x43C2EB erases the profile's entries of both modes (RW 0x8220A3: every key that starts with ":<mode>:<quoted name>:")
	for (int mode = 0; mode < 2; ++mode)
	{
		const std::string prefix = profileKey(mode, unicodeToQuotedPrintable(name), std::string());
		std::vector<std::string> doomed;
		for (const auto &kv : m_file.values())
		{
			if (kv.first.compare(0, prefix.size(), prefix) == 0)
			{
				doomed.push_back(kv.first);
			}
		}
		for (const std::string &k : doomed)
		{
			m_file.erase(k);
		}
	}
	if (current() == name)
	{
		m_currentUserName = m_names.empty() ? std::string() : unicodeToQuotedPrintable(m_names.front());
	}
	write();
	return true;
}

std::string SkirmishPreferences::profileKey(int mode, const std::string &quotedName, const std::string &name)
{
	return ":" + std::to_string(mode) + ":" + quotedName + ":" + name; // RW 0x821AB4
}

std::string SkirmishPreferences::profileValue(int mode, const std::string &name) const
{
	// RW 0x821B89 (the current name's key), then the preferences' getAsciiString ("" when absent)
	if (m_currentUserName.empty())
	{
		return std::string();
	}
	const std::string key = profileKey(mode, m_currentUserName, name);
	return m_file.has(key) ? m_file.get(key) : std::string();
}

void SkirmishPreferences::setProfileValue(int mode, const std::string &name, const std::string &value)
{
	if (!m_currentUserName.empty())
	{
		m_file.set(profileKey(mode, m_currentUserName, name), value); // RW 0x821E55 / 0x821D55: setAsciiString (written by flush)
	}
}

bool SkirmishPreferences::setCurrent(const std::u16string &name)
{
	// RW 0x82259F, then the write (BFME2 0x521B56 / 0x5229D3: slot 3)
	bool known = false;
	for (const std::u16string &n : m_names)
	{
		known = known || equalNoCase(n, name);
	}
	if (!known)
	{
		return false;
	}
	m_currentUserName = unicodeToQuotedPrintable(name);
	write();
	return true;
}

void SkirmishPreferences::write()
{
	// BFME2 0x43C6C7: "UserNames" rebuilt (every name followed by ",", RW 0x822626), then the file
	std::u16string joined;
	for (const std::u16string &n : m_names)
	{
		joined += n;
		joined += u',';
	}
	m_file.set("UserNames", unicodeToQuotedPrintable(joined));
	if (m_currentUserName.empty())
	{
		m_file.erase("CurrentUserName");
	}
	else
	{
		m_file.set("CurrentUserName", m_currentUserName);
	}
	std::error_code ec;
	const std::filesystem::path folder = PathIO::native(m_path).parent_path();
	if (!folder.empty())
	{
		std::filesystem::create_directories(folder, ec);
	}
	std::string error;
	if (!m_file.save(m_path, &error))
	{
		m_errors.push_back("Skirmish.ini: " + error);
	}
}

std::string SkirmishPreferences::folder() const
{
	const std::filesystem::path dir = PathIO::native(m_path).parent_path();
	if (dir.empty())
	{
		return std::string();
	}
	std::string out = dir.u8string();
	if (out.back() != '/' && out.back() != '\\')
	{
		out += (char)std::filesystem::path::preferred_separator;
	}
	return out;
}
