// Disassembles and names Python-wrapper functions listed in re/swig_funcs.tsv (hexVA \t name) as py_<name>.
// Idempotent: stubs from earlier runs are removed and rebuilt.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.util.*;

public class ApplySwigNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs()[0];
        int made = 0, failed = 0, dup = 0;
        Set<Long> seen = new HashSet<>();
        try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.split("\t");
                if (p.length < 2) continue;
                long va = Long.parseLong(p[0], 16);
                if (!seen.add(va)) { dup++; continue; }   // several names can share one address; keep the first
                Address a = toAddr(va);
                Function old = getFunctionAt(a);
                if (old != null && old.getBody().getNumAddresses() <= 1) removeFunction(old);
                if (getInstructionAt(a) == null) disassemble(a);
                Function fn = getFunctionAt(a);
                if (fn == null) fn = createFunction(a, "py_" + p[1]);
                if (fn == null) { failed++; continue; }
                fn.setName("py_" + p[1], SourceType.USER_DEFINED);
                made++;
            }
        }
        println("ok=" + made + " failed=" + failed + " sharedAddr=" + dup);
    }
}
