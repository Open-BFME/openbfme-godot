// OpenBFME. GPL-3.0.
// ModuleFactory: see ModuleFactory.h for the RotWK addresses and the stops.

#include "Common/Thing/ModuleFactory.h"

#include "GameLogic/Module/UnportedModule.h"

#include <stdexcept>

ModuleFactory::ModuleFactory(NameKeyGenerator &keys, const RwGrammar &grammar)
	: m_keys(keys)
	, m_grammar(grammar)
{
}

NameKeyType ModuleFactory::makeDecoratedNameKey(const std::string &name, ModuleType type)
{
	// RW 0x655A0C: buf[0] = type + '0'; strcpy(buf + 1, name); NAMEKEY(buf)
	std::string decorated(1, (char)('0' + (int)type));
	decorated += name;
	return m_keys.nameToKey(decorated);
}

void ModuleFactory::init()
{
	const RwBinaryData &data = m_grammar.data();
	if (!m_templates.empty())
	{
		throw std::logic_error("ModuleFactory::init called twice");
	}
	// Name keys are handed out in call order, so the registrations are replayed in the order the binary executes
	// them (RW 0x464AD2 runs the 0x6579C9 function, 310 non-draw classes starting with AutoHealBehavior, before the draw
	// classes), not in the golden's class order; a repeated registration (WeaponBonusUpgrade) reuses its key.
	for (const auto &step : data.registrationSequence)
	{
		makeDecoratedNameKey(step.first, (ModuleType)step.second);
	}
	for (const RwClass &cls : data.classes)
	{
		if (cls.type < 0 || cls.type >= NUM_MODULE_TYPES)
		{
			throw std::runtime_error("ModuleFactory: registry class '" + cls.name + "' has an unknown module type");
		}
		const NameKeyType key = makeDecoratedNameKey(cls.name, (ModuleType)cls.type);
		// RW addModule uses operator[]: a second registration of the same key overwrites (WeaponBonusUpgrade
		// registers twice with identical arguments; the golden lists it once with two sites)
		const bool isNew = m_templates.find(key) == m_templates.end();
		ModuleTemplate &mt = m_templates[key];
		mt.name = cls.name;
		mt.type = (ModuleType)cls.type;
		mt.interfaceMask = (int)cls.mask;
		mt.isAiModuleData = cls.isAiModuleData;
		mt.slot7Predicate = cls.slot7;
		mt.typed = false;
		mt.rawFieldParse = MultiIniFieldParse();
		m_grammar.buildClassFieldParse(cls, mt.rawFieldParse);
		const std::string className = cls.name;
		const ModuleType type = mt.type;
		ModuleTemplate *self = &mt; // std::map nodes never move
		mt.newModuleData = [self, className, type](INI *ini) -> std::shared_ptr<ModuleData> {
			std::shared_ptr<RawModuleData> raw = std::make_shared<RawModuleData>(className, type);
			if (ini)
			{
				// the body is everything up to the matching End: readLine consumes it, so the first body
				// line is the one at the current line count
				const size_t first = ini->getLineNum();
				ini->initFromINIMulti(raw.get(), self->rawFieldParse);
				raw->capture(*ini, first, ini->getLineNum());
			}
			return raw;
		};
		// the default runtime class: an explicit unported module (LOGIC-1, stop S-140)
		mt.portedModule = false;
		mt.newModule = [](Thing *thing, const ModuleData *data, const ModuleTemplate &info) -> std::unique_ptr<Module> {
			return makeUnportedModule(thing, data, info.name, info.type, info.interfaceMask);
		};
		if (isNew)
		{
			m_order.push_back(key);
		}
	}
}

const ModuleFactory::ModuleTemplate *ModuleFactory::findModuleTemplate(const std::string &name, ModuleType type)
{
	if (name.empty())
	{
		return nullptr; // RW 0x656C3D / 0x656EFA: an empty name finds nothing
	}
	auto it = m_templates.find(makeDecoratedNameKey(name, type));
	return it == m_templates.end() ? nullptr : &it->second;
}

int ModuleFactory::findModuleInterfaceMask(const std::string &name, ModuleType type)
{
	const ModuleTemplate *mt = findModuleTemplate(name, type);
	return mt ? mt->interfaceMask : 0;
}

std::shared_ptr<ModuleData> ModuleFactory::newModuleDataFromINI(INI *ini, const std::string &name, ModuleType type, const std::string &tag)
{
	const ModuleTemplate *mt = findModuleTemplate(name, type);
	if (!mt)
	{
		throw INIException(3, "Unknown module class '%s' (module type %d): RotWK returns NULL here and then dereferences it (acceptance stop S-075).", name.c_str(), (int)type);
	}
	std::shared_ptr<ModuleData> data = mt->newModuleData(ini);
	data->bfmeSetPredicates(mt->isAiModuleData, mt->slot7Predicate);
	data->setModuleTagNameKey(m_keys.nameToKey(tag)); // RW 0x656F1C-0x656F2F
	m_moduleData.push_back(data);
	++m_declarationCounts[name];
	m_moduleDataClasses.push_back(name);
	return data;
}

ModuleFactory::ModuleTemplate &ModuleFactory::mustFind(const std::string &name, ModuleType type)
{
	auto it = m_templates.find(makeDecoratedNameKey(name, type));
	if (it == m_templates.end())
	{
		throw std::logic_error("ModuleFactory: '" + name + "' is not in the binary's registry for this module type");
	}
	return it->second;
}

void ModuleFactory::bindDataProc(const std::string &name, ModuleType type, NewModuleDataProc proc)
{
	ModuleTemplate &mt = mustFind(name, type);
	mt.newModuleData = std::move(proc);
	mt.typed = true;
}

void ModuleFactory::bindModuleProc(const std::string &name, ModuleType type, NewModuleProc proc)
{
	ModuleTemplate &mt = mustFind(name, type);
	mt.newModule = std::move(proc);
	mt.portedModule = true;
}

std::unique_ptr<Module> ModuleFactory::newModule(Thing *thing, const std::string &name, const ModuleData *data, ModuleType type)
{
	ModuleTemplate &mt = mustFind(name, type);
	std::unique_ptr<Module> module = mt.newModule(thing, data, mt);
	if (!module)
	{
		throw std::logic_error("ModuleFactory: the runtime class of '" + name + "' returned no module");
	}
	module->friend_setModuleClass(name, m_keys.nameToKey(name));
	if (module->isUnported())
	{
		++m_unportedCreated[name];
	}
	return module;
}

ModuleFactory::ResolvedModule ModuleFactory::resolveClientModule(const std::string &name, ModuleType type)
{
	ResolvedModule r;
	ModuleTemplate &mt = mustFind(name, type);
	r.mt = &mt;
	r.classKey = m_keys.nameToKey(name);
	if (!mt.portedModule)
	{
		++m_unportedCreated[name]; // newModule's count of an unported class (its module is an UnportedModule)
	}
	return r;
}

std::unique_ptr<Module> ModuleFactory::newResolvedModule(Thing *thing, const ResolvedModule &resolved, const std::string &name, const ModuleData *data)
{
	if (!resolved.mt)
	{
		throw std::logic_error("ModuleFactory: '" + name + "' was not resolved");
	}
	std::unique_ptr<Module> module = resolved.mt->newModule(thing, data, *resolved.mt);
	if (!module)
	{
		throw std::logic_error("ModuleFactory: the runtime class of '" + name + "' returned no module");
	}
	module->friend_setModuleClass(name, resolved.classKey);
	return module;
}

size_t ModuleFactory::portedModuleCount() const
{
	size_t n = 0;
	for (const auto &kv : m_templates)
	{
		n += kv.second.portedModule ? 1 : 0;
	}
	return n;
}

std::vector<std::string> ModuleFactory::unportedModuleClassNames() const
{
	std::vector<std::string> out;
	for (NameKeyType key : m_order)
	{
		const ModuleTemplate &mt = m_templates.at(key);
		if (!mt.portedModule)
		{
			out.push_back(mt.name);
		}
	}
	return out;
}

size_t ModuleFactory::typedCount() const
{
	size_t n = 0;
	for (const auto &kv : m_templates)
	{
		n += kv.second.typed ? 1 : 0;
	}
	return n;
}

std::vector<std::string> ModuleFactory::rawClassNames() const
{
	std::vector<std::string> out;
	for (NameKeyType key : m_order)
	{
		const ModuleTemplate &mt = m_templates.at(key);
		if (!mt.typed)
		{
			out.push_back(mt.name);
		}
	}
	return out;
}

std::vector<std::string> ModuleFactory::acceptanceStops() const
{
	std::vector<std::string> out;
	const size_t raw = m_templates.size() - typedCount();
	if (raw)
	{
		out.push_back("S-070: " + std::to_string(raw) + " of " + std::to_string(m_templates.size()) +
			" registered module classes keep their body as RawModuleData (field values not parsed; only the body extent and field names are checked)");
	}
	out.push_back("S-073: the name of the ModuleData vtable slot 7 predicate (ChildObject body replacement) and the meaning of interface mask 0x2000 (PillageModule) are unverified");
	return out;
}
