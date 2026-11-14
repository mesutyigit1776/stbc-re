// For every py_* wrapper: list callee functions. Writes re/wrapper_callees.tsv (wrapper \t calleeVA ...) and prints top callees.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class WrapperCallees extends GhidraScript {
    @Override
    public void run() throws Exception {
        Map<String,Integer> cnt = new HashMap<>();
        try (PrintWriter w = new PrintWriter(getScriptArgs()[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (!f.getName().startsWith("py_")) continue;
                LinkedHashSet<String> cs = new LinkedHashSet<>();
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction in = it.next();
                    if (!in.getFlowType().isCall()) continue;
                    for (Reference r : in.getReferencesFrom())
                        if (r.getReferenceType().isCall()) cs.add(r.getToAddress().toString());
                }
                w.println(f.getName() + "\t" + f.getEntryPoint() + "\t" + String.join(" ", cs));
                for (String c : cs) cnt.merge(c, 1, Integer::sum);
            }
        }
        cnt.entrySet().stream().sorted((a,b)->b.getValue()-a.getValue()).limit(14)
           .forEach(e -> println(e.getKey() + " " + e.getValue()));
    }
}
