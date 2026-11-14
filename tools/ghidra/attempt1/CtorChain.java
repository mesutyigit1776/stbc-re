// For each constructor in allocmap.tsv: ordered list of distinct vtable pointers stored by the ctor and the ctors it calls (DFS, call order, depth<=6).
// Args: out.tsv vtables.tsv allocmap.tsv
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import java.io.*;
import java.util.*;

public class CtorChain extends GhidraScript {
    Set<Long> vts = new HashSet<>();
    void walk(Function f, int depth, List<Long> out, Set<Address> seen) {
        if (f == null || depth > 6 || !seen.add(f.getEntryPoint())) return;
        for (Instruction in : currentProgram.getListing().getInstructions(f.getBody(), true)) {
            for (int i = 0; i < in.getNumOperands(); i++) {
                Scalar s = in.getScalar(i);
                if (s != null && vts.contains(s.getUnsignedValue()) && !out.contains(s.getUnsignedValue())) out.add(s.getUnsignedValue());
            }
            if (in.getFlowType().isCall()) {
                for (Reference r : in.getReferencesFrom()) if (r.getReferenceType().isCall()) {
                    Function c = getFunctionAt(r.getToAddress());
                    if (c != null && !c.getName().startsWith("py_")) walk(c, depth + 1, out, seen);
                }
            }
        }
    }
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (BufferedReader r = new BufferedReader(new FileReader(a[1]))) { String l; while ((l = r.readLine()) != null) vts.add(Long.parseLong(l.split("\t")[0], 16)); }
        int n = 0;
        try (PrintWriter w = new PrintWriter(a[0]); BufferedReader r = new BufferedReader(new FileReader(a[2]))) {
            String l;
            while ((l = r.readLine()) != null) {
                String[] p = l.split("\t"); Function f = getFunctionAt(toAddr(Long.parseLong(p[0], 16)));
                List<Long> out = new ArrayList<>(); walk(f, 0, out, new HashSet<>());
                StringBuilder sb = new StringBuilder(); for (Long v : out) sb.append(String.format("%08x ", v));
                w.println(p[0] + "\t" + p[1] + "\t" + (p.length > 4 ? p[4] : "") + "\t" + sb.toString().trim()); n++;
            }
        }
        println("CTORCHAIN ctors=" + n);
    }
}
