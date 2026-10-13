# Intel HWP 封装请求与 PECI 覆盖

GUI 的 `Intel Controls → Power / performance` 与 `octool-cli intel-read --cpu N`
共用解码器，按 CPUID 能力增加最多 25 项只读信息：

- 封装请求的最低、最高、期望性能、EPP、活动窗口。
- 当前逻辑 CPU 的封装控制位、五项 flexible valid 位及控制极性。
- PECI 的最低、最高、EPP 输入，以及三个对应的覆盖状态。
- 四项通知使能；最高性能变化、PECI 进入和退出的三项历史状态。

性能级别是请求索引，不能当作 MHz 或处理器实际交付的性能。封装/PECI 输入、选择位
来自分开的寄存器采样，本功能不宣称它们组成原子快照，也不据此显示“实际生效频率”。
通知使能不是已发生事件；历史位不是当前 PECI 覆盖状态。活动窗口沿用微秒编码，0 为硬件选择。

## 能力与访问

[Intel SDM Vol. 3B §16.4.4–16.4.6](https://cdrdv2-public.intel.com/835755/253669-sdm-vol-3b.pdf)
与 [Vol. 4 的架构 MSR 表](https://cdrdv2-public.intel.com/835765/335592-sdm-vol-4.pdf)
提供字段与能力约束；[固定来源记录](validation/intel-hwp-requests-sources.json)记录哈希和已核对页。

| 寄存器 | CPUID.06H:EAX 与额外条件 |
|---|---|
| 772h 封装请求 | HWP bit 7、package bit 11；EPP/window 分别再检查 bit 10/9 |
| 774h 选择位 | package bit 11；五个 valid 位检查 bit 17，EPP/window 同时检查各自能力 |
| 775h PECI | HWP bit 7、PECI bit 16 |
| 776h 控制极性 | HWP bit 7、bit 22，且 770h 表明 HWP 已启用 |
| 773h 通知 | HWP bit 7、bit 8；highest/PECI 再检查 bit 15/16 |
| 777h 新增历史 | HWP bit 7；highest/PECI 分别检查 bit 15/16，不依赖通知使能 |

能力缺失时不尝试对应寄存器；776h 在 HWP 未启用或启用状态读取失败时不访问。
单项读取失败显示不可用，CLI 的原始字和解释值为 null，其它独立信息仍保留。
新增信息全部只读，没有新增封装请求、通知、PECI、极性或历史清除写入接口。

## 现有本地编辑的核验

PECI 正覆盖 minimum、maximum 或 EPP 时，禁止相应的 774h 本地字段编辑。
PECI/极性状态读取失败时不开放 HWP 编辑。package-control XOR polarity 表示选用封装请求时，
继续保守地禁止本地编辑；即使某个 flexible valid 位为 1，也不扩张该编辑范围。

现有设置操作在写入前后比较能力标志、完整 PECI 原始字和极性寄存器，
任何变化或读取失败都不报告验证成功。仍核对整个目标配置字和 HWP 能力值。
没有自动重试、回滚或覆盖外部控制器。系统其它程序和固件仍可能在采样间修改配置。

## 原版对应与验证

原 ELF `Rd_PECI_HWP_REQUEST_Tuner()` 位于 `0x3845F0`，完整 82 字节，
读取 775h 并合成 64 位返回值。[固定指令](../analysis/fixtures/legacy-intel-hwp-requests.json)
及[隔离回放](../analysis/tools/legacy-intel-hwp-requests.py)覆盖 138 个场景。
原版忽略读取失败，输出未初始化时会返回栈残留；新实现不沿用这一行为。

新增共同核心测试覆盖所有 PECI 位、512 种能力组合、全部新增读取失败位置、
两种控制极性和写前/写后上下文改变。GUI/CLI 检查同一数值、只读状态、失败清空与 JSON 输出。
自动化验证使用合成寄存器；未在目标 CPU 上执行调参或验证物理效果。
