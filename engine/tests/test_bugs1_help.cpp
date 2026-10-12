// OpenBFME diagnostics for lane BUGS-1: the help box of Palantir.apt (frames, labels and actions of its clips), for the empty help box over a barracks bubble.
#include "doctest.h"
#include "HudTestUtil.h"

#include "Libraries/Source/Apt/AptActionDecoder.h"
#include "Libraries/Source/Apt/AptFile.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <set>
#include <string>

using namespace hudtest;

namespace
{
std::vector<std::uint8_t> readRetail(SharedWorld &s, const std::string &name)
{
	std::vector<std::uint8_t> bytes;
	std::string err;
	REQUIRE_MESSAGE(s.mount->fs->readFile(name, bytes, &err), err);
	return bytes;
}

void dumpCode(const AptFile &f, std::uint32_t offset, const std::string &indent)
{
	std::string err;
	std::shared_ptr<const AptCodeBlock> code = f.codeAt(offset, &err);
	if (!code)
	{
		std::printf("%s<decode error %s>\n", indent.c_str(), err.c_str());
		return;
	}
	std::function<void(const AptCodeBlock &, const std::string &)> walk = [&](const AptCodeBlock &b, const std::string &in) {
		for (size_t i = 0; i < b.instructions.size(); ++i)
		{
			const AptInstruction &ins = b.instructions[i];
			std::string ops;
			for (const AptConstRef &c : ins.constants)
			{
				ops += " [" + (c.text.empty() ? std::to_string(c.type) + ":" + std::to_string(c.raw) : "'" + c.text + "'") + "]";
			}
			std::printf("%s%zu %s int=%d f=%g '%s'%s%s\n", in.c_str(), i, AptActionDecoder::opcodeName(ins.opcode), ins.intOperand, ins.floatOperand, ins.text.c_str(),
				ops.c_str(), ins.branchTarget >= 0 ? (" -> " + std::to_string(ins.branchTarget)).c_str() : "");
			if (ins.function && ins.function->body)
			{
				std::printf("%s  function %s(", in.c_str(), ins.function->name.c_str());
				for (const AptFunctionParam &p : ins.function->params)
				{
					std::printf("%s r%d, ", p.name.c_str(), p.reg);
				}
				std::printf(")\n");
				walk(*ins.function->body, in + "    ");
			}
		}
	};
	walk(*code, indent);
}

void dumpCharacter(const AptFile &f, std::uint32_t id, const std::string &name, std::set<std::uint32_t> &seen, int depth)
{
	if (id >= f.characters.size() || !seen.insert(id).second || depth > 3)
	{
		return;
	}
	const AptCharacter &c = f.characters[id];
	std::printf("== character %u '%s' type %u frames %zu\n", id, name.c_str(), c.type, c.frames.size());
	for (size_t fr = 0; fr < c.frames.size(); ++fr)
	{
		for (const AptFrameItem &it : c.frames[fr].items)
		{
			if (it.type == APT_ITEM_FRAMELABEL)
			{
				std::printf("  f%zu label '%s'\n", fr, it.label.c_str());
			}
			else if (it.type == APT_ITEM_ACTION)
			{
				std::printf("  f%zu action\n", fr);
				dumpCode(f, it.codeOffset, "    ");
			}
			else if (it.type == APT_ITEM_PLACEOBJECT && it.place)
			{
				std::printf("  f%zu place depth %d char %d name '%s' flags %x ty %g\n", fr, it.place->depth, it.place->characterId, it.place->name.c_str(), it.place->flags,
					it.place->translation[1]);
				for (const AptClipEvent &e : it.place->clipEvents)
				{
					std::printf("    clip event %x\n", e.mask);
					dumpCode(f, e.codeOffset, "      ");
				}
			}
			else if (it.type == APT_ITEM_REMOVEOBJECT)
			{
				std::printf("  f%zu remove depth %d\n", fr, it.removeDepth);
			}
		}
	}
	for (const AptFrame &fr : c.frames)
	{
		for (const AptFrameItem &it : fr.items)
		{
			if (it.type == APT_ITEM_PLACEOBJECT && it.place && (it.place->flags & APT_PLACE_HASCHARACTER))
			{
				dumpCharacter(f, (std::uint32_t)it.place->characterId, it.place->name, seen, depth + 1);
			}
		}
	}
}
} // namespace

TEST_CASE("bugs1 diagnostic: Palantir.apt's helpBox clips and actions" * doctest::skip())
{
	if (!haveWorld("bugs1 help diagnostic"))
	{
		return;
	}
	SharedWorld &s = shared();
	auto apt = std::make_shared<std::vector<std::uint8_t>>(readRetail(s, "InGameHelpBox.apt"));
	AptConstFile cf;
	std::string err;
	REQUIRE_MESSAGE(AptConstFile::parse(readRetail(s, "InGameHelpBox.const"), cf, &err), err);
	AptFile f;
	REQUIRE_MESSAGE(AptFile::parse("InGameHelpBox", apt, cf, f, &err), err);
	std::set<std::uint32_t> seen;
	for (const AptExport &e : f.exports)
	{
		std::printf("export '%s' -> %u\n", e.name.c_str(), e.characterId);
	}
	for (std::uint32_t id = 0; id < f.characters.size(); ++id)
	{
		for (const AptFrame &fr : f.characters[id].frames)
		{
			for (const AptFrameItem &it : fr.items)
			{
				if (it.type == APT_ITEM_PLACEOBJECT && it.place && (it.place->name == "sliver1" || it.place->name == "box"))
				{
					dumpCharacter(f, id, "holder of " + it.place->name, seen, 0);
				}
			}
		}
	}
	// the root timeline's actions that name the help box
	for (size_t fr = 0; fr < f.frames.size(); ++fr)
	{
		for (const AptFrameItem &it : f.frames[fr].items)
		{
			if (it.type == APT_ITEM_ACTION || it.type == APT_ITEM_INITACTION)
			{
				std::printf("root f%zu action %u\n", fr, it.type);
				dumpCode(f, it.codeOffset, "    ");
			}
		}
	}
	for (const AptFrame &fr : f.frames)
	{
		for (const AptFrameItem &it : fr.items)
		{
			if (it.type == APT_ITEM_PLACEOBJECT && it.place && it.place->name == "helpBox")
			{
				std::printf("root place helpBox depth %d char %d\n", it.place->depth, it.place->characterId);
				for (const AptClipEvent &e : it.place->clipEvents)
				{
					std::printf("  clip event %x\n", e.mask);
					dumpCode(f, e.codeOffset, "    ");
				}
				dumpCharacter(f, (std::uint32_t)it.place->characterId, "helpBox", seen, 0);
			}
		}
	}
}
