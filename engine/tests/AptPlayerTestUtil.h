// OpenBFME unit tests. GPL-3.0.
//
// Fixtures for the Apt player tests (display list, timeline, input): synthetic movies are assembled with
// AptTestUtil.h, served from memory, and observed through the recording host (fscommands are the "trace").

#pragma once

#include "AptTestUtil.h"

#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The engine side of the retail WindowManager for the facts every movie needs at start-up, recorded (spec 3.3: "extern.InGame
// must be provided and truthy").  A test double: the real providers belong to the WindowManager lane (A4); retail names live
// here, not in engine code (docs/PLAN.md rule 6).
//
// Retail facts (RotWK game.dat 0x00813102..0x008131F9, handlers 0x0081273C / 0x00812787 / 0x008127A6; BFME2 carries the same
// names; RotWK findings carry the S-001 caveat): reads answer a string - InGame "1" always, InBetaDemo "1" when the global demo
// mode is 1, InDreamMachineDemo "1" when it is 2 (a retail game is in neither), DoTrace "1" when the debug flag at 0x00E0302C is
// set; a write is ignored (the handlers return unless their `setting` argument is 0).
class AptStubHost : public AptRecordingHost
{
public:
	AptStubHost()
	{
		setExternValue("InGame", "1");
		setExternValue("InBetaDemo", "0");
		setExternValue("InDreamMachineDemo", "0");
		setExternValue("DoTrace", "0");
	}
	std::vector<std::string> unknownExterns; // names a script read that no provider answered
	AptExternResult getExtern(const std::string &name, std::string &value) override
	{
		AptExternResult r = AptRecordingHost::getExtern(name, value);
		if (r == AptExternResult::NoProvider)
		{
			unknownExterns.push_back(name);
		}
		return r;
	}
	bool setExtern(const std::string &name, const std::string &value) override
	{
		if (name == "InGame" || name == "InBetaDemo" || name == "InDreamMachineDemo" || name == "DoTrace")
		{
			externSets.push_back({ name, value });
			return true; // recorded, value unchanged
		}
		return AptRecordingHost::setExtern(name, value);
	}
};

namespace apttest
{

// In-memory movie files (lower-cased names).
class MemorySource : public AptFileSource
{
public:
	std::map<std::string, std::vector<std::uint8_t>> files;
	static std::string lower(std::string s)
	{
		for (char &c : s)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		return s;
	}
	bool readFile(const std::string &p, std::vector<std::uint8_t> &out, std::string *error) override
	{
		auto it = files.find(lower(p));
		if (it == files.end())
		{
			if (error)
			{
				*error = "file not found: " + p;
			}
			return false;
		}
		out = it->second;
		return true;
	}
	bool fileExists(const std::string &p) override { return files.count(lower(p)) != 0; }
	std::vector<std::string> listMovies() override { return {}; }
	void add(const std::string &name, TestMovie &m)
	{
		std::vector<std::uint8_t> a, c;
		m.build(a, c);
		files[lower(name) + ".apt"] = a;
		files[lower(name) + ".const"] = c;
	}
};

// A program: `body` assembles into the next code slot, End is appended, the file offset is returned.
inline std::uint32_t program(TestMovie &m, const std::function<void(Asm &)> &body)
{
	Asm a = m.program();
	body(a);
	a.op(APT_OP_END);
	return m.commit(a);
}

// getURL("FSCommand:<name>", ""): the observation point of every ordering test.
inline void fscmd(Asm &a, const std::string &name)
{
	a.getURL("FSCommand:" + name, "");
}

// obj.method(args): arguments are pushed last first, then the count, the object and the name.
inline void callMethod(Asm &a, const std::string &objVar, const std::string &method, const std::vector<std::string> &stringArgs = {}, const std::vector<int> &intArgs = {})
{
	std::vector<std::function<void()>> pushes;
	for (const std::string &s : stringArgs)
	{
		pushes.push_back([&a, s] { a.pushString(s); });
	}
	for (int i : intArgs)
	{
		pushes.push_back([&a, i] { a.pushShort(i); });
	}
	for (std::size_t k = pushes.size(); k-- > 0;)
	{
		pushes[k]();
	}
	a.pushByte((int)pushes.size());
	a.getStringVar(objVar);
	a.pushString(method);
	a.op(APT_OP_EA_CALLMETHODPOP);
}

// A movie with its characters; place helpers.
inline TestMovie::Place placeChar(std::uint32_t id, int depth, const std::string &name = std::string())
{
	TestMovie::Place p;
	p.flags = APT_PLACE_HASCHARACTER;
	p.characterId = (std::int32_t)id;
	p.depth = depth;
	if (!name.empty())
	{
		p.flags |= APT_PLACE_HASNAME;
		p.name = name;
	}
	return p;
}

inline TestMovie::Place placeMove(int depth)
{
	TestMovie::Place p;
	p.flags = APT_PLACE_MOVE;
	p.characterId = -1;
	p.depth = depth;
	return p;
}

struct PlayerFx
{
	MemorySource source;
	AptStubHost host;
	std::unique_ptr<Apt> apt;

	PlayerFx() { apt = std::make_unique<Apt>(source, host); }

	bool load(int level, const std::string &name, TestMovie &m, std::string *err = nullptr)
	{
		source.add(name, m);
		std::string e;
		bool ok = apt->loadMovie(level, name, &e);
		if (err)
		{
			*err = e;
		}
		return ok;
	}

	// fscommand names in order since the last call
	std::vector<std::string> commands()
	{
		std::vector<std::string> out;
		for (const AptRecordingHost::Command &c : host.fscommands)
		{
			out.push_back(c.command);
		}
		host.fscommands.clear();
		return out;
	}
	std::string commandsText() // "a b c"
	{
		std::string s;
		for (const std::string &c : commands())
		{
			s += (s.empty() ? "" : " ") + c;
		}
		return s;
	}
	std::string errorsText() const
	{
		std::string s;
		for (const std::string &e : host.errors)
		{
			s += e + "; ";
		}
		return s;
	}
	AptSpriteInst *root(int level = 0) { return apt->level(level); }
	AptCharacterInst *at(const std::string &path, int level = 0) { return apt->resolvePath(root(level), path); }
	void step(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			apt->update(33);
		}
	}
};

} // namespace apttest
