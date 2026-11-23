// Export the basic facts of an analyzed program to TSV files for grep/diff:
// functions, defined strings, imports, and memory blocks.
// Usage (headless): -postScript ExportBasics.java <outdir>
//@category d2k

import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;

import java.io.File;
import java.io.PrintWriter;

public class ExportBasics extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : ".");
        out.mkdirs();

        try (PrintWriter w = new PrintWriter(new File(out, "functions.tsv"), "UTF-8")) {
            w.println("address\tsize\tname\tcallers\tcallees");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                w.printf("%s\t%d\t%s\t%d\t%d%n", f.getEntryPoint(), f.getBody().getNumAddresses(),
                    f.getName(), f.getCallingFunctions(monitor).size(),
                    f.getCalledFunctions(monitor).size());
            }
        }

        try (PrintWriter w = new PrintWriter(new File(out, "strings.tsv"), "UTF-8")) {
            w.println("address\tlength\tvalue");
            DataIterator it = currentProgram.getListing().getDefinedData(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                String s = StringDataInstance.getStringDataInstance(d).getStringValue();
                if (s == null) continue;
                s = s.replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n");
                w.printf("%s\t%d\t%s%n", d.getAddress(), d.getLength(), s);
            }
        }

        try (PrintWriter w = new PrintWriter(new File(out, "imports.tsv"), "UTF-8")) {
            w.println("library\tname\taddress");
            for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
                if (s.getSymbolType() != SymbolType.FUNCTION && s.getSymbolType() != SymbolType.LABEL) continue;
                w.printf("%s\t%s\t%s%n", s.getParentNamespace().getName(), s.getName(), s.getAddress());
            }
        }

        try (PrintWriter w = new PrintWriter(new File(out, "sections.tsv"), "UTF-8")) {
            w.println("name\tstart\tend\tsize\tperms");
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                w.printf("%s\t%s\t%s\t%d\t%s%s%s%n", b.getName(), b.getStart(), b.getEnd(), b.getSize(),
                    b.isRead() ? "r" : "-", b.isWrite() ? "w" : "-", b.isExecute() ? "x" : "-");
            }
        }
        println("exported to " + out.getAbsolutePath());
    }
}
