# AMD 拓扑与 PStates 写入后续研究

AMD tuning 页新增 Read CPU topology，读取用户选择的逻辑 CPU 的 `CPUID 80000026h`。
依照 [AMD PPR 57238](https://docs.amd.com/v/u/en-US/57238) 卷 1 页 142–144，
以及 [Linux x86 拓扑文档](https://www.kernel.org/doc/html/latest/arch/x86/topology.html)，先检查最大扩展叶，
然后逐级读取 Core、Complex、CCD、Socket，直到逻辑处理器计数为零。
[来源记录](validation/amd-topology-sources.json)保留资料版本及哈希。

## 已实现

- 显示 APIC ID、socket ID、socket 内 CCD、CCD 内 core、core 内 thread 和每级原始信息。
- 用每级提供的位移划分 APIC ID，保留空洞；不按线程总数除以 16，也不把 Linux CPU 号码当核心位置。
- 检查层级顺序、位移/数量、ECX 回显和 APIC 一致性；不支持或不一致时显示失败，不猜测目标。
- 最多八次扩展叶读取；遵循同一事务的期限和取消；完全不读写 MSR、PCI 或 SMU。
- 不把 CPUID 拓扑当作固件命令 ID 的认证，不自动准备或发送频率命令。

测试包含 APIC `0x1af`、socket 1、CCD 10、core 7、thread 1 的合成稀疏拓扑。
该输入故意让逻辑处理器数量小于位移所覆盖的地址数量，验证缺失核心不会导致重新编号。
另覆盖关闭 SMT、单 CCD、相等位移、所有 CPUID 失败位置及错误的层级/计数/ID。
这不是目标 9995WX 的采集记录。

## 原 PStates 写入入口为什么仍不能照抄

[固定 ELF 静态证据](validation/legacy-amd-pstates-apply.json)记录原
`cpufunctions::on_pushButton_7_clicked`（0x7539a0）、转换函数、processor mask getter 与 CCD 估算。
此处是静态分析，不冒充完整原界面执行。

原 Apply 入口先调用 TSC/CPB 相关函数，再处理 PStates 字段；读取返回值没有在后续位运算前检查。
VID 写入仅保留转换结果低 8 位，更新低 DWORD bits21:14，没有同步 bit32。
循环从 `get_Processor_Mask` 返回后执行 `mov ecx,eax` 再调用 WrmsrTx，高 32 位会被清零。
这与具有大量逻辑 CPU 的目标系统尤其相关；尚未据此推断具体机器实际受影响范围。
循环不检查各次写入返回，最后无条件显示 Settings Applied，并刷新两遍。

PPR 页 235 对同一 CPU 的 VID、同一 coherent fabric 的其它字段提出跨核心一致性要求；
而 IddValue/IddDiv 是用于 ACPI 电源描述的预期单核电流信息，不是测量到的当前电流。
这份手册为 Model 02h C1，不足以认证所有 Family 1Ah 主板/固件的写入行为。
因此保留现有只读 PStates；没有为了填满界面而添加未知单位或仅写一个核心的设置按钮。

原 `get_fused_ccd` 对部分平台只依据线程总数映射 1/2/4/8/10/12/14/16，无法表示 CCD ID 的空洞。
其它分支读取 B8/BC 间接寄存器位图。重构的 CPUID 路径不复制前者，也不无条件启用后者。

剩余工作是目标平台的固件目标编号对应关系、VID/Idd 型号规范，以及满足完整跨核范围的设置协议。
这些缺口与 Linux 发行版安装兼容性不同，不能由云端编译成功推导已经恢复。
