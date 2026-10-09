# 原公共邮箱、FIVR及AMD FCH位运算

日期：2026-10-09，原ELF SHA
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
本页只恢复原指令语义及请求，不赋予未核实的硬件含义/单位，不新增生产写入功能。
全部设备、PCI应答和libc接口由模拟器提供；原函数、MSR叶函数、MMIO路由和96字节包装器实际执行。

## PollMailboxReady：耗尽也返回0

`PollMailboxReady(unsigned)@0x23f580`，216字节，先执行RW_MMIO构造器。
type=1读取`base+5da4`的bit31；type=3经原Rdmsr读MSR607，检查**低32位**的bit31，
不看高32位。其他type将内部busy维持1，不读MSR/MMIO，但仍有构造器PCI读取。

所有type每轮调用usleep(1000)，包括首读已经ready的轮次。最多10轮；退出都清eax返回0。
没有超时返回值，甚至不支持的type也返回0。原MSR叶函数仍不检查open/seek/read/close错误；
这里的次数上限不修复错误读缓冲。内核MMIO邮箱层若无法完成，则连这一层预算也到不了。

## MailboxRead：仍含写入，并且一致性比较为AND

`MailboxRead(type,command,data*,status*)@0x23f660`，594字节：
调用PollMailboxReady；即使poll耗尽返回0也继续。再构造RW_MMIO，支持两条路径：

| type | 原命令发出 | 原读取/输出 |
|---|---|---|
| 1 | 32位Wr_MMIO向**低地址5da4**写`command | 80000000` | 再poll；读base+5da4、base+5da0；sleep10us；再读同一对 |
| 3 | MSR608写`high=0,low=*data`；MSR607写`high=0,low=command|80000000` | 再poll；读607/608；sleep10us；再读607及经AsmReadMsr64读608 |

type1不写5da0；type3却先把调用者的输入data写到608。两路都不清除command的bit29/30，
这与[NVL读/写helper](legacy-mmio-clients.md)的1fffffff掩码不同，不可合成同一抽象而丢掉差异。

两对样本**只有status低32位变化且data低32位也变化**才报`0x8000000000000002`。
status单独改变、data单独改变，以及MSR高32位单独改变，都返回0，并输出第一份data低32位和
第一份status低8位。原指令实验分别覆盖四种变化组合及高位变化。
不支持type在10轮poll后输出诊断，返回`0x8000000000000003`。两个错误码路径都不改调用者输出。

MSR系统调用失败另有9种实验；它们仍能返回0。read-error/EOF/open-error的本实验中输出为3/3，
这是此合成初始栈和原函数实际复用栈内容所得，不是任何真实失败固定应返回3的定义。
成功读取/短读等的完整前后缓冲留在JSON，未用“失败填零”隐藏原数据来源。
函数名有Read也不能列作只读硬件操作。

## FIVR两个函数

两函数调用`find_pci_dev2(0,0,0)`；本页执行其25字节原指令，它只是打包BDF，不执行PCI查找。
这些实参必然得0，随后与-1比较不会证明设备存在。RW_MMIO构造后又读取PCI48，按低32位减1，
覆盖对象基址：即使NVL构造读过PCI4c高位，此处也丢弃了高32位。
PCI配置访问在本实验为合成后端，不冒充实际libpci枚举。

`Wr_intel_fivr_spread(int)@0x23a1b0`，183字节：
读`low_base+5a08`，计算`(old & ffffff00) | input32 | 100`，向低地址5a08写。
input没有限制在8位；合成input=80000000、old=0，结果为80000100，能改动高位。

`Wr_intel_fivr_fsw(unsigned short)@0x23a320`，266字节：
读`low_base+5a18`；input仅保留低16位。原binary64常量来自`0x1620948`，
字节`0000000000000840`（值3.0），不是从界面标签猜出的比例。
原除法、截断、整数乘移位、上限夹值可等价写为：

```text
n = input & 0xffff
q = min(floor((n + 3) / 9), 1023)
new = (old & 0xff00c7ff) | ((q & 7) << 11) | ((q >> 3) << 16)
write(0x5a18, new)
```

这是16位非负输入域的整数等价式，不是频率/电压换算规格。位23被清除但q不足以置回。
实验覆盖5/6、14/15、9203/9204、9212/9213边界和65536截成0，以及旧值全0/全1。
尚未核实这两个函数的全部上层合法输入范围，因此异常实参是接口反例，不代表UI一定可输入。

## SET_DYNAMIC_FREQ及AMD FCH

`SET_DYNAMIC_FREQ(unsigned char)@0x23a760`，201字节：构造后再次读取PCI基址（NVL含4c），
向低地址5da0写input低8位、5da4写80000122，无MMIO状态读取或完成验证。

`RW_MMIO_AMD::Wr_FCH_MISC@0x232310`，114字节，按this+3c的32位值覆盖基址。
读base+offset；范围参数start/end无有效性检查。掩码宽度使用x86的**32位shift计数取低5位**，
之后放到64位值上左移start（计数取低6位）；最终写数据截为32位，目标地址仍是offset低32位。
因此start=0,end=31的“32位范围”得到mask=0，字段保持旧值，不会按输入替换。
end<start、start>=32/64等也不被拒绝，位运算及截断结果留证；未声称这些参数来自真实上层。

随后无条件tail-call `CG1_cfg_update_req@0x2322e0`（47字节）：读base+40，OR40000000，
写低地址40。即使前面的字段没有改变，仍发送update；没有把其硬件意义扩展为已验证的提交协议。

## 覆盖与复现

9函数1723字节新fixture，SHA
`09acb5fe81cf16c1ac7d0181490ab4c13502ed36b2e28db39bccee085c66ce56`；
另带8字节原常量，复用[NGU/MSR/MMIO固定fixture](legacy-intel-ngu.md)。
260项刻画含4项预期底层持续等待；6项关键回归覆盖耗尽返回0、AND比较、高位忽略、FIVR基址/输入、
FCH全宽范围和量化边界。Windows已通过，CI已接好；无真实设备、完整Qt或平台写入验收。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-mmio-services.py \
  --output build/mmio-services.json
PYTHONPATH="$PWD/build/reference-tools" python3 -m unittest discover \
  -s analysis/tests -p 'test_mmio_services.py' -v
```

[静态指令/原常量](validation/legacy-mmio-services-analysis.json)；
[260项动态观测](validation/legacy-mmio-services-windows.json)。
32位Wr_MMIO的10个直接调用者现有9个执行到原请求，第10个CpmReadWriteI2CBytes5继续研究。
这不是全部MMIO用法，也不是9个上层面板已经恢复；未知固件定义、间接调用、完整构造/线程还未解决。
