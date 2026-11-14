// Writes "addr<TAB>callee1 callee2 ..." (call targets in instruction order, direct calls only, hex) for every function.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;
import java.io.PrintWriter;
import java.util.*;

public class ExportCalls extends GhidraScript {
    @Override
    public void run() throws Exception {
        try (PrintWriter w = new PrintWriter(getScriptArgs()[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                StringBuilder sb = new StringBuilder();
                for (Instruction in : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    if (!in.getFlowType().isCall()) continue;
                    for (Reference r : in.getReferencesFrom()) if (r.getReferenceType().isCall() && r.getToAddress().isMemoryAddress())
                        sb.append(String.format("%08x ", r.getToAddress().getOffset()));
                }
                w.println(f.getEntryPoint() + "\t" + sb.toString().trim());
            }
        }
    }
}
