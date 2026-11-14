// For each creator thunk (re/creator_ids.tsv: addr, typeId): find the constructor it calls (not the allocation helpers)
// and the last vtable pointer that constructor stores directly. Args: out.tsv creator_ids.tsv vtables.tsv
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import java.io.*;
import java.util.*;

public class CreatorVt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Set<Long> vts = new HashSet<>();
        try (BufferedReader r = new BufferedReader(new FileReader(a[2]))) { String l; while ((l = r.readLine()) != null) vts.add(Long.parseLong(l.split("\t")[0], 16)); }
        Set<Long> skip = new HashSet<>(Arrays.asList(0x717b70L, 0x718010L, 0x718cb0L));
        int ok = 0, n = 0;
        try (PrintWriter w = new PrintWriter(a[0]); BufferedReader r = new BufferedReader(new FileReader(a[1]))) {
            String l;
            while ((l = r.readLine()) != null) {
                String[] p = l.split("\t"); n++;
                Function t = getFunctionAt(toAddr(Long.parseLong(p[0], 16))); if (t == null) continue;
                Address ctor = null;
                for (Instruction in : currentProgram.getListing().getInstructions(t.getBody(), true)) {
                    if (!in.getFlowType().isCall()) continue;
                    for (Reference rf : in.getReferencesFrom()) if (rf.getReferenceType().isCall() && !skip.contains(rf.getToAddress().getOffset()) && ctor == null) ctor = rf.getToAddress();
                }
                Long vt = null;
                if (ctor != null) { Function cf = getFunctionAt(ctor);
                    if (cf != null) for (Instruction in : currentProgram.getListing().getInstructions(cf.getBody(), true))
                        for (int i = 0; i < in.getNumOperands(); i++) { Scalar s = in.getScalar(i); if (s != null && vts.contains(s.getUnsignedValue())) vt = s.getUnsignedValue(); } }
                w.println(p[0] + "\t" + p[1] + "\t" + (ctor == null ? "-" : ctor.toString()) + "\t" + (vt == null ? "-" : String.format("%08x", vt)));
                if (vt != null) ok++;
            }
        }
        println("CREATORVT thunks=" + n + " withVtable=" + ok);
    }
}
