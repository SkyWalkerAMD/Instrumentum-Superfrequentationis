# AMD PStates：只读恢复及证据边界

更新：2026-09-30。作者确认 TRX50 SAGE 使用 Threadripper PRO 9995WX，BIOS 未知；
针对截图中的 306 mV、31 A，作者明确选择“暂无定义，先恢复可核实的只读频率和原始值”。
当前实现位于 [pstates.cpp](../gui/pstates.cpp)，复用原 HAL，没有新增或修改线级命令。

## 当前行为

- 主窗口新增 AMD PStates 页。打开窗口/切页不访问 CPUID 或 MSR；点击 Read once 才读取。
- 先在指定逻辑 CPU 查询 CPUID vendor、family/model/stepping、最大扩展叶及硬件 P-state 能力。
  只接受 AuthenticAMD、Family 1Ah、CPUID 80000007h:EDX[7]=1。其他 CPU 不尝试自动 MSR 读取。
- 使用现有 `hwio_cpuid`。模块后端沿用 CPU user_id；直接后端临时绑定当前工作线程到所选 CPU，
  读完恢复线程原亲和性。固定 cpu_set_t 无法表示的 CPU 返回 ERANGE；绑定失败不会在其他 CPU 上代读。
- 先读 C0010061h，按 bits[6:4] 得到最大 P-state 编号，再读 C0010064h 起至该编号。
  表格保留 P0–P7，超出上限的行明确标为未读取，避免伪造零值。
- 原始值完整保留 64 位。bit63 未使能时保留原始值，但不显示频率；FID 保留编码同样不显示 0 MHz。
- Frequency ID 为低 12 位，10h–FFFh 的配置频率为 FID × 5 MHz。它是传统 P-state 定义值，
  不是实时测频；使用 CPPC/amd-pstate 时运行频率可能独立控制。
- 电压、电流、实时传感器及任何 P-state 写入均未实现。没有从截图填默认数值或采样曲线。
- 各行读失败独立显示负 errno，不把上轮值保留为本轮结果。改变 CPU 清空所有结果。
  Copy snapshot 将 CPU 身份、时间、原始值和错误复制到剪贴板，便于真机提交证据。

## 实现依据

频率、寄存器范围、使能位和 PstateMaxVal 的主要依据是
[Linux cpupower amd.c](https://github.com/torvalds/linux/blob/551c722f40809618230001baccf219193e22fc5a/tools/power/cpupower/utils/helpers/amd.c)。
硬件 P-state 能力位见同提交的
[cpuid.c](https://github.com/torvalds/linux/blob/551c722f40809618230001baccf219193e22fc5a/tools/power/cpupower/utils/helpers/cpuid.c)。
下载内容哈希见 [pstates-sources.json](validation/pstates-sources.json)。
上游在 amd-pstate 驱动工作时不把旧 HW P-state 表用于频率控制；本界面只提供明确标注的配置快照。
不据此保证新 CPU 的旧表能代表 CPPC 当前策略，真机若返回访问错误如实显示。

[AMD PPR 57238](https://docs.amd.com/v/u/en-US/57238)，Rev 0.24、2024-09-29，
卷 1 页 48/233–235，与上述频率和基本位域一致。已解压原 ZIP、提取文本并渲染第 235 页核对。
ZIP SHA-256：`9749f8e98437bf8cdb78e6e16b1799cfb420e5a561ca1ad0ec6cdc5d8a9d9995`。
这份 PPR 的型号是 Family 1Ah Model 02h C1；没有把它说成 9995WX 专用手册。
它在 bit32 定义 CpuVid[8]，说明仅截取 bits[21:14] 可能不足以解释某些 1Ah 型号的 VID。
手册该页没有完整 VID→mV 或 IddDiv 编码表，不能填入经验公式。
公开产品页确认 9995WX 为 Zen5/Shimada Peak，但不足以证明 Model 02h 所有寄存器定义适用。

## 旧 ELF 观察到的行为

原二进制来自作者的 Linux ZIP，输入哈希见 [输入清单](validation/reference-packages-20260930.json)。
静态指令地址按 ELF 虚拟地址记录；没有执行它或读取真实寄存器。
输入 ELF 与各函数代码哈希见 [pstates-legacy-functions.json](validation/pstates-legacy-functions.json)。

| 符号 | 地址/大小 | 静态观察 |
|---|---|---|
| AMD_Configuration::Get_PStates_Vec | 0x22c450 / 1340 B | 循环读取 C0010064h–C001006Bh；SHIMADA/GRANITE/GPT 路径低 12 位乘 5 |
| AMD_Configuration::cal_vid_from_raw | 0x21f3e0 / 131 B | 含 `1550 - raw*6.25` 与 `245 + raw*5` 两条分支，转换为整数时截断 |
| cpufunctions::refresh_pstates | 0x752960 / 4158 B | 旧界面刷新入口；不能仅凭符号名证明字段物理意义 |

Get_PStates_Vec 在 0x22c5be 前提取低 DWORD bits14–21 后调用 cal_vid_from_raw，
没有合入高 DWORD bit0（整体 bit32）。后者访问 GLOBAL_IS_GRANITE/SHIMADA/GPT，
SHIMADA 分支落到 `1550 - raw*6.25`。原始数据和标志实际值仍需真机证明，不能把这些公式
直接升级为新 9995WX 硬件规范。

电流路径先取低 DWORD bits30–31：编码为 0 时输出 0，非零时用 bits22–29 整数除以该编码。
这证明了旧程序的算法，不证明它正确，也不证明截图 31 A 是实时电流/限制值。
截图 306/793/856 mV 和 31/23/15 A 只保留在原图中，不作新测试的硬件真值。

## 回归与真机验证

新增五项 Qt 测试：频率/禁用/保留值；非 AMD/旧 family/缺能力拒绝读取；上限与逐行错误；
真实控件异步刷新/清空；直接 CPUID 恢复线程亲和性。连同原五项业务测试和 init/cleanup，
QtTest 应报告 12 项结果。模拟 transport 明确验证逻辑 CPU、原始 bit32 保留、地址集合及零写入。
这些是离线回归，不是 9995WX 实机运行证据。

真机步骤接入 [验收清单](hardware-acceptance.md)。需记录实际 CPUID 和 BIOS，
将快照中的 MSR 与同 CPU 的 raw MSR 页对照；禁用定义不显示配置频率，错误/不支持不伪造成功。
后续恢复电压、电流或写入前，仍需对应型号资料或作者确认的原始值/算法。
