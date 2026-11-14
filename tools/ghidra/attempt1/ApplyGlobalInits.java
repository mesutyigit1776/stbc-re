// Pre-script. Arg: ginit_targets.tsv. Creates a function "ginit_<addr>" at each C++ global-initializer stub
// and marks the table at the start of .data as an array of code pointers.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.*;

public class ApplyGlobalInits extends GhidraScript {
    @Override
    public void run() throws Exception {
        int made = 0, had = 0, fail = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
            String l;
            while ((l = r.readLine()) != null) {
                l = l.trim(); if (l.isEmpty()) continue;
                Address ad = toAddr(Long.parseLong(l, 16));
                Function f = getFunctionAt(ad);
                if (f != null) { had++; continue; }
                if (getInstructionAt(ad) == null) disassemble(ad);
                f = createFunction(ad, "ginit_" + l);
                if (f != null) made++; else fail++;
            }
        }
        println("GINIT created=" + made + " existing=" + had + " failed=" + fail);
    }
}
