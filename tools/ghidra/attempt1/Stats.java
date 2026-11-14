// Prints coverage stats: functions by naming class, with byte sizes.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.MemoryBlock;
import java.util.*;

public class Stats extends GhidraScript {
    @Override
    public void run() throws Exception {
        long nAll=0,bAll=0,nPy=0,bPy=0,nCpp=0,bCpp=0,nFun=0,bFun=0,nThunk=0,nTiny=0,nBig=0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            long sz = f.getBody().getNumAddresses(); String n = f.getName();
            boolean ns = !f.getParentNamespace().isGlobal();
            nAll++; bAll += sz;
            if (f.isThunk()) nThunk++;
            if (n.startsWith("py_")) { nPy++; bPy += sz; }
            else if (ns) { nCpp++; bCpp += sz; }
            else if (n.startsWith("FUN_")) { nFun++; bFun += sz; if (sz < 16) nTiny++; if (sz > 1000) nBig++; }
        }
        long text = 0; for (MemoryBlock b : currentProgram.getMemory().getBlocks()) if (b.getName().equals(".text")) text = b.getSize();
        println(String.format("STATS total=%d bytes=%d | py=%d(%d B) cpp=%d(%d B) FUN=%d(%d B) thunk=%d tinyFUN=%d bigFUN(>1000B)=%d | .text=%d B",
            nAll,bAll,nPy,bPy,nCpp,bCpp,nFun,bFun,nThunk,nTiny,nBig,text));
        long strs = 0; DataIterator di = currentProgram.getListing().getDefinedData(true);
        while (di.hasNext()) if (di.next().hasStringValue()) strs++;
        println("STATS defined strings=" + strs);
    }
}
