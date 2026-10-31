import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import java.io.*;
import java.nio.file.*;

public class Decomp extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String in = args[0], out = args[1];
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter pw = new PrintWriter(new FileWriter(out));
        for (String line : Files.readAllLines(Paths.get(in))) {
            line = line.trim();
            if (line.isEmpty()) continue;
            long rva = Long.parseLong(line.replace("0x",""), 16);
            Address a = currentProgram.getImageBase().add(rva);
            Function f = getFunctionContaining(a);
            if (f == null) {
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                new CreateFunctionCmd(a).applyTo(currentProgram, monitor);
                f = getFunctionAt(a);
                if (f == null) f = getFunctionContaining(a);
            }
            pw.println("// ===== " + line + " -> " + (f == null ? "NOFUNC" : f.getEntryPoint().subtract(currentProgram.getImageBase())));
            if (f == null) continue;
            // disassemble callees bodies too so the decompiler sees them as functions
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            if (r.decompileCompleted()) pw.println(r.getDecompiledFunction().getC());
            else pw.println("// FAIL " + r.getErrorMessage());
            pw.flush();
        }
        pw.close();
    }
}
