// Exercise the actual compiled decoder on a separate synthetic program.
// @category OCTool
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.database.ProgramDB;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.pcode.PcodeOp;
import java.nio.file.Files;
import java.nio.file.Path;

public class SmuFpSelfTest extends GhidraScript {
    private int checks;
    private ProgramDB probe;
    private Address cursor;

    private Instruction decode(int word) throws Exception {
        Address at = cursor;
        cursor = cursor.add(8);
        probe.getMemory().setBytes(at, new byte[]{(byte)word, (byte)(word>>8), (byte)(word>>16)});
        new DisassembleCommand(at, null, false).applyTo(probe, monitor);
        return probe.getListing().getInstructionAt(at);
    }
    private void require(boolean condition, String message) {
        if(!condition) throw new IllegalStateException(message);
    }
    private void reg(Instruction ins, int operand, int number) {
        Object[] objects=ins.getOpObjects(operand);
        require(objects.length==1 && objects[0] instanceof Register &&
            ((Register)objects[0]).getName().equals("f"+number), "Wrong FP operand: "+ins);
    }
    public void run() throws Exception {
        probe=new ProgramDB("OCTool synthetic FP decoder checks", currentProgram.getLanguage(),
            currentProgram.getLanguage().getCompilerSpecByID(currentProgram.getCompilerSpec().getCompilerSpecID()), this);
        int tx=probe.startTransaction("synthetic opcode checks");
        try {
            cursor=probe.getAddressFactory().getDefaultAddressSpace().getAddress(0x1000);
            probe.getMemory().createInitializedBlock("synthetic",cursor,0x20000,(byte)0,monitor,false);
            long[] constants={0,0x3f800000L,0x40000000L,0x3f000000L};
            for(int r=0;r<16;r++) for(int s=0;s<16;s++) {
                Instruction ins=decode(0xfa0030 | r<<12 | s<<8);
                if(s>=4) require(ins==null,"Reserved CONST.S decoded");
                else {
                    require(ins!=null && ins.getMnemonicString().equals("const.s"),"Missing CONST.S");
                    reg(ins,0,r);
                    require(ins.getScalar(1).getUnsignedValue()==s,"Wrong constant selector");
                    boolean copy=false;
                    for(PcodeOp op:ins.getPcode()) if(op.getOpcode()==PcodeOp.COPY &&
                        op.getOutput().isRegister() && op.getOutput().getOffset()==0x100+4*r &&
                        op.getInput(0).isConstant() && op.getInput(0).getOffset()==constants[s]) copy=true;
                    require(copy,"Wrong CONST.S p-code value");
                }
                checks++;
            }
            String[] names={"div0","recip0","sqrt0","rsqrt0","nexp01","mksadj","mkdadj","addexp","addexpm"};
            for(int kind=0;kind<names.length;kind++) for(int r=0;r<16;r++) for(int s=0;s<16;s++) {
                Instruction ins=decode(0xfa0000 | (kind+7)<<4 | r<<12 | s<<8);
                require(ins!=null && ins.getMnemonicString().equals(names[kind]+".s"),"Missing opaque unary operation");
                reg(ins,0,r); reg(ins,1,s);
                boolean opaque=false;
                for(PcodeOp op:ins.getPcode()) if(op.getOpcode()==PcodeOp.CALLOTHER) opaque=true;
                require(opaque,"Approximation must not become numeric arithmetic");
                checks++;
            }
            for(int kind=0;kind<2;kind++) for(int r=0;r<16;r++) for(int s=0;s<16;s++) for(int t=0;t<16;t++) {
                Instruction ins=decode((kind+6)<<20 | 0xa0000 | r<<12 | s<<8 | t<<4);
                require(ins!=null && ins.getMnemonicString().equals((kind==0?"maddn":"divn")+".s"),"Missing opaque ternary operation");
                reg(ins,0,r); reg(ins,1,s); reg(ins,2,t);
                boolean opaque=false;
                for(PcodeOp op:ins.getPcode()) if(op.getOpcode()==PcodeOp.CALLOTHER) opaque=true;
                require(opaque,"Refinement must remain opaque");
                checks++;
            }
            String[] conversions={"round","trunc","floor","ceil","utrunc"};
            int[] codes={8,9,10,11,14};
            for(int kind=0;kind<codes.length;kind++) for(int r:new int[]{0,7,15}) for(int s:new int[]{0,7,15}) for(int t=0;t<16;t++) {
                Instruction ins=decode(codes[kind]<<20 | 0xa0000 | r<<12 | s<<8 | t<<4);
                require(ins!=null && ins.getMnemonicString().equals(conversions[kind]+".s"),"Missing integer conversion");
                Object[] objects=ins.getOpObjects(0);
                require(objects.length==1 && objects[0] instanceof Register &&
                    ((Register)objects[0]).getName().equals("a"+r), "Wrong integer destination");
                reg(ins,1,s);
                require(ins.getScalar(2).getUnsignedValue()==t,"Wrong conversion scale");
                boolean opaque=false;
                for(PcodeOp op:ins.getPcode()) {
                    if(op.getOpcode()==PcodeOp.CALLOTHER) opaque=true;
                    require(op.getOpcode()!=PcodeOp.FLOAT_MULT,"Stock integer-as-float scale survived");
                }
                require(opaque,"Conversion rounding/overflow must stay explicit");
                checks++;
            }
            boolean postincrement=getScriptArgs().length>1 && getScriptArgs()[1].equals("granite-postincrement");
            if(postincrement) {
                String[] updateNames={"lsip","ssip","lsxp","ssxp"};
                for(int kind=0;kind<4;kind++) for(int f:new int[]{0,7,15}) for(int base=0;base<16;base++)
                    for(int step:new int[]{0,1,7,15}) {
                    boolean indexed=kind>=2, store=(kind&1)!=0;
                    int opcode=indexed ? (store?0x580000:0x180000)|f<<12|base<<8|step<<4
                        : (store?0xc003:0x8003)|f<<4|base<<8|step<<16;
                    Instruction ins=decode(opcode);
                    require(ins!=null && ins.getMnemonicString().equals(updateNames[kind]),"Wrong FP update variant");
                    reg(ins,0,f);
                    Register baseReg=probe.getRegister("a"+base);
                    int memoryAt=-1,updateAt=-1,index=0;
                    for(PcodeOp op:ins.getPcode()) {
                        if(op.getOpcode()==(store?PcodeOp.STORE:PcodeOp.LOAD)) {
                            require(memoryAt<0,"Multiple memory operations");
                            require(op.getInput(1).isRegister() && op.getInput(1).getOffset()==baseReg.getOffset(),
                                "FP update accessed incremented address");
                            memoryAt=index;
                        }
                        if(op.getOutput()!=null && op.getOutput().isRegister() && op.getOutput().getOffset()==baseReg.getOffset()) updateAt=index;
                        index++;
                    }
                    require(memoryAt>=0 && updateAt>memoryAt,"FP pointer must update after memory access");
                    checks++;
                }
            }
            Path output=Path.of(getScriptArgs()[0]);
            Files.writeString(output,"{\"schema\":1,\"hardware_execution\":false,\"firmware_modified\":false,\"synthetic_cases\":"+checks+",\"passed\":true}\n");
            println("Synthetic FP decoding checks passed: "+checks);
        } finally {
            probe.endTransaction(tx,false);
            probe.release(this);
        }
    }
}
