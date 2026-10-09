// OpenBFME unit tests. GPL-3.0. See W3DDrawRetail.h.

#include "W3DDrawRetail.h"

#include "Common/AsciiString.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/WeaponStores.h"
#include "RetailTestMount.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <cstring>

namespace drawtest
{

namespace
{
struct Scanner
{
	RetailDrawScan &scan;
	std::string currentObject;

	explicit Scanner(RetailDrawScan &s) : scan(s) {}

	// The Object / ChildObject / ObjectReskin handler: everything up to the end of the file is walked here.
	void run(INI *ini)
	{
		currentObject = nameOfHeader(ini->currentLineText());
		while (!ini->isEOF())
		{
			ini->readLine();
			if (ini->isEOF())
			{
				break;
			}
			const std::string text = ini->currentLineText();
			const char *first = ini->firstToken();
			if (!first)
			{
				continue;
			}
			const std::string key = first;
			if ((key == "Object" || key == "ChildObject" || key == "ObjectReskin") && text.find('=') == std::string::npos)
			{
				currentObject = nameOfHeader(text);
				continue;
			}
			if (key != "Draw")
			{
				continue;
			}
			const char *cls = ini->getNextTokenOrNull();
			if (!cls)
			{
				continue;
			}
			const std::string className = cls;
			const char *tag = ini->getNextTokenOrNull();
			const W3DDrawModuleClass kind = W3DDrawModuleClassFromName(className);
			if (kind == W3D_DRAW_NONE)
			{
				++scan.otherDrawCounts[className];
				continue;
			}
			RetailDrawModule m;
			m.object = currentObject;
			m.file = ini->getFilename();
			m.line = ini->currentSourceLine();
			m.className = className;
			m.tag = tag ? tag : "";
			++scan.declarationsByClass[className];
			try
			{
				if (kind == W3D_DRAW_DEFAULT)
				{
					m.defaultData = W3DParseDefaultDrawBody(ini);
				}
				else
				{
					m.data = W3DParseDrawModuleBody(ini, kind);
				}
			}
			catch (const INIException &e)
			{
				m.error = e.message();
			}
			scan.modules.push_back(std::move(m));
		}
	}

	static std::string nameOfHeader(const std::string &line)
	{
		size_t b = line.find_first_not_of(" \t");
		if (b == std::string::npos)
		{
			return std::string();
		}
		size_t e = line.find_first_of(" \t", b);
		if (e == std::string::npos)
		{
			return std::string();
		}
		b = line.find_first_not_of(" \t", e);
		if (b == std::string::npos)
		{
			return std::string();
		}
		e = line.find_first_of(" \t", b);
		return line.substr(b, e == std::string::npos ? std::string::npos : e - b);
	}
};
} // namespace

RetailDrawScan &retailDrawScan()
{
	static RetailDrawScan scan;
	static bool done = false;
	if (done)
	{
		return scan;
	}
	done = true;
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		scan.mountError = "ROTWK_INSTALL / BFME2_INSTALL not set";
		return scan;
	}
	if (!mount->error.empty() || !mount->fs)
	{
		scan.mountError = mount->error.empty() ? "mount failed" : mount->error;
		scan.available = true;
		return scan;
	}
	scan.available = true;

	static INIEnvironment env;
	static INIBlockRecorder recorder;
	static SubsystemLegend legend;
	env.fileSystem = mount->fs.get();
	legend.registerBlock(env.blocks);
	// Lenient stubs for every block except the three the scanner owns (INIBlockStubs.h: lexer, #define and dispatch are
	// exercised; block boundaries are not certified, and need not be here).
	RegisterRecordingBlockStubs(env.blocks, recorder, { "LoadSubsystem", "Object", "ChildObject", "ObjectReskin" }, StubExtent::Lenient);
	static Scanner scanner(scan);
	for (const char *keyword : { "Object", "ChildObject", "ObjectReskin" })
	{
		env.blocks.registerBlock(keyword, [](INI *ini) { scanner.run(ini); });
	}
	// the legend's Locomotor block has a real parser now (lane HORDE-1) and needs the store; restored after the load
	static LocomotorStore locomotors;
	LocomotorStore *const savedLocomotors = TheLocomotorStore;
	TheLocomotorStore = &locomotors;
	WeaponStores weaponStores; // WEAPON-1: the Weapon, Armor and DamageFX blocks have real parsers
	weaponStores.install();
	INI ini(env);
	SubsystemLoadOptions options;
	options.collectErrors = true;
	options.cinematics = true; // the census counts every object in the data, including the cinematic ones a normal launch skips
	RunSubsystemIniLoad(legend, ini, options, scan.report);
	TheLocomotorStore = savedLocomotors;
	scan.macroCount = env.macros.size();
	return scan;
}

} // namespace drawtest
