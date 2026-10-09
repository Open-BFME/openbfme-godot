// OpenBFME. GPL-3.0.
//
// Core natives of the EA Apt runtime: Object, Array, String methods and Math.
//
// Sources (BFME1 decomp game/Libraries/Source/EA/Apt/):
//   Math.*     aptMath{Sin,Cos,Tan,Asin,Acos,Atan,Atan2,Abs,Ceil,Floor,Exp,Log,Sqrt,Round}.cpp, aptMax.cpp,
//              aptMin.cpp, Rva008A5160MathPow.cpp, Rva008A51B0MathRandom.cpp.  A native receives
//              (self, argc) and reads its arguments from the global argument array with the FIRST argument
//              on top (g_bfmeArr1233[count-1]); fewer arguments than required returns the fallback value
//              (undefined).  Results are boxed with MakeFloat (float) except round and abs, which return
//              integers (round: (int)(v +/- g_bfmeK1253, taken as 0.5); abs: toInteger then abs).
//              The functions named aptMax/aptMin in the ledger compute min/max respectively (the file
//              bodies: `pick = (b > a) ? a : b` is the smaller); Math.max/min here follow behaviour.
//   String     StringProperties008AB0E0.cpp (length = UTF-8 code point count; 12 cached method callbacks),
//              Rva008A9E20 (charAt), Rva008A9F70 (charCodeAt: the decimal code point as a STRING),
//              FindStringValue008A9C30 (indexOf: getName of the needle, optional start clamped at 0),
//              SliceValue008AA420, SubstringValue008AAB20 (substr), SubstringBoundsValue008AACF0 (substring).
//              split / lastIndexOf / concat / toLowerCase / toUpperCase: standard ECMA-262 behaviour (their
//              EA bodies were not read).
//   Array      ArrayProperties008BA0B0.cpp (length plus 11 method callbacks), JoinArray008B94F0.cpp,
//              AppendStackArray008B96A0.cpp, SpliceArray008B9A40.cpp, ConcatArray008B9CC0.cpp,
//              SliceArray008B9F30.cpp: standard ECMA-262 semantics.

#include "Libraries/Source/Apt/AptActionInterpreter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{

const AptValue &arg(const AptCallInfo &c, std::size_t i)
{
	static const AptValue undef;
	return i < c.args.size() ? c.args[i] : undef;
}

// ---- UTF-8 helpers (EAStringC code point indexing) ---------------------------------------------

// Splits `s` into UTF-8 code points.  Malformed text is a reported script error (never a partial read).
bool splitCodepoints(AptCallInfo &c, const char *method, const std::string &s, std::vector<std::string> &out)
{
	std::string why;
	if (!AptUtf8Validate(s, why))
	{
		c.vm.reportError(std::string("String.") + method + " on malformed UTF-8 text: " + why);
		return false;
	}
	std::size_t i = 0;
	while (i < s.size())
	{
		unsigned char lead = (unsigned char)s[i];
		std::size_t len = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
		out.push_back(s.substr(i, len));
		i += len;
	}
	return true;
}

std::string joinRange(const std::vector<std::string> &cps, std::size_t from, std::size_t to)
{
	std::string out;
	for (std::size_t i = from; i < to && i < cps.size(); ++i)
	{
		out += cps[i];
	}
	return out;
}

int codepointValue(const std::string &cp)
{
	// `cp` is one validated sequence from splitCodepoints: its length matches its lead byte
	unsigned char c = (unsigned char)cp[0];
	if (c < 0x80)
	{
		return c;
	}
	if (cp.size() == 2)
	{
		return ((c & 0x1f) << 6) | (cp[1] & 0x3f);
	}
	if (cp.size() == 3)
	{
		return ((c & 0x0f) << 12) | ((cp[1] & 0x3f) << 6) | (cp[2] & 0x3f);
	}
	return ((c & 7) << 18) | ((cp[1] & 0x3f) << 12) | ((cp[2] & 0x3f) << 6) | (cp[3] & 0x3f);
}

std::string thisString(const AptCallInfo &c)
{
	return c.thisValue.toString(); // value->getName(&text)
}

// ---- Math ----------------------------------------------------------------------------------------

typedef double (*Unary)(double);

void defineUnaryMath(AptActionInterpreter &vm, AptObject *math, const char *name, Unary fn)
{
	vm.defineNative(math, name, [fn](AptCallInfo &c) -> AptValue {
		if (c.args.empty())
		{
			return AptValue(); // argc < 1 -> g_bfmeFallbackDB
		}
		return AptValue::number((float)fn((double)arg(c, 0).toNumber()));
	});
}

// ---- Array ---------------------------------------------------------------------------------------

AptArray *thisArray(const AptCallInfo &c)
{
	if (c.thisValue.isObject() && c.thisValue.asObject() && c.thisValue.asObject()->kind() == AptObjectKind::Array)
	{
		return static_cast<AptArray *>(c.thisValue.asObject());
	}
	return nullptr;
}

// Resource bound (AptArray::kMaxLength): an array native that would grow past it reports and changes nothing.
bool arrayRoom(AptCallInfo &c, const char *method, std::size_t current, std::size_t extra)
{
	if (current + extra > AptArray::kMaxLength)
	{
		c.vm.reportError(std::string("Array.") + method + " would grow the array to " + std::to_string(current + extra) +
			" elements, past the resource bound of " + std::to_string(AptArray::kMaxLength));
		return false;
	}
	return true;
}

std::int64_t relativeIndex(const AptValue &v, std::int64_t length, std::int64_t dflt)
{
	if (v.isUndefined())
	{
		return dflt;
	}
	std::int64_t i = v.toInteger();
	if (i < 0)
	{
		i += length;
	}
	return std::max<std::int64_t>(0, std::min<std::int64_t>(i, length));
}

void sortValues(AptActionInterpreter &vm, std::vector<AptValue> &items, const AptValue &comparator)
{
	// stable merge sort; the comparator is a script function returning a number
	auto less = [&](const AptValue &a, const AptValue &b) -> bool {
		if (comparator.isObject())
		{
			AptValue r = vm.callFunction(comparator, AptValue(), { a, b });
			return r.toNumber() < 0;
		}
		return std::strcmp(a.toString().c_str(), b.toString().c_str()) < 0; // default: string order
	};
	std::vector<AptValue> tmp(items.size());
	for (std::size_t width = 1; width < items.size(); width *= 2)
	{
		for (std::size_t lo = 0; lo < items.size(); lo += 2 * width)
		{
			std::size_t mid = std::min(lo + width, items.size());
			std::size_t hi = std::min(lo + 2 * width, items.size());
			std::size_t i = lo, j = mid, k = lo;
			while (i < mid && j < hi)
			{
				tmp[k++] = less(items[j], items[i]) ? items[j++] : items[i++];
			}
			while (i < mid)
			{
				tmp[k++] = items[i++];
			}
			while (j < hi)
			{
				tmp[k++] = items[j++];
			}
		}
		items.swap(tmp);
	}
}

} // namespace

void aptInstallCoreNatives(AptActionInterpreter &vm)
{
	AptObject *global = vm.global();

	// ---- Object ---------------------------------------------------------------------------------
	{
		AptFunction *ctor = vm.defineGlobalNative("Object", [](AptCallInfo &c) -> AptValue { return AptValue::object(c.vm.newObject()); });
		ctor->setMember("prototype", AptValue::object(vm.objectPrototype()));
		vm.objectPrototype()->setNativeMember("constructor", AptValue::object(ctor));
		vm.defineNative(vm.objectPrototype(), "toString", [](AptCallInfo &c) -> AptValue { return AptValue::string(c.thisValue.toString()); });
	}

	// ---- Boolean --------------------------------------------------------------------------------
	// Target: the clean BFME2 1.06 native at 0x00AFF850 (a global function; the name's string-pool id is 0x1D at
	// 0x00DDD358).  Donor: BFME1 retail 0x008C6AD0 (decompile attempt BooleanCoerce008C6AD0), the same flow.
	//   argc == 0                      -> the undefined singleton (0x00AFF880)
	//   type 12..19 (objects)          -> true (0x00AFF8C5 via 0x00ADC580)
	//   the undefined singleton        -> false (0x00AFF8E1)
	//   not integer/float and isNonNumeric (0x00AFC370: strings that are not numbers, booleans, extern) -> false
	//   otherwise toNumber(v) compared with 0.0f (0x00BBAEAC) by fucompp: equal -> false, anything else including
	//   an unordered NaN -> true (0x00AFF90C..0x00AFF920)
	// So Boolean(true) is FALSE in retail: the isBoolean arm at 0x00AFF92E is only reached from integer and float
	// values.  This is the target's behaviour, ported as it is.
	{
		vm.defineGlobalNative("Boolean", [](AptCallInfo &c) -> AptValue {
			return AptOps::booleanNative(c.args.empty() ? nullptr : &c.args[0]);
		});
	}

	// ---- Array ----------------------------------------------------------------------------------
	{
		AptFunction *ctor = vm.defineGlobalNative("Array", [](AptCallInfo &c) -> AptValue {
			AptArray *a = c.vm.newArray();
			if (c.args.size() == 1 && c.args[0].isNumber())
			{
				std::int32_t n = c.args[0].toInteger();
				std::size_t want = n < 0 ? 0 : (std::size_t)n;
				if (!arrayRoom(c, "constructor", 0, want))
				{
					return AptValue();
				}
				a->items.resize(want);
			}
			else
			{
				a->items = c.args;
			}
			return AptValue::object(a);
		});
		ctor->setMember("prototype", AptValue::object(vm.arrayPrototype()));
		vm.arrayPrototype()->setNativeMember("constructor", AptValue::object(ctor));
		AptObject *proto = vm.arrayPrototype();
		vm.defineNative(proto, "push", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a)
			{
				return AptValue();
			}
			if (!arrayRoom(c, "push", a->items.size(), c.args.size()))
			{
				return AptValue();
			}
			for (const AptValue &v : c.args)
			{
				a->items.push_back(v);
			}
			return AptValue::integer((std::int32_t)a->items.size());
		});
		vm.defineNative(proto, "pop", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a || a->items.empty())
			{
				return AptValue();
			}
			AptValue v = a->items.back();
			a->items.pop_back();
			return v;
		});
		vm.defineNative(proto, "shift", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a || a->items.empty())
			{
				return AptValue();
			}
			AptValue v = a->items.front();
			a->items.erase(a->items.begin());
			return v;
		});
		vm.defineNative(proto, "unshift", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a)
			{
				return AptValue();
			}
			if (!arrayRoom(c, "unshift", a->items.size(), c.args.size()))
			{
				return AptValue();
			}
			a->items.insert(a->items.begin(), c.args.begin(), c.args.end());
			return AptValue::integer((std::int32_t)a->items.size());
		});
		AptFunction::Native join = [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a)
			{
				return AptValue();
			}
			std::string sep = arg(c, 0).isUndefined() ? std::string(",") : arg(c, 0).toString();
			std::string out;
			for (std::size_t i = 0; i < a->items.size(); ++i)
			{
				if (i)
				{
					out += sep;
				}
				out += a->items[i].toString();
			}
			return AptValue::string(out);
		};
		vm.defineNative(proto, "join", join);
		vm.defineNative(proto, "toString", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			return a ? AptValue::string(a->displayString()) : AptValue();
		});
		vm.defineNative(proto, "concat", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a)
			{
				return AptValue();
			}
			std::size_t total = a->items.size();
			for (const AptValue &v : c.args)
			{
				total += (v.isObject() && v.asObject() && v.asObject()->kind() == AptObjectKind::Array) ? static_cast<const AptArray *>(v.asObject())->items.size() : 1;
			}
			if (!arrayRoom(c, "concat", 0, total))
			{
				return AptValue();
			}
			AptArray *r = c.vm.newArray();
			r->items = a->items;
			for (const AptValue &v : c.args)
			{
				if (v.isObject() && v.asObject() && v.asObject()->kind() == AptObjectKind::Array) // type 22 arrays flatten
				{
					const AptArray *o = static_cast<const AptArray *>(v.asObject());
					r->items.insert(r->items.end(), o->items.begin(), o->items.end());
				}
				else
				{
					r->items.push_back(v);
				}
			}
			return AptValue::object(r);
		});
		vm.defineNative(proto, "slice", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a)
			{
				return AptValue();
			}
			std::int64_t len = (std::int64_t)a->items.size();
			std::int64_t from = relativeIndex(arg(c, 0), len, 0);
			std::int64_t to = relativeIndex(arg(c, 1), len, len);
			AptArray *r = c.vm.newArray();
			for (std::int64_t i = from; i < to; ++i)
			{
				r->items.push_back(a->items[(std::size_t)i]);
			}
			return AptValue::object(r);
		});
		vm.defineNative(proto, "splice", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (!a)
			{
				return AptValue();
			}
			std::int64_t len = (std::int64_t)a->items.size();
			std::int64_t start = relativeIndex(arg(c, 0), len, 0);
			std::int64_t count = c.args.size() < 2 ? len - start : std::max<std::int64_t>(0, std::min<std::int64_t>(arg(c, 1).toInteger(), len - start));
			if (c.args.size() > 2 && !arrayRoom(c, "splice", a->items.size() - (std::size_t)count, c.args.size() - 2))
			{
				return AptValue();
			}
			AptArray *removed = c.vm.newArray();
			removed->items.assign(a->items.begin() + start, a->items.begin() + start + count);
			a->items.erase(a->items.begin() + start, a->items.begin() + start + count);
			if (c.args.size() > 2)
			{
				a->items.insert(a->items.begin() + start, c.args.begin() + 2, c.args.end());
			}
			return AptValue::object(removed);
		});
		vm.defineNative(proto, "reverse", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (a)
			{
				std::reverse(a->items.begin(), a->items.end());
			}
			return c.thisValue;
		});
		vm.defineNative(proto, "sort", [](AptCallInfo &c) -> AptValue {
			AptArray *a = thisArray(c);
			if (a)
			{
				sortValues(c.vm, a->items, arg(c, 0));
			}
			return c.thisValue;
		});
	}

	// ---- String ---------------------------------------------------------------------------------
	{
		AptFunction *ctor = vm.defineGlobalNative("String", [](AptCallInfo &c) -> AptValue {
			return AptValue::string(c.args.empty() ? std::string() : c.args[0].toString());
		});
		ctor->setMember("prototype", AptValue::object(vm.stringPrototype()));
		AptObject *proto = vm.stringPrototype();
		vm.defineNative(proto, "charAt", [](AptCallInfo &c) -> AptValue {
			// Rva008A9E20: index < 0 or past the end -> fallback (undefined)
			std::vector<std::string> cps;
			if (!splitCodepoints(c, "charAt", thisString(c), cps))
			{
				return AptValue();
			}
			std::int32_t i = arg(c, 0).toInteger();
			if (i < 0 || (std::size_t)i >= cps.size())
			{
				return AptValue();
			}
			return AptValue::string(cps[(std::size_t)i]);
		});
		vm.defineNative(proto, "charCodeAt", [](AptCallInfo &c) -> AptValue {
			// Rva008A9F70: the code point as a decimal STRING
			std::vector<std::string> cps;
			if (!splitCodepoints(c, "charCodeAt", thisString(c), cps))
			{
				return AptValue();
			}
			std::int32_t i = arg(c, 0).toInteger();
			if (i < 0 || (std::size_t)i >= cps.size())
			{
				return AptValue();
			}
			return AptValue::string(std::to_string(codepointValue(cps[(std::size_t)i])));
		});
		vm.defineNative(proto, "indexOf", [](AptCallInfo &c) -> AptValue {
			// FindStringValue008A9C30: needle = getName(arg0), start = arg1 clamped at 0, no args -> undefined
			if (c.args.empty())
			{
				return AptValue();
			}
			std::string text = thisString(c);
			std::string needle = c.args[0].toString();
			std::int32_t start = c.args.size() >= 2 ? std::max(0, c.args[1].toInteger()) : 0;
			if ((std::size_t)start > text.size())
			{
				return AptValue::integer(-1);
			}
			std::size_t at = text.find(needle, (std::size_t)start);
			return AptValue::integer(at == std::string::npos ? -1 : (std::int32_t)at);
		});
		vm.defineNative(proto, "lastIndexOf", [](AptCallInfo &c) -> AptValue {
			if (c.args.empty())
			{
				return AptValue();
			}
			std::string text = thisString(c);
			std::size_t from = c.args.size() >= 2 ? (std::size_t)std::max(0, c.args[1].toInteger()) : std::string::npos;
			std::size_t at = text.rfind(c.args[0].toString(), from);
			return AptValue::integer(at == std::string::npos ? -1 : (std::int32_t)at);
		});
		vm.defineNative(proto, "slice", [](AptCallInfo &c) -> AptValue {
			// SliceValue008AA420 (arguments are start and END; no arguments -> undefined)
			if (c.args.empty())
			{
				return AptValue();
			}
			std::vector<std::string> cps;
			if (!splitCodepoints(c, "slice", thisString(c), cps))
			{
				return AptValue();
			}
			std::int32_t total = (std::int32_t)cps.size();
			std::int32_t start = c.args[0].toInteger();
			std::int32_t end = c.args.size() >= 2 ? c.args[1].toInteger() : 9999999;
			if (start < 0)
			{
				start += total;
			}
			if (end < 0)
			{
				end += total;
			}
			start = std::max(start, 0);
			end = std::max(end, 0);
			start = std::min(start, total);
			end = std::min(end, total);
			return AptValue::string(end > start ? joinRange(cps, (std::size_t)start, (std::size_t)end) : std::string());
		});
		vm.defineNative(proto, "substr", [](AptCallInfo &c) -> AptValue {
			// SubstringValue008AAB20: (start, length); negative start counts from the end; length defaults to 9999999
			if (c.args.empty())
			{
				return AptValue();
			}
			std::vector<std::string> cps;
			if (!splitCodepoints(c, "substr", thisString(c), cps))
			{
				return AptValue();
			}
			std::int32_t total = (std::int32_t)cps.size();
			std::int32_t start = c.args[0].toInteger();
			std::int32_t length = c.args.size() >= 2 ? c.args[1].toInteger() : 9999999;
			if (start < 0)
			{
				start += total;
			}
			start = std::max(start, 0);
			if (length <= 0 || start >= total)
			{
				return AptValue::string(std::string());
			}
			std::int64_t end = std::min<std::int64_t>((std::int64_t)start + length, total);
			return AptValue::string(joinRange(cps, (std::size_t)start, (std::size_t)end));
		});
		vm.defineNative(proto, "substring", [](AptCallInfo &c) -> AptValue {
			// SubstringBoundsValue008AACF0: (start, end), swapped when reversed, negatives clamp to 0
			if (c.args.empty())
			{
				return AptValue();
			}
			std::vector<std::string> cps;
			if (!splitCodepoints(c, "substring", thisString(c), cps))
			{
				return AptValue();
			}
			std::int32_t start = c.args[0].toInteger();
			std::int32_t end = c.args.size() >= 2 ? c.args[1].toInteger() : 9999999;
			if (start > end)
			{
				std::swap(start, end);
			}
			start = std::max(start, 0);
			end = std::max(end, 0);
			if (start > end)
			{
				std::swap(start, end);
			}
			return AptValue::string(joinRange(cps, (std::size_t)start, (std::size_t)end));
		});
		vm.defineNative(proto, "concat", [](AptCallInfo &c) -> AptValue {
			std::string out = thisString(c);
			for (const AptValue &v : c.args)
			{
				out += v.toString();
			}
			return AptValue::string(out);
		});
		vm.defineNative(proto, "split", [](AptCallInfo &c) -> AptValue {
			AptArray *r = c.vm.newArray();
			std::string text = thisString(c);
			if (c.args.empty() || c.args[0].isUndefined())
			{
				r->items.push_back(AptValue::string(text));
				return AptValue::object(r);
			}
			std::string sep = c.args[0].toString();
			std::int32_t limit = c.args.size() >= 2 ? c.args[1].toInteger() : INT32_MAX;
			if (sep.empty())
			{
				std::vector<std::string> cps;
				if (!splitCodepoints(c, "split", text, cps))
				{
					return AptValue();
				}
				for (const std::string &cp : cps)
				{
					if ((std::int32_t)r->items.size() >= limit)
					{
						break;
					}
					if (!arrayRoom(c, "split", r->items.size(), 1))
					{
						return AptValue();
					}
					r->items.push_back(AptValue::string(cp));
				}
				return AptValue::object(r);
			}
			std::size_t pos = 0;
			while (true)
			{
				std::size_t at = text.find(sep, pos);
				std::string piece = text.substr(pos, at == std::string::npos ? std::string::npos : at - pos);
				if ((std::int32_t)r->items.size() >= limit)
				{
					break;
				}
				if (!arrayRoom(c, "split", r->items.size(), 1))
				{
					return AptValue();
				}
				r->items.push_back(AptValue::string(piece));
				if (at == std::string::npos)
				{
					break;
				}
				pos = at + sep.size();
			}
			return AptValue::object(r);
		});
		vm.defineNative(proto, "toLowerCase", [](AptCallInfo &c) -> AptValue {
			std::string s = thisString(c);
			for (char &ch : s)
			{
				if (ch >= 'A' && ch <= 'Z')
				{
					ch = (char)(ch - 'A' + 'a');
				}
			}
			return AptValue::string(s);
		});
		vm.defineNative(proto, "toUpperCase", [](AptCallInfo &c) -> AptValue {
			std::string s = thisString(c);
			for (char &ch : s)
			{
				if (ch >= 'a' && ch <= 'z')
				{
					ch = (char)(ch - 'a' + 'A');
				}
			}
			return AptValue::string(s);
		});
	}

	// ---- Math -----------------------------------------------------------------------------------
	{
		AptObject *math = vm.newObject();
		global->setNativeMember("Math", AptValue::object(math));
		defineUnaryMath(vm, math, "sin", [](double x) { return std::sin(x); });
		defineUnaryMath(vm, math, "cos", [](double x) { return std::cos(x); });
		defineUnaryMath(vm, math, "tan", [](double x) { return std::tan(x); });
		defineUnaryMath(vm, math, "asin", [](double x) { return std::asin(x); });
		defineUnaryMath(vm, math, "acos", [](double x) { return std::acos(x); });
		defineUnaryMath(vm, math, "atan", [](double x) { return std::atan(x); });
		defineUnaryMath(vm, math, "ceil", [](double x) { return std::ceil(x); });
		defineUnaryMath(vm, math, "floor", [](double x) { return std::floor(x); });
		defineUnaryMath(vm, math, "exp", [](double x) { return std::exp(x); });
		defineUnaryMath(vm, math, "log", [](double x) { return std::log(x); });
		defineUnaryMath(vm, math, "sqrt", [](double x) { return std::sqrt(x); });
		vm.defineNative(math, "atan2", [](AptCallInfo &c) -> AptValue {
			if (c.args.size() < 2)
			{
				return AptValue();
			}
			float y = c.args[0].toNumber(); // top of the argument array = first argument
			float x = c.args[1].toNumber();
			return AptValue::number((float)std::atan2((double)y, (double)x));
		});
		vm.defineNative(math, "pow", [](AptCallInfo &c) -> AptValue {
			if (c.args.size() < 2)
			{
				return AptValue();
			}
			return AptValue::number((float)std::pow((double)c.args[0].toNumber(), (double)c.args[1].toNumber()));
		});
		vm.defineNative(math, "max", [](AptCallInfo &c) -> AptValue {
			if (c.args.size() < 2)
			{
				return AptValue();
			}
			float a = c.args[0].toNumber();
			float b = c.args[1].toNumber();
			return AptValue::number(b < a ? a : b); // the 0x008A4F00 body returns the larger
		});
		vm.defineNative(math, "min", [](AptCallInfo &c) -> AptValue {
			if (c.args.size() < 2)
			{
				return AptValue();
			}
			float a = c.args[0].toNumber();
			float b = c.args[1].toNumber();
			return AptValue::number(b > a ? a : b); // the 0x008A4EA0 body returns the smaller
		});
		vm.defineNative(math, "round", [](AptCallInfo &c) -> AptValue {
			if (c.args.empty())
			{
				return AptValue();
			}
			float v = c.args[0].toNumber();
			v = v > 0.0f ? v + 0.5f : v - 0.5f; // g_bfmeK1253 (0.5, UNVERIFIED value)
			return AptValue::integer(AptFloatToInt(v));
		});
		vm.defineNative(math, "abs", [](AptCallInfo &c) -> AptValue {
			if (c.args.empty())
			{
				return AptValue();
			}
			std::int32_t v = c.args[0].toInteger(); // aptMathAbs: abs(toInteger), an integer result
			return AptValue::integer(v < 0 ? (std::int32_t)(0u - (std::uint32_t)v) : v);
		});
		vm.defineNative(math, "random", [](AptCallInfo &c) -> AptValue {
			// aptMathRandom: (float)bfmeNext1221() * scale; the scale constant is not in the decomp (1/2^32 assumed)
			return AptValue::number((float)((double)c.vm.host().random() * (1.0 / 4294967296.0)));
		});
	}
}
