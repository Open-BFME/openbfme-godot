// OpenBFME. GPL-3.0.
//
// Apt value model and the EA coercion rules.
//
// Ported from the BFME1 decompile (Open-BFME-1, game/Libraries/Source/EA/Apt/), each function
// byte-matched against retail there:
//   AptValue_toNumber.cpp          AptValue::toNumber      (string -> atof, bool -> 1/0, int, float)
//   AptValueToInteger.cpp          AptValue::toInteger     ("0x" strings via strtol base 16, else atoi)
//   AptValue_rva00898480Check.cpp  AptValue truthiness     (SWF 7: non-empty string; else hex/atof)
//   Rva008985C0ValueString.cpp     Rva8CD130Value::getName (value -> string: "%d", "%f", "[function]" ...)
//   Rva008C8350TypeOf.cpp          ActionTypeOf            ("undefined", "number", "string", ...)
//   AddValues008C8500.cpp          Add2                    (string concat / int add / float add)
//   AptActionInterpreter{Subtract,Divide,Equals,LessThan,BitAnd}.cpp, Remainder008C8160.cpp,
//   Rva008C7BF0Action.cpp (ToInteger), LessThan008C8840.cpp (Less2)
// and, where BFME1 has no matched body (Equals2, ToNumber, Increment, Decrement, the 0x75/0x76 pushes),
// from the clean BFME2 1.06 game.dat disassembly (virtual addresses; dispatch table at 0x00DDC984 with
// 8-byte entries): Equals2 0x00B031E0, ToNumber 0x00B03730, Increment 0x00B03F40, Decrement 0x00B04020,
// PushNull and PushUndefined both 0x00B05320 (they push the same undefined singleton, pointer at
// 0x00E18078); helpers isNonNumeric 0x00AFC370, toInteger 0x00ADD360, toNumber 0x00ADD460.
//
// Numbers are single precision: the EA runtime stores floats as `float` and boxes every arithmetic
// result through MakeFloat (Rva008A4EA0MakeFloat).  The "fallback" value those routines return for
// undefined operands (SWF 7) and for division by zero is the undefined singleton (BFME1 0x013379BC;
// BFME2 0x00E18078).  There is no separate null value: opcodes 0x75 and 0x76 push the same singleton.
//
// The Apt movie version digit ("Apt Data:6/7") is the SWF version the EA code branches on
// (AptGetSwfVersion() == 7); callers pass the version of the code's owning movie.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

class AptObject;

enum class AptValueType : std::uint8_t
{
	Undefined,
	Boolean,
	Integer,
	Float,
	String,
	Object, // script/native objects, arrays, functions, clips: AptObject
	Extern  // the engine-provided `extern` object (EA value type 11)
};

class AptValue
{
public:
	AptValue() : m_obj(nullptr) {}

	static AptValue undefined() { return AptValue(); }
	static AptValue boolean(bool b);
	static AptValue integer(std::int32_t i);
	static AptValue number(float f);
	static AptValue string(std::string s);
	static AptValue object(AptObject *o);
	static AptValue externValue();

	AptValueType type() const { return m_type; }
	bool isUndefined() const { return m_type == AptValueType::Undefined; }
	bool isBoolean() const { return m_type == AptValueType::Boolean; }
	bool isInteger() const { return m_type == AptValueType::Integer; }
	bool isFloat() const { return m_type == AptValueType::Float; }
	bool isNumber() const { return m_type == AptValueType::Integer || m_type == AptValueType::Float; }
	bool isString() const { return m_type == AptValueType::String; }
	bool isObject() const { return m_type == AptValueType::Object; }
	bool isExtern() const { return m_type == AptValueType::Extern; }

	bool asBool() const { return m_bool; }
	std::int32_t asInteger() const { return m_int; }
	float asFloat() const { return m_float; }
	const std::string &asString() const;
	AptObject *asObject() const { return m_obj; }

	// ---- EA coercions ------------------------------------------------------------------
	float toNumber() const;
	// toNumber as the x87 register holds it before any store (BFME2 0x00ADD460): an exact integer, a string's
	// atof DOUBLE, a float.  Everything else equals toNumber().
	double toNumberWide() const;
	std::int32_t toInteger() const;
	bool toBoolean(std::uint32_t swfVersion) const;
	std::string toString() const; // getName
	const char *typeOf() const;

private:
	AptValueType m_type = AptValueType::Undefined;
	union
	{
		bool m_bool;
		std::int32_t m_int;
		float m_float;
		AptObject *m_obj;
	};
	std::shared_ptr<const std::string> m_str;
};

// ---- C library number parsing/formatting the EA runtime uses -----------------------------

// MSVC 7.1 atof on a decimal string (no hex, no inf/nan), narrowed to float.
float AptAtof(const std::string &text);
// The same conversion at the width retail keeps it: BFME2 0x00ADD460 returns a string's atof result in ST0 as a
// DOUBLE, which the arithmetic handlers use before any store to float.  AptAtof is this value narrowed to float.
double AptAtofDouble(const std::string &text);
// Acceptance stop S-017 (docs/STOPS.md): a conversion whose RESULT lies in the subnormal double range is not retail-exact
// (MSVCR71 atof mishandles exact powers of two there; observed through the retail oracle).  The conversion leaves a pending
// report that the interpreter drains into its error list; AptTakeNumericStop returns it and clears it.
bool AptNumericStopPending();
std::string AptTakeNumericStop();
// atoi: leading whitespace, sign, decimal digits; saturates like a 32-bit strtol.
std::int32_t AptAtoi(const std::string &text);
// strtol(text, 0, 16): optional whitespace, sign, optional 0x prefix, hex digits; saturates.
std::int32_t AptStrtolHex(const std::string &text);
// Float to int32 as the retail game does it: BFME2 0x00ADD400 calls 0x00A29228 (an _ftol2-style routine), which
// converts through a 64-bit integer (truncating toward zero) and returns its LOW 32 bits.  So 4294967296.0 gives 0,
// 3e9 wraps to -1294967296, and NaN, infinities and |f| >= 2^63 (the 64-bit "integer indefinite") give 0.
std::int32_t AptFloatToInt(float f);
// getName for a float: "%d" of (int)f when f has no fractional part, else "%f".
std::string AptFloatToString(float f);

// Structural UTF-8 check for the code point indexing natives: false (with `why`) for a truncated sequence, an
// invalid lead byte (0x80-0xBF, 0xF8-0xFF) or a missing continuation byte.  Overlong forms and surrogates are
// not rejected: the EA decoder (EAStringC code point walk) does not check them either.
bool AptUtf8Validate(const std::string &text, std::string &why);

// ---- Arithmetic, comparison and bit operations (EA semantics) ----------------------------
// `under` is the value below the top of stack, `top` the top: the result of "under OP top".
namespace AptOps
{
AptValue add2(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue subtract(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue multiply(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue divide(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue modulo(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue less2(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue greater(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue equals2(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue bitAnd(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue bitRShift(const AptValue &under, const AptValue &top, std::uint32_t swfVersion);
AptValue toIntegerOp(const AptValue &v, std::uint32_t swfVersion);
AptValue toNumberOp(const AptValue &v, std::uint32_t swfVersion);
AptValue toStringOp(const AptValue &v, std::uint32_t swfVersion);
AptValue increment(const AptValue &v, std::uint32_t swfVersion);
AptValue decrement(const AptValue &v, std::uint32_t swfVersion);
AptValue stringConcat(const AptValue &under, const AptValue &top);
AptValue stringEquals(const AptValue &under, const AptValue &top);
// BFME2 0x00AFC370: true when the value is not a usable number (non-numeric strings, empty strings,
// booleans, objects; undefined only in SWF 7).
bool isNonNumeric(const AptValue &v, std::uint32_t swfVersion);
// The global Boolean() native (BFME2 0x00AFF850): `arg` is the first argument, or null for none.
AptValue booleanNative(const AptValue *arg);
} // namespace AptOps
