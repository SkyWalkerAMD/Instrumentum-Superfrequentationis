# 原 AMD 命令传输与失败传播

日期：2026-10-08。接续[AMD初始化软件表](legacy-amd-initialization.md)，输入原ELF SHA
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
这里恢复原程序做了什么；不凭函数名确认固件命令含义，也不将实验当作硬件写入成功。
作者暂不能采集四台真机，继续离线研究；没有要求再次提供同一项采集。

## 可复现的行为

- `find_pci_dev2(0,0,0)`只是BDF位拼接，返回0，**没有搜索/枚举PCI设备**。
  默认SMU路径经原ABI桥和libpci包装访问domain0、bus0、device0、function0。
- `lock_amd_mutex`读I/O端口`0x4d0`，忙时最多再读10次；如果11次都非0，仍写入1并继续。
  等待耗尽没有失败返回分支。实验只记录端口数值，端口在特定主板上的意义未核实。
- `smu_cmd2`发送完立即进入固定延迟的两次读取，没有轮询应答完成。
  它不检查前18次PCI后端写入的返回值；两次读取后无论原数值如何，都走端口写0的释放路径。
- `retrieve_message2`返回第一次读值，并无条件把第二次读值写入输出指针。
  `retrieve_message`直接丢弃第一次读值，只返回第二次读值。
- 原`pci_read_long`在后端返回0时产生`0xffffffff`。实验在后端不写缓冲，确认该值确实来自原指令；
  继续穿过原包装器后，命令的返回值/输出也可能同时为`0xffffffff`，没有转换为单独的I/O错误。

上述行为在448项有界原指令实验中核对。没有把返回0、1、0xfc–0xff等数值自行定义成硬件状态；
报告中的`status`只是第一次读取的输入字段名称。原上层如何解释这些返回仍须按调用点继续分析。

## 地址与ABI

| 原函数 | 地址 | 本轮范围 |
|---|---|---|
| `find_pci_dev2(unsigned,unsigned,unsigned)` | `0x21db90` | 全函数，纯位运算 |
| `Wr_SMU_arg(unsigned,unsigned)` | `0x21eab0` | 全函数，默认BDF0 |
| `Wr_SMU_arg(unsigned,unsigned,unsigned)` | `0x21eb20` | 全函数，显式BDF |
| `reset_response` | `0x21eb80` | 全函数 |
| `retrieve_message` | `0x21eba0` | 全函数 |
| `retrieve_message2` | `0x21ec40` | 全函数 |
| `smu_cmd2` | `0x21f330` | 全函数，TSC入口为合成边界 |
| `lock_amd_mutex` / `release_amd_mutex` | `0x371260` / `0x3712d0` | 全函数，端口入口为合成边界 |
| `WritePciConfigDword` / `ReadPciConfigDword` | `0x3ac6b0` / `0x3ac770` | 原Microsoft ABI至SysV桥 |
| `libpci_write_dword` / `libpci_read_dword` | `0x36e800` / `0x36e690` | 原domain0参数/初始化标志/当前对象逻辑 |
| `pci_get_dev` | `0xa7fce0` | 原PCI对象身份字段赋值；分配为合成边界 |
| `pci_read_long` / `pci_write_long` | `0xa7fe90` / `0xa80030` | 原4字节访问，后端函数指针为合成边界 |

BDF位运算是`((bus & 255)<<8) | ((device & 31)<<3) | (function & 7)`。
这会截断超范围参数，不等价于验证目标存在。默认传入三个0，故固定访问`0000:00:00.0`。
`Wr_SMU_arg`采用SysV参数，转调PCI包装时使用`rcx=BDF,rdx=offset,r8=value`与shadow space；
包装再转回SysV进入内嵌libpci。实验保留整个转换，不把原代码直接套成单一ABI。

## 命令顺序和返回

一次`smu_cmd2(command,argument,out)`的正常控制流（括号内名称取自原ELF）：

1. 调`lock_tsc`，再调`lock_amd_mutex`。
2. 写`SMU_IOPORT=0`（reset_response）。
3. 顺序写`SMU_ARG0=argument低32位`、`ARG1=高32位`、`ARG2…5=0`。
4. 写`SMU_DATAPORT=command低8位`；511截断为255。
5. 写PCI offset`0xf8`选择`SMU_IOPORT`，延迟1000微秒；读`0xfc`得到第一次读值，再延迟1000微秒。
6. 写`0xf8`选择`SMU_ARG0`，延迟1000微秒；读`0xfc`得到第二次读值。
7. 第二次值写`out`，释放端口，返回第一次值（32位）。

步骤2–4的每次SMU写实际都是两次PCI写：先`0xf8=地址`、延迟1000微秒、再`0xfc=数据`、
再延迟1000微秒。完整命令合计18次PCI写、2次PCI读、19次1000微秒延迟请求。
这只是请求时长，不能当作Linux调度后的实测耗时；合成`sleep`不真正等待。
等待端口若一直忙，另有10次2000微秒延迟请求，随后照常发送命令。

原`pci_get_dev`每次分配一个对象，这条完整路径观察到20次合成对象分配。
本轮没有执行全程序清理/库分配器，不能只据这个计数断言进程最终泄漏总量。
实验将PCI配置缓存长度设为0，因此执行后端路径；缓存命中、不对齐fatal、分配失败和初始化失败
不在448项覆盖内。`libpci_initialize`只模拟成功；真实sysfs/procfs后端及错误回调仍未执行。

## 与EL8–EL10移植的关系

运行库闭包和新`/dev/mydev`模块不能自动代替这里的直接I/O端口/libpci/MSR路径。
如果I/O权限/lockdown/backend可用性变化，等待策略和错误值的传播也不会被换发行版自动修复。
本次没有新增权限、跳过检测或用合成身份启动原窗口；GUI调用点、协议、HAL、内核模块保持原样。

尤其不能把命令调用视作只读：静态检查`lock_tsc_all_threads`（`0x21e920`）发现它循环调用
`RdmsrTx(0xc0010015,...)`，将低32位OR`0x200000`后调用`WrmsrTx`；该函数本身没有恢复旧值。
本轮只保存这段静态指令，**不运行它**。函数名与位操作不作为寄存器功能/单位的官方定义，
线程数量和前置拓扑构造也没有在此实验中核实。后续恢复AMD面板要审计这些隐含写入及调用者反馈。

## 上层直接调用清单（静态证据）

对有长度声明的可执行ELF函数逐条核对后，找到5个槽内的8条直接CALL；没有把这个计数当作全部SMU
调用或所有间接路径。分析JSON附各函数完整SHA及调用前后指令。其他代码也可能绕过smu_cmd2直接访问。

| `cpufunctions`槽后缀 | 槽入口 | smu_cmd2调用点 |
|---|---|---|
| `on_per_ccx_oc_apply_3_clicked` | `0x750170` | `0x750297` |
| `on_per_ccx_oc_apply_4_clicked` | `0x7502d0` | `0x7507ed` |
| `on_per_ccx_oc_apply_8_clicked` | `0x7517d0` | `0x751a8b`、`0x751a99` |
| `on_per_ccx_oc_apply_19_clicked` | `0x7520b0` | `0x7521a8` |
| `on_smnmailbox_cmd_apply_clicked` | `0x7548c0` | `0x7549dc`、`0x754d50`、`0x754d7a` |

其中两个短槽可以静态完整跟进，但尚未加入本页448项动态实验：

- `_3`测试对象`+0xf88`：为0时以常量`command=0x25,argument=1`调用smu_cmd2；非0时调用
  另一条`MP1_C2PMSG(0x18,1,out)`路径。两支返回后都直接显示原字符串`Applied!`，不检验返回或输出。
  `+0xf88`的完整来源和实际控件绑定仍待恢复，不凭偏移给该标志命名为具体CPU系列。
- `_19`从`this+0x1008 -> Ui+0x268`取文字，先`QString::toInt(ok=null,base=10)`，再取一次文字判断非空。
  非空时把解析结果**有符号扩展至64位**后调用`smu_cmd2(0x56,argument,out)`，返回值和输出均不处理。
  若解析结果为-1，静态指令应送`0xffffffffffffffff`，而非只给低32位；未执行真实Qt解析，未定义单位。

2026-10-09补充：两个短槽及MP1已加入独立563项[原指令实验](legacy-amd-mp1.md)，仍不计入本页448项。
实际控件连接/完整构造/Qt解析及固件定义仍须继续核对，再决定重构界面的错误反馈。
两个完整短槽的[反汇编文本](validation/legacy-amd-short-callers.txt)一并保留，合计339+272字节；
可用已有`analysis/tools/elf-runtime-audit.py`的`--instructions --functions`选这两个完整符号复核，
实际SHA须与本页输入一致。调用上下文JSON和完整文本都只是静态证据。

## 复现、门禁与未决项

公开fixture含16个完整函数、1,876字节原指令，SHA
`11b9e60fa785bb4f5c37c9a2f1be40579e4c1aaf58703551b3eb44e4f29231dc`。
原ELF初始SMU地址和先前已固定的三套profile可重复选择，依赖fixture SHA也被检查。
PC/指令白名单拒绝未知执行；所有PCI后端/端口/TSC/分配/初始化/延迟为Unicorn内合成回调，
不执行宿主文件/设备/syscall/IN/OUT/MSR，也不加载内核模块。

448项包括：260种32位第一次读值、8种丢弃第一次读值、64个profile/64位参数/命令组合、
24个写/读/单次读/延迟失败与持续忙组合、16个端口等待边界、1个释放、11个SMU写入口、64个BDF组合。
其中持续忙和失败传播是**刻画原缺陷的预期结果**，并非已修复这些缺陷。
3项独立回归分别固定持续忙仍发送、原libpci失败哨兵、失败写与零状态仍继续。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-amd-transport.py \
  --binary build/input-audit/octool --export-fixture analysis/fixtures/legacy-amd-transport.json \
  --analysis-output docs/validation/legacy-amd-transport-analysis.json \
  --output docs/validation/legacy-amd-transport-windows.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-amd-transport.py \
  --fixture analysis/fixtures/legacy-amd-transport.json --output build/amd-transport.json
```

[静态地址/指令](validation/legacy-amd-transport-analysis.json)和
[Windows原指令观测](validation/legacy-amd-transport-windows.json)随源码保存，接入portability前置门禁。
`ac0ed66 / 37769633840`中Linux448项已通过且与Windows除environment完全一致，分析26项无跳过；
该轮Debian11显示连接失败，所以这里只签[原指令门禁证据](validation/legacy-transport-gate-ac0ed66.json)。
还未恢复所有SMU调用者、消息的硬件定义、原上层错误提示、并发竞争、实际PCI后端、真实权限失败
以及用户TRX50平台上的完成时序。不可把本轮结果称作整页恢复或真机兼容验收。
