# REA：PM 数组与监控路径后续研究

2026-10-20。本轮实际完成两次 REA/Ghidra 云端静态分析：
[七函数研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39425573426)
与[五函数监控追踪](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39427529490)。
12 次函数查询对应 11 个不同函数，`get_pm_patch` 在两轮中各查一次。
两个 MCP 会话均完成伪代码、函数信息、调用者、证据导出和关闭；工具调用累计
631.75 秒、612.88 秒，首个查询包含整个 ELF 的分析启动时间。

工具固定为 REA `6.7.0`、Ghidra `12.1.4`，沿用已验证的 900000 ms 启动上限，
关闭 `GCC Exception Handlers`、`Decompiler Switch Analysis`、`Call Convention ID`。
原 ELF SHA256 为 `44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
没有运行原程序、访问真实寄存器或改动系统工具安装；凭证不传给分析子进程。

每个查询使用 `file-offset:`，再核对 REA 返回的地址和原 ELF 符号。
本 ELF 在 Ghidra 中统一偏移 `100000h`。原符号字节数与报告函数体字节数不同，
不能把返回伪代码等同于完整函数恢复。

| 函数 | 原文件偏移 | Ghidra 入口 | 原符号 / 报告函数体字节 |
|---|---|---|---:|
| `AMD_PM_LOG::get_pm_patch` | `0x2e7360` | `0x3e7360` | 22281 / 21923 |
| `AMD_PM_LOG::AMD_PM_LOG` | `0x2c7910` | `0x3c7910` | 36961 / 34284 |
| `amd_bycore::populate_lines` | `0x6d4710` | `0x7d4710` | 13371 / 12585 |
| `amd_bycore::on_pushButton_clicked` | `0x6d7e90` | `0x7d7e90` | 16025 / 14479 |
| `AMD_PM_LOG::Update1_fast` | `0x2e66f0` | `0x3e66f0` | 3183 / 3060 |
| `AMD_PM_LOG::Update2_fast` | `0x2daee0` | `0x3daee0` | 1095 / 1058 |
| `Reader_Thread::pm_monitor` | `0x6f2800` | `0x7f2800` | 406 / 399 |
| `amd_am3::pm0_monitor` | `0x6c1210` | `0x7c1210` | 8035 / 7863 |
| `amd_am4::pm0_monitor` | `0x6c40c0` | `0x7c40c0` | 7304 / 7150 |
| `amd_am5::pm0_monitor` | `0x6c7690` | `0x7c7690` | 3904 / 3781 |
| `mainwindow2::handleTimeout` | `0x8fb1f0` | `0x9fb1f0` | 12223 / 11951 |

完整工具响应、提取的伪代码、依赖锁、运行日志和云端来源已保存在用户目录：
`build/rea-arrays-39425573426/`、`build/rea-arrays-39427529490/`。
[紧凑记录](validation/amd-pm-rea-arrays.json)列出原文件摘要、函数入口、范围、每份证据的 SHA256。

本轮确认了以下边界：

- `amd_bycore::populate_lines` 和按钮处理函数属于调参输入。前者收集 `QLineEdit` 指针，
  后者读取文本并转换输入；与 PM 对象相同的成员偏移不是数组显示绑定的证明。
- 三个 `amd_am*::pm0_monitor` 确实走文本监控显示；`amd_am3`、`amd_am4`
  访问原对象 `910h` 名称和 `928h` 逐核矩阵，`amd_am4` 调用 `amd_am5` 进行后续显示。
  `mainwindow2::handleTimeout` 则涉及逐核数据和图表更新。仍未证明这些入口覆盖全部 GMI/DIMM/PCIe 数组界面。
- 原指令中的 `Reader_Thread::pm_monitor` 以 `ESI=4` 调用 `get_pm`；
  `amd_am3`、`amd_am4` 依次传入 `1`、`4`。REA 伪代码漏掉这些参数，
  不能直接移植它推断的函数签名。
- `Update2_fast` 原指令选取 `2B0h` 或 `320h` 初始读偏移，伪代码中的 MMIO
  调用同样缺少偏移参数。这条快速路径不能单独证明八种 Shimada 表的逐核布局。

根据原界面实际传入的模式 `4`，又在合成环境补跑了 24 组逐核/CCD 实验，
覆盖八个表版本和 CCD 输入 `1/2/4`。原构造、公共路径、逐核与 CCD 指令执行后，
停在 `2EA4A8h`，后续路径未执行；传输、拓扑、分配/复制均为合成边界，校准假定完成。
24 组读范围全部与此前模式 `3` 的探针一致；仍有
1 组逐核、
15 组 CCD 范围超过对应公共表长度。
这证明“只改调用模式”没有解决探针里的不一致，不能据此指认真实硬件越界。
版本与物理拓扑、缓冲区契约仍需确认，未启用新的逐核/CCD 生产解码。
见[模式对照记录](validation/amd-pm-monitor-modes.json)，可重跑文件为
`build/pm-monitor-mode-probe.py`，完整报告为同目录 JSON。

[本批新增 92 个数组值](amd-pm-arrays.md)依据原构造和公共解码指令的独立回放，
并经过 GUI/CLI 共用实现。数组槽号、数量与名称不证明物理对象、真实单位或启用状态。
REA 帮助追踪控制流与发现错误候选，没有消除这些尚未验证的条件。
