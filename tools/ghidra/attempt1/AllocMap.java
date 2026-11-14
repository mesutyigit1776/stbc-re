// Whole-program: "push imm; call operator_new" then the next call (within 12 instr.) is the constructor.
// For each constructor, collect vtable immediates it stores. Output: ctor, size, vtables, creator function names.
// Args: out.tsv vtables.tsv allocFnHex
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import java.io.*;
import java.util.*;

public class AllocMap extends GhidraScript {
    static class Rec { Set<Long> sizes = new TreeSet<>(); Set<String> creators = new TreeSet<>(); int n; }
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Set<Long> vts = new HashSet<>();
        try (BufferedReader r = new BufferedReader(new FileReader(a[1]))) { String l; while ((l = r.readLine()) != null) vts.add(Long.parseLong(l.split("\t")[0], 16)); }
        long alloc = Long.parseLong(a[2], 16);
        Map<Address, Rec> ctors = new HashMap<>();
        int sites = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.getName().startsWith("ginit_")) continue;
            Long lastImm = null; int sinceAlloc = -1; Long size = null;
            for (Instruction in : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                if (in.getMnemonicString().equals("PUSH") && in.getScalar(0) != null) lastImm = in.getScalar(0).getUnsignedValue();
                if (sinceAlloc >= 0) sinceAlloc++;
                if (in.getFlowType().isCall()) {
                    Address t = null; for (Reference rf : in.getReferencesFrom()) if (rf.getReferenceType().isCall()) t = rf.getToAddress();
                    if (t != null && t.getOffset() == alloc) { size = lastImm; sinceAlloc = 0; sites++; }
                    else if (t != null && sinceAlloc > 0 && sinceAlloc <= 12 && size != null) {
                        Rec r = ctors.computeIfAbsent(t, k -> new Rec()); r.sizes.add(size); r.n++;
                        String nm = f.getParentNamespace().isGlobal() ? f.getName() : f.getParentNamespace().getName() + "::" + f.getName();
                        if (!nm.startsWith("FUN_") && r.creators.size() < 6) r.creators.add(nm);
                        sinceAlloc = -1; size = null;
                    }
                }
            }
        }
        int withVt = 0;
        try (PrintWriter w = new PrintWriter(a[0])) {
            for (Map.Entry<Address, Rec> e : ctors.entrySet()) {
                Function cf = getFunctionAt(e.getKey()); if (cf == null) continue;
                List<String> found = new ArrayList<>();
                for (Instruction in : currentProgram.getListing().getInstructions(cf.getBody(), true))
                    for (int i = 0; i < in.getNumOperands(); i++) { Scalar s = in.getScalar(i); if (s != null && vts.contains(s.getUnsignedValue())) found.add(String.format("%08x", s.getUnsignedValue())); }
                if (found.isEmpty()) continue;
                withVt++;
                w.println(e.getKey() + "\t" + e.getValue().sizes + "\t" + e.getValue().n + "\t" + String.join(" ", found) + "\t" + String.join(",", e.getValue().creators));
            }
        }
        println("ALLOCMAP allocSites=" + sites + " ctors=" + ctors.size() + " ctorsWithVtable=" + withVt);
    }
}
