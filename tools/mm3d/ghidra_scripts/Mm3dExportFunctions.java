// Post-analysis: export entry,size,name,thumb,body_ranges for every function in .text.
// Usage: -postScript Mm3dExportFunctions.java <out.csv>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;
import java.math.BigInteger;

public class Mm3dExportFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        Register tmode = currentProgram.getRegister("TMode");
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
            out.println("entry,size,name,thumb,ranges");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.isExternal() || f.isThunk() && f.getBody().isEmpty()) continue;
                BigInteger t = currentProgram.getProgramContext().getValue(tmode, f.getEntryPoint(), false);
                StringBuilder ranges = new StringBuilder();
                for (AddressRange r : f.getBody()) {
                    if (ranges.length() > 0) ranges.append(';');
                    ranges.append(String.format("%x-%x", r.getMinAddress().getOffset(), r.getMaxAddress().getOffset() + 1));
                }
                out.printf("0x%08X,%d,%s,%d,%s%n", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses(),
                    f.getName(), t != null && t.signum() != 0 ? 1 : 0, ranges);
            }
        }
    }
}
