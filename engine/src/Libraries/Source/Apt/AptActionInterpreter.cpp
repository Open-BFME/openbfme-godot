// OpenBFME. GPL-3.0.
// See AptActionInterpreter.h for the model and citations.

#include "Libraries/Source/Apt/AptActionInterpreter.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

struct AptActionInterpreter::Frame
{
	AptObject *thisObj = nullptr;
	AptObject *target = nullptr; // timeline in scope (the clip the program runs on / the function's defining clip)
	AptObject *root = nullptr;
	std::shared_ptr<const std::vector<AptValue>> pool;
	std::vector<AptValue> *regs = nullptr;
	AptPropertyMap *locals = nullptr; // null for timeline-level code
	std::shared_ptr<AptScope> scope;  // owns `locals` for function frames; its parents are the enclosing functions' locals
	std::uint32_t swfVersion = 7;
	AptValue returnValue;
	bool returned = false;
	bool fault = false;
	std::vector<AptValue> stack;
	const AptInstruction *ins = nullptr;
};

namespace
{

std::string whereIs(const AptInstruction *ins)
{
	if (!ins)
	{
		return "";
	}
	return std::string(" (") + AptActionDecoder::opcodeName(ins->opcode) + " at file offset " + std::to_string(ins->offset) + ")";
}

std::size_t utf8Length(const std::string &s)
{
	std::size_t n = 0;
	for (unsigned char c : s)
	{
		if ((c & 0xC0) != 0x80)
		{
			++n;
		}
	}
	return n;
}

} // namespace

AptActionInterpreter::AptActionInterpreter(AptGC &gc, AptHost &host) : m_gc(gc), m_host(host)
{
	m_global = m_gc.create<AptObject>();
	m_objectProto = m_gc.create<AptObject>();
	m_functionProto = m_gc.create<AptObject>();
	m_arrayProto = m_gc.create<AptObject>();
	m_stringProto = m_gc.create<AptObject>();
	m_numberProto = m_gc.create<AptObject>();
	m_booleanProto = m_gc.create<AptObject>();
	m_functionProto->setProto(m_objectProto);
	m_arrayProto->setProto(m_objectProto);
	m_stringProto->setProto(m_objectProto);
	m_numberProto->setProto(m_objectProto);
	m_booleanProto->setProto(m_objectProto);
	m_global->setProto(m_objectProto);
	m_globalRegisters.assign(kGlobalRegisterCount, AptValue());
	aptInstallCoreNatives(*this);
}

AptActionInterpreter::~AptActionInterpreter() = default;

void AptActionInterpreter::markRoots(AptGC &gc)
{
	gc.mark(m_global);
	gc.mark(m_objectProto);
	gc.mark(m_functionProto);
	gc.mark(m_arrayProto);
	gc.mark(m_stringProto);
	gc.mark(m_numberProto);
	gc.mark(m_booleanProto);
	for (const AptValue &v : m_globalRegisters)
	{
		gc.mark(v);
	}
}

void AptActionInterpreter::reportError(const std::string &message)
{
	m_errors.push_back(message);
	m_host.scriptError(message);
}

namespace
{
bool isFunctionValue(const AptValue &v)
{
	return v.isObject() && v.asObject() && v.asObject()->kind() == AptObjectKind::Function;
}
} // namespace

bool AptActionInterpreter::isUnportedRetailNative(const std::string &name)
{
	// docs/STOPS.md S-104 / S-380: in the native name table of game.dat, not implemented here (a call is the port's gap); registerClass:
	// RotWK getter 0x00B22160, body 0x00AF1EB0 (not read)
	static const char *const kUnported[] = { "createTextField", "removeTextField", "loadVariables", "getTextFormat", "registerClass" };
	for (const char *n : kUnported)
	{
		if (name == n)
		{
			return true;
		}
	}
	return false;
}

void AptActionInterpreter::reportCallWithoutFunction(const std::string &name, const std::string &message)
{
	if (isUnportedRetailNative(name))
	{
		reportError(message);
		return;
	}
	m_errors.push_back(message);
	m_host.scriptCallWithoutFunction(message);
}

// ---- object helpers -------------------------------------------------------------------------

AptObject *AptActionInterpreter::newObject()
{
	AptObject *o = m_gc.create<AptObject>();
	o->setProto(m_objectProto);
	return o;
}

AptArray *AptActionInterpreter::newArray()
{
	AptArray *a = m_gc.create<AptArray>();
	a->setProto(m_arrayProto);
	return a;
}

AptFunction *AptActionInterpreter::newNativeFunction(const std::string &name, AptFunction::Native fn)
{
	AptFunction *f = m_gc.create<AptFunction>();
	f->native = std::move(fn);
	f->nativeName = name;
	f->setProto(m_functionProto);
	return f;
}

AptFunction *AptActionInterpreter::defineGlobalNative(const std::string &name, AptFunction::Native fn)
{
	AptFunction *f = newNativeFunction(name, std::move(fn));
	m_global->setNativeMember(name, AptValue::object(f)); // built-in: not a hash slot, never enumerated
	return f;
}

void AptActionInterpreter::defineNative(AptObject *owner, const std::string &name, AptFunction::Native fn)
{
	owner->setNativeMember(name, AptValue::object(newNativeFunction(name, std::move(fn))));
}

// ---- members --------------------------------------------------------------------------------

bool AptActionInterpreter::fetchMember(const AptObject *object, const std::string &name, AptValue &out)
{
	// every script read of a member goes through here so an exhausted prototype walk (S-015) is reported
	bool found = object->getMember(name, out);
	std::string pending = const_cast<AptObject *>(object)->takeError();
	if (!pending.empty())
	{
		reportError(pending);
	}
	return found;
}

bool AptActionInterpreter::memberExists(const AptObject *object, const std::string &name)
{
	AptValue ignored;
	return fetchMember(object, name, ignored);
}

AptValue AptActionInterpreter::getMember(const AptValue &object, const std::string &name)
{
	AptValue out;
	switch (object.type())
	{
		case AptValueType::Object:
			if (object.asObject() && fetchMember(object.asObject(), name, out))
			{
				return out;
			}
			return AptValue();
		case AptValueType::String:
			if (AptPropertyMap::foldKey(name) == "length")
			{
				{
					std::string why;
					if (!AptUtf8Validate(object.asString(), why))
					{
						reportError("String.length on malformed UTF-8 text: " + why);
						return AptValue();
					}
					return AptValue::integer((std::int32_t)utf8Length(object.asString()));
				}
			}
			if (fetchMember(m_stringProto, name, out))
			{
				return out;
			}
			return AptValue();
		case AptValueType::Integer:
		case AptValueType::Float:
			return fetchMember(m_numberProto, name, out) ? out : AptValue();
		case AptValueType::Boolean:
			return fetchMember(m_booleanProto, name, out) ? out : AptValue();
		case AptValueType::Extern:
		{
			// the WindowManager provider map is keyed by name (menus-apt.md; AptScreenFactories.cpp:751-756): a
			// provider that answers "no value" reads as undefined, a name nobody registered is an error
			std::string text;
			switch (m_host.getExtern(name, text))
			{
				case AptExternResult::Value:
					return AptValue::string(text);
				case AptExternResult::Number:
					return AptValue::integer((std::int32_t)std::strtol(text.c_str(), nullptr, 10));
				case AptExternResult::Undefined:
					return AptValue();
				case AptExternResult::NoProvider:
					break;
			}
			reportError("extern." + name + " read: no extern provider is registered under that name");
			return AptValue();
		}
		default:
			return AptValue(); // member of undefined/null: undefined, as in Flash
	}
}

void AptActionInterpreter::storeMember(AptObject *object, const std::string &name, const AptValue &value)
{
	// every script store goes through here so a pending resource-bound or acceptance-stop error is reported
	object->setMember(name, value);
	std::string pending = object->takeError();
	if (!pending.empty())
	{
		reportError(pending);
	}
}

void AptActionInterpreter::setMember(const AptValue &object, const std::string &name, const AptValue &value)
{
	if (object.isObject() && object.asObject())
	{
		object.asObject()->setMember(name, value);
		std::string pending = object.asObject()->takeError();
		if (!pending.empty())
		{
			reportError(pending);
		}
	}
	else if (object.isExtern())
	{
		if (!m_host.setExtern(name, value.toString()))
		{
			reportError("extern." + name + " write: no extern provider is registered under that name");
		}
	}
	// primitives, undefined and null ignore member assignment (SWF spec)
}

std::vector<std::string> AptActionInterpreter::enumerateNames(const AptValue &object)
{
	std::vector<std::string> names;
	if (object.isObject() && object.asObject())
	{
		std::string error;
		if (!object.asObject()->enumerateForIn(names, error))
		{
			names.clear();
			reportError(error);
		}
	}
	else
	{
		// the EA handler asks the operand for its hash (vtable slot 3); how a non-object answers is not decoded
		reportError(std::string("for-in over a non-object value (") + object.typeOf() + ") is an acceptance stop: the EA handler's behaviour for a value without a hash is not decoded");
	}
	return names;
}

// ---- variables ------------------------------------------------------------------------------

bool AptActionInterpreter::lookupVariable(Frame &f, const std::string &name, AptValue &out, AptObject **holder)
{
	if (holder)
	{
		*holder = nullptr;
	}
	if (f.locals && f.locals->get(name, out))
	{
		return true;
	}
	if (f.scope)
	{
		for (const AptScope *outer = f.scope->parent.get(); outer; outer = outer->parent.get())
		{
			if (outer->locals.get(name, out))
			{
				return true;
			}
		}
	}
	std::string key = AptPropertyMap::foldKey(name);
	if (key == "this")
	{
		out = f.thisObj ? AptValue::object(f.thisObj) : AptValue();
		return true;
	}
	if (f.target && fetchMember(f.target, name, out))
	{
		if (holder)
		{
			*holder = f.target;
		}
		return true;
	}
	if (key == "_root" && f.root)
	{
		out = AptValue::object(f.root);
		return true;
	}
	if (key == "_global")
	{
		out = AptValue::object(m_global);
		return true;
	}
	if (key == "extern")
	{
		out = AptValue::externValue();
		return true;
	}
	if (fetchMember(m_global, name, out))
	{
		if (holder)
		{
			*holder = m_global;
		}
		return true;
	}
	return false;
}

void AptActionInterpreter::assignVariable(Frame &f, const std::string &name, const AptValue &value)
{
	if (f.locals && f.locals->has(name))
	{
		f.locals->set(name, value);
		return;
	}
	if (f.scope)
	{
		for (AptScope *outer = f.scope->parent.get(); outer; outer = outer->parent.get())
		{
			if (outer->locals.has(name))
			{
				outer->locals.set(name, value);
				return;
			}
		}
	}
	if (f.target && memberExists(f.target, name))
	{
		storeMember(f.target, name, value);
		return;
	}
	if (memberExists(m_global, name))
	{
		storeMember(m_global, name, value);
		return;
	}
	if (f.target)
	{
		storeMember(f.target, name, value); // not found anywhere: created on the timeline in scope
	}
	else
	{
		storeMember(m_global, name, value);
	}
}

void AptActionInterpreter::defineLocal(Frame &f, const std::string &name, const AptValue &value)
{
	if (f.locals)
	{
		f.locals->set(name, value);
	}
	else if (f.target)
	{
		storeMember(f.target, name, value); // `var` at timeline level is a timeline variable
	}
	else
	{
		storeMember(m_global, name, value);
	}
}

AptValue AptActionInterpreter::constantToValue(Frame &f, const AptConstRef &c)
{
	switch (c.type)
	{
		case APT_CONST_STRING:
			return AptValue::string(c.text);
		case APT_CONST_REGISTER:
			// PushData of a register pushes its current value (saved registers restore from the stack)
			if (!f.regs || c.raw >= f.regs->size())
			{
				reportError("PushData reads register " + std::to_string(c.raw) + " outside the register file" + whereIs(f.ins));
				return AptValue();
			}
			return (*f.regs)[c.raw];
		case APT_CONST_NONE:
			// no null value exists in the EA runtime (0x75 and 0x76 push the same undefined singleton) and no
			// corpus movie uses a type-3 constant, so its meaning is an acceptance stop, not a guess
			reportError("None constant (const index " + std::to_string(c.constIndex) + ") has no verified runtime meaning (acceptance stop S-013)" + whereIs(f.ins));
			return AptValue();
		case APT_CONST_BOOLEAN:
			return AptValue::boolean(c.raw != 0);
		case APT_CONST_FLOAT:
		{
			float v;
			std::memcpy(&v, &c.raw, 4);
			return AptValue::number(v);
		}
		case APT_CONST_INTEGER:
			return AptValue::integer((std::int32_t)c.raw);
		default:
			reportError("constant type " + std::to_string(c.type) + " (const index " + std::to_string(c.constIndex) +
				") has no verified runtime meaning (acceptance stop S-013)" + whereIs(f.ins));
			return AptValue();
	}
}

AptObject *AptActionInterpreter::resolveTarget(Frame &f, const AptValue &path)
{
	if (path.isObject())
	{
		return path.asObject();
	}
	if (path.isUndefined())
	{
		return f.target;
	}
	std::string p = path.toString();
	if (p.empty())
	{
		return f.target;
	}
	AptObject *cur = nullptr;
	std::size_t pos = 0;
	bool first = true;
	while (pos <= p.size())
	{
		std::size_t end = p.find_first_of("./", pos);
		if (end == std::string::npos)
		{
			end = p.size();
		}
		std::string seg = p.substr(pos, end - pos);
		pos = end + 1;
		if (seg.empty())
		{
			if (first)
			{
				cur = f.root; // a leading '/' is the root
			}
			first = false;
			if (end >= p.size())
			{
				break;
			}
			continue;
		}
		if (first)
		{
			std::string key = AptPropertyMap::foldKey(seg);
			if (key == "_root")
			{
				cur = f.root;
			}
			else if (key == "this")
			{
				cur = f.thisObj;
			}
			else
			{
				AptValue v;
				cur = (f.target && fetchMember(f.target, seg, v) && v.isObject()) ? v.asObject() : nullptr;
			}
			first = false;
		}
		else if (cur)
		{
			AptValue v;
			cur = (fetchMember(cur, seg, v) && v.isObject()) ? v.asObject() : nullptr;
		}
		if (end >= p.size())
		{
			break;
		}
	}
	return cur;
}

bool AptActionInterpreter::timeline(Frame &f, const AptTimelineRequest &req, const char *what)
{
	if (!f.target || !f.target->timelineOp(req))
	{
		reportError(std::string("the target has no timeline for ") + what + whereIs(f.ins));
		return false;
	}
	return true;
}

void AptActionInterpreter::dispatchUrl(Frame &, const std::string &url, const std::string &arg)
{
	// BFME1 DispatchLiteral008C5840.cpp / Rva008D16A0StringDispatch.cpp: the prefix test is a
	// case-sensitive strncmp with the string at 0x012D5A08 ("FSCommand:" per spec 3.3).
	static const char kPrefix[] = "FSCommand:";
	if (url.compare(0, sizeof(kPrefix) - 1, kPrefix) == 0)
	{
		m_host.fscommand(url.substr(sizeof(kPrefix) - 1), arg);
		return;
	}
	if (url.size() >= 4)
	{
		std::string tail = url.substr(url.size() - 4);
		for (char &c : tail)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		if (tail == ".swf")
		{
			m_host.loadMovie(url.substr(0, url.size() - 4), arg);
			return;
		}
	}
	if (url.empty())
	{
		m_host.loadMovie(std::string(), arg);
		return;
	}
	m_host.getURL(url, arg);
}

// ---- calls ----------------------------------------------------------------------------------

AptValue AptActionInterpreter::callFunction(const AptValue &function, const AptValue &thisValue, const std::vector<AptValue> &args, bool isConstruct)
{
	if (!function.isObject() || !function.asObject() || function.asObject()->kind() != AptObjectKind::Function)
	{
		reportError(std::string("call of a non-function value (") + function.typeOf() + ")");
		return AptValue();
	}
	if (m_depth >= 128)
	{
		++m_faults;
		reportError("call stack overflow (128 nested calls)");
		return AptValue();
	}
	if (m_depth == 0)
	{
		m_executionStart = m_executed; // the instruction budget is per outermost call
	}
	AptFunction *fn = static_cast<AptFunction *>(function.asObject());
	++m_depth;
	AptValue result;
	if (fn->isNative())
	{
		AptCallInfo info{ *this, thisValue, args };
		info.isConstruct = isConstruct;
		result = fn->native(info);
	}
	else
	{
		result = callScript(fn, thisValue, args);
	}
	--m_depth;
	return result;
}

AptValue AptActionInterpreter::callMethod(const AptValue &object, const std::string &name, const std::vector<AptValue> &args)
{
	AptValue fn = getMember(object, name);
	if (!isFunctionValue(fn)) // RotWK 0x00B1AA5F: an object that is not a function is skipped like undefined
	{
		reportCallWithoutFunction(name, std::string("method '") + name + "' is not a function on a value of type " + object.typeOf());
		return AptValue();
	}
	return callFunction(fn, object, args);
}

AptValue AptActionInterpreter::callScript(AptFunction *fn, const AptValue &thisValue, const std::vector<AptValue> &args)
{
	const AptFunctionDef &def = *fn->def;
	Frame f;
	f.thisObj = thisValue.isObject() ? thisValue.asObject() : nullptr;
	f.target = fn->scopeTarget;
	f.root = fn->scopeRoot;
	f.pool = fn->pool;
	f.swfVersion = def.swfVersion;
	f.scope = std::make_shared<AptScope>();
	f.scope->parent = fn->outerScope;
	AptPropertyMap &locals = f.scope->locals;
	f.locals = &locals;
	std::vector<AptValue> regs;
	if (def.isV2)
	{
		regs.assign(std::max<std::uint32_t>(def.registerCount, 1u), AptValue());
		f.regs = &regs;
		std::size_t next = 1;
		auto preload = [&](const AptValue &v, const char *what) {
			if (next >= regs.size())
			{
				reportError(std::string("DefineFunction2 preload of ") + what + " does not fit in " + std::to_string(def.registerCount) + " registers (function '" + def.name + "')");
			}
			else
			{
				regs[next] = v;
			}
			++next;
		};
		if (def.flags & APT_FN2_PRELOAD_THIS)
		{
			preload(f.thisObj ? AptValue::object(f.thisObj) : AptValue(), "this");
		}
		if (def.flags & APT_FN2_PRELOAD_ARGUMENTS)
		{
			AptArray *arguments = newArray();
			arguments->items = args;
			preload(AptValue::object(arguments), "arguments");
		}
		if (def.flags & APT_FN2_PRELOAD_SUPER)
		{
			reportError("DefineFunction2 PreloadSuper has no decoded behaviour (acceptance stop S-014; function '" + def.name + "')");
			preload(AptValue(), "super");
		}
		if (def.flags & APT_FN2_PRELOAD_ROOT)
		{
			preload(f.root ? AptValue::object(f.root) : AptValue(), "_root");
		}
		if (def.flags & APT_FN2_PRELOAD_PARENT)
		{
			AptValue parent;
			if (f.target)
			{
				fetchMember(f.target, "_parent", parent);
			}
			preload(parent, "_parent");
		}
		if (def.flags & APT_FN2_PRELOAD_GLOBAL)
		{
			preload(AptValue::object(m_global), "_global");
		}
		if (def.flags & APT_FN2_PRELOAD_EXTERN)
		{
			preload(AptValue::externValue(), "extern");
		}
		for (std::size_t i = 0; i < def.params.size(); ++i)
		{
			AptValue arg = i < args.size() ? args[i] : AptValue();
			const AptFunctionParam &p = def.params[i];
			if (p.reg > 0)
			{
				if ((std::size_t)p.reg >= regs.size())
				{
					reportError("DefineFunction2 parameter '" + p.name + "' targets register " + std::to_string(p.reg) + " of " +
						std::to_string(def.registerCount) + " (function '" + def.name + "')");
				}
				else
				{
					regs[p.reg] = arg;
				}
			}
			else
			{
				locals.set(p.name, arg);
			}
		}
	}
	else
	{
		f.regs = &m_globalRegisters;
		for (std::size_t i = 0; i < def.params.size(); ++i)
		{
			locals.set(def.params[i].name, i < args.size() ? args[i] : AptValue());
		}
	}
	run(f, *def.body);
	m_lastStackDepth = f.stack.size();
	return f.returnValue;
}

AptValue AptActionInterpreter::construct(Frame &f, const std::string &className, const std::vector<AptValue> &args)
{
	AptValue ctor;
	if (!lookupVariable(f, className, ctor) || !ctor.isObject() || ctor.asObject()->kind() != AptObjectKind::Function)
	{
		reportError("NewObject: class '" + className + "' is not defined");
		return AptValue();
	}
	AptFunction *fn = static_cast<AptFunction *>(ctor.asObject());
	if (fn->isNative())
	{
		return callFunction(ctor, AptValue(), args, true);
	}
	AptObject *instance = newObject();
	AptValue protoValue;
	if (fetchMember(fn, "prototype", protoValue) && protoValue.isObject())
	{
		instance->setProto(protoValue.asObject());
	}
	AptValue result = callFunction(ctor, AptValue::object(instance), args, true);
	return result.isObject() ? result : AptValue::object(instance);
}

// ---- execution ------------------------------------------------------------------------------

bool AptActionInterpreter::execute(const AptCodeBlock &block, AptObject *target, AptObject *root)
{
	if (m_depth == 0)
	{
		m_executionStart = m_executed;
	}
	Frame f;
	f.thisObj = target;
	f.target = target;
	f.root = root;
	f.regs = &m_globalRegisters;
	f.swfVersion = block.swfVersion;
	++m_depth;
	bool ok = run(f, block);
	--m_depth;
	m_lastStackDepth = f.stack.size();
	return ok;
}

bool AptActionInterpreter::run(Frame &f, const AptCodeBlock &block)
{
	const std::size_t n = block.instructions.size();
	std::size_t pc = 0;

	auto fail = [&](const std::string &message) {
		if (!f.fault)
		{
			f.fault = true;
			++m_faults;
			reportError(message + whereIs(f.ins));
		}
	};
	auto pop = [&]() -> AptValue {
		if (f.stack.empty())
		{
			fail("value stack underflow");
			return AptValue();
		}
		AptValue v = std::move(f.stack.back());
		f.stack.pop_back();
		return v;
	};
	auto push = [&](AptValue v) {
		if (f.stack.size() >= kMaxValueStack)
		{
			fail("value stack overflow (limit " + std::to_string(kMaxValueStack) + " values)");
			return;
		}
		f.stack.push_back(std::move(v));
	};
	auto poolString = [&](std::int32_t idx, std::string &out) -> bool {
		if (!f.pool || idx < 0 || (std::size_t)idx >= f.pool->size())
		{
			reportError("constant pool index " + std::to_string(idx) + " is outside the active pool" + whereIs(f.ins));
			return false;
		}
		out = (*f.pool)[(std::size_t)idx].toString();
		return true;
	};
	// argument list: argc is on top of the stack, then argc values with arg0 nearest the top
	auto popArgs = [&](std::vector<AptValue> &args) -> bool {
		AptValue count = pop();
		if (f.fault)
		{
			return false;
		}
		std::int32_t c = count.toInteger();
		if (c < 0 || c > 1024)
		{
			fail("argument count " + std::to_string(c) + " is out of range");
			return false;
		}
		for (std::int32_t i = 0; i < c; ++i)
		{
			args.push_back(pop());
		}
		return !f.fault;
	};

	// a numeric conversion of the previous instruction that is not retail-exact reports here (S-017)
	auto drainNumericStop = [&]() {
		if (AptNumericStopPending())
		{
			reportError(AptTakeNumericStop() + whereIs(f.ins));
		}
	};
	while (pc < n && !f.fault && !f.returned)
	{
		drainNumericStop();
		if (++m_executed - m_executionStart > m_budget)
		{
			f.ins = &block.instructions[pc];
			fail("instruction budget of " + std::to_string(m_budget) + " exceeded (runaway script?)");
			break;
		}
		const AptInstruction &ins = block.instructions[pc];
		f.ins = &ins;
		++pc;
		switch (ins.opcode)
		{
			case APT_OP_END:
				pc = n;
				break;

			// ---- timeline -------------------------------------------------------------
			case APT_OP_NEXTFRAME:
			{
				AptTimelineRequest r;
				r.op = AptTimelineOp::NextFrame;
				timeline(f, r, "NextFrame");
				break;
			}
			case APT_OP_PLAY:
			{
				AptTimelineRequest r;
				r.op = AptTimelineOp::Play;
				timeline(f, r, "Play");
				break;
			}
			case APT_OP_STOP:
			{
				AptTimelineRequest r;
				r.op = AptTimelineOp::Stop;
				timeline(f, r, "Stop");
				break;
			}
			case APT_OP_GOTOFRAME:
			{
				AptTimelineRequest r;
				r.op = AptTimelineOp::GotoFrame;
				r.a = AptValue::integer(ins.intOperand);
				timeline(f, r, "GotoFrame");
				break;
			}
			case APT_OP_GOTOLABEL:
			{
				AptTimelineRequest r;
				r.op = AptTimelineOp::GotoLabel;
				r.a = AptValue::string(ins.text);
				timeline(f, r, "GotoLabel");
				break;
			}
			case APT_OP_GOTOFRAME2:
			{
				// Rva008CAC20GotoFrame2.cpp: the operand is the play flag, the frame (label or number) is popped
				AptTimelineRequest r;
				r.op = AptTimelineOp::GotoFrame2;
				r.a = pop();
				r.flag = (ins.intOperand & 1) != 0;
				if (!f.fault)
				{
					timeline(f, r, "GotoFrame2");
				}
				break;
			}
			case APT_OP_CLONESPRITE:
			{
				AptTimelineRequest r;
				r.op = AptTimelineOp::CloneSprite;
				r.c = pop();
				r.b = pop();
				r.a = pop();
				if (!f.fault)
				{
					timeline(f, r, "CloneSprite");
				}
				break;
			}
			case APT_OP_REMOVESPRITE:
			{
				AptTimelineRequest r;
				r.op = AptTimelineOp::RemoveSprite;
				r.a = pop();
				if (!f.fault)
				{
					timeline(f, r, "RemoveSprite");
				}
				break;
			}
			case APT_OP_GETPROPERTY:
			{
				// SWF spec ActionGetProperty: pop index, pop target path, push the property
				AptValue idx = pop();
				AptValue path = pop();
				if (f.fault)
				{
					break;
				}
				const char *name = AptPropertyName(idx.toInteger());
				AptObject *obj = resolveTarget(f, path);
				if (!name || !obj)
				{
					reportError(std::string("GetProperty on ") + (obj ? "an unknown property index" : "an unresolved target") + whereIs(f.ins));
					push(AptValue());
				}
				else
				{
					push(getMember(AptValue::object(obj), name));
				}
				break;
			}
			case APT_OP_SETPROPERTY:
			{
				AptValue value = pop();
				AptValue idx = pop();
				AptValue path = pop();
				if (f.fault)
				{
					break;
				}
				const char *name = AptPropertyName(idx.toInteger());
				AptObject *obj = resolveTarget(f, path);
				if (!name || !obj)
				{
					reportError(std::string("SetProperty on ") + (obj ? "an unknown property index" : "an unresolved target") + whereIs(f.ins));
				}
				else
				{
					storeMember(obj, name, value);
				}
				break;
			}

			// ---- host ----------------------------------------------------------------
			case APT_OP_TRACE:
			{
				AptValue v = pop();
				if (!f.fault)
				{
					m_host.trace(v.toString());
				}
				break;
			}
			case APT_OP_RANDOM:
			{
				// AptActionInterpreterRandom.cpp: undefined -> 0, else bfmeNext1221() % toInteger
				AptValue v = pop();
				if (f.fault)
				{
					break;
				}
				if (v.isUndefined())
				{
					push(AptValue::integer(0));
					break;
				}
				std::int32_t max = v.toInteger();
				if (max == 0)
				{
					reportError("Random with a zero range" + whereIs(f.ins));
					push(AptValue::integer(0));
				}
				else
				{
					push(AptValue::integer((std::int32_t)(m_host.random() % (std::uint32_t)max)));
				}
				break;
			}
			case APT_OP_GETURL:
				dispatchUrl(f, ins.text, ins.text2);
				break;
			case APT_OP_GETURL2:
			{
				AptValue target = pop();
				AptValue url = pop();
				if (!f.fault)
				{
					std::string u = url.toString();
					std::string tail = u.size() >= 4 ? u.substr(u.size() - 4) : std::string();
					for (char &c : tail)
					{
						c = (char)std::tolower((unsigned char)c);
					}
					if (tail == ".swf" && target.isObject() && target.asObject())
					{
						m_host.loadMovieInto(u.substr(0, u.size() - 4), target.asObject()); // the target is a clip value
					}
					else
					{
						dispatchUrl(f, u, target.toString());
					}
				}
				break;
			}

			// ---- arithmetic, comparison, conversion ---------------------------------------
			case APT_OP_SUBTRACT:
			case APT_OP_MULTIPLY:
			case APT_OP_DIVIDE:
			case APT_OP_MODULO:
			case APT_OP_ADD2:
			case APT_OP_LESS2:
			case APT_OP_GREATER:
			case APT_OP_EQUALS2:
			case APT_OP_BITAND:
			case APT_OP_BITRSHIFT:
			case APT_OP_STRINGEQUALS:
			case APT_OP_STRINGCONCAT:
			{
				AptValue top = pop();
				AptValue under = pop();
				if (f.fault)
				{
					break;
				}
				const std::uint32_t v = f.swfVersion;
				switch (ins.opcode)
				{
					case APT_OP_SUBTRACT: push(AptOps::subtract(under, top, v)); break;
					case APT_OP_MULTIPLY: push(AptOps::multiply(under, top, v)); break;
					case APT_OP_DIVIDE: push(AptOps::divide(under, top, v)); break;
					case APT_OP_MODULO: push(AptOps::modulo(under, top, v)); break;
					case APT_OP_ADD2: push(AptOps::add2(under, top, v)); break;
					case APT_OP_LESS2: push(AptOps::less2(under, top, v)); break;
					case APT_OP_GREATER: push(AptOps::greater(under, top, v)); break;
					case APT_OP_EQUALS2: push(AptOps::equals2(under, top, v)); break;
					case APT_OP_BITAND: push(AptOps::bitAnd(under, top, v)); break;
					case APT_OP_BITRSHIFT: push(AptOps::bitRShift(under, top, v)); break;
					case APT_OP_STRINGEQUALS: push(AptOps::stringEquals(under, top)); break;
					default: // StringConcat: Rva008C7C80.cpp, SWF 7 undefined operands read "undefined"
					{
						AptValue u = (v == 7 && under.isUndefined()) ? AptValue::string("undefined") : under;
						AptValue t = (v == 7 && top.isUndefined()) ? AptValue::string("undefined") : top;
						push(AptOps::stringConcat(u, t));
						break;
					}
				}
				break;
			}
			case APT_OP_NOT:
			{
				// BfmeRva008C7490SwapTop.cpp: AptBoolean::Create(!top->check())
				AptValue v = pop();
				if (!f.fault)
				{
					push(AptValue::boolean(!v.toBoolean(f.swfVersion)));
				}
				break;
			}
			case APT_OP_TOINTEGER:
			{
				AptValue v = pop();
				if (!f.fault)
				{
					push(AptOps::toIntegerOp(v, f.swfVersion));
				}
				break;
			}
			case APT_OP_TONUMBER:
			{
				AptValue v = pop();
				if (!f.fault)
				{
					push(AptOps::toNumberOp(v, f.swfVersion));
				}
				break;
			}
			case APT_OP_TOSTRING:
			{
				AptValue v = pop();
				if (!f.fault)
				{
					push(AptOps::toStringOp(v, f.swfVersion));
				}
				break;
			}
			case APT_OP_INCREMENT:
			{
				AptValue v = pop();
				if (!f.fault)
				{
					push(AptOps::increment(v, f.swfVersion));
				}
				break;
			}
			case APT_OP_DECREMENT:
			{
				AptValue v = pop();
				if (!f.fault)
				{
					push(AptOps::decrement(v, f.swfVersion));
				}
				break;
			}
			case APT_OP_TYPEOF:
			{
				AptValue v = pop();
				if (!f.fault)
				{
					push(AptValue::string(v.typeOf()));
				}
				break;
			}

			// ---- stack ----------------------------------------------------------------
			case APT_OP_POP:
				pop();
				break;
			case APT_OP_PUSHDUPLICATE:
				if (f.stack.empty())
				{
					fail("value stack underflow");
				}
				else
				{
					AptValue top = f.stack.back();
					push(top);
				}
				break;
			case APT_OP_EA_PUSHZERO:
				push(AptValue::integer(0));
				break;
			case APT_OP_EA_PUSHONE:
				push(AptValue::integer(1));
				break;
			case APT_OP_EA_PUSHTRUE:
				push(AptValue::boolean(true));
				break;
			case APT_OP_EA_PUSHFALSE:
				push(AptValue::boolean(false));
				break;
			case APT_OP_EA_PUSHNULL:
				push(AptValue()); // BFME2 0x00B05320 serves both 0x75 and 0x76
				break;
			case APT_OP_EA_PUSHUNDEFINED:
				push(AptValue());
				break;
			case APT_OP_EA_PUSHTHISVAR:
				push(f.thisObj ? AptValue::object(f.thisObj) : AptValue());
				break;
			case APT_OP_EA_PUSHGLOBALVAR:
				push(AptValue::object(m_global));
				break;
			case APT_OP_EA_PUSHBYTE:
			case APT_OP_EA_PUSHSHORT:
			case APT_OP_EA_PUSHLONG:
				push(AptValue::integer(ins.intOperand));
				break;
			case APT_OP_EA_PUSHFLOAT:
				push(AptValue::number(ins.floatOperand));
				break;
			case APT_OP_EA_PUSHSTRING:
				push(AptValue::string(ins.text));
				break;
			case APT_OP_EA_PUSHCONSTANTBYTE:
			{
				// Bfme5PushConstant8CB180.cpp: the u8 indexes the active constant pool
				if (!f.pool || ins.intOperand < 0 || (std::size_t)ins.intOperand >= f.pool->size())
				{
					reportError("constant pool index " + std::to_string(ins.intOperand) + " is outside the active pool" + whereIs(f.ins));
					push(AptValue());
				}
				else
				{
					push((*f.pool)[(std::size_t)ins.intOperand]);
				}
				break;
			}
			case APT_OP_CONSTANTPOOL:
			{
				auto pool = std::make_shared<std::vector<AptValue>>();
				for (const AptConstRef &c : ins.constants)
				{
					pool->push_back(constantToValue(f, c));
				}
				f.pool = pool;
				break;
			}
			case APT_OP_PUSHDATA:
				for (const AptConstRef &c : ins.constants)
				{
					push(constantToValue(f, c));
				}
				break;
			case APT_OP_SETREGISTER:
			{
				// AptActionInterpreterStoreRegister.cpp: the top of the stack is copied into the register
				if (f.stack.empty())
				{
					fail("value stack underflow");
					break;
				}
				if (ins.intOperand < 0 || !f.regs || (std::size_t)ins.intOperand >= f.regs->size())
				{
					fail("register " + std::to_string(ins.intOperand) + " is outside the register file");
					break;
				}
				(*f.regs)[(std::size_t)ins.intOperand] = f.stack.back();
				break;
			}

			// ---- variables and members -----------------------------------------------------
			case APT_OP_GETVARIABLE:
			{
				AptValue name = pop();
				if (f.fault)
				{
					break;
				}
				AptValue v;
				const std::string n = name.toString();
				if (!lookupVariable(f, n, v) && n.find_first_of(".:/") != std::string::npos)
				{
					// a path ("TabButtons.Units", "/clip:var"): the Flash player resolves it as a target path, then the variable after a ':' or the
					// last segment on the clip before it (lane END-2: TimeLine.apt's DoShowHideTabs reads eval("TabButtons." + Tabs[i]); a movie that
					// works on retail needs it)
					const size_t colon = n.rfind(':');
					if (colon != std::string::npos)
					{
						AptObject *o = resolveTarget(f, AptValue::string(n.substr(0, colon)));
						if (o)
						{
							fetchMember(o, n.substr(colon + 1), v);
						}
					}
					else if (AptObject *o = resolveTarget(f, AptValue::string(n)))
					{
						v = AptValue::object(o);
					}
					else
					{
						const size_t cut = n.find_last_of("./");
						AptObject *holder = cut == 0 ? f.root : resolveTarget(f, AptValue::string(n.substr(0, cut)));
						if (holder)
						{
							fetchMember(holder, n.substr(cut + 1), v);
						}
					}
				}
				push(v);
				break;
			}
			case APT_OP_SETVARIABLE:
			{
				AptValue value = pop();
				AptValue name = pop();
				if (!f.fault)
				{
					assignVariable(f, name.toString(), value);
				}
				break;
			}
			case APT_OP_EA_ZEROVAR:
			{
				AptValue name = pop();
				if (!f.fault)
				{
					assignVariable(f, name.toString(), AptValue::integer(0));
				}
				break;
			}
			case APT_OP_EA_GETSTRINGVAR:
			{
				AptValue v;
				lookupVariable(f, ins.text, v);
				push(v);
				break;
			}
			case APT_OP_EA_SETSTRINGVAR:
			{
				AptValue name = pop();
				if (!f.fault)
				{
					assignVariable(f, name.toString(), AptValue::string(ins.text));
				}
				break;
			}
			case APT_OP_EA_PUSHVALUEOFVAR:
			{
				std::string name;
				AptValue v;
				if (poolString(ins.intOperand, name))
				{
					lookupVariable(f, name, v);
				}
				push(v);
				break;
			}
			case APT_OP_DEFINELOCAL:
			{
				AptValue value = pop();
				AptValue name = pop();
				if (!f.fault)
				{
					defineLocal(f, name.toString(), value);
				}
				break;
			}
			case APT_OP_DEFINELOCAL2:
			{
				AptValue name = pop();
				if (f.fault)
				{
					break;
				}
				std::string key = name.toString();
				bool exists = f.locals ? f.locals->has(key) : (f.target && memberExists(f.target, key));
				if (!exists)
				{
					defineLocal(f, key, AptValue());
				}
				break;
			}
			case APT_OP_DELETE:
			{
				AptValue name = pop();
				AptValue obj = pop();
				if (f.fault)
				{
					break;
				}
				bool deleted = obj.isObject() && obj.asObject() && obj.asObject()->deleteOwn(name.toString());
				push(AptValue::boolean(deleted));
				break;
			}
			case APT_OP_DELETE2:
			{
				AptValue name = pop();
				if (f.fault)
				{
					break;
				}
				std::string key = name.toString();
				bool deleted = (f.locals && f.locals->erase(key)) || (f.target && f.target->deleteOwn(key)) || m_global->deleteOwn(key);
				push(AptValue::boolean(deleted));
				break;
			}
			case APT_OP_GETMEMBER:
			{
				AptValue name = pop();
				AptValue obj = pop();
				if (!f.fault)
				{
					push(getMember(obj, name.toString()));
				}
				break;
			}
			case APT_OP_EA_GETNAMEDMEMBER:
			{
				std::string name;
				AptValue obj = pop();
				if (f.fault)
				{
					break;
				}
				push(poolString(ins.intOperand, name) ? getMember(obj, name) : AptValue());
				break;
			}
			case APT_OP_EA_GETSTRINGMEMBER:
			{
				AptValue obj = pop();
				if (!f.fault)
				{
					push(getMember(obj, ins.text));
				}
				break;
			}
			case APT_OP_SETMEMBER:
			{
				AptValue value = pop();
				AptValue name = pop();
				AptValue obj = pop();
				if (!f.fault)
				{
					setMember(obj, name.toString(), value);
				}
				break;
			}
			case APT_OP_EA_SETSTRINGMEMBER:
			{
				AptValue name = pop();
				AptValue obj = pop();
				if (!f.fault)
				{
					setMember(obj, name.toString(), AptValue::string(ins.text));
				}
				break;
			}

			// ---- objects ------------------------------------------------------------------
			case APT_OP_INITARRAY:
			{
				// Rva8C8240 ArrayFromStack008C8240.cpp: element i is the value i below the top (after the count)
				AptValue count = pop();
				if (f.fault)
				{
					break;
				}
				std::int32_t c = count.toInteger();
				if (c < 0 || c > 65536)
				{
					fail("InitArray count " + std::to_string(c) + " is out of range");
					break;
				}
				AptArray *a = newArray();
				for (std::int32_t i = 0; i < c; ++i)
				{
					a->items.push_back(pop());
				}
				push(AptValue::object(a));
				break;
			}
			case APT_OP_INITOBJECT:
			{
				AptValue count = pop();
				if (f.fault)
				{
					break;
				}
				std::int32_t c = count.toInteger();
				if (c < 0 || c > 65536)
				{
					fail("InitObject count " + std::to_string(c) + " is out of range");
					break;
				}
				AptObject *o = newObject();
				for (std::int32_t i = 0; i < c; ++i)
				{
					AptValue value = pop();
					AptValue name = pop();
					if (f.fault)
					{
						break;
					}
					storeMember(o, name.toString(), value);
				}
				push(AptValue::object(o));
				break;
			}
			case APT_OP_NEWOBJECT:
			{
				// Rva8D0C60NewObject.cpp: pops the class name and the argument count; a failed
				// construction pushes the fallback (undefined)
				AptValue name = pop();
				std::vector<AptValue> args;
				if (f.fault || !popArgs(args))
				{
					break;
				}
				push(construct(f, name.toString(), args));
				break;
			}
			case APT_OP_ENUMERATE2:
			{
				AptValue obj = pop();
				if (f.fault)
				{
					break;
				}
				if (obj.isString())
				{
					// BFME2 0x00B001B3..0x00B001D4: a string operand is resolved as a variable name through 0x00AFFD80, the
					// same call (same argument shape) GetVariable makes at 0x00B01940; the result replaces the operand
					AptValue resolved;
					lookupVariable(f, obj.asString(), resolved);
					obj = resolved;
				}
				push(AptValue()); // terminator: the undefined singleton (BFME2 0x00B00214); the loop pops names until it meets it
				for (const std::string &memberName : enumerateNames(obj))
				{
					push(AptValue::string(memberName));
				}
				break;
			}

			// ---- functions ------------------------------------------------------------------
			case APT_OP_DEFINEFUNCTION:
			case APT_OP_DEFINEFUNCTION2:
			{
				AptFunction *fn = m_gc.create<AptFunction>();
				fn->setProto(m_functionProto);
				fn->def = ins.function;
				fn->pool = f.pool;
				fn->scopeTarget = f.target;
				fn->scopeRoot = f.root;
				fn->outerScope = f.scope;
				if (ins.function->name.empty())
				{
					push(AptValue::object(fn));
				}
				else if (f.locals)
				{
					f.locals->set(ins.function->name, AptValue::object(fn));
				}
				else if (f.target)
				{
					storeMember(f.target, ins.function->name, AptValue::object(fn));
				}
				else
				{
					storeMember(m_global, ins.function->name, AptValue::object(fn));
				}
				break;
			}
			case APT_OP_CALLFUNCTION:
			case APT_OP_EA_CALLFUNCPOP:
			{
				AptValue name = pop();
				std::vector<AptValue> args;
				if (f.fault || !popArgs(args))
				{
					break;
				}
				std::string fname = name.toString();
				AptValue fn;
				AptObject *holder = nullptr;
				AptValue result;
				if (lookupVariable(f, fname, fn, &holder) && isFunctionValue(fn))
				{
					result = callFunction(fn, holder ? AptValue::object(holder) : AptValue(), args);
				}
				else
				{
					reportCallWithoutFunction(fname, "call of undefined function '" + fname + "'" + whereIs(f.ins));
				}
				if (ins.opcode == APT_OP_CALLFUNCTION)
				{
					push(result);
				}
				break;
			}
			case APT_OP_EA_CALLNAMEDFUNCPOP:
			case APT_OP_EA_CALLNAMEDFUNC:
			{
				std::string fname;
				std::vector<AptValue> args;
				if (!popArgs(args))
				{
					break;
				}
				AptValue result;
				if (poolString(ins.intOperand, fname))
				{
					AptValue fn;
					AptObject *holder = nullptr;
					if (lookupVariable(f, fname, fn, &holder) && isFunctionValue(fn))
					{
						result = callFunction(fn, holder ? AptValue::object(holder) : AptValue(), args);
					}
					else
					{
						reportCallWithoutFunction(fname, "call of undefined function '" + fname + "'" + whereIs(f.ins));
					}
				}
				if (ins.opcode == APT_OP_EA_CALLNAMEDFUNC)
				{
					AptValue varName = pop();
					if (!f.fault)
					{
						defineLocal(f, varName.toString(), result);
					}
				}
				break;
			}
			case APT_OP_CALLMETHOD:
			case APT_OP_EA_CALLMETHODPOP:
			case APT_OP_EA_CALLMETHOD:
			{
				AptValue name = pop();
				AptValue obj = pop();
				std::vector<AptValue> args;
				if (f.fault || !popArgs(args))
				{
					break;
				}
				AptValue result;
				if (name.isUndefined() || (name.isString() && name.asString().empty()))
				{
					if (isFunctionValue(obj))
					{
						result = callFunction(obj, AptValue(), args); // the object is the function value
					}
					else
					{
						reportCallWithoutFunction(std::string(), std::string("call of a non-function value (") + obj.typeOf() + ")" + whereIs(f.ins)); // S-380
					}
				}
				else
				{
					result = callMethod(obj, name.toString(), args);
				}
				if (ins.opcode == APT_OP_CALLMETHOD)
				{
					push(result);
				}
				else if (ins.opcode == APT_OP_EA_CALLMETHOD)
				{
					AptValue varName = pop();
					if (!f.fault)
					{
						defineLocal(f, varName.toString(), result);
					}
				}
				break;
			}
			case APT_OP_EA_CALLNAMEDMETHODPOP:
			case APT_OP_EA_CALLNAMEDMETHOD:
			{
				AptValue obj = pop();
				std::vector<AptValue> args;
				if (f.fault || !popArgs(args))
				{
					break;
				}
				std::string mname;
				AptValue result;
				if (poolString(ins.intOperand, mname))
				{
					result = callMethod(obj, mname, args);
				}
				if (ins.opcode == APT_OP_EA_CALLNAMEDMETHOD)
				{
					AptValue varName = pop();
					if (!f.fault)
					{
						defineLocal(f, varName.toString(), result);
					}
				}
				break;
			}
			case APT_OP_RETURN:
				f.returnValue = f.stack.empty() ? AptValue() : pop();
				f.returned = true;
				break;

			// ---- branches -----------------------------------------------------------------
			case APT_OP_BRANCHALWAYS:
				pc = (std::size_t)ins.branchTarget;
				break;
			case APT_OP_BRANCHIFTRUE:
			{
				AptValue v = pop();
				if (!f.fault && v.toBoolean(f.swfVersion))
				{
					pc = (std::size_t)ins.branchTarget;
				}
				break;
			}

			default:
				fail("opcode has no interpreter handler");
				break;
		}
	}
	drainNumericStop();
	return !f.fault;
}
