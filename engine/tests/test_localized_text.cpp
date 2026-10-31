// OpenBFME tests. GPL-3.0. Synthetic localized templates; no game files.

#include "doctest.h"

#include "GameClient/LocalizedText.h"
#include "GameClient/DrawableIconUI.h"
#include "GameClient/EndGame.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptSimpleScreens.h"
#include "GameClient/UnitHelpSource.h"

#include <limits>

TEST_CASE("localized text: typed arguments preserve the retail conversions without interpreting argument text")
{
	CHECK(LocalizedText::format("Rank %d", { 7 }) == "Rank 7");
	CHECK(LocalizedText::format("%d%%", { 75 }) == "75%");
	CHECK(LocalizedText::format("%s (%d)", { "Hero", 7 }) == "Hero (7)");
	CHECK(LocalizedText::format("%i %u", { -7, std::numeric_limits<unsigned>::max() }) == "-7 4294967295");
	CHECK(LocalizedText::format("%s %ls %S", { "one", "two", "three" }) == "one two three");
	CHECK(LocalizedText::format(u"%s %ls %S", { u"one", u"\u00e9", u"\U0001f40e" }) == u"one \u00e9 \U0001f40e");
	CHECK(LocalizedText::format("%s", { "%n%s" }) == "%n%s");
	CHECK(LocalizedText::format("%s/%d/%u/%ls", { 7, "text", -1, 8u }) == "7/text/-1/8");
	CHECK(LocalizedText::format("%s (%d)", { "Hero" }) == "Hero ()");
	CHECK(LocalizedText::format("%s", { "Hero", 7 }) == "Hero");
	CHECK(LocalizedText::format("%d%d", {}) == "");
	CHECK(LocalizedText::format("", {}) == "");
	CHECK(LocalizedText::format("%s", { static_cast<const char *>(nullptr) }) == "");
	CHECK(LocalizedText::format(u"%s", { static_cast<const char16_t *>(nullptr) }) == u"");
	CHECK(LocalizedText::format("%d", { std::numeric_limits<int>::min() }) == "-2147483648");
	CHECK(LocalizedText::format("%% %", {}) == "% %");
	CHECK(LocalizedText::format("%02d %02d %02d", { 1, 12, -1 }) == "01 12 -1");
	CHECK(LocalizedText::format(u"%02d %02d %02d", { 1, 12, -1 }) == u"01 12 -1");
	CHECK(LocalizedText::format(u"%s %% %", { u"\u00e9%02d" }) == u"\u00e9%02d % %");
	CHECK(LocalizedText::format("Building: %.0f%%", { 37.8 }) == "Building: 38%");
	CHECK(LocalizedText::format("%d%%", { 37.8 }) == "38%");
	const std::string longText(1024, 'x');
	CHECK(LocalizedText::format("%s", { longText }) == longText);
}

TEST_CASE("localized text: unsupported conversions are literal and never consume a typed argument")
{
	for (const char *pattern : { "%n", "%x", "%p", "%999999d", "%", "%*d", "%lld", "%1", "%9", "%1$s", "%.999999f" })
	{
		INFO(pattern);
		CHECK(LocalizedText::format(pattern, { 7 }) == pattern);
		const std::string input(pattern);
		const std::u16string wide(input.begin(), input.end());
		CHECK(LocalizedText::format(wide, { u"text" }) == wide);
	}
	CHECK(LocalizedText::format("%n %x %s %p %d", { "Hero", 7 }) == "%n %x Hero %p 7");
}

namespace
{
struct TemplateCase
{
	const char *pattern;
	const char *rank;
	const char16_t *version;
};
const TemplateCase cases[] = {
	{ "%s", "7", u"2" },
	{ "%n", "%n", u"%n" },
	{ "%x", "%x", u"%x" },
	{ "%p", "%p", u"%p" },
	{ "%999999d", "%999999d", u"%999999d" },
	{ "%", "%", u"%" },
	{ "%s/%ls/%S", "7//", u"2/1/" },
	{ "%d/%d/%d", "7//", u"2/1/" },
	{ "plain", "plain", u"plain" }
};
}

TEST_CASE("localized text: synthetic CSF APT:RankLabel is formatted by the Palantir rank path")
{
	for (const TemplateCase &c : cases)
	{
		INFO(c.pattern);
		std::vector<std::uint8_t> bytes;
		auto word = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back((std::uint8_t)(v >> (8 * i))); };
		auto raw = [&](const std::string &s) { bytes.insert(bytes.end(), s.begin(), s.end()); };
		raw(" FSC"); word(3); word(1); word(1); word(0); word(0);
		raw(" LBL"); word(1); word(13); raw("APT:RankLabel");
		const std::string pattern(c.pattern);
		raw(" RTS"); word((std::uint32_t)pattern.size());
		for (unsigned char ch : pattern)
		{
			const auto encoded = (std::uint16_t)~(std::uint16_t)ch;
			bytes.push_back(encoded & 255);
			bytes.push_back(encoded >> 8);
		}
		GameTextTable table;
		std::string error;
		REQUIRE_MESSAGE(table.parseCSF(bytes.data(), bytes.size(), &error), error);
		AptPalantir::LocalState state;
		state.rank = 7;
		REQUIRE(table.lookup("APT:RankLabel", state.rankLabelFormat));
		CHECK(AptPalantir::rankText(state) == c.rank);
	}
	AptPalantir::LocalState state;
	state.rank = 7;
	state.rankLabelFormat = "Rank %d";
	CHECK(AptPalantir::rankText(state) == "Rank 7");
}

TEST_CASE("localized text: construction and options version paths use typed table values")
{
	GameTextTableSource source;
	source.table.setOverride("CONTROLBAR:UnderConstructionDesc", "Building: %.0f%%");
	CHECK(DrawableIconUI::constructionText(&source, 37.8) == "Building: 38%");
	source.table.setOverride("Version:Format2", "Version %d.%02d");
	CHECK(AptOptionsScreen::versionText(&source) == u"Version 2.01");
	for (const TemplateCase &c : cases)
	{
		INFO(c.pattern);
		source.table.setOverride("CONTROLBAR:UnderConstructionDesc", c.pattern);
		source.table.setOverride("Version:Format2", c.pattern);
		CHECK(DrawableIconUI::constructionText(&source, 7.0) == c.rank);
		CHECK(AptOptionsScreen::versionText(&source) == c.version);
	}
}

TEST_CASE("localized text: help and score duration paths share the restricted conversions")
{
	CHECK(UnitHelpSource::format(u"%s (%d)", { u"Hero", u"7" }) == u"Hero (7)");
	CHECK(UnitHelpSource::format(u"%ls %S %n %x %p %999999d %", { u"Hero", u"7" }) == u"Hero 7 %n %x %p %999999d %");
	CHECK(UnitHelpSource::format(u"%s/%d/%s", { u"Hero", u"7" }) == u"Hero/7/");
	CHECK(ScoreScreenData::timeCell(2, [](const std::string &) { return "%d seconds"; }).text == "2 seconds");
	for (const TemplateCase &c : cases)
	{
		CHECK(ScoreScreenData::timeCell(7, [&](const std::string &) { return c.pattern; }).text == c.rank);
	}
}
