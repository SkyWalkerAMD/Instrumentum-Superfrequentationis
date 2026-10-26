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
import java.util.*;

public class SmuStudy extends GhidraScript {
    private Map<String,Object> record(Object... pairs) {
        Map<String,Object> result = new LinkedHashMap<>();
        for (int i=0; i<pairs.length; i+=2) result.put((String)pairs[i], pairs[i+1]);
        return result;
    }
    public void run() throws Exception {
        Path output = Path.of(getScriptArgs()[0]);
        Files.createDirectories(output);
        Map<String,Object> result = record("schema", 1, "hardware_execution", false,
            "source_sha256", "a2f3724ccaef1d67db04afb553816f111dc86207480b28d7998cc15fa92ea7d7",
            "language", currentProgram.getLanguageID().toString());
        LinkedHashMap<Address,Integer> selected = new LinkedHashMap<>();
        List<Object> targets = new ArrayList<>();
        // Queue identities are hypotheses until queue dispatch and mailbox
        // descriptor references are checked. Keep numeric provenance explicit.
        for (long base : new long[]{0x7980, 0x7c38}) {
            int[] ids = base == 0x7980 ? new int[]{1,2,0x3c,0x3d,0x3e,0x56} :
                new int[]{1,2,3,4,5,0x5c,0x5d,0x5e,0x61,0x6f,0x70,0xa3,0xa6,0xa7,0xa8};
            for (int id : ids) {
                long entry = base + 8L*id;
                Address function = toAddr(Integer.toUnsignedLong(getInt(toAddr(entry))));
                targets.add(record("table", Long.toHexString(base), "id", id,
                    "entry", Long.toHexString(entry), "function", function.toString(),
                    "flags", Integer.toUnsignedLong(getInt(toAddr(entry+4)))));
                selected.put(function, 0);
            }
        }
        // Prioritize PM materialization and OC application before expanding
        // generic callers. Addresses belong only to the pinned SMU body.
        for(long entry:new long[]{0x2f750,0x30594,0x3026c,0x2f918,0x323d8})
            selected.put(toAddr(entry),0);
        List<Object> literalRefs = new ArrayList<>();
        for (long offset : new long[]{0x17f20,0x17f24,0x17f28,0x17f2c,0x17f30,0x17f34,0x8748,0x87ac,0x7880}) {
            for (Reference ref : getReferencesTo(toAddr(offset))) {
                Function fn = getFunctionContaining(ref.getFromAddress());
                literalRefs.add(record("literal",Long.toHexString(offset),"from",ref.getFromAddress().toString(),
                    "function",fn==null?null:fn.getEntryPoint().toString()));
                if (fn!=null) selected.put(fn.getEntryPoint(),0);
            }
        }
        // Trace uses of independently observed data bases as well as the literal
        // pools. This discovers initialization and writers without treating a
        // pointer's presence as proof of a field's meaning.
        Set<Long> dataBases = Set.of(0xe108L,0xe040L,0xb318L,0x8894L,0x1abc0L,
            0x1d300L,0x1ab80L,0x9124L,0x8748L);
        for(long offset=0;offset<0x20000;offset+=4) {
            long value=Integer.toUnsignedLong(getInt(toAddr(offset)));
            if(!dataBases.contains(value) && !(value>=0xe100 && value<0xe290) &&
               !(value>=0x1abc0 && value<0x1d940)) continue;
            for(Reference ref:getReferencesTo(toAddr(offset))) {
                Function fn=getFunctionContaining(ref.getFromAddress());
                literalRefs.add(record("literal",Long.toHexString(offset),"value",Long.toHexString(value),
                    "from",ref.getFromAddress().toString(),"function",fn==null?null:fn.getEntryPoint().toString()));
                if(fn!=null) selected.put(fn.getEntryPoint(),0);
            }
        }
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        List<Object> functions = new ArrayList<>();
        Set<Address> done = new HashSet<>();
        while (done.size()<selected.size() && done.size()<280) {
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
            for (Function callee : fn.getCalledFunctions(monitor)) {
                calls.add(callee.getEntryPoint().toString());
                if(depth<2) selected.putIfAbsent(callee.getEntryPoint(),depth+1);
            }
            List<Object> instructions=new ArrayList<>();
            var iterator=currentProgram.getListing().getInstructions(fn.getBody(),true);
            while(iterator.hasNext() && instructions.size()<4096) {
                Instruction ins=iterator.next();
                instructions.add(record("address",ins.getAddress().toString(),"text",ins.toString(),
                    "bytes",HexFormat.of().formatHex(ins.getBytes())));
            }
            functions.add(record("entry",address.toString(),"name",fn.getName(),"depth",depth,
                "decompile_completed",dc.decompileCompleted(),"error",dc.getErrorMessage(),
                "pseudocode",pseudo,"calls",calls,"instructions",instructions));
            Files.writeString(output.resolve("function-"+address.toString()+".txt"),pseudo);
        }
        decompiler.dispose();
        result.put("targets",targets); result.put("literal_references",literalRefs);
        result.put("functions",functions); result.put("selection_count",selected.size());
        result.put("exported_count",done.size());
        Files.writeString(output.resolve("smu-study.json"),new GsonBuilder().setPrettyPrinting().create().toJson(result));
        println("Exported selected static functions: "+done.size());
    }
}
