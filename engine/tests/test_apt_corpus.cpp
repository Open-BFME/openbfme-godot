// OpenBFME corpus tests: parse and decode every RotWK 2.01 Apt movie (spec step A0).
//
// Expected values come from tests/data/apt/apt_golden.json, generated once by an independent oracle
// (the archived Python importer decoders driven by the spec author's disasm.py logic over a pure-2.01
// extraction; see the lane report), never from this engine.  The tests print SKIP and pass when
// ROTWK_INSTALL / BFME2_INSTALL are unset.  GPL-3.0.

#include "doctest.h"
#include "AptTestUtil.h"
#include "AptRetail.h"

#include "Common/MiniJson.h"
#include "Libraries/Source/Apt/AptActionDecoder.h"
#include "Libraries/Source/Apt/AptLoad.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace
{

const JsonValue &golden()
{
	static JsonValue root;
	static bool loaded = false;
	if (!loaded)
	{
		loaded = true;
		std::string path = std::string(OPENBFME_APT_TEST_DATA_DIR) + "/apt_golden.json";
		std::ifstream in(path, std::ios::binary);
		REQUIRE_MESSAGE(in.good(), "cannot open " << path);
		std::stringstream ss;
		ss << in.rdbuf();
		std::string error;
		REQUIRE_MESSAGE(JsonValue::parse(ss.str(), root, &error), error);
	}
	return root;
}

double num(const JsonValue *v, double dflt = 0)
{
	return v && v->isNumber() ? v->number : dflt;
}

std::string lower(std::string s)
{
	for (char &c : s)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	return s;
}

struct Corpus
{
	AptArchiveFileSource source;
	AptLoader loader;
	std::vector<std::string> names; // lower-case, sorted
	explicit Corpus(ArchiveFileSystem &fs) : source(fs), loader(source)
	{
		names = source.listMovies();
		for (std::string &n : names)
		{
			n = lower(n);
		}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
	}
};

Corpus &corpus(AptRetail &mount)
{
	static std::unique_ptr<Corpus> c;
	if (!c)
	{
		c = std::make_unique<Corpus>(mount.fs);
	}
	return *c;
}

std::map<std::string, std::uint32_t> kindIds()
{
	return { { "null", APT_CHAR_NULL }, { "shape", APT_CHAR_SHAPE }, { "text", APT_CHAR_EDITTEXT }, { "font", APT_CHAR_FONT },
		{ "button", APT_CHAR_BUTTON }, { "sprite", APT_CHAR_SPRITE }, { "sound", APT_CHAR_SOUND }, { "image", APT_CHAR_IMAGE },
		{ "morph", APT_CHAR_MORPH }, { "movie", APT_CHAR_MOVIE }, { "static-text", APT_CHAR_STATICTEXT }, { "none", APT_CHAR_NONE },
		{ "video", APT_CHAR_VIDEO } };
}

} // namespace

TEST_CASE("corpus: the pure 2.01 mount provides exactly the 86 golden movies")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	Corpus &c = corpus(mount);
	const JsonValue *movies = golden().get("movies");
	REQUIRE(movies);
	std::vector<std::string> want;
	for (const auto &kv : movies->object)
	{
		want.push_back(kv.first);
	}
	CHECK(num(golden().get("movieCount")) == 86);
	CHECK(c.names.size() == 86);
	CHECK(c.names == want);
}

TEST_CASE("corpus: per-movie header, character, frame, import and constant counts match the golden")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	Corpus &c = corpus(mount);
	const JsonValue *movies = golden().get("movies");
	REQUIRE(movies);
	std::map<std::string, std::uint32_t> ids = kindIds();
	std::map<std::uint32_t, std::string> idName;
	for (const auto &kv : ids)
	{
		idName[kv.second] = kv.first;
	}
	std::size_t opaque = 0;
	std::map<std::string, std::size_t> opaqueKinds;
	std::uint32_t v6 = 0, v7 = 0;
	for (const std::string &name : c.names)
	{
		INFO("movie " << name);
		std::string err;
		std::shared_ptr<const AptFile> f = c.loader.loadMovie(name, &err);
		REQUIRE_MESSAGE(f, err);
		const JsonValue *g = movies->get(name);
		REQUIRE(g);
		CHECK(f->version == (std::uint32_t)num(g->get("aptVersion")));
		(f->version == 6 ? v6 : v7)++;
		CHECK(f->width == (std::uint32_t)num(g->get("width")));
		CHECK(f->height == (std::uint32_t)num(g->get("height")));
		CHECK(f->msPerFrame == (std::uint32_t)num(g->get("msPerFrame")));
		CHECK(f->frames.size() == (std::size_t)num(g->get("frameCount")));
		CHECK(f->characters.size() == (std::size_t)num(g->get("characterCount")));
		CHECK(f->imports.size() == (std::size_t)num(g->get("importCount")));
		CHECK(f->exports.size() == (std::size_t)num(g->get("exportCount")));
		CHECK(f->consts.entries.size() == (std::size_t)num(g->get("constCount")));

		std::map<std::string, std::size_t> kinds;
		std::size_t nullSlots = 0;
		for (const AptCharacter &ch : f->characters)
		{
			kinds[idName.at(ch.type)]++;
			nullSlots += ch.type == APT_CHAR_NULL;
			if (ch.opaque)
			{
				++opaque;
				opaqueKinds[idName.at(ch.type)]++;
			}
		}
		CHECK(nullSlots == (std::size_t)num(g->get("nullCharacterSlots")));
		const JsonValue *gk = g->get("characterKinds");
		REQUIRE(gk);
		for (const auto &kv : gk->object)
		{
			CHECK_MESSAGE(kinds[kv.first] == (std::size_t)kv.second.number, "character kind " << kv.first);
		}
		std::size_t total = 0;
		for (const auto &kv : kinds)
		{
			total += kv.second;
		}
		CHECK(total == f->characters.size());

		std::map<std::uint32_t, std::size_t> consts;
		for (const AptConstEntry &e : f->consts.entries)
		{
			consts[e.type]++;
		}
		const JsonValue *gc = g->get("constTypes");
		REQUIRE(gc);
		for (const auto &kv : gc->object)
		{
			CHECK(consts[(std::uint32_t)std::stoul(kv.first)] == (std::size_t)kv.second.number);
		}
		CHECK(consts.size() == gc->object.size());
	}
	CHECK(v6 + v7 == 86);
	CHECK(v6 == 43); // golden: 43 movies at Apt version 6, 43 at version 7
	CHECK(v7 == 43);
	// The only characters without a decoded layout in the corpus: 3 morphs and 25 static texts.
	CHECK(opaqueKinds["morph"] == 3);
	CHECK(opaqueKinds["static-text"] == 25);
	CHECK(opaque == 28);
}

TEST_CASE("corpus: frame items, clip events, buttons, fonts and text fields match the golden")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	Corpus &c = corpus(mount);
	const JsonValue *movies = golden().get("movies");
	std::size_t flaggedNull = 0;
	std::size_t peakItems = 0, peakFrames = 0, peakEvents = 0, peakButtons = 0, peakGlyphs = 0;
	for (const std::string &name : c.names)
	{
		INFO("movie " << name);
		std::string err;
		std::shared_ptr<const AptFile> f = c.loader.loadMovie(name, &err);
		REQUIRE_MESSAGE(f, err);
		const JsonValue *tl = movies->get(name)->get("timeline");
		REQUIRE(tl);
		std::map<std::string, std::size_t> st;
		auto walk = [&](const std::vector<AptFrame> &frames) {
			for (const AptFrame &fr : frames)
			{
				for (const AptFrameItem &it : fr.items)
				{
					switch (it.type)
					{
						case APT_ITEM_ACTION: st["item:action-script"]++; break;
						case APT_ITEM_INITACTION: st["item:init-action-script"]++; break;
						case APT_ITEM_FRAMELABEL: st["item:frame-label"]++; break;
						case APT_ITEM_REMOVEOBJECT: st["item:remove-object"]++; break;
						case APT_ITEM_BACKGROUNDCOLOR: st["item:background-color"]++; break;
						case APT_ITEM_PLACEOBJECT:
							st["item:place-object"]++;
							if (it.place->clipActionsFlagged)
							{
								if (it.place->clipActionsNull)
								{
									st["flaggedNullClipActions"]++;
								}
								else
								{
									st["placeWithClipActions"]++;
									st["clipEvents"] += it.place->clipEvents.size();
								}
							}
							break;
					}
				}
			}
		};
		walk(f->frames);
		for (const AptCharacter &ch : f->characters)
		{
			if (ch.type == APT_CHAR_SPRITE)
			{
				st["spriteFrames"] += ch.frames.size();
				walk(ch.frames);
			}
			else if (ch.type == APT_CHAR_BUTTON)
			{
				st["buttonRecords"] += ch.button->records.size();
				st["buttonActions"] += ch.button->actions.size();
				st["buttonVertices"] += ch.button->vertices.size() / 2;
				st["buttonTriangles"] += ch.button->triangles.size() / 3;
			}
			else if (ch.type == APT_CHAR_FONT)
			{
				st["fontGlyphs"] += ch.glyphs.size();
			}
			else if (ch.type == APT_CHAR_EDITTEXT)
			{
				st["textFields"]++;
			}
		}
		for (const char *key : { "item:action-script", "item:init-action-script", "item:frame-label", "item:remove-object", "item:background-color",
				 "item:place-object", "flaggedNullClipActions", "placeWithClipActions", "clipEvents", "spriteFrames", "buttonRecords", "buttonActions",
				 "buttonVertices", "buttonTriangles", "fontGlyphs", "textFields" })
		{
			CHECK_MESSAGE(st[key] == (std::size_t)num(tl->get(key)), key);
		}
		flaggedNull += st["flaggedNullClipActions"];
		std::size_t items = st["item:action-script"] + st["item:init-action-script"] + st["item:frame-label"] + st["item:remove-object"] +
			st["item:background-color"] + st["item:place-object"];
		peakItems = std::max(peakItems, items);
		peakFrames = std::max(peakFrames, f->frames.size() + st["spriteFrames"]);
		peakEvents = std::max(peakEvents, st["clipEvents"]);
		peakGlyphs = std::max(peakGlyphs, st["fontGlyphs"]);
		peakButtons = std::max(peakButtons, st["buttonVertices"] * 2 + st["buttonTriangles"] * 3 + st["buttonRecords"] + st["buttonActions"]);
	}
	CHECK(flaggedNull == 29); // golden total of PlaceObject records with flag 0x80 and a null pointer
	// The parser's aggregate budgets (AptFile.cpp: 2^18 frames, 2^20 frame items, 2^20 clip events, 2^20 button
	// elements per file) must leave the real corpus at least a 4x margin.
	MESSAGE("retail peaks per file: frames " << peakFrames << ", frame items " << peakItems << ", clip events " << peakEvents << ", button elements " << peakButtons << ", font glyphs " << peakGlyphs);
	CHECK(peakFrames * 4 < (std::size_t(1) << 18));
	CHECK(peakItems * 4 < (std::size_t(1) << 20));
	CHECK(peakEvents * 4 < (std::size_t(1) << 20));
	CHECK(peakButtons * 4 < (std::size_t(1) << 20));
	CHECK(peakGlyphs * 4 < (std::size_t(1) << 20)); // the glyph budget in AptFile.cpp is 2^20 per file
}

TEST_CASE("corpus: every program decodes; instruction and opcode counts match; the opcode set is the 86-opcode census")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	Corpus &c = corpus(mount);
	const JsonValue *movies = golden().get("movies");
	std::set<std::uint8_t> seen;
	std::size_t programs = 0;
	std::size_t instructions = 0;
	for (const std::string &name : c.names)
	{
		INFO("movie " << name);
		std::string err;
		std::shared_ptr<const AptFile> f = c.loader.loadMovie(name, &err);
		REQUIRE_MESSAGE(f, err);
		const JsonValue *g = movies->get(name);
		std::vector<std::uint32_t> offs = f->programOffsets();
		CHECK(offs.size() == (std::size_t)num(g->get("programCount")));
		std::uint32_t counts[256] = {};
		std::size_t flat = 0;
		for (std::uint32_t off : offs)
		{
			std::shared_ptr<const AptCodeBlock> b = f->codeAt(off, &err);
			REQUIRE_MESSAGE(b, "program at " << off << ": " << err);
			flat += b->flatInstructionCount();
			b->countOpcodes(counts);
		}
		CHECK(flat == (std::size_t)num(g->get("instructionCount")));
		const JsonValue *go = g->get("opcodes");
		REQUIRE(go);
		std::size_t distinct = 0;
		for (int op = 0; op < 256; ++op)
		{
			if (counts[op])
			{
				++distinct;
				seen.insert((std::uint8_t)op);
				const JsonValue *e = go->get(std::to_string(op));
				CHECK_MESSAGE(e, "opcode " << op << " not in golden");
				if (e)
				{
					CHECK_MESSAGE(counts[op] == (std::uint32_t)e->number, "opcode " << op);
				}
			}
		}
		CHECK(distinct == go->object.size());
		programs += offs.size();
		instructions += flat;
	}
	CHECK(programs == 4767);
	CHECK(programs == (std::size_t)num(golden().get("programCount")));
	CHECK(instructions == (std::size_t)num(golden().get("instructionCount")));
	std::vector<std::uint8_t> got(seen.begin(), seen.end());
	CHECK(got == AptActionDecoder::supportedOpcodes());
	CHECK(got.size() == 86);
}

TEST_CASE("corpus: GuiTest init actions decode; the old importer's 0x1A came from reading the sprite id as a code pointer")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	Corpus &c = corpus(mount);
	std::string err;
	std::shared_ptr<const AptFile> f = c.loader.loadMovie("guitest", &err);
	REQUIRE_MESSAGE(f, err);
	// The corpus has exactly two InitAction records: GuiTest root frame 0 and OnlineHome root frame 14.
	auto initsOf = [&](const AptFile &file) {
		std::vector<const AptFrameItem *> inits;
		auto collect = [&](const std::vector<AptFrame> &frames) {
			for (const AptFrame &fr : frames)
			{
				for (const AptFrameItem &it : fr.items)
				{
					if (it.type == APT_ITEM_INITACTION)
					{
						inits.push_back(&it);
					}
				}
			}
		};
		collect(file.frames);
		for (const AptCharacter &ch : file.characters)
		{
			collect(ch.frames);
		}
		return inits;
	};
	std::vector<const AptFrameItem *> inits = initsOf(*f);
	REQUIRE(inits.size() == 1);
	CHECK(inits[0]->spriteId == 8);      // the u32 at +4: a sprite character id
	CHECK(inits[0]->codeOffset == 1020); // the real code pointer at +8 (the old importer decoded offset 8)
	CHECK_MESSAGE(f->codeAt(inits[0]->codeOffset, &err), err);
	std::shared_ptr<const AptFile> home = c.loader.loadMovie("onlinehome", &err);
	REQUIRE_MESSAGE(home, err);
	std::vector<const AptFrameItem *> homeInits = initsOf(*home);
	REQUIRE(homeInits.size() == 1);
	CHECK(homeInits[0]->spriteId == 21);
	CHECK(homeInits[0]->codeOffset == 10940);
	CHECK_MESSAGE(home->codeAt(homeInits[0]->codeOffset, &err), err);
	// Decoding at offset 8 (the sprite id used as a pointer) reads bytes 3A 37 1A ...: Delete is fine,
	// 0x37 (MBAsciiToChar, not in the census) is rejected first; starting at offset 10 the 0x1A the
	// spec recorded for GuiTest is rejected.  Both are loud errors naming opcode and offset.
	std::shared_ptr<const AptCodeBlock> bogus;
	CHECK_FALSE(AptActionDecoder::decodeProgram(*f, 8, bogus, &err));
	CHECK(err.find("opcode 0x37 at offset 9 is not supported") != std::string::npos);
	CHECK_FALSE(AptActionDecoder::decodeProgram(*f, 10, bogus, &err));
	CHECK(err.find("opcode 0x1a at offset 10 is not supported") != std::string::npos);
}

TEST_CASE("corpus: every import resolves; MainMenu imports 18 from MenuExport and BinkMovie/View3D from GameWindowGadgets")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	Corpus &c = corpus(mount);
	const JsonValue *movies = golden().get("movies");
	CHECK(num(golden().get("unresolvedImportTotal")) == 0);
	std::size_t total = 0;
	for (const std::string &name : c.names)
	{
		INFO("movie " << name);
		std::string err;
		std::shared_ptr<const AptFile> f = c.loader.loadMovie(name, &err);
		REQUIRE_MESSAGE(f, err);
		std::vector<AptResolvedImport> resolved;
		REQUIRE_MESSAGE(c.loader.resolveImports(*f, resolved, &err), err);
		CHECK(resolved.size() == f->imports.size());
		total += resolved.size();
		for (std::size_t i = 0; i < resolved.size(); ++i)
		{
			// each import fills a null slot of the importer and names a real, non-null character
			CHECK(f->characters[resolved[i].localSlot].type == APT_CHAR_NULL);
			CHECK(resolved[i].movie->characters[resolved[i].characterId].type != APT_CHAR_NULL);
		}
		const JsonValue *gi = movies->get(name)->get("imports");
		REQUIRE(gi);
		REQUIRE(gi->array.size() == f->imports.size());
		std::multiset<std::string> have, want;
		for (const AptImport &imp : f->imports)
		{
			have.insert(imp.movie + "::" + imp.name + "@" + std::to_string(imp.characterId));
		}
		for (const JsonValue &e : gi->array)
		{
			want.insert(e.array[0].string + "::" + e.array[1].string + "@" + std::to_string((std::uint32_t)e.array[2].number));
		}
		CHECK(have == want);
	}
	CHECK(total == 582);

	std::string err;
	std::shared_ptr<const AptFile> mm = c.loader.loadMovie("MainMenu", &err);
	REQUIRE_MESSAGE(mm, err);
	std::map<std::string, int> byMovie;
	std::set<std::string> gadgets;
	for (const AptImport &imp : mm->imports)
	{
		byMovie[lower(imp.movie)]++;
		if (lower(imp.movie) == "gamewindowgadgets")
		{
			gadgets.insert(imp.name);
		}
	}
	CHECK(mm->imports.size() == 20);
	CHECK(byMovie["menuexport"] == 18);
	CHECK(byMovie["gamewindowgadgets"] == 2);
	CHECK(gadgets == std::set<std::string>{ "BinkMovie", "View3D" });
	std::vector<AptResolvedImport> resolved;
	REQUIRE_MESSAGE(c.loader.resolveImports(*mm, resolved, &err), err);
	for (const AptResolvedImport &r : resolved)
	{
		if (lower(r.movie->name) == "menuexport")
		{
			CHECK(r.movie->exports.size() == 4574); // spec 1.1: MenuExport exports 4,574 symbols
		}
	}
}

TEST_CASE("corpus: image maps and shape geometry parse and match the golden record counts")
{
	OPENBFME_REQUIRE_RETAIL(mount);
	Corpus &c = corpus(mount);
	const JsonValue *movies = golden().get("movies");
	std::map<std::string, std::size_t> total;
	for (const std::string &name : c.names)
	{
		INFO("movie " << name);
		std::string err;
		std::shared_ptr<const AptFile> f = c.loader.loadMovie(name, &err);
		REQUIRE_MESSAGE(f, err);
		const JsonValue *g = movies->get(name);
		AptImageMap map;
		REQUIRE_MESSAGE(c.loader.loadImageMap(name, map, &err), err);
		std::size_t arrows = 0, rects = 0;
		for (const AptImageMapEntry &e : map.entries)
		{
			(e.isRect ? rects : arrows)++;
		}
		CHECK(arrows == (std::size_t)num(g->get("dat")->get("arrow")));
		CHECK(rects == (std::size_t)num(g->get("dat")->get("rect")));

		const JsonValue *gg = g->get("geometry");
		std::size_t files = 0, clears = 0, solid = 0, line = 0, textured = 0, tris = 0, lines = 0;
		std::set<std::uint32_t> done;
		for (const AptCharacter &ch : f->characters)
		{
			if (ch.type != APT_CHAR_SHAPE || !done.insert(ch.geometryId).second)
			{
				continue;
			}
			AptGeometry geo;
			REQUIRE_MESSAGE(c.loader.loadGeometry(name, ch.geometryId, geo, &err), err);
			++files;
			clears += geo.clearCount;
			for (const AptGeometryStyle &s : geo.styles)
			{
				(s.kind == APT_STYLE_SOLID ? solid : s.kind == APT_STYLE_LINE ? line : textured)++;
				tris += s.triangles.size() / 6;
				lines += s.lines.size() / 4;
			}
		}
		CHECK(files == (std::size_t)num(g->get("shapeCount")));
		CHECK(files == (std::size_t)num(gg->get("files")));
		CHECK(clears == (std::size_t)num(gg->get("c")));
		CHECK(solid == (std::size_t)num(gg->get("s_s")));
		CHECK(line == (std::size_t)num(gg->get("s_l")));
		CHECK(textured == (std::size_t)num(gg->get("s_tc")));
		CHECK(tris == (std::size_t)num(gg->get("t")));
		CHECK(lines == (std::size_t)num(gg->get("l")));
		total["files"] += files;
		total["tris"] += tris;
	}
	CHECK(total["files"] > 0);
	CHECK(total["tris"] > 0);
}
