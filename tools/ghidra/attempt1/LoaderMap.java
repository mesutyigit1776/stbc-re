// For each py_new_<Class>: find allocation size (push imm before call to operator new), the constructor callee,
// and the vtable address(es) the constructor stores. Args: out.tsv vtables.tsv allocFnHex
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import java.io.*;
import java.util.*;

public class LoaderMap extends GhidraScript {
    Set<Long> vts = new HashSet<>();
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (BufferedReader r = new BufferedReader(new FileReader(a[1]))) {
            String l; while ((l = r.readLine()) != null) vts.add(Long.parseLong(l.split("\t")[0], 16));
        }
        long alloc = Long.parseLong(a[2], 16);
        int ok = 0, noCtor = 0, noVt = 0;
        try (PrintWriter w = new PrintWriter(a[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (!(f.getName().equals("StreamLoader") && !f.getParentNamespace().isGlobal())) continue;
                String cls = f.getParentNamespace().getName();
                long size = -1; Address ctor = null; Long lastImm = null; boolean sawAlloc = false;
                for (Instruction in : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    if (in.getMnemonicString().equals("PUSH") && in.getScalar(0) != null) lastImm = in.getScalar(0).getUnsignedValue();
                    if (!in.getFlowType().isCall()) continue;
                    Address t = null; for (Reference rf : in.getReferencesFrom()) if (rf.getReferenceType().isCall()) t = rf.getToAddress();
                    if (t == null) continue;
                    if (t.getOffset() == alloc) { size = lastImm == null ? -1 : lastImm; sawAlloc = true; }
                    else if (sawAlloc && ctor == null) ctor = t;
                }
                if (ctor == null) { noCtor++; w.println(cls + "\t" + size + "\t-\t-"); continue; }
                Function cf = getFunctionAt(ctor);
                List<Long> found = new ArrayList<>();
                if (cf != null) for (Instruction in : currentProgram.getListing().getInstructions(cf.getBody(), true))
                    for (int i = 0; i < in.getNumOperands(); i++) { Scalar s = in.getScalar(i); if (s != null && vts.contains(s.getUnsignedValue())) found.add(s.getUnsignedValue()); }
                if (found.isEmpty()) { noVt++; w.println(cls + "\t" + size + "\t" + ctor + "\t-"); continue; }
                ok++;
                StringBuilder sb = new StringBuilder(); for (Long v : found) sb.append(String.format("%08x ", v));
                w.println(cls + "\t" + size + "\t" + ctor + "\t" + sb.toString().trim());
            }
        }
        println("LOADERMAP ok=" + ok + " noCtor=" + noCtor + " noVtable=" + noVt);
    }
}
