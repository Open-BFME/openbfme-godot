// OpenBFME. GPL-3.0.
//
// The interface between the Apt interpreter and the engine around it.
//
// getURL / getURL2 with an "FSCommand:" prefix reach the engine as (command, argument) strings:
// BFME1 EA Apt DispatchLiteral008C5840.cpp:60-84 and Rva008D16A0StringDispatch.cpp:67-118 strip the
// prefix and call the host callback installed at 0x01337858, which is WindowManager::invokeCallback
// (spec menus-apt.md 3.3).  A ".swf" url is a loadMovie request, an empty url an unload.  The
// `extern` object exchanges its values as strings with the host (WindowManager provider map: the map
// is keyed by name and each provider is called with (name, value buffer, setting) - spec menus-apt.md,
// AptScreenFactories.cpp:751-756).  A name with no provider is not the same thing as a provider that
// answers "no value": the first is a script-visible error, the second reads as undefined.

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

class AptObject;
class AptCharacterInst;

enum class AptExternResult : std::uint8_t
{
	Value,      // a provider answered with a string
	Undefined,  // a provider exists and answered with no value: the read is undefined, not an error
	NoProvider, // nothing is registered under this name: the interpreter reports an error
	Number      // lane END-1: a provider declared numeric answered with a decimal integer: the read is that number (WindowManager::markNumericProvider)
};

class AptHost
{
public:
	virtual ~AptHost() = default;

	// The render list marks an instance of an exported symbol as a native component (gadget, View3D, BinkMovie ...) when this
	// returns true: the WindowManager's component-factory map is keyed by those symbol names (menus-apt.md 3.3).
	virtual bool isComponentSymbol(const std::string &movieName, const std::string &symbolName)
	{
		(void)movieName;
		(void)symbolName;
		return false;
	}
	// A clip instance of a symbol for which isComponentSymbol() answered true was created (it has run through the new-instance
	// flush, AptDisplayList::place -> 0x00AE4390 in BFME2): the WindowManager creates a native gadget for it and calls the screen's
	// InitGadgets (menus-apt.md 3.3 "Component factories" / "Gadget init").  That the retail creation point is the clip
	// instantiation is the spec's strong inference, not traced in code (stop S-171).
	virtual void componentInstanceCreated(AptCharacterInst &inst, const std::string &movieName, const std::string &symbolName)
	{
		(void)inst;
		(void)movieName;
		(void)symbolName;
	}
	// The instance reported by componentInstanceCreated() was destroyed (removed, replaced or its movie unloaded); the pointer is
	// valid for this call only.
	virtual void componentInstanceDestroyed(AptCharacterInst &inst) { (void)inst; }
	// ActionTrace output.
	virtual void trace(const std::string &message) = 0;
	// FSCommand:<command> with its argument string.
	virtual void fscommand(const std::string &command, const std::string &argument) = 0;
	// getURL("<movie>.swf", target): movieName without the suffix; empty movieName = unload target.
	virtual void loadMovie(const std::string &movieName, const std::string &target) = 0;
	// getURL2("<movie>.swf", <clip>): the target is a clip object, not a path (the player resolves it; the default
	// notification reports the opaque target text).
	virtual void loadMovieInto(const std::string &movieName, AptObject *targetClip)
	{
		(void)targetClip;
		loadMovie(movieName, "[MovieClip]");
	}
	// Any other getURL.
	virtual void getURL(const std::string &url, const std::string &target) = 0;
	// extern.<name> read; `value` is set only for AptExternResult::Value.
	virtual AptExternResult getExtern(const std::string &name, std::string &value) = 0;
	// extern.<name> write; false when nothing is registered under the name (the interpreter reports it).
	virtual bool setExtern(const std::string &name, const std::string &value) = 0;
	// ActionRandom source (bfmeNext1221, the client RNG).
	virtual std::uint32_t random() = 0;
	// A script fault the interpreter could not continue past or had to skip; never silent.
	virtual void scriptError(const std::string &message) = 0;
	// A call whose callee is not a function: a method a defined value does not have, any method of undefined / null, or a named
	// function no scope defines.  The retail interpreter calls nothing and reports nothing (stop S-380: RotWK CallMethod 0x00B1C220 /
	// BFME2 0x00B08070 resolve the member and skip the call when it is not a function; neither reaches the Apt log 0x00ACC110, whose only
	// ActionScript callers are the uncaught-exception reports, RotWK 0x00B11574 / 0x00B1AB47).  The player records it as the note
	// `call-without-function` (Apt::notes); a host without a player keeps the default, a script error.
	virtual void scriptCallWithoutFunction(const std::string &message) { scriptError(message); }
};

// Records every call; tests install providers for extern with setExternValue().
class AptRecordingHost : public AptHost
{
public:
	struct Command
	{
		std::string command;
		std::string argument;
	};
	struct Load
	{
		std::string movie;
		std::string target;
	};

	std::vector<std::string> traces;
	std::vector<Command> fscommands;
	std::vector<Load> loads;
	std::vector<Command> urls; // (url, target)
	std::vector<Command> externSets;
	std::vector<std::string> errors;
	std::map<std::string, std::string> externValues;
	std::set<std::string> externUndefined; // providers registered that answer "no value"
	std::uint32_t randomState = 12345u;

	void setExternValue(const std::string &name, const std::string &value) { externValues[name] = value; }
	void setExternUndefined(const std::string &name) { externUndefined.insert(name); }

	void trace(const std::string &message) override { traces.push_back(message); }
	void fscommand(const std::string &command, const std::string &argument) override { fscommands.push_back({ command, argument }); }
	void loadMovie(const std::string &movieName, const std::string &target) override { loads.push_back({ movieName, target }); }
	void getURL(const std::string &url, const std::string &target) override { urls.push_back({ url, target }); }
	AptExternResult getExtern(const std::string &name, std::string &value) override
	{
		auto it = externValues.find(name);
		if (it != externValues.end())
		{
			value = it->second;
			return AptExternResult::Value;
		}
		if (externUndefined.count(name))
		{
			return AptExternResult::Undefined;
		}
		return AptExternResult::NoProvider;
	}
	bool setExtern(const std::string &name, const std::string &value) override
	{
		if (!externValues.count(name) && !externUndefined.count(name))
		{
			return false;
		}
		externSets.push_back({ name, value });
		externValues[name] = value;
		externUndefined.erase(name);
		return true;
	}
	std::uint32_t random() override
	{
		randomState = randomState * 1664525u + 1013904223u; // deterministic test source
		return randomState >> 8;
	}
	void scriptError(const std::string &message) override { errors.push_back(message); }
};
