// OpenBFME unit tests. GPL-3.0.
// Lane FACT-A: the templates a faction's player owns right after a real skirmish start (see FactAStart.cpp).

#pragma once

#include <string>
#include <vector>

// in the order the started game lists them; empty when the install variables are unset
std::vector<std::string> factaStartTemplates(const std::string &faction);
