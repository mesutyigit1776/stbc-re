// Args: oldName newName. Moves every function from namespace oldName to newName (created if missing) and renames the vtbl_<old> label.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.*;
import java.util.*;

public class RenameNamespace extends GhidraScript {
    @Override
    public void run() throws Exception {
        String o = getScriptArgs()[0], n = getScriptArgs()[1]; int c = 0, clash = 0;
        SymbolTable st = currentProgram.getSymbolTable();
        Namespace dst = st.getOrCreateNameSpace(currentProgram.getGlobalNamespace(), n, SourceType.USER_DEFINED);
        List<Function> todo = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) if (f.getParentNamespace().getName().equals(o) && !f.getParentNamespace().isGlobal()) todo.add(f);
        for (Function f : todo) {
            try { f.setParentNamespace(dst); c++; }
            catch (Exception e) { try { f.setParentNamespace(dst); f.setName(f.getName() + "_" + f.getEntryPoint(), SourceType.USER_DEFINED); c++; } catch (Exception e2) { clash++; } }
        }
        SymbolIterator it = st.getSymbols("vtbl_" + o); List<Symbol> v = new ArrayList<>(); while (it.hasNext()) v.add(it.next());
        for (Symbol s : v) s.setName("vtbl_" + n, SourceType.USER_DEFINED);
        println("RENAMENS moved=" + c + " clash=" + clash + " vtblLabels=" + v.size());
    }
}
