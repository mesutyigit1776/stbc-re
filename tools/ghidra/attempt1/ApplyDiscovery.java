// Pre-script. Args: newFuncTargets.tsv vtables.tsv
// 1) disassemble + create a function at each new vtable target. 2) label each vtable "vtbl_<addr>" with a pointer array.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.symbol.SourceType;
import java.io.*;

public class ApplyDiscovery extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        int made = 0, skip = 0, vt = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(a[0]))) {
            String l;
            while ((l = r.readLine()) != null) {
                l = l.trim(); if (l.isEmpty()) continue;
                Address ad = toAddr(Long.parseLong(l, 16));
                if (getFunctionAt(ad) != null) continue;
                if (getFunctionContaining(ad) != null) { skip++; continue; }   // inside an existing function: not an entry
                if (getInstructionAt(ad) == null) disassemble(ad);
                if (createFunction(ad, null) != null) made++; else skip++;
            }
        }
        DataType ptr = new PointerDataType(new VoidDataType());
        try (BufferedReader r = new BufferedReader(new FileReader(a[1]))) {
            String l;
            while ((l = r.readLine()) != null) {
                String[] p = l.split("\t"); if (p.length < 2) continue;
                Address ad = toAddr(Long.parseLong(p[0], 16)); int n = Integer.parseInt(p[1]);
                try {
                    clearListing(ad, ad.add(4L * n - 1));
                    createData(ad, new ArrayDataType(ptr, n, 4));
                    createLabel(ad, "vtbl_" + p[0], true, SourceType.USER_DEFINED);
                    vt++;
                } catch (Exception e) { /* overlap with existing data: leave it */ }
            }
        }
        println("DISCOVERY functionsCreated=" + made + " skipped=" + skip + " vtablesLabeled=" + vt);
    }
}
