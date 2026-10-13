// Decompile the functions containing the given addresses: -postScript Mm3dDecompile.java <out.c> <addr>...
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class Mm3dDecompile extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0])) {
            for (int i = 1; i < args.length; i++) {
                Function f = getFunctionContaining(toAddr(Long.decode(args[i])));
                if (f == null) { out.printf("// no function at %s%n%n", args[i]); continue; }
                DecompileResults r = d.decompileFunction(f, 120, monitor);
                out.printf("// ===== %s (contains %s)%n%s%n", f.getName(), args[i],
                    r.decompileCompleted() ? r.getDecompiledFunction().getC() : r.getErrorMessage());
            }
        }
    }
}
