# 原 MSR 失败传播与 UI 写路径

日期：2026-10-08。输入原 ELF SHA固定为 `44598dc8…aff10`。
本页继续[平台分派](legacy-platform-dispatch.md)和[Qt方法入口](legacy-qt-callbacks.md)，
分析两个完整槽体到原始 MSR 文件接口的调用链。原 GUI/协议/模块/HAL 均未修改。

## 已核实的新问题

原 `Wrmsr` 与 `Rdmsr` 都有相同大小的栈帧，8字节 I/O 临时缓冲区均在自身 `rsp+8`。
在这里两个槽的写→读调用序列中，这两处对应**相同虚拟地址**：

1. UI先发查询，`Wrmsr` 在栈上放低32位0、高32位 `0x80000110`（GT）或 `0x80000810`（NPU）。
2. 即使 open/lseek/write 失败，这个栈上赋值也已发生；Wrmsr不检查这些返回值。
3. 随后 Rdmsr 使用同一处栈空间。其 read 失败/EOF 时没有覆盖数据，原函数仍把缓冲区复制给调用者。
4. 槽检测高32位的符号位，仍看到上次查询中的bit31=1，于是继续轮询；循环没有次数/时间上限。

所以此处不只是“未初始化值可能随机”：**前一次写命令可以稳定地变成失败读的伪应答**。
148个有界原指令实验中的24个错误/短读/busy条件在2,000条指令处仍轮询，符合已恢复控制流。
模拟器没有预先给读缓冲区塞忙标志，初次读之前的值来自实际执行的原Wrmsr栈写指令。
该实验不意味着所有原MSR调用都必然以相同方式失败；目前动态核对的是下列两个完整槽。

这与EL8–EL10的关联：若 `/dev/cpu/0/msr` 不存在、无权限，或后端不能完整读取，
加载器兼容/新内核模块加载都不能自动修复这条直接文件访问链。原 `/dev/mydev` MMIO 保护层
也不覆盖它。没有通过增加runner权限或伪造硬件启动旧GUI来验证。

## 两个槽的确定行为

| 原函数/地址 | 查询MSR0x150高32位 | 最终写MSR0x150高32位 |
|---|---|---|
| `intel_ctl6::gt_clicked` / `0x614f90` | `0x80000110`，调用点`0x615070` | `0x80000111`，调用点`0x6150bc` |
| `intel_ctl6::npu_clicked` / `0x6152e0` | `0x80000810`，调用点`0x6153c0` | **同为`0x80000111`**，调用点`0x61540c` |

NPU查询/写命令不对称是原字节证据，不是抄写错误；尚不能没有规范就把最终值“修正”为
`0x80000811`，也不能据函数名确认这条指令实际影响哪个硬件域。此项仅记录为待核对旧行为。

两个槽仅检查文字长度非0，之后调用 `QString::toUInt(ok=null, base=10)`。
原运算把返回值截断到低8位：`256→0`、`257→1`、`0xffffffff→255`。
本实验给的是合成转换结果；**没有执行Qt字符串解析**，不据此穷举真实文本输入格式。

设 `old` 为合成读返回的低32位、`value` 为合成toUInt结果，实际最终低32位为：

```text
upper = old & 0xffffff00
payload = (upper != 0 ? upper : 0x26600) | (value & 0xff)
```

两者第一次查询等bit31清除后都不检查高32位的其他位；最终写以后不做回读/完成轮询，
直接进入 `Applied!` 消息路径。合成write返回-1或短写4字节时也会显示该消息边界。
这里不定义命令状态码/电压/频率/单位；`Applied!` 是原文字，不能视为硬件已应用成功的确认。

先前Controls传给 `intel_ctl6` 的bool，在构造函数 `0x600372` 存到对象+0x40；
构造的 `0x6019d3` 分支会改变 `rd_fused_pcratio` 使用的索引，另有列表构造差异。
这两个完整槽在bool为0和1时都能走到相同写路径，原指令执行中对该字段的读计数为0。
**这个bool不是这两个槽体的统一禁止写入条件**。Qt信号是否连接、按钮是否在具体会话可触发、
构造后的其他UI约束仍须继续核对，不把直接调用槽的模拟当作实际鼠标点击。

## 静态轮询清单

[静态报告](validation/legacy-msr-analysis.json)还找到174个声明函数中的407处固定形状：
`call Rdmsr; mov REG,[buffer]; test REG,REG; js BACK`，回边到读前仅有mov/lea/xor/nop。
报告保留函数哈希、调用点、回边地址及完整小循环指令。此形状内没有额外计数/错误/超时退出。

这是**窄模式的已确认清单**，未声称穷尽所有MSR循环，更未把407处全部等同于上述栈复用实验。
其他寄存器、其他包装器、间接调用、不同循环结构及实际执行可达性需要分别分析。

## 复现与门禁

```sh
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/audit-legacy-msr.py \
  build/input-audit/octool --output docs/validation/legacy-msr-analysis.json \
  --export-fixture analysis/fixtures/legacy-msr-slots.json
PYTHONPATH="$PWD/build/reference-tools" python3 analysis/tools/emulate-legacy-msr.py \
  --output build/msr-emulation.json
```

- [公开片段](../analysis/fixtures/legacy-msr-slots.json)仅四个完整函数、2,034原指令字节，
  SHA `8fa956c195a5afe65dd63284d796520adc70519a3e23b1ab2d13b81b4d4f8574`。
- 保留原UI槽、Wrmsr/Rdmsr代码；Qt边界、sprintf/open/lseek/read/write/close全是模拟器内的合成回调。
  路径 `/dev/cpu/0/msr` 只写入模拟内存并核对，宿主不打开任何设备/文件、不执行任何MSR指令。
- 148例包括96个值/应答组合、4空输入、44 I/O/延迟组合、4个非busy高位应答；24例预期停在指令上限。
  错误、EOF、4/7字节短读、忙位不清分别验证；write/seek/close失败被原代码忽略也有证据。
- [Windows报告](validation/legacy-msr-emulation-windows.json)保留各次I/O参数/返回值、读前后字节、
  两包装器缓冲区地址和UI文字。模拟1秒上限若先于指令上限触发会失败，不计作预期busy结果。
- 基础Machine增加可选的明确指令上限观察模式，原550决策实验仍要求正常边界；新增边界RET仅控制
  Unicorn预翻译，不执行外部函数。相关分析测试和全部离线门禁继续接入CI。

Windows本地148例及专门回归已验证；`2270c6b / 37761865986`云端23/23成功，Linux14项分析测试
无跳过，148例与Windows除environment外完全一致，见[该轮证据](validation/legacy-qt-msr-ci-2270c6b.json)。
原Qt/GUI可见窗口、用户四平台实际MSR值、
命令正确性、硬件写入成功、Secure Boot/lockdown均不由本实验推断。
