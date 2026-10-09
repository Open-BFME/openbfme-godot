// OpenBFME unit tests. GPL-3.0.
// Fixtures for the draw module tests: parse one module body from text.

#pragma once

#include "Common/INI.h"
#include "Common/ModelState.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"

#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace drawtest
{

// Parses module bodies ("... End" without the Draw header line) with the real INI machinery. One harness can parse many bodies
// (the retail scan reuses it so the macro table is built once).
struct Harness
{
	INIEnvironment env;
	W3DDrawModuleClass cls = W3D_DRAW_SCRIPTED_MODEL;
	std::unique_ptr<W3DModelDrawModuleData> data;
	std::unique_ptr<W3DDefaultDrawModuleData> defaultData; ///< filled instead of `data` for W3D_DRAW_DEFAULT (parseDefault)
	std::string error;

	Harness()
	{
		env.blocks.registerBlock("TestDraw", [this](INI *ini) {
			if (cls == W3D_DRAW_DEFAULT)
			{
				defaultData = W3DParseDefaultDrawBody(ini);
			}
			else
			{
				data = W3DParseDrawModuleBody(ini, cls);
			}
			while (!ini->isEOF())
			{
				ini->readLine(); // the rest of the text is not part of the module
			}
		});
		env.blocks.registerBlock("TestFlags", [this](INI *ini) {
			flags = ModelConditionFlags();
			ModelCondition::parseFromLine(ini, flags);
			while (!ini->isEOF())
			{
				ini->readLine();
			}
		});
	}

	ModelConditionFlags flags;

	// Returns the module data, or nullptr with `error` set. `text` is the body lines, ending with the module's End.
	W3DModelDrawModuleData *parse(const std::string &text, W3DDrawModuleClass c = W3D_DRAW_SCRIPTED_MODEL, INILoadType loadType = INI_LOAD_OVERWRITE)
	{
		if (c == W3D_DRAW_DEFAULT)
		{
			throw std::logic_error("Harness::parse: W3DDefaultDraw has its own data class, use parseDefault");
		}
		run(text, c, loadType);
		return data.get();
	}

	W3DDefaultDrawModuleData *parseDefault(const std::string &text, INILoadType loadType = INI_LOAD_OVERWRITE)
	{
		run(text, W3D_DRAW_DEFAULT, loadType);
		return defaultData.get();
	}

	void run(const std::string &text, W3DDrawModuleClass c, INILoadType loadType)
	{
		cls = c;
		data.reset();
		defaultData.reset();
		error.clear();
		const std::string full = "TestDraw\n" + text;
		INI ini(env);
		try
		{
			ini.loadMemory("test.ini", std::vector<std::uint8_t>(full.begin(), full.end()), loadType);
		}
		catch (const INIException &e)
		{
			error = e.message();
			data.reset();
			defaultData.reset();
		}
	}

	// The condition flags a "TestFlags A B C" line parses to; `error` is set on failure.
	ModelConditionFlags parseFlags(const std::string &line)
	{
		const std::string full = "TestFlags " + line + "\n";
		INI ini(env);
		error.clear();
		try
		{
			ini.loadMemory("flags.ini", std::vector<std::uint8_t>(full.begin(), full.end()), INI_LOAD_OVERWRITE);
		}
		catch (const INIException &e)
		{
			error = e.message();
		}
		return flags;
	}
};

inline ModelConditionFlags flagsOf(std::initializer_list<const char *> names)
{
	ModelConditionFlags f;
	for (const char *n : names)
	{
		const int bit = ModelCondition::indexOf(n);
		if (bit < 0)
		{
			throw std::logic_error(std::string("unknown model condition ") + n);
		}
		f.set(bit);
	}
	return f;
}

} // namespace drawtest
