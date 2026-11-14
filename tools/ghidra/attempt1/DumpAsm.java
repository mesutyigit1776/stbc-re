// Disassemble a VA range and write the instruction listing. Args: startVA endVA outFile
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpAsm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Address s = toAddr(Long.parseLong(a[0], 16)), e = toAddr(Long.parseLong(a[1], 16));
        disassemble(s);
        try (PrintWriter w = new PrintWriter(new FileWriter(a[2]))) {
            Instruction in = getInstructionAt(s);
            if (in == null) in = getInstructionAfter(s);
            while (in != null && in.getAddress().compareTo(e) < 0) {
                w.println(in.getAddress() + "  " + in.toString());
                in = in.getNext();
            }
        }
    }
}
