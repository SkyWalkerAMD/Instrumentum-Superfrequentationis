# REA：逐核/CCD 功耗表与 PBO 事务

后续进展：新的 [12 函数 REA 研究与 GUI/CLI 实现](amd-pbo-fmax-layout.md) 已增加
Fmax 命令确认、rails 离线编码和逐核/CCD 布局诊断。三类构造入口跨八个版本的 128 个原指令实验
确认其初始化方式不同；Windows 候选函数仍有相同固定起点。
偏移与长度冲突已可直接在 GUI/CLI 列出，真实逐核/CCD 遥测映射仍未确定。
以下保留前一批研究记录，其中“Fmax 未开放”描述的是当时状态。

2026-10-21：实际运行 [REA 云端任务 39529351417](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39529351417)，
使用 REA 6.7.0、Ghidra 12.1.4 分析 11 个函数。
原版 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
工具返回、函数身份、证据导出和关闭均已核验，见[REA 记录](validation/amd-pbo-layout-rea-study.json)。
Ghidra 地址比原 ELF 地址多 `100000h`；混合调用约定会丢失或误标参数，结论同时核对原始指令。

分析覆盖 `MP1_C2PMSG`、`AMD_PBO` 构造、Fmax/rails setter、两个原版调参按钮、
`amdvf` 析构及 `tr5_get_both`、`cpufunctions` 构造、`get_pm` 和 `refresh_monitor`。
本轮新增两套可重复的原指令实验；宿主硬件没有执行这些函数。

| 实验 | 新增证据 | 对实现的影响 |
|---|---|---|
| 逐核/CCD `get_pm_patch(mode=4)` | 8 个表版本 × 9 个合成 CCD 输入 × 2 种原始字模式，共 144 组合；逐项核对 15 种 core 行和 11 种 CCD 行 | 明确原数组存储顺序与偏移，但未建立有效固件布局 |
| PBO/邮箱完整函数 | 4 个构造分支、21 个 setter 输入、12 个 A8 返回组合、42 个访问故障位置 | 核对初始化、邮箱地址、两种 Fmax 命令和原版错误处理 |
| 新的生产功能 | [PBO Scalar 读取与设置](amd-pbo-scalar.md) | GUI/CLI 共用核心、核验旧值与回读；限制在 Shimada 1Ah/08h |

逐核/CCD 回放保留 NaN、Inf、负零等原始字，用偏移相关的合成模式验证数组拷贝。
它运行原版默认构造与 mode 4 循环，在 `2ea4a8h` 停止；C++ 容器、拓扑和传输为合成边界，
并假定原版校准已经完成。合成 CCD 输入刻意与表版本做交叉组合，不能将冲突数解释成真机故障比例。

| 表版本 / 合成 CCD 输入 | 原指令槽位 | 观察 |
|---|---|---|
| `730204h / 2` | core 16，CCD 2 | CCD 偏移 `1fb4h..2008h` 超出公开表长度 2844 字节 |
| `730604h / 6` | core 16，CCD 2 | 构造分支回落为 16/2，没有生成想当然的 48/6 |
| `730c04h / 12` | core 96，CCD 12 | 数组在界内，仍与公共字段的含义冲突 |
| `731004h / 16` | core 128，CCD 16 | 均在 11636 字节内，但 core 与 CCD 共用 176 个字的位置 |

144 组合中，92 组合有超出公开表长的范围，18 组合有 core/CCD 数组重叠。
全部组合的 core 读取包含 `714h`、`71ch`；现有公开表在这两处标为 VDDG_IOD / VDDG_CCD。
“都在 buffer 内”不能证明两套解释兼容。
固定 [PowerTable.cs](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/PowerTable.cs)
对这八个 Shimada 版本的逐核功耗偏移均保留为 `-1`，没有提供独立对应证据。
因此本轮未启用逐核/CCD 生产解码器，已有 220 个 PM 字段保持原合同。
完整回放摘要见[布局记录](validation/legacy-amd-pm-percore-local.json)。

`MP1_C2PMSG` 的名字不能作为邮箱身份依据：在本轮核对的 Shimada 分支，
它实际用 B8/BC 和 RSMU 的 `3b10924 / 3b10970 / 3b10a40` 三个地址。
它先隐含写 `50200=1`，再写入 A8；固定等待 100 ms 后最多读取 response 21 次。
response 不是 1 时仍读取并发布 argument；上层 offset wrapper 又忽略返回状态。
`cpufunctions` 只是把该字格式化到 UI 字段，不能据此确认它是有效 byte offset、数组数目或物理映射。
此前文档所称“MP1 wrapper”指原函数名，不代表该分支使用真正的 MP1 邮箱。
新运行时没有开放 A8 或隐含初始化。

PBO 构造回放确认 PCI 153a 的 Shimada 路径把对象 `+11h` 置为 1，Fmax 因而选 `70h`；
只有另一标志分支选 `6dh`。在固定 Shimada 设置表中，`6dh` 是 Scalar 查询，不能混用。
Fmax 原函数只提交命令，不读完成状态。Rails 原函数仍连续提交 `61h`、`5dh`，
忽略访问失败并主动改 response，不构成可靠的两阶段设置事务。
详细记录见[PBO 事务回放](validation/legacy-amd-pbo-transactions-local.json)和[调用输入补充](amd-pbo-setter-research.md)。

可在有 Python、Unicorn、Capstone、pyelftools 的环境重放；fixture 已固定原始指令及 SHA256：

```sh
python analysis/tools/legacy-amd-pm-percore.py --output build/legacy-amd-pm-percore.json
python analysis/tools/legacy-amd-pbo-transactions.py --output build/legacy-amd-pbo-transactions.json
python -m unittest discover -s analysis/tests -p 'test_amd_pm_percore.py' -v
python -m unittest discover -s analysis/tests -p 'test_amd_pbo_transactions.py' -v
```

剩余工作需要把固件表版本、真实返回长度、有效 core/CCD 数目与独立带标签的只读样本对应起来，
并核对 A8 的返回合同与初始化前提。Fmax、rails 及其它 PBO 写入还缺可靠查询、单位/范围及固件验收。
已知问题被保留为可重放实验；没有把不确定的偏移或命令转换为 GUI/CLI 的高层写入功能。
