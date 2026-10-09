// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Header templates (ZH GameClient/HeaderTemplate.h, GUI/HeaderTemplate.cpp): `HeaderTemplate <name>` blocks of
// Data\English\HeaderTemplate.ini give a font name, point size and bold flag; a WND window with `HEADERTEMPLATE = "<name>"` takes its
// font from the template (parseWindow's END case: getFontFromTemplate).  RotWK's file defines AptGadgets (Albertus MT 14) and
// AptGadgetsTiny for the APT gadgets and the legacy templates of the ZH menus (retail data: tests only).
// Not ported: GlobalLanguage::adjustFontSize (the resolution scaling of the point size): stop S-176.

#pragma once

#include "Common/INI.h"

#include <map>
#include <string>

struct HeaderTemplate
{
	std::string name;
	std::string fontName;
	int point = 0;
	bool bold = false;
};

class HeaderTemplateManager
{
public:
	void registerBlocks(INIBlockRegistry &registry);
	const HeaderTemplate *findHeaderTemplate(const std::string &name) const;
	std::size_t size() const { return m_templates.size(); }
	void addTemplate(const HeaderTemplate &t) { m_templates[t.name] = t; }

private:
	void parseDefinition(INI *ini);
	std::map<std::string, HeaderTemplate> m_templates; // ZH compares with AsciiString::compare (case-sensitive)
	std::vector<std::string> m_duplicates;

public:
	// Template names defined more than once (ZH DEBUG_CRASHes "Duplicate header Template"; the port records them).
	const std::vector<std::string> &duplicates() const { return m_duplicates; }
};
