// List references to addresses. Args: outFile addr1 [addr2 ...]  (hex VAs)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class RefsTo extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
            for (int i = 1; i < a.length; i++) {
                Address t = toAddr(Long.parseLong(a[i], 16));
                for (Reference r : getReferencesTo(t)) {
                    w.println(a[i] + " <- " + r.getFromAddress() + " " + r.getReferenceType() + " " + (getInstructionAt(r.getFromAddress()) != null ? getInstructionAt(r.getFromAddress()).toString() : ""));
                }
            }
        }
    }
}
