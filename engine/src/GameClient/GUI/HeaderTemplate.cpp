// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameClient/GUI/HeaderTemplate.h.

#include "GameClient/GUI/HeaderTemplate.h"

#include <cstddef>

namespace
{
// ZH HeaderTemplateManager::m_headerFieldParseTable
const FieldParse kHeaderFieldParse[] = {
	{ "Font", INI::parseQuotedAsciiString, nullptr, (int)offsetof(HeaderTemplate, fontName) },
	{ "Point", INI::parseInt, nullptr, (int)offsetof(HeaderTemplate, point) },
	{ "Bold", INI::parseBool, nullptr, (int)offsetof(HeaderTemplate, bold) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void HeaderTemplateManager::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("HeaderTemplate", [this](INI *ini) { parseDefinition(ini); });
}

void HeaderTemplateManager::parseDefinition(INI *ini)
{
	const std::string name = ini->getNextToken();
	auto it = m_templates.find(name);
	if (it != m_templates.end())
	{
		m_duplicates.push_back(name);
	}
	HeaderTemplate &t = m_templates[name];
	t.name = name;
	ini->initFromINI(&t, kHeaderFieldParse);
}

const HeaderTemplate *HeaderTemplateManager::findHeaderTemplate(const std::string &name) const
{
	auto it = m_templates.find(name);
	return it == m_templates.end() ? nullptr : &it->second;
}
