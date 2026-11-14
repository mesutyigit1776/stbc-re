// Decompile every real function to text, grouped by 4 KB page: <out>/<page>.c   plus index <out>/index_<shard>.tsv
// Args: outdir shardIndex shardCount. Skips ginit_ stubs and their +0x10 halves.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        int shard = Integer.parseInt(a[1]), count = Integer.parseInt(a[2]);
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        TreeMap<Long, List<Function>> pages = new TreeMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            String n = f.getName(); if (n.startsWith("ginit_")) continue;
            Function prev = getFunctionAt(f.getEntryPoint().subtract(0x10));
            if (prev != null && prev.getName().startsWith("ginit_")) continue;
            long page = f.getEntryPoint().getOffset() >> 12;
            if (page % count != shard) continue;
            pages.computeIfAbsent(page, k -> new ArrayList<>()).add(f);
        }
        int done = 0, fail = 0, total = 0; for (List<Function> l : pages.values()) total += l.size();
        try (PrintWriter idx = new PrintWriter(new File(out, "index_" + shard + ".tsv"))) {
            for (Map.Entry<Long, List<Function>> e : pages.entrySet()) {
                try (PrintWriter w = new PrintWriter(new File(out, String.format("%05x000.c", e.getKey())))) {
                    for (Function f : e.getValue()) {
                        String q = f.getParentNamespace().isGlobal() ? f.getName() : f.getParentNamespace().getName() + "::" + f.getName();
                        Set<String> callers = new TreeSet<>(); int nc = 0;
                        for (Reference r : getReferencesTo(f.getEntryPoint())) {
                            if (!r.getReferenceType().isCall() && !r.getReferenceType().isData()) continue;
                            nc++;
                            Function c = getFunctionContaining(r.getFromAddress());
                            if (c != null && callers.size() < 8) callers.add(c.getName() + "@" + c.getEntryPoint());
                            else if (c == null && callers.size() < 8) callers.add("data@" + r.getFromAddress());
                        }
                        DecompileResults res = di.decompileFunction(f, 20, monitor);
                        w.println("// ==== " + q + " @ " + f.getEntryPoint() + " size=" + f.getBody().getNumAddresses() + " xrefs=" + nc);
                        w.println("// callers: " + String.join(", ", callers));
                        if (res.decompileCompleted()) w.println(res.getDecompiledFunction().getC()); else { w.println("// DECOMPILE FAILED: " + res.getErrorMessage()); fail++; }
                        idx.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + nc + "\t" + q);
                        if (++done % 500 == 0) println("PROGRESS shard " + shard + " " + done + "/" + total);
                    }
                }
            }
        }
        println("EXPORTDONE shard=" + shard + " functions=" + done + " failed=" + fail);
    }
}
