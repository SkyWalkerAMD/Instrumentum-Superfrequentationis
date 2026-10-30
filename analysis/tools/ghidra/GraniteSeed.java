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
        int candidates = 0;
        for (int offset = 0; offset + 3 < input.length; offset += 4) {
            if ((input[offset] & 255) == 0x36 && input[offset + 2] == 0) {
                // A decoding candidate only, not a claim that this is a valid
                // function or a reachable path. Post-analysis records failures.
                Address address = toAddr(offset);
                if (getFunctionContaining(address) == null && disassemble(address)) {
                    if (createFunction(address, null) != null) candidates++;
                }
            }
        }
        println("Seeded candidate entries: " + candidates);
    }
}
