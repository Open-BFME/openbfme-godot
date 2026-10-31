// OpenBFME tests. GPL-3.0. Lane L10N-1: the OpenBFME text catalog's engine loader on the generated catalog and synthetic patterns.

#include <ostream>
#include "doctest.h"

#include "GameClient/OpenBFMEText.h"

namespace
{
std::u16string fmt(std::u16string_view pattern, std::vector<OpenBFMEText::Argument> args, std::string_view language = "en",
	std::vector<std::string> *errors = nullptr)
{
	std::vector<std::string> local;
	std::u16string out = OpenBFMEText::format(pattern, args, language, "test", errors ? errors : &local);
	if (!errors)
	{
		CHECK(local.empty());
	}
	return out;
}
} // namespace

TEST_CASE("openbfme text: the generated catalog, English first, a translation falls back to English per id")
{
	const auto &langs = OpenBFMEText::languages();
	REQUIRE(!langs.empty());
	CHECK(langs.front().code == "en");
	CHECK(langs.front().status == "source");
	CHECK(OpenBFMEText::hasLanguage("de"));
	CHECK(OpenBFMEText::has("common.cancel"));
	CHECK(!OpenBFMEText::has("launcher.settings.language")); // the launcher's ids are not compiled into the engine
	std::vector<std::string> errors;
	CHECK(OpenBFMEText::text("en", "common.cancel", {}, &errors) == u"Cancel");
	CHECK(OpenBFMEText::text("de", "common.close", {}, &errors) == u"Schließen");
	CHECK(OpenBFMEText::text("xx", "common.cancel", {}, &errors) == u"Cancel");
	CHECK(errors.empty());
}

TEST_CASE("openbfme text: named typed placeholders, read once")
{
	using A = OpenBFMEText::Argument;
	CHECK(fmt(u"{a} lists {b} twice.", { A("a", u"X"), A("b", u"Y") }) == u"X lists Y twice.");
	CHECK(fmt(u"{b} before {a}", { A("a", 1), A("b", 2) }) == u"2 before 1");
	CHECK(fmt(u"{n:d} files", { A("n", 3) }) == u"3 files");
	CHECK(fmt(u"{n:d}", { A("n", 3.9) }) == u"3");
	CHECK(fmt(u"{x:.1f} GB", { A("x", 1.25) }) == u"1.2 GB");
	CHECK(fmt(u"{x:.2f} GB", { A("x", 1.5) }, "de") == u"1,50 GB");
	CHECK(fmt(u"{x:.0f}%", { A("x", 99.6) }) == u"100%");
	CHECK(fmt(u"{{literal}} {a}", { A("a", u"{b}") }) == u"{literal} {b}");
	CHECK(fmt(u"{a}", { A("a", u"{a}{a}") }) == u"{a}{a}");
	CHECK(fmt(u"100% {a}", { A("a", u"%s") }) == u"100% %s");
}

TEST_CASE("openbfme text: misuse is reported and stays visible")
{
	using A = OpenBFMEText::Argument;
	std::vector<std::string> errors;
	CHECK(fmt(u"a {name} b", {}, "en", &errors) == u"a {name} b");
	CHECK(fmt(u"{n:d} files", { A("n", u"many") }, "en", &errors) == u"many files");
	CHECK(OpenBFMEText::text("en", "common.no_such_id", {}, &errors) == u"common.no_such_id");
	REQUIRE(errors.size() == 3);
	CHECK(errors[0] == "test needs the argument 'name'");
	CHECK(errors[1] == "test: the argument 'n' is a number ({n:d})");
	CHECK(errors[2] == "unknown text id 'common.no_such_id'");
}

TEST_CASE("openbfme text: CLDR plural categories match the launcher's")
{
	struct Case
	{
		const char *language;
		long long n;
		const char *category;
	};
	for (const Case &c : { Case{ "en", 0, "other" }, Case{ "en", 1, "one" }, Case{ "de", 5, "other" }, Case{ "fr", 0, "one" },
			 Case{ "fr", 2, "other" }, Case{ "pl", 3, "few" }, Case{ "pl", 5, "many" }, Case{ "pl", 22, "few" }, Case{ "pl", 12, "many" },
			 Case{ "ru", 21, "one" }, Case{ "ru", 11, "many" }, Case{ "ru", 23, "few" }, Case{ "cs", 3, "few" }, Case{ "cs", 5, "other" },
			 Case{ "zh", 1, "other" } })
	{
		CAPTURE(c.language);
		CAPTURE(c.n);
		CHECK(OpenBFMEText::pluralCategory(c.language, c.n) == c.category);
	}
}
