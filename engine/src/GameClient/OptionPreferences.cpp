// OpenBFME. GPL-3.0.
// See GameClient/OptionPreferences.h.

#include "GameClient/OptionPreferences.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace
{
std::string trimmed(const std::string &s)
{
	std::size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a]))
	{
		++a;
	}
	while (b > a && std::isspace((unsigned char)s[b - 1]))
	{
		--b;
	}
	return s.substr(a, b - a);
}
} // namespace

void OptionPreferences::parse(const std::string &text)
{
	std::istringstream in(text);
	std::string raw;
	while (std::getline(in, raw))
	{
		const std::string line = trimmed(raw);
		// AsciiString::nextToken(&key, "="): leading '=' are skipped, the key runs to the next '='; the value is what follows that '='
		std::size_t start = 0;
		while (start < line.size() && line[start] == '=')
		{
			++start;
		}
		const std::size_t eq = line.find('=', start);
		if (eq == std::string::npos)
		{
			continue; // no value (ZH reads past the end here; nothing is kept)
		}
		const std::string key = trimmed(line.substr(start, eq - start));
		const std::string value = trimmed(line.substr(eq + 1));
		if (key.empty() || value.empty())
		{
			continue;
		}
		m_values[key] = value;
	}
}

std::string OptionPreferences::text() const
{
	std::string out;
	for (const auto &kv : m_values)
	{
		out += kv.first + " = " + kv.second + "\n";
	}
	return out;
}

bool OptionPreferences::load(const std::string &path, std::string *error)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		if (error)
		{
			*error = "cannot read " + path;
		}
		return false;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	parse(buffer.str());
	return true;
}

bool OptionPreferences::save(const std::string &path, std::string *error) const
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
	{
		if (error)
		{
			*error = "cannot write " + path;
		}
		return false;
	}
	out << text();
	if (!out)
	{
		if (error)
		{
			*error = "write failed: " + path;
		}
		return false;
	}
	return true;
}

std::string OptionPreferences::get(const std::string &key) const
{
	auto it = m_values.find(key);
	return it == m_values.end() ? std::string() : it->second;
}

bool OptionPreferences::getYesNo(const std::string &key, bool defaultValue) const
{
	auto it = m_values.find(key);
	if (it == m_values.end())
	{
		return defaultValue;
	}
	std::string v = it->second;
	for (char &c : v)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return v == "yes";
}
