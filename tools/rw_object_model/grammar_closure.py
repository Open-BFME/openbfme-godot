"""Transitive closure of the tables reachable from a set of root tables (see rwtables.py)."""
from __future__ import annotations

from rwtables import Tables


def hx(v):
    return f"0x{v:x}"


def closure(img, roots):
    """roots: [(tableVA, extra)]. Returns (tables, functions):
    tables    {id: {"rows": [(name, fn, user, offset)], "terminated": bool, "catchAll": fn|None}}
    functions {fn: info from Tables.fn_info with its tables rewritten to ids}"""
    T = Tables(img)
    tables: dict[str, dict] = {}
    functions: dict[int, dict] = {}
    work = []

    def reg_static(va, origin=None):
        tid = hx(va)
        if tid not in tables:
            patches = T._patches_for(va, origin) if origin else {}
            rows, term = T.read_table(va, patches)
            tables[tid] = {"rows": rows, "terminated": term is not None, "catchAll": term[0] if term and term[0] else None, "va": va}
            if patches:
                tables[tid]["lazy"] = True
            work.append(("rows", tid))
        return tid

    def reg_inline(entry, fn, idx):
        tid = f"stack-{fn:x}-{idx}"
        if tid not in tables:
            tables[tid] = {"rows": entry["rows"], "terminated": True, "catchAll": None, "stack": True}
            work.append(("rows", tid))
        return tid

    for root in roots:
        reg_static(root[0], root[2] if len(root) > 2 else None)
    while work:
        kind, tid = work.pop()
        if kind != "rows":
            continue
        for (nm, f, u, o) in tables[tid]["rows"]:
            if f in functions:
                continue
            info = dict(T.fn_info(f))
            if info["kind"] == "block":
                ts = []
                for k, e in enumerate(info["tables"]):
                    if "va" in e and "rows" not in e:
                        ts.append({"table": reg_static(e["va"], e.get("origin")), "extra": e["extra"]})
                    else:
                        ts.append({"table": reg_inline(e, f, k), "extra": e["extra"]})
                info["tables"] = ts
            functions[f] = info
        ca = tables[tid]["catchAll"]
        if ca and ca not in functions:
            functions[ca] = dict(T.fn_info(ca))
            info = functions[ca]
            if info["kind"] == "block":
                ts = []
                for k, e in enumerate(info["tables"]):
                    ts.append({"table": reg_static(e["va"], e.get("origin")) if "rows" not in e else reg_inline(e, ca, k), "extra": e["extra"]})
                info["tables"] = ts
    return tables, functions
