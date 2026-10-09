// OpenBFME. GPL-3.0.
// Test driver: runs the engine's Apt value operations (engine/src/Libraries/Source/Apt/AptValue.cpp) on request
// lines from stdin and prints the result. tools/retail_oracle/test_retail_oracle.py runs the SAME operands through
// the real BFME2 handlers (apt_oracle.py) and compares the two.
//
// Request:  <op> <swf version> <operand>...        (operands: under first, then top)
// Operand:  I:<decimal int32>   F:<hex float32 bits>   B:<0|1>   U (undefined)   S:<hex of the text bytes>
// Ops:      equals2 add2 subtract multiply divide modulo less2 greater   (two operands)
//           increment decrement tonumber tointeger s017                    (one operand; s017: did atof report the S-017 stop?)
//           boolean                                                       (zero or one operand)
// Result:   I:<n>  F:<bits>  B:<0|1>  U  S:<hex>   or  D:<hex float64 bits> (tonumber: the WIDE value) / I:<n> (tointeger)
#include "Libraries/Source/Apt/AptValue.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

bool parseOperand(const std::string &tok, AptValue &out)
{
	if (tok == "U")
	{
		out = AptValue::undefined();
		return true;
	}
	if (tok.size() < 2 || tok[1] != ':')
	{
		return false;
	}
	const std::string body = tok.substr(2);
	switch (tok[0])
	{
		case 'I':
			out = AptValue::integer((std::int32_t)std::strtol(body.c_str(), nullptr, 10));
			return true;
		case 'F':
		{
			std::uint32_t bits = (std::uint32_t)std::strtoul(body.c_str(), nullptr, 16);
			float f;
			std::memcpy(&f, &bits, sizeof(f));
			out = AptValue::number(f);
			return true;
		}
		case 'B':
			out = AptValue::boolean(body == "1");
			return true;
		case 'S':
		{
			std::string text;
			for (std::size_t i = 0; i + 1 < body.size(); i += 2)
			{
				text.push_back((char)std::strtol(body.substr(i, 2).c_str(), nullptr, 16));
			}
			out = AptValue::string(text);
			return true;
		}
	}
	return false;
}

std::string format(const AptValue &v)
{
	char buf[64];
	switch (v.type())
	{
		case AptValueType::Undefined:
			return "U";
		case AptValueType::Boolean:
			return v.asBool() ? "B:1" : "B:0";
		case AptValueType::Integer:
			std::snprintf(buf, sizeof(buf), "I:%d", v.asInteger());
			return buf;
		case AptValueType::Float:
		{
			float f = v.asFloat();
			std::uint32_t bits;
			std::memcpy(&bits, &f, sizeof(bits));
			std::snprintf(buf, sizeof(buf), "F:%08x", bits);
			return buf;
		}
		case AptValueType::String:
		{
			std::string out = "S:";
			for (unsigned char c : v.asString())
			{
				std::snprintf(buf, sizeof(buf), "%02x", c);
				out += buf;
			}
			return out;
		}
		default:
			return "?";
	}
}

} // namespace

int main()
{
	std::string line;
	while (std::getline(std::cin, line))
	{
		std::istringstream in(line);
		std::string op;
		unsigned swf = 7;
		in >> op >> swf;
		std::vector<AptValue> args;
		std::string tok;
		bool ok = true;
		while (in >> tok)
		{
			AptValue v;
			if (!parseOperand(tok, v))
			{
				ok = false;
				break;
			}
			args.push_back(v);
		}
		std::string result = "ERR";
		if (ok)
		{
			if (op == "boolean")
			{
				result = format(AptOps::booleanNative(args.empty() ? nullptr : &args[0]));
			}
			else if (args.size() == 2)
			{
				const AptValue &under = args[0];
				const AptValue &top = args[1];
				if (op == "equals2") result = format(AptOps::equals2(under, top, swf));
				else if (op == "add2") result = format(AptOps::add2(under, top, swf));
				else if (op == "subtract") result = format(AptOps::subtract(under, top, swf));
				else if (op == "multiply") result = format(AptOps::multiply(under, top, swf));
				else if (op == "divide") result = format(AptOps::divide(under, top, swf));
				else if (op == "modulo") result = format(AptOps::modulo(under, top, swf));
				else if (op == "less2") result = format(AptOps::less2(under, top, swf));
				else if (op == "greater") result = format(AptOps::greater(under, top, swf));
			}
			else if (args.size() == 1)
			{
				const AptValue &v = args[0];
				if (op == "increment") result = format(AptOps::increment(v, swf));
				else if (op == "decrement") result = format(AptOps::decrement(v, swf));
				else if (op == "s017")
				{
					// 1 when the conversion of this string reported the S-017 stop (decided from the text, not the rounded result)
					AptTakeNumericStop();
					(void)v.toNumberWide();
					result = AptTakeNumericStop().empty() ? "B:0" : "B:1";
				}
				else if (op == "tointeger")
				{
					char buf[32];
					std::snprintf(buf, sizeof(buf), "I:%d", v.toInteger());
					result = buf;
				}
				else if (op == "tonumber")
				{
					double d = v.toNumberWide();
					std::uint64_t bits;
					std::memcpy(&bits, &d, sizeof(bits));
					char buf[40];
					std::snprintf(buf, sizeof(buf), "D:%016llx", (unsigned long long)bits);
					result = buf;
				}
			}
		}
		std::printf("%s\n", result.c_str());
		std::fflush(stdout);
	}
	return 0;
}
