# 原 Intel NGU：输入、附带操作与MMIO请求地址

日期：2026-10-09；原ELF SHA
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
本页以未修改原指令一路执行到原96字节请求，确认读写目标差异；所有系统/硬件应答均为合成。
未改原GUI、生产GUI、HAL、模块或协议，没有对任何机器执行这些写入。

## 已确认的地址差异

`RW_MMIO::Rd_MMIO(unsigned long)`先取`this+0x18`，加上传入值，再调用原Read_MMIO。
`RW_MMIO::Wr_MMIO(unsigned,unsigned)`只有22字节，不读取this；把第一个参数低32位直接送Write_MMIO。
原Write_MMIO在模块标志非零时也不再加基址，直接交给Write_MMIO_kernel生成请求。

| 合成前提/原调用 | 原请求opcode | 原请求data0地址 |
|---|---|---|
| 对象基址`123450000`，读`5da4` | `0x0c` | `123455da4` |
| 同一对象，写`5da0` | `0x0d` | `00005da0` |
| 同一对象，写`5da4` | `0x0d` | `00005da4` |

这不是反编译器对类型/参数的猜测：记录包含write@plt收到的完整96字节、fd、token与邮箱应答。
对象基址为0、低32位地址及大于4GB的地址都另测；读参数64位，写参数先截断为32位。
本轮只动态执行`MY_KMOD_LOADED=1`路径，原/dev/mem分支仍留在静态证据中，未执行其mmap或写内存。

在NGU完整函数中，NVL及非NVL分支都实际传`5da0/5da4`，不是传已经加过基址的地址。
NVL mailbox helper因此读加基址后的地址、写低地址；非NVL也生成两组低地址写请求。
这属于原上层地址生成行为；glibc/soname适配、DKMS和签名不会改变请求里的data0。
MMIO线级兼容仍要求模块按原data0处理，不能在内核里为某些数字偷偷补基址。

本页未核实这些请求在四台机器上的实际映射、接受性或硬件效果，也不据此猜出正确平台地址。
恢复NGU功能前须明确该平台寄存器定义和调用者地址契约；目前不把此写入放进重构GUI。

## 入口和已连接的UI证据

| 入口 | 地址 | 原代码字节 |
|---|---|---|
| `set_ngu_ratio(int)` | `0x2bfc20` | 571 |
| `intel_ctl6::ngu_clicked()` | `0x614cc0` | 719 |
| `intel_ctl3::on_pushButton_17_clicked()` | `0x5d0f70` | 719 |
| `NVL_MEM_CFG::write_BIOS_MAILBOX_DATA` | `0x2458e0` | 180 |
| `Rd_SAGV_CONFIG_POLICY_Tuner` | `0x384780` | 238 |
| RW_MMIO构造/析构/读/写 | `0x230de0/0x230e90/0x230ea0/0x230f20` | 165/5/27/22 |
| 原Read_MMIO/Write_MMIO | `0x36ef70/0x36f310` | 228/453 |

新fixture共3327字节，SHA
`25c9a574a7bd50bcb07a4fa1ff18fd0313690be3dc4f1231717582411fdb8e68`；
复用已固定的原Rdmsr/Wrmsr和两个kernel MMIO包装器。原调用ABI转换也在执行范围内。

[NVL控件证据](legacy-ui-connections.md)已确认`Ui+0x4c8`、objectName=`pushButton_17`、文字
`Apply NGU`的clicked连接到`ngu_clicked()`（method index7）。本轮完整槽从
`this+0x30 → left+0x30 → Ui+0x490`取文字；该输入objectName=`d1_69`。
原按钮可见性、构造完成、事件循环实际点击仍待真实Qt执行，不能由局部connect实验代替。
另一个ctl3槽取`this+0x4e0 → Ui+0x1498`，本页恢复槽体，不声明已核实该按钮全部绑定。

两个槽都先检查文字非空，再另取文字作十进制`toUInt(ok=null)`，把原32位返回送入int参数。
输入Qt返回`7fffffff`时上限夹到255，返回`80000000/ffffffff`时在有符号比较中成为负数，
不受仅有的上限限制。NVL后续MMIO data保留完整32位，MSR字段只保留低8位。
这里执行的是原槽/位运算；Qt解析结果明确为合成输入，没有执行任意文本的真实解析器。

空输入显示`Nothing to Write!`且不访问MSR/MMIO。正常返回后两槽均显示`Applied!`。
MSR写失败但后续合成读可返回时，以及NVL邮箱持续busy耗尽时，仍可到达该文字。
若原MSR读或原MMIO完整done判断陷入持续等待，则到不了提示，实验由指令上限截停。

## NGU完整访问顺序

先按有符号int执行`min(input,255)`，没有下限限制。

1. 向CPU0的MSR`0x150`写`high=80000710,low=0`。
2. 调原Rdmsr直到high的bit31为0，无次数/时间上限。
3. 再读一次`0x150`，此额外读取不复查busy；用这次low清低8位后OR输入低8位。
4. 向`0x150`写`high=80000711,low=合成值`，再次无限轮询high符号位。
5. 按GLOBAL_IS_NVL进入以下分支。原函数没有将系统调用错误转换成上层失败。

NVL分支：写MSR`0x607`的`low=80001222,high=0`，sleep1000us，读`0x608`。
随后写`0x607`的`low=80001322,high=刚读回的high`，sleep1000us，再读`0x608`。
最后通过缓存NVL_MEM_CFG（空时构造）调用`write_BIOS_MAILBOX_DATA(80001322,clamped_int32)`。
这些数值仅按原指令记录，未从NGU名称赋予频率、单位或硬件域含义。

非NVL分支：调用Rd_SAGV_CONFIG_POLICY_Tuner，返回值替换bit8..15为输入低8位，再置bit16。
构造RW_MMIO，依次写`5da0=结果、5da4=80000122`，完整重复一次后析构。
没有检查这些写的完成状态。SAGV读取的非GNR分支用`0x607/0x608`；GNR分支访问另一对象的
punit读方法。本轮GNR方法及GNR/NVL大构造器为显式合成边界，不声称它们内部的硬件路径已验证。

RW_MMIO完整小构造器已执行：PCI BDF0读`0x48`，低32位减1存入基址；NVL标志非零再读`0x4c`
拼接高32位。没有发现本构造器对0/ffffffff错误值的拒绝，相关输入另有实验；PCI后端在此为合成。

## NVL mailbox等待

`write_BIOS_MAILBOX_DATA`先读取加基址的`5da4`；若busy，最多再读100次，总计101。
无论是否仍忙，随后写`5da0=data`，再写`5da4=(command & 1fffffff) | 80000000`。
之后相同规则再读最多101次并返回；循环内无sleep，未转换为显式超时错误。
持续busy输入下，完整槽最终仍显示Applied。参数中bit29/30被清除，bit31被置位，保留原位运算。

这些busy与`/dev/mydev`邮箱done是两层条件：实验保持内核邮箱done=1、返回的MMIO数值busy=1，
才能观察此101次上限。若内核邮箱为errno高位done、无应答、短写等，原底层包装先无限等待。
新模块不能靠返回“成功零值”解决这种区别；失败兼容边界沿用[邮箱契约](legacy-mailbox-contract.md)。

## 覆盖、复现及剩余范围

266项实验包括204项函数级行为及62项完整槽；其中30项是预期持续轮询，由20000指令上限截停。
另4项关键回归固定原96字节目标地址、busy耗尽后Applied、unsigned→signed输入和第二次MSR轮询失败。
调用静态清单还记录NGU的第三个调用者`intel_ctl6::on_pushButton_12_clicked`，其完整业务未模拟。
同一32位Wr_MMIO重载在10个声明函数中有28个直接CALL；已记录各处上下文/函数SHA。
这不说明28处都传偏移，也不代表间接/内联调用齐全，须逐调用者确认地址契约。

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-intel-ngu.py \
  --binary build/input-audit/octool --export-fixture analysis/fixtures/legacy-intel-ngu.json \
  --analysis-output docs/validation/legacy-intel-ngu-analysis.json \
  --output build/intel-ngu.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/legacy-intel-ngu.py \
  --output build/intel-ngu.json
```

静态证据：[完整函数/调用上下文](validation/legacy-intel-ngu-analysis.json)；
动态证据：[Windows原指令观测](validation/legacy-intel-ngu-windows.json)。已接入CI，云端结果单独归档。
未执行真实文件系统MSR/PCI、原/dev/mem路径、固件、内核或完整Qt；没有套用用户CPU营销型号。
下一步跟进10个Wr_MMIO调用者、NVL读mailbox、剩余NGU槽及四平台寄存器规格。
