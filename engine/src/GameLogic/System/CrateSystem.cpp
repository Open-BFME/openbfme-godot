#include "GameLogic/System/CrateSystem.h"
#include "Common/AsciiString.h"
#include "Common/INIException.h"
#include "Common/Xfer.h"
#include "GameLogic/SimMath.h"
#include <cctype>
CrateSystem *TheCrateSystem = nullptr;
void CrateSystem::reset(){ m_templates.clear(); }
void CrateSystem::parseCrateData(INI *ini){
    std::string name = ini->getNextToken();
    CrateTemplate *tmpl = nullptr;
    for (auto &t : m_templates) if (AsciiStringUtil::compareNoCase(t.name, name)==0) { tmpl = &t; break; }
    if (!tmpl) {
        const CrateTemplate *def = findTemplate("DefaultCrate");
        if (def) { m_templates.push_back(*def); tmpl = &m_templates.back(); tmpl->name = name; }
        else { m_templates.emplace_back(); tmpl = &m_templates.back(); tmpl->name = name; }
    }
    struct Parsers {
        static void creationChance(INI *ini2, void *instance, void*, const void*){ INI::parseReal(ini2, nullptr, &static_cast<CrateTemplate*>(instance)->creationChance, nullptr); }
        static void veterancy(INI *ini2, void *instance, void*, const void*){
            std::string tok = ini2->getNextToken();
            int v = 5;
            if (AsciiStringUtil::compareNoCase(tok,"ELITE")==0) v=3;
            else if (AsciiStringUtil::compareNoCase(tok,"HEROIC")==0) v=4;
            else if (AsciiStringUtil::compareNoCase(tok,"VETERAN")==0) v=1;
            else try { v=std::stoi(tok); } catch(...){}
            static_cast<CrateTemplate*>(instance)->veterancyLevel = v;
        }
        static void killedBy(INI *ini2, void *instance, void*, const void*){
            std::string tok = ini2->getNextToken();
            static_cast<CrateTemplate*>(instance)->killedByType = tok;
        }
        static void crateObj(INI *ini2, void *instance, void*, const void*){
            CrateTemplate *t = static_cast<CrateTemplate*>(instance);
            const char *tok1 = ini2->getNextTokenOrNull();
            const char *tok2 = ini2->getNextTokenOrNull();
            if (!tok1 || !tok2) throw INIException(3, "CrateObject requires name and chance");
            float chance = 0.0f;
            try { chance = std::stof(tok2); } catch(...) { throw INIException(3, "CrateObject chance parse failed"); }
            t->crates.emplace_back(tok1, chance);
        }
        static void killerScience(INI *ini2, void *instance, void*, const void*){
            std::string tok = ini2->getNextToken();
            int id = -1;
            if (AsciiStringUtil::compareNoCase(tok,"None")!=0) id = (int)std::hash<std::string>{}(tok) & 0x7fffffff;
            static_cast<CrateTemplate*>(instance)->killerScience = id;
        }
        static void owned(INI *ini2, void *instance, void*, const void*){ INI::parseBool(ini2, nullptr, &static_cast<CrateTemplate*>(instance)->ownedByMaker, nullptr); }
    };
    const FieldParse table[] = {
        { "CreationChance", Parsers::creationChance, nullptr, 0 },
        { "VeterancyLevel", Parsers::veterancy, nullptr, 0 },
        { "KilledByType", Parsers::killedBy, nullptr, 0 },
        { "CrateObject", Parsers::crateObj, nullptr, 0 },
        { "KillerScience", Parsers::killerScience, nullptr, 0 },
        { "OwnedByMaker", Parsers::owned, nullptr, 0 },
        { nullptr, nullptr, nullptr, 0 }
    };
    ini->initFromINI(tmpl, table);
}
const CrateTemplate *CrateSystem::findTemplate(const std::string &name) const {
    for (auto &t : m_templates) if (AsciiStringUtil::compareNoCase(t.name, name)==0) return &t;
    return nullptr;
}
bool CrateSystem::testCreationChance(const std::string &crateDataName, float draw) const {
    const CrateTemplate *t = findTemplate(crateDataName);
    if (!t) return false;
    return t->creationChance > draw;
}
std::string CrateSystem::rollCrate(const std::string &crateDataName, float random01) const {
    const CrateTemplate *t = findTemplate(crateDataName);
    if (!t || t->crates.empty()) return "";
    float sum = 0.0f;
    for (auto &p : t->crates) {
        sum = SimMath::addf32(sum, p.second);
        if (sum > random01) return p.first;
    }
    return t->crates.back().first;
}
bool CrateSystem::testKilledByType(const std::string &crateDataName, const std::string &killerKind) const {
    const CrateTemplate *t = findTemplate(crateDataName);
    if (!t) return false;
    if (t->killedByType.empty()) return true;
    return AsciiStringUtil::compareNoCase(t->killedByType, killerKind)==0 || t->killedByType.find(killerKind)!=std::string::npos;
}
void CrateSystem::crc(StateHasher &h) const {
    h.addU32((std::uint32_t)m_templates.size());
    for (auto &t : m_templates){ h.addString(t.name); h.addFloat(t.creationChance); h.addI32(t.veterancyLevel); h.addString(t.killedByType); h.addI32(t.killerScience); h.addBool(t.ownedByMaker); h.addU32((std::uint32_t)t.crates.size()); for(auto &p:t.crates){h.addString(p.first); h.addFloat(p.second);} }
}
void CrateSystem::xfer(Xfer &x){
    XferVersion v{1,1}; x.xferVersion(v);
    x.xferVectorSize(m_templates, 32);
    for (auto &tm : m_templates){
        x.xferAsciiString(&tm.name);
        x.xferReal(&tm.creationChance);
        x.xferInt(&tm.veterancyLevel);
        x.xferAsciiString(&tm.killedByType);
        x.xferInt(&tm.killerScience);
        x.xferBool(&tm.ownedByMaker);
        x.xferVectorSize(tm.crates, 16);
        for (auto &p : tm.crates){ x.xferAsciiString(&p.first); x.xferReal(&p.second); }
    }
}
void CrateSystem::registerBlocks(INIBlockRegistry &registry, CrateSystem &cs){
    registry.registerBlock("CrateData", [&cs](INI *ini){ cs.parseCrateData(ini); });
}
