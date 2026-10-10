// OpenBFME. GPL-3.0.
// See GameClient/GlobalLanguage.h for the target facts.

#include "GameClient/GlobalLanguage.h"

#include "Common/INI.h"

#include <cmath>

namespace
{
// the field's byte offset in GlobalLanguage (the class holds std::string members, so offsetof is not used)
template <typename T>
int fieldOffset(T GlobalLanguage::*member)
{
	static const GlobalLanguage probe;
	return (int)(reinterpret_cast<const char *>(&(probe.*member)) - reinterpret_cast<const char *>(&probe));
}

const FieldParse *buildTable()
{
	// RW 0xBF4C20, in its order
	static const FieldParse table[] = {
		{ "DecimalSeparator", INI::parseAsciiString, nullptr, fieldOffset(&GlobalLanguage::decimalSeparator) },
		{ "ThousandSeparator", INI::parseAsciiString, nullptr, fieldOffset(&GlobalLanguage::thousandSeparator) },
		{ "TimeMinuteToSecondSeparator", INI::parseAsciiString, nullptr, fieldOffset(&GlobalLanguage::timeMinuteToSecondSeparator) },
		{ "UnicodeFontName", INI::parseAsciiString, nullptr, fieldOffset(&GlobalLanguage::unicodeFontName) },
		{ "LocalFontFile", GlobalLanguage::parseLocalFontFile, nullptr, 0 },
		{ "MilitaryCaptionSpeed", INI::parseInt, nullptr, fieldOffset(&GlobalLanguage::militaryCaptionSpeed) },
		{ "UseHardWordWrap", INI::parseBool, nullptr, fieldOffset(&GlobalLanguage::useHardWordWrap) },
		{ "ResolutionFontAdjustment", INI::parseReal, nullptr, fieldOffset(&GlobalLanguage::resolutionFontAdjustment) },
		{ "AudioLanguage", INI::parseAsciiString, nullptr, fieldOffset(&GlobalLanguage::audioLanguage) },
		{ "CopyrightFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::copyrightFont) },
		{ "MessageFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::messageFont) },
		{ "MilitaryCaptionTitleFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::militaryCaptionTitleFont) },
		{ "MilitaryCaptionFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::militaryCaptionFont) },
		{ "AudioSubtitleFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::audioSubtitleFont) },
		{ "SuperweaponCountdownNormalFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::superweaponCountdownNormalFont) },
		{ "SuperweaponCountdownReadyFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::superweaponCountdownReadyFont) },
		{ "NamedTimerCountdownNormalFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::namedTimerCountdownNormalFont) },
		{ "NamedTimerCountdownReadyFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::namedTimerCountdownReadyFont) },
		{ "DrawableCaptionFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::drawableCaptionFont) },
		{ "DefaultWindowFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::defaultWindowFont) },
		{ "DefaultDisplayStringFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::defaultDisplayStringFont) },
		{ "TooltipFontName", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::tooltipFontName) },
		{ "NativeDebugDisplay", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::nativeDebugDisplay) },
		{ "DrawGroupInfoFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::drawGroupInfoFont) },
		{ "CreditsTitleFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::creditsTitleFont) },
		{ "CreditsMinorTitleFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::creditsMinorTitleFont) },
		{ "CreditsNormalFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::creditsNormalFont) },
		{ "HelpBoxNameFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::helpBoxNameFont) },
		{ "HelpBoxCostFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::helpBoxCostFont) },
		{ "HelpBoxShortcutFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::helpBoxShortcutFont) },
		{ "HelpBoxDescriptionFont", GlobalLanguage::parseFontDesc, nullptr, fieldOffset(&GlobalLanguage::helpBoxDescriptionFont) },
		{ nullptr, nullptr, nullptr, 0 },
	};
	return table;
}
} // namespace

const FieldParse *GlobalLanguage::fieldParseTable()
{
	static const FieldParse *table = buildTable();
	return table;
}

void GlobalLanguage::registerBlocks(INIBlockRegistry &registry)
{
	registry.registerBlock("Language", [this](INI *ini) {
		ini->initFromINI(this, fieldParseTable());
		loaded = true;
	});
}

int GlobalLanguage::adjustFontSize(int size, int xResolution)
{
	// RW 0x5E9BEF: (float)xResolution * (1 / 1024.0f) (the divisor folded to a multiply), times the size, floored (MSVCR71 floor, then fistp)
	const float ratio = (float)xResolution * (1.0f / 1024.0f);
	return (int)std::floor((double)((float)size * ratio));
}

void GlobalLanguage::parseFontDesc(INI *ini, void *, void *store, const void *)
{
	FontDesc *font = static_cast<FontDesc *>(store);
	font->name = ini->getNextQuotedAsciiString();
	font->size = ini->scanInt(ini->getNextToken());
	font->bold = ini->scanBool(ini->getNextToken());
}

void GlobalLanguage::parseLocalFontFile(INI *ini, void *instance, void *, const void *)
{
	static_cast<GlobalLanguage *>(instance)->localFonts.push_front(ini->getNextAsciiString());
}
