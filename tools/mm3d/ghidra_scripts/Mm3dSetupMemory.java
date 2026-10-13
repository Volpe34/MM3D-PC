// Pre-analysis: split the raw code.bin block into ExHeader segments (text rx, ro r, data rw) and add BSS.
// Usage: -preScript Mm3dSetupMemory.java <exheader.bin>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.Path;

public class Mm3dSetupMemory extends GhidraScript {
    @Override
    protected void run() throws Exception {
        ByteBuffer exh = ByteBuffer.wrap(Files.readAllBytes(Path.of(getScriptArgs()[0]))).order(ByteOrder.LITTLE_ENDIAN);
        long textAddr = exh.getInt(0x10) & 0xffffffffL, textSize = (exh.getInt(0x14) & 0xffffffffL) * 0x1000;
        long roAddr = exh.getInt(0x20) & 0xffffffffL, roSize = (exh.getInt(0x24) & 0xffffffffL) * 0x1000;
        long dataAddr = exh.getInt(0x30) & 0xffffffffL, dataSize = exh.getInt(0x38) & 0xffffffffL;
        long bssSize = exh.getInt(0x3c) & 0xffffffffL;
        Memory mem = currentProgram.getMemory();
        MemoryBlock all = mem.getBlock(toAddr(textAddr));
        all.setName(".text");
        mem.split(all, toAddr(roAddr));
        MemoryBlock ro = mem.getBlock(toAddr(roAddr));
        ro.setName(".rodata");
        mem.split(ro, toAddr(dataAddr));
        MemoryBlock data = mem.getBlock(toAddr(dataAddr));
        data.setName(".data");
        MemoryBlock text = mem.getBlock(toAddr(textAddr));
        text.setPermissions(true, false, true);
        ro.setPermissions(true, false, false);
        data.setPermissions(true, true, false);
        Address bssStart = toAddr(dataAddr + ((dataSize + 0xfff) & ~0xfffL));
        // A patched code.bin (Project Restoration) carries extra code after
        // .data, loaded into the start of .bss: an executable block.
        MemoryBlock tail = mem.getBlock(bssStart);
        if (tail != null && tail == data) {
            mem.split(data, bssStart);
            tail = mem.getBlock(bssStart);
            tail.setName(".newcode");
            tail.setPermissions(true, true, true);
        }
        if (mem.getBlock(bssStart) == null) {
            MemoryBlock bss = mem.createUninitializedBlock(".bss", bssStart, bssSize, false);
            bss.setPermissions(true, true, false);
        }
        println(String.format("text %x+%x ro %x+%x data %x+%x bss %s+%x", textAddr, textSize, roAddr, roSize, dataAddr, dataSize, bssStart, bssSize));
        createFunction(toAddr(textAddr), "_start");
    }
}
