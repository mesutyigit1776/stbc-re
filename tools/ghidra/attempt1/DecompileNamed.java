// Decompiles functions whose names are given as script args and prints the C.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;

public class DecompileNamed extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (String want : getScriptArgs()) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (!(f.getParentNamespace().getName().equals("Global") ? f.getName() : f.getParentNamespace().getName()+"::"+f.getName()).equals(want)) continue;
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                println("=== " + want + " @ " + f.getEntryPoint());
                println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "FAILED");
            }
        }
    }
}
