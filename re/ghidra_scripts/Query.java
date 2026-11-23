// Ad-hoc queries against an analyzed program, written to a text file.
// Usage (headless, -process <prog> -noanalysis -readOnly):
//   -postScript Query.java <outfile> string <substring>     functions referencing matching strings (+decompile)
//   -postScript Query.java <outfile> import <name>          callers of an imported function (+decompile)
//   -postScript Query.java <outfile> decompile <addr> [...] decompile functions containing addresses
//   -postScript Query.java <outfile> callers <addr>         callers of the function at addr
//   -postScript Query.java <outfile> refs <addr> [...]      all references to addresses
//   -postScript Query.java <outfile> disasm <addr> [...]    instruction listing
//   -postScript Query.java <outfile> bytes <n> <addr> [...] raw bytes + float/int views
// Output is derived from the original binary: keep it under re/exports (gitignored).
//@category d2k

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

import java.io.File;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class Query extends GhidraScript {

    private DecompInterface decomp;
    private PrintWriter out;

    @Override
    protected void run() throws Exception {
        String[] a = getScriptArgs();
        out = new PrintWriter(new File(a[0]), "UTF-8");
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try {
            switch (a[1]) {
                case "string": queryString(a[2]); break;
                case "import": queryImport(a[2]); break;
                case "decompile":
                    for (int i = 2; i < a.length; i++) decompile(funcAt(a[i]));
                    break;
                case "callers": callers(funcAt(a[2])); break;
                case "disasm":
                    for (int i = 2; i < a.length; i++) disasm(funcAt(a[i]));
                    break;
                case "bytes":
                    for (int i = 3; i < a.length; i++) bytes(a[2], a[i]);
                    break;
                case "refs":
                    for (int i = 2; i < a.length; i++) refs(a[i]);
                    break;
                default: out.println("unknown mode " + a[1]);
            }
        } finally {
            out.close();
            decomp.dispose();
        }
    }

    private Function funcAt(String addr) {
        Address ad = currentProgram.getAddressFactory().getAddress(addr);
        return getFunctionContaining(ad);
    }

    private void queryString(String needle) {
        Set<Function> funcs = new LinkedHashSet<>();
        DataIterator it = currentProgram.getListing().getDefinedData(true);
        while (it.hasNext()) {
            Data d = it.next();
            if (!d.hasStringValue()) continue;
            Object v = d.getValue();
            if (v == null || !v.toString().toLowerCase().contains(needle.toLowerCase())) continue;
            out.println("STRING " + d.getAddress() + " \"" + v + "\"");
            for (Reference r : getReferencesTo(d.getAddress())) {
                Function f = getFunctionContaining(r.getFromAddress());
                out.println("   ref from " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName()));
                if (f != null) funcs.add(f);
            }
        }
        for (Function f : funcs) decompile(f);
    }

    private void queryImport(String name) {
        Set<Function> funcs = new LinkedHashSet<>();
        for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
            if (!s.getName().equalsIgnoreCase(name)) continue;
            for (Reference r : s.getReferences()) {
                // References usually hit the IAT slot; follow refs to that too.
                Function f = getFunctionContaining(r.getFromAddress());
                if (f != null) funcs.add(f);
                for (Reference r2 : getReferencesTo(r.getFromAddress())) {
                    Function f2 = getFunctionContaining(r2.getFromAddress());
                    if (f2 != null) funcs.add(f2);
                }
            }
        }
        for (Function f : funcs) out.println("CALLER " + f.getEntryPoint() + " " + f.getName());
        for (Function f : funcs) decompile(f);
    }

    private void callers(Function f) {
        if (f == null) return;
        for (Reference r : getReferencesTo(f.getEntryPoint())) {
            Function c = getFunctionContaining(r.getFromAddress());
            out.println("CALLER " + r.getFromAddress() + " in " + (c == null ? "?" : c.getEntryPoint() + " " + c.getName()));
        }
    }

    private void refs(String addr) {
        Address ad = currentProgram.getAddressFactory().getAddress(addr);
        out.println("REFS to " + ad);
        for (Reference r : getReferencesTo(ad)) {
            Function c = getFunctionContaining(r.getFromAddress());
            out.println("   " + r.getReferenceType() + " from " + r.getFromAddress() + " in "
                + (c == null ? "?" : c.getEntryPoint() + " " + c.getName()));
        }
    }

    private void disasm(Function f) {
        if (f == null) return;
        out.println("\n======== " + f.getEntryPoint() + " " + f.getName() + " (disasm) ========");
        for (ghidra.program.model.listing.Instruction ins :
                currentProgram.getListing().getInstructions(f.getBody(), true)) {
            out.println(ins.getAddress() + "  " + ins);
        }
    }

    // bytes <count> <addr>...: hex dump plus float32/float64/int interpretations.
    private void bytes(String count, String addr) throws Exception {
        int n = Integer.parseInt(count);
        Address ad = currentProgram.getAddressFactory().getAddress(addr);
        byte[] b = new byte[n];
        currentProgram.getMemory().getBytes(ad, b);
        StringBuilder sb = new StringBuilder("BYTES " + ad + ":");
        for (byte x : b) sb.append(String.format(" %02x", x));
        out.println(sb);
        java.nio.ByteBuffer bb = java.nio.ByteBuffer.wrap(b).order(java.nio.ByteOrder.LITTLE_ENDIAN);
        if (n >= 4) out.println("   u32=" + Integer.toUnsignedString(bb.getInt(0)) + " f32=" + bb.getFloat(0));
        if (n >= 8) out.println("   f64=" + bb.getDouble(0));
    }

    private void decompile(Function f) {
        if (f == null) return;
        out.println("\n======== " + f.getEntryPoint() + " " + f.getName() + " ========");
        DecompileResults res = decomp.decompileFunction(f, 60, monitor);
        if (res.decompileCompleted()) {
            out.println(res.getDecompiledFunction().getC());
        } else {
            out.println("// decompile failed: " + res.getErrorMessage());
        }
    }
}
