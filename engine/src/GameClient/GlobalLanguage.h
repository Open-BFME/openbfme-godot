// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameClient/GlobalLanguage.cpp).
//
// GlobalLanguage (lane UI-4): the "Language" block of the language archive's language.ini, which names the fonts the engine's own text uses
// (the credits roll's three fonts among them).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
// - GlobalLanguage::init (RW 0x5E9D3C) loads "language.ini" (RW 0xBF510C; the English archive's file sits at the archive root) with INI_LOAD_OVERWRITE
//   (RW 0x5E9D61 .. 0x5E9D7D); the block keyword "Language" runs initFromINI over the field table RW 0xBF4C20 (BFME2 decomp
//   GlobalLanguageAdjustFontSize.cpp INI::parseLanguageDefinition, tier A for the BFME2 twin);
// - the table's 31 rows, in order: DecimalSeparator, ThousandSeparator, TimeMinuteToSecondSeparator, UnicodeFontName (parseAsciiString RW 0x42EE5E,
//   + 0x0C .. + 0x18), LocalFontFile (RW 0x5EA54B: getNextAsciiString pushed to the front of the list at + 0x138), MilitaryCaptionSpeed (parseInt
//   RW 0x42EC5E, + 0x28), UseHardWordWrap (parseBool RW 0x42E558, + 0x24), ResolutionFontAdjustment (parseReal RW 0x42ED00, + 0x134), AudioLanguage
//   (parseAsciiString, + 0x20), then 22 fonts through parseFontDesc RW 0x778C93 (+ 0x2C .. + 0x128, 12 bytes each);
// - parseFontDesc (RW 0x778C93, BFME2 decomp GlobalLanguageParseFontDesc.cpp, tier A): getNextQuotedAsciiString, scanInt, scanBool;
// - adjustFontSize (RW 0x5E9BEF, BFME2 decomp GlobalLanguageAdjustFontSize.cpp, tier A): floor(size * xResolution / 1024).
// DONOR (ZH GlobalLanguage.cpp): the constructor's defaults (empty strings and fonts, ResolutionFontAdjustment 0.7). The font files of LocalFontFile
// are recorded, not registered with the device (the device draws with the corpus fonts, S-176).

#pragma once

#include <list>
#include <string>

class INIBlockRegistry;
class INI;
struct FieldParse;

struct FontDesc
{
	std::string name;
	int size = 0;
	bool bold = false;
};

class GlobalLanguage
{
public:
	// RW 0xBF510C
	static const char *fileName() { return "language.ini"; }
	// "Language" -> initFromINI over the RW 0xBF4C20 table
	void registerBlocks(INIBlockRegistry &registry);
	// RW 0x5E9BEF
	static int adjustFontSize(int size, int xResolution);
	static const FieldParse *fieldParseTable();

	std::string decimalSeparator, thousandSeparator, timeMinuteToSecondSeparator, unicodeFontName, audioLanguage;
	std::list<std::string> localFonts; // LocalFontFile, newest first (push_front)
	int militaryCaptionSpeed = 0;
	bool useHardWordWrap = false;
	float resolutionFontAdjustment = 0.7f;
	FontDesc copyrightFont, messageFont, militaryCaptionTitleFont, militaryCaptionFont, audioSubtitleFont, superweaponCountdownNormalFont,
		superweaponCountdownReadyFont, namedTimerCountdownNormalFont, namedTimerCountdownReadyFont, drawableCaptionFont, defaultWindowFont,
		defaultDisplayStringFont, tooltipFontName, nativeDebugDisplay, drawGroupInfoFont, creditsTitleFont, creditsMinorTitleFont, creditsNormalFont,
		helpBoxNameFont, helpBoxCostFont, helpBoxShortcutFont, helpBoxDescriptionFont;
	bool loaded = false; // a "Language" block was read

	static void parseFontDesc(INI *ini, void *instance, void *store, const void *userData);    // RW 0x778C93
	static void parseLocalFontFile(INI *ini, void *instance, void *store, const void *userData); // RW 0x5EA54B
};
