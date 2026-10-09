// OpenBFME. GPL-3.0.
// See GameClient/MapCreationHooks.h for the sources.

#include "GameClient/MapCreationHooks.h"

#include "Common/AsciiString.h"
#include "Common/Thing/RawModuleData.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace
{
std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace((unsigned char)s[a])) ++a;
	while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
	return s.substr(a, b - a);
}

bool isIdentChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// the value of attribute `name` ( name="value" ) in a tag's text, found at a word boundary; false when absent
bool attribute(const std::string &tag, const std::string &name, std::string &value)
{
	size_t pos = 0;
	while ((pos = tag.find(name, pos)) != std::string::npos)
	{
		const bool startOk = pos == 0 || std::isspace((unsigned char)tag[pos - 1]);
		size_t p = pos + name.size();
		while (p < tag.size() && std::isspace((unsigned char)tag[p])) ++p;
		if (startOk && p < tag.size() && tag[p] == '=')
		{
			++p;
			while (p < tag.size() && std::isspace((unsigned char)tag[p])) ++p;
			if (p < tag.size() && (tag[p] == '"' || tag[p] == '\''))
			{
				const size_t end = tag.find(tag[p], p + 1);
				if (end == std::string::npos)
				{
					return false;
				}
				value = tag.substr(p + 1, end - p - 1);
				return true;
			}
		}
		pos += name.size();
	}
	return false;
}

std::string stripXmlComments(const std::string &s, std::string *error)
{
	std::string out;
	size_t pos = 0;
	for (;;)
	{
		const size_t a = s.find("<!--", pos);
		if (a == std::string::npos)
		{
			out += s.substr(pos);
			return out;
		}
		out += s.substr(pos, a - pos);
		const size_t b = s.find("-->", a + 4);
		if (b == std::string::npos)
		{
			if (error)
			{
				*error = "scriptevents.xml: an XML comment is not closed";
			}
			return std::string();
		}
		pos = b + 3;
	}
}

// the part of a Lua line before a `--` comment that is not inside a string
std::string stripLuaComment(const std::string &line)
{
	char quote = 0;
	for (size_t i = 0; i < line.size(); ++i)
	{
		const char c = line[i];
		if (quote)
		{
			if (c == quote)
			{
				quote = 0;
			}
		}
		else if (c == '"' || c == '\'')
		{
			quote = c;
		}
		else if (c == '-' && i + 1 < line.size() && line[i + 1] == '-')
		{
			return line.substr(0, i);
		}
	}
	return line;
}

bool scanEvents(const std::string &xmlRaw, CreationScriptData &out, std::string *error)
{
	std::string cerr;
	const std::string xml = stripXmlComments(xmlRaw, &cerr);
	if (!cerr.empty())
	{
		if (error) *error = cerr;
		return false;
	}
	CreationScriptData::EventList *cur = nullptr;
	size_t pos = 0;
	while ((pos = xml.find('<', pos)) != std::string::npos)
	{
		const size_t end = xml.find('>', pos);
		if (end == std::string::npos)
		{
			if (error) *error = "scriptevents.xml: a tag is not closed";
			return false;
		}
		const std::string tag = xml.substr(pos + 1, end - pos - 1);
		pos = end + 1;
		if (tag.compare(0, 9, "EventList") == 0 && tag.size() > 9 && std::isspace((unsigned char)tag[9]))
		{
			CreationScriptData::EventList l;
			if (!attribute(tag, "Name", l.name) || l.name.empty())
			{
				if (error) *error = "scriptevents.xml: an EventList has no Name";
				return false;
			}
			attribute(tag, "Inherit", l.inherit);
			if (out.eventLists.count(l.name))
			{
				if (error) *error = "scriptevents.xml: EventList " + l.name + " is defined twice";
				return false;
			}
			cur = &out.eventLists.emplace(l.name, l).first->second;
			if (!tag.empty() && tag.back() == '/')
			{
				cur = nullptr;
			}
		}
		else if (tag == "/EventList")
		{
			cur = nullptr;
		}
		else if (tag.compare(0, 12, "EventHandler") == 0 && tag.size() > 12 && std::isspace((unsigned char)tag[12]))
		{
			std::string ev, fn;
			if (!attribute(tag, "EventName", ev) || !attribute(tag, "ScriptFunctionName", fn))
			{
				if (error) *error = "scriptevents.xml: an EventHandler has no EventName or ScriptFunctionName";
				return false;
			}
			if (!cur)
			{
				if (error) *error = "scriptevents.xml: EventHandler " + ev + " outside an EventList";
				return false;
			}
			if (ev == "OnCreated")
			{
				cur->onCreated.push_back(fn);
			}
		}
	}
	if (out.eventLists.empty())
	{
		if (error) *error = "scriptevents.xml: no EventList";
		return false;
	}
	return true;
}

bool scanLua(const std::string &lua, CreationScriptData &out, std::string *error)
{
	CreationScriptData::LuaFunction *cur = nullptr;
	std::string curName;
	size_t pos = 0;
	size_t lineNo = 0;
	while (pos <= lua.size())
	{
		size_t nl = lua.find('\n', pos);
		if (nl == std::string::npos)
		{
			nl = lua.size();
		}
		std::string line = lua.substr(pos, nl - pos);
		pos = nl + 1;
		++lineNo;
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		if (line.compare(0, 9, "function ") == 0)
		{
			if (cur)
			{
				if (error) *error = "scripts.lua line " + std::to_string(lineNo) + ": function " + curName + " has no closing end line";
				return false;
			}
			const std::string rest = line.substr(9);
			size_t a = 0;
			while (a < rest.size() && std::isspace((unsigned char)rest[a])) ++a;
			size_t b = a;
			while (b < rest.size() && isIdentChar(rest[b])) ++b;
			curName = rest.substr(a, b - a);
			if (curName.empty())
			{
				if (error) *error = "scripts.lua line " + std::to_string(lineNo) + ": function without a name";
				return false;
			}
			if (out.luaFunctions.count(curName))
			{
				if (error) *error = "scripts.lua line " + std::to_string(lineNo) + ": function " + curName + " is defined twice";
				return false;
			}
			cur = &out.luaFunctions[curName];
			continue;
		}
		if (!cur)
		{
			continue;
		}
		if (line.compare(0, 3, "end") == 0 && (line.size() == 3 || !isIdentChar(line[3])))
		{
			cur = nullptr;
			continue;
		}
		const std::string code = stripLuaComment(line);
		// calls: identifier followed by (
		for (size_t i = 0; i < code.size();)
		{
			if (std::isalpha((unsigned char)code[i]) || code[i] == '_')
			{
				size_t j = i;
				while (j < code.size() && isIdentChar(code[j])) ++j;
				const std::string word = code.substr(i, j - i);
				size_t k = j;
				while (k < code.size() && std::isspace((unsigned char)code[k])) ++k;
				static const std::set<std::string> kw = { "if", "elseif", "while", "and", "or", "not", "function", "return" };
				if (k < code.size() && code[k] == '(' && !kw.count(word))
				{
					cur->calls.push_back(word);
					if (word == "ObjectHideSubObjectPermanently")
					{
						// the only shape retail scripts use is ( self , "X" , true ); anything else is reported, not guessed
						const size_t close = code.find(')', k);
						std::vector<std::string> parts;
						if (close != std::string::npos)
						{
							const std::string args = code.substr(k + 1, close - k - 1);
							size_t p0 = 0;
							for (;;)
							{
								const size_t c = args.find(',', p0);
								parts.push_back(trim(args.substr(p0, c == std::string::npos ? std::string::npos : c - p0)));
								if (c == std::string::npos) break;
								p0 = c + 1;
							}
						}
						if (parts.size() != 3 || parts[0] != "self" || parts[1].size() < 2 || parts[1].front() != '"' || parts[1].back() != '"' || (parts[2] != "true" && parts[2] != "false"))
						{
							if (error) *error = "scripts.lua line " + std::to_string(lineNo) + ": ObjectHideSubObjectPermanently call is not (self, \"name\", true or false): " + trim(code);
							return false;
						}
						// the third argument is the hidden flag: false shows the sub object permanently (ObjectHideSubObjectPermanently(self, "X", false))
						(parts[2] == "true" ? cur->permanentHides : cur->permanentShows).push_back(parts[1].substr(1, parts[1].size() - 2));
					}
				}
				i = j;
			}
			else
			{
				++i;
			}
		}
	}
	if (cur)
	{
		if (error) *error = "scripts.lua: function " + curName + " has no closing end line";
		return false;
	}
	if (out.luaFunctions.empty())
	{
		if (error) *error = "scripts.lua: no function";
		return false;
	}
	return true;
}
} // namespace

bool MapCreationHooks::scan(const std::string &scriptEventsXml, const std::string &scriptsLua, CreationScriptData &out, std::string *error)
{
	out = CreationScriptData();
	out.xmlText = scriptEventsXml;
	out.luaText = scriptsLua;
	out.rawLoaded = true;
	// the inventory is report-only: a failure is kept, never a gate on the real runtime
	std::string scanError;
	CreationScriptData inventory;
	if (!scanEvents(scriptEventsXml, inventory, &scanError) || !scanLua(scriptsLua, inventory, &scanError))
	{
		out.inventoryError = scanError;
		if (error)
		{
			*error = scanError;
		}
		return false;
	}
	out.eventLists = std::move(inventory.eventLists);
	out.luaFunctions = std::move(inventory.luaFunctions);
	out.loaded = true;
	return true;
}

bool MapCreationHooks::load(ArchiveFileSystem &fs, CreationScriptData &out, std::string *error)
{
	std::vector<std::uint8_t> xml, lua;
	if (!fs.readFile("data\\scripts\\scriptevents.xml", xml, error) || !fs.readFile("data\\scripts\\scripts.lua", lua, error))
	{
		out = CreationScriptData();
		return false;
	}
	// The files are kept whatever the inventory scanner thinks of them (a valid one-line function, a redefinition or a computed hide argument
	// make it fail, the real VM runs them); only a missing file is a load failure. The scan result is in out.loaded / out.inventoryError.
	std::string scanError;
	scan(std::string(xml.begin(), xml.end()), std::string(lua.begin(), lua.end()), out, &scanError);
	return out.rawLoaded;
}

std::vector<MapCreationHook> MapCreationHooks::templateEventLists(const ThingTemplate &tmpl)
{
	std::vector<MapCreationHook> out;
	for (const ThingTemplate::Nugget &n : tmpl.behaviorModules().nuggets())
	{
		const RawModuleData *raw = dynamic_cast<const RawModuleData *>(n.data.get());
		if (!raw)
		{
			continue;
		}
		for (const RawModuleData::Line &l : raw->lines())
		{
			std::string text = l.text;
			const size_t semi = text.find(';');
			if (semi != std::string::npos)
			{
				text.erase(semi);
			}
			text = trim(text);
			const size_t eq = text.find('=');
			if (eq == std::string::npos || AsciiStringUtil::compareNoCase(trim(text.substr(0, eq)), "AILuaEventsList") != 0)
			{
				continue;
			}
			MapCreationHook h;
			h.module = n.tag;
			h.eventList = trim(text.substr(eq + 1));
			out.push_back(std::move(h));
		}
	}
	return out;
}

std::vector<MapCreationHook> MapCreationHooks::templateHooks(const ThingTemplate &tmpl, const CreationScriptData &data, std::vector<std::string> *problems)
{
	std::vector<MapCreationHook> out;
	auto problem = [&](const std::string &p) {
		if (problems)
		{
			problems->push_back("template " + tmpl.getName() + ": " + p);
		}
	};
	for (const ThingTemplate::Nugget &n : tmpl.behaviorModules().nuggets())
	{
		const RawModuleData *raw = dynamic_cast<const RawModuleData *>(n.data.get());
		if (!raw)
		{
			continue;
		}
		for (const RawModuleData::Line &l : raw->lines())
		{
			std::string text = l.text;
			const size_t semi = text.find(';');
			if (semi != std::string::npos)
			{
				text.erase(semi);
			}
			text = trim(text);
			const size_t eq = text.find('=');
			if (eq == std::string::npos || AsciiStringUtil::compareNoCase(trim(text.substr(0, eq)), "AILuaEventsList") != 0)
			{
				continue;
			}
			MapCreationHook h;
			h.module = n.tag;
			h.eventList = trim(text.substr(eq + 1));
			std::set<std::string> seen;
			std::string name = h.eventList;
			while (!name.empty())
			{
				auto it = data.eventLists.find(name);
				if (it == data.eventLists.end())
				{
					problem("AILuaEventsList " + h.eventList + (name == h.eventList ? "" : " (inherits " + name + ")") + " is not an EventList of scriptevents.xml");
					break;
				}
				if (!seen.insert(name).second)
				{
					problem("EventList " + name + " inherits itself");
					break;
				}
				for (const std::string &fn : it->second.onCreated)
				{
					h.functions.push_back(fn);
				}
				name = it->second.inherit;
			}
			std::set<std::string> hides, shows;
			for (const std::string &fn : h.functions)
			{
				auto f = data.luaFunctions.find(fn);
				if (f == data.luaFunctions.end())
				{
					problem("OnCreated handler " + fn + " of " + h.eventList + " is not defined in scripts.lua");
					continue;
				}
				for (const std::string &s : f->second.permanentHides)
				{
					if (hides.insert(s).second)
					{
						h.permanentHides.push_back(s);
					}
				}
				for (const std::string &s : f->second.permanentShows)
				{
					if (shows.insert(s).second)
					{
						h.permanentShows.push_back(s);
					}
				}
			}
			if (!h.functions.empty())
			{
				out.push_back(std::move(h));
			}
		}
	}
	return out;
}
