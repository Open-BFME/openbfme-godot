"""Object-level field semantics: which RW parse function is which INI parser.

Every mapping below was read from the disassembly of the function (RW addresses, caveat S-001);
the evidence is the callee pattern: getNextToken = RW 0x42DC9F (arg 0 = default separators),
getNextTokenOrNull = 0x42DBF5, scanInt = 0x42E9D7, scanUnsignedInt = 0x42EA42, scanReal = 0x42EAAD,
scanBool = 0x42CE52, scanIndexList = 0x42B999, scanPercentToReal = 0x42EB18.

A function not listed here keeps the semantic "raw": the engine stores the line's tokens (or the
block's lines) verbatim and registers a stop. Nothing is guessed.
"""

# fn -> (semantic, evidence). Semantics name the engine's typed parser.
OBJECT_FN_SEMANTICS = {
    0x42E558: ("parseBool", "getNextToken; scanBool; store byte"),
    0x42E956: ("parseIndexList", "getNextToken; scanIndexList(userData names); store int"),
    0x42E976: ("parseByteSizedIndexList", "getNextToken; scanIndexList; 0..255 else throw 1; store byte"),
    0x42EB2A: ("parseByte", "scanInt; -128..127 else throw 1; store byte"),
    0x42EB75: ("parseUnsignedByte", "scanInt; 0..255 else throw 1; store byte"),
    0x42EC11: ("parseUnsignedShort", "scanInt; 0..65535 else throw 1; store word"),
    0x42EC5E: ("parseInt", "scanInt; store dword"),
    0x42ECB2: ("parseUnsignedIntMax", "scanUnsignedInt; when userData != 0 and value > userData throw 3 (message RW 0xBD42E0); store dword"),
    0x42ED00: ("parseReal", "scanReal; store float"),
    0x42EE15: ("parseAngleReal", "scanReal * RADS_PER_DEGREE (0xBD1900); store float"),
    0x42EE5E: ("parseAsciiString", "getNextAsciiString into the AsciiString"),
    0x42EED6: ("parseAsciiStringVector", "clear (0x42CA04) then append the tokens (0x42E59E)"),
    0x42EEFA: ("parsePercentToReal", "getNextToken with the percent separators; scanPercentToReal"),
    0x73A429: ("parseDurationUnsignedInt", "RW 0x73A429 (PLAN rule 2)"),
    0x73A4B6: ("parseVelocityReal", "scanReal * the float at RW 0xD9F61C (0.2f, the logic-frame global that INI::parseVelocityReal multiplies by)"),
    0x42EF5A: ("parseBitString16", "RW 0x42E840 (the bit string parser, userData = name list); a bit above 15 throws the plain int 1; store word"),
    0x42F247: ("parseCoord3D", "getNextSubToken X / Y / Z, scanReal"),
    0x42F13E: ("parseColorInt", "R: G: B: [A:] tokens, scanInt 0..255, packed ARGB"),
    # module and module-editing rows (spec 4.4, 4.7)
    0x73F24D: ("module", "ThingTemplate::parseModuleName; userData = module type"),
    0x73BEC3: ("AddModule", "mode := ADD_REMOVE_REPLACE; initFromINI(template, object table 0xDA3DF8)"),
    0x73BF17: ("InheritableModule", "mode := INHERITABLE; initFromINI(template, object table)"),
    0x73E716: ("RemoveModule", "RW 0x73E716"),
    0x73E7ED: ("ReplaceModule", "RW 0x73E7ED"),
    # blocks with template-level behaviour on top of their raw body
    0x73FAC4: ("ArmorSet", "clears the sets once after a copy (flag +0x5F9), then appends"),
    0x73ED19: ("WeaponSet", "clears the sets once after a copy (flag +0x5FA), then appends"),
    0x73F5B1: ("UnitSpecificFX", "clears the map (0x6D0F38), then parses"),
    0x73F684: ("UnitSpecificSounds", "clears the map (0x656FA6), then parses"),
    0x74057B: ("Prerequisites", "load type 2 clears (0x74053D), then parses into the vector"),
    0x73BF6B: ("LocomotorSet", "parses one LocomotorSet (0x5E9B90) and hands it on (0x5E9AB1)"),
}

# parse functions whose userData is a NULL-terminated array of name strings
NAME_LIST_SEMANTICS = {"parseIndexList", "parseByteSizedIndexList", "parseBitString16"}

# Conditional block openers, reviewed by hand (see rwtables.Tables.conditional_exits).
#   NULL_GUARDED: the only early return is `if (instance == NULL) return;` (the data pointer is
#       NULL only when a module is built without an INI), so the block is always opened.
#   CONDITIONAL: the block really is opened only for some first tokens.
NULL_GUARDED = {
    0x4C7E42: "AttachModel: returns at once when arg 2 ([ebp+0xC]) is NULL (RW 0x4C7E59)",
    0x8CF47D: "SoundState: returns at once when arg 2 is NULL (RW 0x8CF493)",
    0x8CFF8F: "SoundUpgrade: returns at once when arg 2 is NULL (RW 0x8CFFA5)",
}
CONDITIONAL = {
    0x73B723: {
        "opens": "unlessFloat",
        "evidence": "RW 0x73B723: sscanf(firstToken, \"%f\") (format 0xBD423C); a result of 1 stores a constant and returns, otherwise the function falls into the keyframe block (initFromINI at 0x90B191)",
    },
    0x8B618C: {
        "opens": "firstTokenIs",
        "value": "override",
        "evidence": "RW 0x8B618C: stricmp(firstToken, \"override\" 0xC6D364) == 0 sets the flag that makes it run the block (0x8E0D47 -> initFromINI, table 0xC77FD0)",
    },
}

# Dispatching blocks: the first token selects the table set through an index list.
DISPATCH = {
    0x86C30A: {
        "evidence": "RW 0x86C30A (MeleeBehavior): scanIndexList(token, 0xDAEB2C) picks one of four melee behavior objects whose vtable slot 2 (+8) is its buildFieldParse; the table is then parsed with initFromINIMulti (0x86C3D5)",
        "names_va": 0xDAEB2C,
        "vtables": [0xC5AE38, 0xC8707C, 0xC5AE2C, 0xC872C8],
    },
}
