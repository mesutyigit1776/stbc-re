// Creates labels from TSV (hexAddr \t name). Replaces default/auto labels (DAT_, vtbl_<addr>); keeps user names that are not auto-generated.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ApplyLabels extends GhidraScript {
    @Override
    public void run() throws Exception {
        int ok = 0, fail = 0; SymbolTable st = currentProgram.getSymbolTable();
        try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
            String l;
            while ((l = r.readLine()) != null) {
                String[] p = l.split("\t"); if (p.length < 2) continue;
                try {
                    Address a = toAddr(Long.parseLong(p[0], 16));
                    Symbol s = st.getPrimarySymbol(a);
                    if (s != null && s.getSource() == SourceType.USER_DEFINED && !s.getName().startsWith("vtbl_")) { fail++; continue; }
                    if (p[1].startsWith("vtbl_")) for (Symbol old : st.getSymbols(a)) if (old.getName().startsWith("vtbl_")) old.delete();
                    createLabel(a, p[1].replaceAll("[^A-Za-z0-9_]", "_"), true, SourceType.USER_DEFINED); ok++;
                } catch (Exception e) { fail++; }
            }
        }
        println("LABELS ok=" + ok + " skipped=" + fail);
    }
}
