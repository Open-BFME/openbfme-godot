// Ghidra headless post-script: export a function inventory, string xrefs and
// RTTI-labelled vtables from an analyzed program in one pass.
//
// Usage (after one analysis run, see tools/re/README.md):
//   analyzeHeadless <projDir> <projName> -process <program> -noanalysis \
//       -scriptPath tools/re/ghidra -postScript export_inventory.java <outPrefix>
//
// Writes:
//   <outPrefix>_functions.csv   rva,size,start_block_size,name
//       size             = total addresses in the function body (may be non-contiguous)
//       start_block_size = length of the contiguous range that starts at the entry point
//   <outPrefix>_string_xrefs.tsv  string_rva <TAB> string <TAB> referencing function rvas (comma list)
//   <outPrefix>_vtables.tsv       vtable_rva <TAB> class <TAB> slot <TAB> func_rva
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class export_inventory extends GhidraScript {
    private static String clean(String s) {
        return s.replace("\t", " ").replace("\n", "\\n").replace("\r", "\\r");
    }

    public void run() throws Exception {
        String prefix = getScriptArgs().length > 0 ? getScriptArgs()[0] : "inventory";
        long base = currentProgram.getImageBase().getOffset();
        FunctionManager fm = currentProgram.getFunctionManager();
        ReferenceManager rm = currentProgram.getReferenceManager();
        Memory mem = currentProgram.getMemory();

        // functions
        PrintWriter w = new PrintWriter(new BufferedWriter(new FileWriter(prefix + "_functions.csv")));
        w.println("rva,size,start_block_size,name");
        int nf = 0;
        for (Function f : fm.getFunctions(true)) {
            AddressSetView body = f.getBody();
            Address entry = f.getEntryPoint();
            AddressRange first = body.getRangeContaining(entry);
            long startBlock = first == null ? 0 : first.getMaxAddress().subtract(entry) + 1;
            w.printf("0x%X,%d,%d,%s%n", entry.getOffset() - base, body.getNumAddresses(), startBlock,
                    f.getName(true).replace(",", ";"));
            nf++;
        }
        w.close();

        // string xrefs
        w = new PrintWriter(new BufferedWriter(new FileWriter(prefix + "_string_xrefs.tsv")));
        int ns = 0;
        DataIterator di = currentProgram.getListing().getDefinedData(true);
        while (di.hasNext()) {
            Data d = di.next();
            Object v = d.getValue();
            if (!(v instanceof String)) continue;
            String s = clean((String) v);
            if (s.length() < 3) continue;
            LinkedHashSet<Long> seen = new LinkedHashSet<Long>();
            ReferenceIterator it = rm.getReferencesTo(d.getMinAddress());
            while (it.hasNext()) {
                Function f = fm.getFunctionContaining(it.next().getFromAddress());
                if (f != null) seen.add(f.getEntryPoint().getOffset() - base);
            }
            if (seen.isEmpty()) continue;
            StringBuilder sb = new StringBuilder();
            for (Long r : seen) {
                if (sb.length() > 0) sb.append(",");
                sb.append("0x").append(Long.toHexString(r));
            }
            w.printf("0x%X\t%s\t%s%n", d.getMinAddress().getOffset() - base, s, sb);
            ns++;
        }
        w.close();

        // vtables with owning class namespace
        w = new PrintWriter(new BufferedWriter(new FileWriter(prefix + "_vtables.tsv")));
        AddressSpace space = currentProgram.getAddressFactory().getDefaultAddressSpace();
        int nv = 0;
        SymbolIterator si = currentProgram.getSymbolTable().getAllSymbols(true);
        while (si.hasNext()) {
            Symbol s = si.next();
            String nm = s.getName();
            if (nm == null || !nm.toLowerCase().contains("vftable")) continue;
            String cls = s.getParentNamespace().getName(true) + "::" + nm;
            Address addr = s.getAddress();
            int written = 0;
            for (int slot = 0; slot < 1024; slot++) {
                long target;
                try { target = Integer.toUnsignedLong(mem.getInt(addr.add(slot * 4L))); }
                catch (Exception e) { break; }
                Function f;
                try { f = fm.getFunctionAt(space.getAddress(target)); }
                catch (Exception e) { break; }
                if (f == null) break;
                w.printf("0x%X\t%s\t%d\t0x%X%n", addr.getOffset() - base, clean(cls), slot,
                        f.getEntryPoint().getOffset() - base);
                written++;
            }
            if (written > 0) nv++;
        }
        w.close();
        println("export_inventory: " + nf + " functions, " + ns + " strings, " + nv + " vtables -> " + prefix);
    }
}
