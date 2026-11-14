// Renames functions from TSV (hexVA \t name) even when they already have a non-default name. Use for corrections only.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;
import java.io.*;

public class RenameForce extends GhidraScript {
    @Override
    public void run() throws Exception {
        int ok = 0; SymbolTable st = currentProgram.getSymbolTable();
        try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
            String l;
            while ((l = r.readLine()) != null) {
                String[] p = l.split("\t"); if (p.length < 2) continue;
                Function fn = getFunctionAt(toAddr(Long.parseLong(p[0], 16))); if (fn == null) continue;
                String n = p[1]; Namespace ns = currentProgram.getGlobalNamespace(); int i = n.indexOf("::");
                if (i > 0) { ns = st.getOrCreateNameSpace(ns, n.substring(0, i), SourceType.USER_DEFINED); n = n.substring(i + 2); }
                fn.setParentNamespace(ns); fn.setName(n, SourceType.USER_DEFINED); ok++;
            }
        }
        println("FORCE renamed=" + ok);
    }
}
