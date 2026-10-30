// Selected static SMU observations; no emulation or hardware execution.
// @category OCTool
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import com.google.gson.GsonBuilder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.*;

public class GraniteStudy extends GhidraScript {
    private Map<String,Object> record(Object... pairs) {
        Map<String,Object> result = new LinkedHashMap<>();
        for (int i=0; i<pairs.length; i+=2) result.put((String)pairs[i], pairs[i+1]);
        return result;
    }
    public void run() throws Exception {
        Path output = Path.of(getScriptArgs()[0]);
        Files.createDirectories(output);
        boolean mp5=getScriptArgs().length>1 && getScriptArgs()[1].equals("mp5");
        Map<String,Object> result = record("schema", 1, "hardware_execution", false,
            "source_sha256", mp5?"fb5d96c17b909cc4f681523ccc931796e27c7457ba5ad637a3d4fce1c78ce556":
                "5d7951d30fefea6c5f435ac219fe4879adb0d8f66727211e32af1ff89d6b7596",
            "component",mp5?"MP5":"SMU",
            "decoder_extension", "OCTool static Xtensa FP; see xtensa-fp-extension.json",
            "numeric_fp_emulation", false,
            "language", currentProgram.getLanguageID().toString());
        String actual=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(
            Files.readAllBytes(Path.of(currentProgram.getExecutablePath()))));
        if(!actual.equals(result.get("source_sha256")))
            throw new IllegalArgumentException("Component hash does not match the requested study");
        LinkedHashMap<Address,Integer> selected = new LinkedHashMap<>();
        List<Object> targets = new ArrayList<>();
        // Fresh platform: no Shimada addresses, command meanings or data bases
        // are imported. Keep literal references only as discovery candidates.
        List<Object> literalRefs = new ArrayList<>();
        Set<Long> literals = Set.of(0x620205L,0x620105L,0x625400L,0x994L,0x724L);
        for(long offset=0;offset<0x40000;offset+=4) {
            long value=Integer.toUnsignedLong(getInt(toAddr(offset)));
            if(!literals.contains(value)) continue;
            for(Reference ref:getReferencesTo(toAddr(offset))) {
                Function fn=getFunctionContaining(ref.getFromAddress());
                literalRefs.add(record("literal",Long.toHexString(offset),"value",Long.toHexString(value),
                    "from",ref.getFromAddress().toString(),"function",fn==null?null:fn.getEntryPoint().toString()));
                if(fn!=null) selected.put(fn.getEntryPoint(),0);
            }
        }
        {
            // Consumers can receive aliases through startup data or helper
            // arguments. Retain every discovered function in both components;
            // absence from a selected-function study is not absence of a use.
            var iterator=currentProgram.getFunctionManager().getFunctions(true);
            while(iterator.hasNext()) selected.putIfAbsent(iterator.next().getEntryPoint(),0);
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        List<Object> functions = new ArrayList<>();
        Set<Address> done = new HashSet<>();
        while (done.size()<selected.size() && done.size()<1800) {
            Address address = selected.keySet().stream().filter(a -> !done.contains(a)).findFirst().get();
            done.add(address);
            Function fn = getFunctionAt(address);
            if (fn==null) {
                functions.add(record("entry", address.toString(), "error", "No exact function"));
                continue;
            }
            DecompileResults dc = decompiler.decompileFunction(fn, 45, monitor);
            String pseudo = dc.getDecompiledFunction()==null ? "" : dc.getDecompiledFunction().getC();
            List<Object> calls = new ArrayList<>();
            int depth=selected.get(address);
            List<Function> callees = new ArrayList<>(fn.getCalledFunctions(monitor));
            callees.sort(Comparator.comparing(f -> f.getEntryPoint().getOffset()));
            for (Function callee : callees) {
                calls.add(callee.getEntryPoint().toString());
                if(depth<2) selected.putIfAbsent(callee.getEntryPoint(),depth+1);
            }
            List<Object> instructions=new ArrayList<>();
            Set<String> opaqueFpOperations=new TreeSet<>();
            Set<String> opaqueMnemonics=Set.of("div0.s","recip0.s","sqrt0.s","rsqrt0.s",
                "nexp01.s","mksadj.s","mkdadj.s","addexp.s","addexpm.s","maddn.s","divn.s",
                "round.s","trunc.s","floor.s","ceil.s","utrunc.s");
            var iterator=currentProgram.getListing().getInstructions(fn.getBody(),true);
            while(iterator.hasNext() && instructions.size()<4096) {
                Instruction ins=iterator.next();
                instructions.add(record("address",ins.getAddress().toString(),"text",ins.toString(),
                    "bytes",HexFormat.of().formatHex(ins.getBytes())));
                if(opaqueMnemonics.contains(ins.getMnemonicString())) opaqueFpOperations.add(ins.getMnemonicString());
            }
            functions.add(record("entry",address.toString(),"name",fn.getName(),"depth",depth,
                "decompile_completed",dc.decompileCompleted(),"error",dc.getErrorMessage(),
                "pseudocode",pseudo,"calls",calls,"instructions",instructions,
                "bad_instruction_truncation",pseudo.contains("halt_baddata"),
                "opaque_fp_operations",opaqueFpOperations,
                "instructions_truncated",iterator.hasNext()));
            Files.writeString(output.resolve("function-"+address.toString()+".txt"),pseudo);
        }
        decompiler.dispose();
        result.put("targets",targets); result.put("literal_references",literalRefs);
        result.put("functions",functions); result.put("selection_count",selected.size());
        result.put("exported_count",done.size());
        result.put("function_selection_truncated",done.size()<selected.size());
        Files.writeString(output.resolve("smu-study.json"),new GsonBuilder().setPrettyPrinting().create().toJson(result));
        println("Exported selected static functions: "+done.size());
    }
}
