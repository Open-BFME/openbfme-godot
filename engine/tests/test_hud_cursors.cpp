// OpenBFME retail tests of the mouse cursors (lane HUD-1): every `MouseCursor` of Mouse.ini that names a file in the install's Data\Cursors decodes. SKIP without ROTWK_INSTALL. GPL-3.0.

#include "doctest.h"

#include "GameClient/CursorFile.h"
#include "GameClient/InGameUI.h"
#include "HudTestUtil.h"

#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace hudtest;

namespace
{
bool readLoose(const std::string &dir, const std::string &stem, std::vector<std::uint8_t> &out, std::string &used)
{
	for (const char *ext : { "", ".ani", ".cur" }) // Mouse.ini writes some names with their extension (`SCCPointer.cur`)
	{
		const std::string path = FindFileNoCase(dir, stem + ext);
		if (path.empty())
		{
			continue;
		}
		std::ifstream f(path, std::ios::binary);
		out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		used = path;
		return true;
	}
	return false;
}
} // namespace

TEST_CASE("hud cursors: the cursor names the translators choose are Mouse.ini cursors, and their files decode (animations, hot spots)")
{
	const char *install = std::getenv("ROTWK_INSTALL");
	if (!install || !*install || !haveWorld("hud cursors"))
	{
		return;
	}
	SharedWorld &s = shared();
	std::vector<std::uint8_t> ini;
	std::string error;
	REQUIRE(s.mount->fs->readFile("data/ini/mouse.ini", ini, &error));
	std::map<std::string, MouseCursorEntry> cursors;
	REQUIRE_MESSAGE(ParseMouseCursors(std::string(ini.begin(), ini.end()), cursors, &error), error);
	CHECK(cursors.size() > 40);
	// the names the HUD picks (InGameUI.h MouseCursorName) are blocks of the table
	for (const char *n : { MouseCursorName::Arrow, MouseCursorName::Select, MouseCursorName::Move, MouseCursorName::AttackMove, MouseCursorName::AttackObj, MouseCursorName::ForceAttackObj,
		MouseCursorName::ForceAttackGround, MouseCursorName::GenericInvalid, MouseCursorName::EnterFriendly, MouseCursorName::EnterAggressive, MouseCursorName::SetRallyPoint, MouseCursorName::Scroll })
	{
		CHECK_MESSAGE(cursors.count(n) == 1, n);
	}
	size_t decoded = 0, animated = 0, missing = 0;
	std::string dir = std::string(install) + "/data/cursors";
	for (const auto &kv : cursors)
	{
		if (kv.second.image.empty())
		{
			continue;
		}
		std::vector<std::uint8_t> bytes;
		std::string used;
		if (!readLoose(dir, kv.second.image, bytes, used))
		{
			++missing; // a name of the table with no file in this install (reported, not hidden)
			continue;
		}
		CursorImage img;
		const bool ok = DecodeCursorFile(bytes, img, &error);
		REQUIRE_MESSAGE(ok, kv.first << " " << used << ": " << error);
		CHECK(img.width >= 16);
		CHECK(img.width <= 64);
		CHECK(img.height == img.width);
		CHECK(img.hotX >= 0);
		CHECK(img.hotX < img.width);
		CHECK(img.sequence.size() == img.jiffies.size());
		bool opaque = false;
		for (std::uint8_t a : std::vector<std::uint8_t>(img.frames.front().begin(), img.frames.front().end()))
		{
			(void)a;
		}
		for (size_t i = 3; i < img.frames.front().size(); i += 4)
		{
			opaque = opaque || img.frames.front()[i] != 0;
		}
		CHECK(opaque);
		animated += img.sequence.size() > 1;
		++decoded;
	}
	std::printf("cursors: %zu decoded (%zu animated), %zu table entries without a file in the install\n", decoded, animated, missing);
	CHECK(decoded > 30);
	// the cursors the HUD chooses all have their files
	for (const char *n : { MouseCursorName::Arrow, MouseCursorName::Select, MouseCursorName::Move, MouseCursorName::AttackMove, MouseCursorName::AttackObj, MouseCursorName::ForceAttackObj,
		MouseCursorName::ForceAttackGround, MouseCursorName::GenericInvalid, MouseCursorName::EnterFriendly, MouseCursorName::EnterAggressive, MouseCursorName::SetRallyPoint })
	{
		std::vector<std::uint8_t> bytes;
		std::string used;
		CHECK_MESSAGE(readLoose(dir, cursors.at(n).image, bytes, used), n << " -> " << cursors.at(n).image);
	}
	CHECK(missing == 1); // the table names cursors the install does not ship (the engine's own pointer shows instead): pinned, stop S-295
}
