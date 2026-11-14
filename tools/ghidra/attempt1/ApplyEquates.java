// Creates equates (named constants) and applies them to every instruction scalar operand with that value.
// Args: tsv (hexValue \t NAME) minValue(hex). Values below minValue are ignored (avoid false matches).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class ApplyEquates extends GhidraScript {
    @Override
    public void run() throws Exception {
        long min = Long.parseLong(getScriptArgs()[1], 16);
        Map<Long, String> names = new HashMap<>();
        try (BufferedReader r = new BufferedReader(new FileReader(getScriptArgs()[0]))) {
            String l; while ((l = r.readLine()) != null) { String[] p = l.split("\t"); if (p.length >= 2) { long v = Long.parseLong(p[0], 16); if (v >= min && !names.containsKey(v)) names.put(v, p[1]); } }
        }
        EquateTable et = currentProgram.getEquateTable();
        int applied = 0;
        for (Instruction in : currentProgram.getListing().getInstructions(true)) {
            for (int i = 0; i < in.getNumOperands(); i++) {
                Scalar s = in.getScalar(i); if (s == null) continue;
                String n = names.get(s.getUnsignedValue()); if (n == null) continue;
                Equate eq = et.getEquate(n); if (eq == null) eq = et.createEquate(n, s.getValue());
                if (eq.getValue() != s.getValue()) continue;
                eq.addReference(in.getAddress(), i); applied++;
            }
        }
        println("EQUATES applied=" + applied + " names=" + names.size());
    }
}
