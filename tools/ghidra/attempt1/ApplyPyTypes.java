// Args: header.h typeobjs.tsv(addr,symbol,tpname) sigs.tsv(addr,kind)
// 1) parse the CPython 1.5.2 header into the program data types. 2) put PyTypeObject on each type object and label it.
// 3) give table-derived functions their real prototypes.
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParser;
import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.io.*;
import java.nio.file.*;
import java.util.*;

public class ApplyPyTypes extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        CParser parser = new CParser(dtm, true, null);
        parser.parse(new String(Files.readAllBytes(Paths.get(a[0]))));
        DataType pto = dtm.getDataType("/PyTypeObject");
        println("PYTYPES PyTypeObject size=" + (pto == null ? -1 : pto.getLength()) + " PyFrameObject size=" + dtm.getDataType("/PyFrameObject").getLength());
        int nt = 0, ns = 0, nf = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(a[1]))) {
            String l;
            while ((l = r.readLine()) != null) {
                String[] p = l.split("\t"); Address ad = toAddr(Long.parseLong(p[0], 16));
                try { clearListing(ad, ad.add(pto.getLength() - 1)); createData(ad, pto); createLabel(ad, p[1], true, SourceType.USER_DEFINED); nt++; } catch (Exception e) { println("TYPEFAIL " + p[0] + " " + e.getMessage()); }
            }
        }
        Map<String, String> proto = new HashMap<>();
        proto.put("method", "PyObject * N(PyObject * self, PyObject * args);");
        proto.put("tp_dealloc", "void N(PyObject * op);");
        proto.put("tp_print", "int N(PyObject * op, FILE * fp, int flags);");
        proto.put("tp_getattr", "PyObject * N(PyObject * op, char * name);");
        proto.put("tp_setattr", "int N(PyObject * op, char * name, PyObject * v);");
        proto.put("tp_compare", "int N(PyObject * a, PyObject * b);");
        proto.put("tp_repr", "PyObject * N(PyObject * op);");
        proto.put("tp_str", "PyObject * N(PyObject * op);");
        proto.put("tp_hash", "long N(PyObject * op);");
        proto.put("tp_call", "PyObject * N(PyObject * op, PyObject * args, PyObject * kw);");
        proto.put("tp_getattro", "PyObject * N(PyObject * op, PyObject * name);");
        proto.put("tp_setattro", "int N(PyObject * op, PyObject * name, PyObject * v);");
        try (BufferedReader r = new BufferedReader(new FileReader(a[2]))) {
            String l;
            while ((l = r.readLine()) != null) {
                String[] p = l.split("\t"); Function fn = getFunctionAt(toAddr(Long.parseLong(p[0], 16)));
                String pr = proto.get(p[1]); if (fn == null || pr == null) { ns++; continue; }
                if (p.length > 2 && !p[2].isEmpty()) pr = pr.replaceFirst("PyObject \\* (op|self|a)\\b", p[2] + " * $1");
                try {
                    DataType dt = new CParser(dtm, false, null).parse(pr.replace("N(", fn.getName() + "("));
                    if (dt instanceof FunctionDefinition && new ApplyFunctionSignatureCmd(fn.getEntryPoint(), (FunctionDefinition) dt, SourceType.USER_DEFINED).applyTo(currentProgram)) nf++; else ns++;
                } catch (Exception e) { ns++; println("SIGFAIL " + fn.getName() + " " + e.getMessage()); }
            }
        }
        println("PYTYPES typeObjects=" + nt + " signaturesApplied=" + nf + " skipped=" + ns);
    }
}
