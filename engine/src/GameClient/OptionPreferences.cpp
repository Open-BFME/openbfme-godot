// OpenBFME. GPL-3.0.
// See GameClient/OptionPreferences.h.

#include "GameClient/OptionPreferences.h"
#include "Common/PathIO.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
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
	// lane OPTS-2: the file's line ending (see text())
	std::size_t crlf = 0, lf = 0;
	for (std::size_t i = 0; i < text.size(); ++i)
	{
		if (text[i] == '\n')
		{
			(i > 0 && text[i - 1] == '\r') ? ++crlf : ++lf;
		}
	}
	if (crlf + lf != 0)
	{
		m_eol = crlf == 0 ? "\n" : "\r\n";
	}
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
		out += kv.first + " = " + kv.second + m_eol;
	}
	return out;
}

bool OptionPreferences::load(const std::string &path, std::string *error)
{
	std::ifstream in(PathIO::native(path), std::ios::binary);
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
	std::ofstream out(PathIO::native(path), std::ios::binary | std::ios::trunc);
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

float OptionPreferences::getScrollFactor(float defaultFactor) const
{
	auto it = m_values.find("ScrollFactor");
	if (it == m_values.end())
	{
		return defaultFactor;
	}
	int factor = std::atoi(it->second.c_str());
	if (factor < 0)
	{
		factor = 1;
	}
	if (factor > 100)
	{
		factor = 100;
	}
	return (float)factor * 0.02f;
}

float OptionPreferences::getBrightness() const
{
	auto it = m_values.find("Brightness");
	return it == m_values.end() ? 50.0f : (float)std::atoi(it->second.c_str());
}

float OptionPreferences::displayGamma(int brightness, float current)
{
	if (brightness < 50)
	{
		return brightness <= 0 ? 0.6f : 1.0f - (float)(50 - brightness) * 0.008f; // RW 0xBDAD70, 0xBD1908, 0xC0412C
	}
	if (brightness > 50)
	{
		return (float)(brightness - 50) * 0.02f + 1.0f; // RW 0xBDC320
	}
	return current;
}

bool OptionPreferences::getResolution(int *x, int *y) const
{
	auto it = m_values.find("Resolution");
	int rx = 0, ry = 0;
	if (it == m_values.end() || std::sscanf(it->second.c_str(), "%d%d", &rx, &ry) != 2)
	{
		return false;
	}
	*x = rx;
	*y = ry;
	return true;
}

std::vector<std::string> OptionPreferences::migrateLegacyKeys()
{
	std::vector<std::string> renamed;
	auto it = m_values.find(kLegacySoftParticles);
	if (it != m_values.end())
	{
		if (!has(kSoftParticles))
		{
			m_values[kSoftParticles] = it->second;
		}
		m_values.erase(it);
		renamed.push_back(kLegacySoftParticles);
	}
	return renamed;
}
