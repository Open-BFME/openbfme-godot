// OpenBFME. GPL-3.0.
//
// ModuleFactory: the registry of module classes. Port of ZH Source/Common/Thing/ModuleFactory.cpp
// (addModule, findModuleInterfaceMask, newModuleDataFromINI) with the RotWK registry.
//
// TARGET FACTS (RW addresses, caveat S-001; see tools/rw_object_model/module_registry.py):
//   * RW 0x6570FE addModule(create, createData, extra, type, name, mask) stores the entry under
//     NAMEKEY('0' + type + name) (makeDecoratedNameKey, RW 0x655A0C): the key goes through the
//     case-sensitive NameKeyGenerator, so "activebody" is not "ActiveBody".
//   * 330 call sites register 329 classes with their interface masks (golden: module-registry.json).
//     init() registers ALL of them (PLAN rule 6: the binary's registry, not the subset retail data
//     uses), each with a RawModuleData factory until a typed class is bound.
//   * RW 0x656C36 findModuleInterfaceMask: the stored mask, or 0 for an empty or unknown name.
//   * RW 0x656EF1 newModuleDataFromINI: createData(ini), then data->setModuleTagNameKey(NAMEKEY(tag)),
//     then the data is appended to the factory's module-data list. For an unknown class RW returns
//     NULL and parseModuleName dereferences it (RW 0x73F496): a crash. This port raises an INIException
//     instead (stop S-075).
//
// LOGIC-1: the runtime half of the registry. Each entry also has a createProc (RW 0x656480 newModule: createProc(thing, data) for the class
// the name selects). A class whose behaviour is ported binds its proc (bindModuleProc); every other registered class gets the default
// proc, which makes an explicit UNPORTED module (GameLogic/Module/UnportedModule.h): an instance that does nothing, says so
// (Module::isUnported) and is counted here by class and in GameLogic's report (stop S-140). A mod class the binary registers is therefore
// created, never refused or silently dropped.
//
// Hook for typed data classes (ZH style): derive from ModuleData with a static
// `buildFieldParse(MultiIniFieldParse &)` and call bindTypedData<T>("ClassName", type); the class must
// already be in the registry (a typed class can not add a class the binary does not register).

#pragma once

#include "Common/INI.h"
#include "Common/Module.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Thing/RawModuleData.h"
#include "Common/Thing/RwGrammar.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class ModuleFactory
{
public:
	typedef std::function<std::shared_ptr<ModuleData>(INI *ini)> NewModuleDataProc;
	struct ModuleTemplate;
	// createProc: `data` is the template's module data (never null for a declared module); the module keeps the pointer
	typedef std::function<std::unique_ptr<Module>(Thing *thing, const ModuleData *data, const ModuleTemplate &info)> NewModuleProc;

	struct ModuleTemplate
	{
		std::string name;
		ModuleType type = MODULETYPE_BEHAVIOR;
		int interfaceMask = 0;
		bool isAiModuleData = false;
		bool slot7Predicate = false;
		bool typed = false;
		bool portedModule = false; ///< a runtime class is bound (bindModuleProc); otherwise the default unported module is made
		NewModuleDataProc newModuleData;
		NewModuleProc newModule;
		MultiIniFieldParse rawFieldParse; ///< the binary's tables for this class (used while raw)
	};

	// Registers nothing until init(). `grammar` and `keys` must outlive the factory.
	ModuleFactory(NameKeyGenerator &keys, const RwGrammar &grammar);

	// Registers every class of the binary's registry (329 classes), as raw.
	void init();

	// RW 0x655A0C: NAMEKEY of '0'+type followed by the class name.
	NameKeyType makeDecoratedNameKey(const std::string &name, ModuleType type);

	const ModuleTemplate *findModuleTemplate(const std::string &name, ModuleType type);
	// RW 0x656C36: 0 for an empty or unknown name.
	int findModuleInterfaceMask(const std::string &name, ModuleType type);
	// RW 0x656EF1 (+ NAMEKEY of the tag): throws INIException(3, ...) for an unknown class (stop S-075).
	std::shared_ptr<ModuleData> newModuleDataFromINI(INI *ini, const std::string &name, ModuleType type, const std::string &tag);

	// Binds a typed data class (see the file comment).
	template <class T>
	void bindTypedData(const std::string &name, ModuleType type)
	{
		bindDataProc(name, type, [](INI *ini) -> std::shared_ptr<ModuleData> {
			std::shared_ptr<T> data = std::make_shared<T>();
			if (ini)
			{
				ini->initFromINIMultiProc(data.get(), &T::buildFieldParse);
			}
			return data;
		});
	}
	void bindDataProc(const std::string &name, ModuleType type, NewModuleDataProc proc);
	// Binds the runtime class of a registered class (the class must be in the registry).
	void bindModuleProc(const std::string &name, ModuleType type, NewModuleProc proc);

	// RW 0x656480 newModule. Never returns null: an unported class yields an UnportedModule. The module's class name and key are set.
	// An unregistered name is a logic error (the module data could not have been made).
	std::unique_ptr<Module> newModule(Thing *thing, const std::string &name, const ModuleData *data, ModuleType type);

	// Lane SMOOTH-1 (S-810): newModule in two halves for the drawables' ClientUpdate / ClientBehavior modules, whose drawable is made on the render
	// side while the logic may be running. resolveClientModule runs on the logic owner, in the order the drawable makes its modules (the client
	// event recorder, when the object is created): it interns the class's keys (the decorated key of mustFind, the class name key) and counts an
	// unported class, exactly as newModule did at that point. newResolvedModule then makes the module on any thread: it reads the resolved
	// template only (no interning, no counter, no lookup in the factory's maps).
	struct ResolvedModule
	{
		const ModuleTemplate *mt = nullptr;
		NameKeyType classKey = NAMEKEY_INVALID;
	};
	ResolvedModule resolveClientModule(const std::string &name, ModuleType type);
	static std::unique_ptr<Module> newResolvedModule(Thing *thing, const ResolvedModule &resolved, const std::string &name, const ModuleData *data);

	// how many UNPORTED modules were ever made, by class name (stop S-140), and the registered classes that have a runtime class
	const std::map<std::string, size_t> &unportedCreated() const { return m_unportedCreated; }
	size_t portedModuleCount() const;
	std::vector<std::string> unportedModuleClassNames() const;

	// how many entries are registered / typed, and the names still raw ("name" per entry, registry order)
	size_t classCount() const { return m_templates.size(); }
	size_t typedCount() const;
	std::vector<std::string> rawClassNames() const;
	// every module data ever made through newModuleDataFromINI (RW keeps them in the factory, +0x1C list)
	size_t moduleDataCount() const { return m_moduleData.size(); }
	// ... counted per class name (every declaration of a class, whatever its module type)
	const std::map<std::string, size_t> &declarationCounts() const { return m_declarationCounts; }
	// class name of the module data made n-th (0-based), for per-definition accounting
	const std::string &moduleDataClass(size_t n) const { return m_moduleDataClasses.at(n); }

	// The acceptance stops this factory is responsible for, as report lines (empty parts omitted).
	std::vector<std::string> acceptanceStops() const;

private:
	ModuleTemplate &mustFind(const std::string &name, ModuleType type);

	NameKeyGenerator &m_keys;
	const RwGrammar &m_grammar;
	std::map<NameKeyType, ModuleTemplate> m_templates;
	std::vector<NameKeyType> m_order; ///< registration order
	std::vector<std::shared_ptr<ModuleData>> m_moduleData;
	std::map<std::string, size_t> m_declarationCounts;
	std::vector<std::string> m_moduleDataClasses;
	std::map<std::string, size_t> m_unportedCreated;
};
