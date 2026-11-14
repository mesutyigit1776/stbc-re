// Names functions from a TSV (hexVA \t name). "Class::Method" is placed in namespace Class.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ApplyCppNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        int ok = 0, skip = 0;
        SymbolTable st = currentProgram.getSymbolTable();
        for (String path : getScriptArgs()) try (BufferedReader r = new BufferedReader(new FileReader(path))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.split("\t");
                if (p.length < 2) continue;
                Function fn = getFunctionAt(toAddr(Long.parseLong(p[0], 16)));
                if (fn == null) { skip++; continue; }
                if (!fn.getName().startsWith("FUN_")) { skip++; continue; }   // never clobber an existing name
                String n = p[1]; Namespace ns = currentProgram.getGlobalNamespace();
                int i = n.indexOf("::");
                if (i > 0) { ns = st.getOrCreateNameSpace(ns, n.substring(0, i), SourceType.USER_DEFINED); n = n.substring(i + 2); }
                try { fn.setParentNamespace(ns); fn.setName(n, SourceType.USER_DEFINED); ok++; }
                catch (Exception e) { println("SKIP " + p[1] + ": " + e.getMessage()); skip++; }
            }
        }
        println("named=" + ok + " skipped=" + skip);
    }
}
