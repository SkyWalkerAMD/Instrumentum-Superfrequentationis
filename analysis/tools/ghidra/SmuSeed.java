// Static research: seed candidate Xtensa entry instructions, never execute them.
// @category OCTool
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;

public class SmuSeed extends GhidraScript {
    public void run() throws Exception {
        byte[] input = Files.readAllBytes(Path.of(currentProgram.getExecutablePath()));
        String sha = HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(input));
        if ((!sha.equals("a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7") &&
             !sha.equals("b5e9c94ef3a27d55a95c1a0d703cc7041b734f9475d44b8eecd49a15794cdeaf")) ||
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
