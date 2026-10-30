// Static research: seed candidate Xtensa entry instructions, never execute them.
// @category OCTool
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;

public class GraniteSeed extends GhidraScript {
    public void run() throws Exception {
        byte[] input = Files.readAllBytes(Path.of(currentProgram.getExecutablePath()));
        String sha = HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(input));
        if ((!sha.equals("5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596") &&
             !sha.equals("fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556")) ||
                !currentProgram.getLanguageID().toString().equals("Xtensa:LE:32:default")) {
            throw new IllegalArgumentException("Wrong firmware or analysis language");
        }
        setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "false");
        int candidates = 0, largeFrames = 0;
        for (int offset = 0; offset + 3 < input.length; offset += 4) {
            // ENTRY's upper twelve bits encode the frame size in units of
            // eight bytes. Requiring byte 2 == 0 loses frames >= 128 bytes,
            // including the indirect dispatch target at 0x1d7ec.
            // The low nibble of byte 1 selects a1 (the stack pointer).
            if ((input[offset] & 255) == 0x36 && (input[offset + 1] & 15) == 1) {
                // A decoding candidate only, not a claim that this is a valid
                // function or a reachable path. Post-analysis records failures.
                Address address = toAddr(offset);
                if (getFunctionContaining(address) == null && disassemble(address)) {
                    if (createFunction(address, null) != null) {
                        candidates++;
                        if (input[offset + 2] != 0) largeFrames++;
                    }
                }
            }
        }
        if (sha.equals("5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596") &&
                getFunctionAt(toAddr(0x1d7ec)) == null) {
            throw new IllegalStateException("Missing byte-checked large-frame dispatch target 0x1d7ec");
        }
        println("Seeded candidate entries: " + candidates + "; large frames: " + largeFrames);
    }
}
