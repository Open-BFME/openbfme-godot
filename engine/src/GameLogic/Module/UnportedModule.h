// OpenBFME. GPL-3.0.
//
// Unported modules (lane LOGIC-1, stop S-140). The binary registers 329 module classes; the live object layer ports the few the
// creation path needs. Every other class is created as one of these instances so an object of any template is complete: its module
// list has the same length and order as retail's, the scheduler holds its update module, the interface masks are the registry's.
// An unported module does NOTHING, says so (Module::isUnported), is counted by class in ModuleFactory::unportedCreated and in
// GameLogic::report(), and reaches every runtime report as a stop. It is never a silent default:
//   * an unported UPDATE class is registered with the scheduler like a retail one but sleeps forever from creation (its next call
//     frame is UPDATE_SLEEP_FOREVER), so registerObject files it in the sleeping vector; update() counts a call if anything wakes it;
//   * the typed accessors (getBody, getContain, getDie, ...) return null, so an object whose only body module is an unported class has
//     no body (the report names the class);
//   * an unported DRAW / CLIENT_UPDATE / CLIENT_BEHAVIOR class is a DrawableModule that draws nothing.
//
// The helper modules of the Object constructor (SMCHelper, RecoveryHelper, ...) are not registry classes; they are the shells of
// GameLogic/Module/ObjectHelper.h.

#pragma once

#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>

class UnportedBehaviorModule : public BehaviorModule
{
public:
	UnportedBehaviorModule(Thing *thing, const ModuleData *data, int interfaceMask)
		: BehaviorModule(thing, data)
		, m_mask(interfaceMask)
	{
	}
	bool isUnported() const override { return true; }
	int interfaceMask() const { return m_mask; }

private:
	int m_mask;
};

class UnportedUpdateModule : public UpdateModule
{
public:
	UnportedUpdateModule(Thing *thing, const ModuleData *data, int interfaceMask);
	bool isUnported() const override { return true; }
	int interfaceMask() const { return m_mask; }
	UpdateSleepTime update() override;
	// how many times the scheduler (or a wake) ran this module: a module that never acts has nothing to do, so a non-zero count means
	// something woke it (reported)
	unsigned calls() const { return m_calls; }

private:
	int m_mask;
	unsigned m_calls = 0;
};

class UnportedDrawableModule : public DrawableModule
{
public:
	UnportedDrawableModule(Thing *thing, const ModuleData *data, ModuleType type, int interfaceMask)
		: DrawableModule(thing, data)
		, m_type(type)
		, m_mask(interfaceMask)
	{
	}
	bool isUnported() const override { return true; }
	ModuleType moduleType() const { return m_type; }
	int interfaceMask() const { return m_mask; }

private:
	ModuleType m_type;
	int m_mask;
};

// the default createProc of ModuleFactory
std::unique_ptr<Module> makeUnportedModule(Thing *thing, const ModuleData *data, const std::string &className, ModuleType type, int interfaceMask);
