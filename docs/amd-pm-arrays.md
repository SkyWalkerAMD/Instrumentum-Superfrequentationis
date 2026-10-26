# AMD PM 原始数组监控

2026-10-26 更新：[逐核 / CCD 存储区](amd-pm-core-ccd.md)已由 MP5 原指令闭合边界并接入两端；PM 快照上限 1 MiB，公共显示字段由 24 项改为 22 项，撤回 VDDG 两项误映射。标签/单位/物理映射和双 rails 硬件写入仍待完成。以下保留此前阶段记录。

2026-10-20。本批增加 92 个原版存储值，PM 共显示 220 个字段：
24 个公共遥测、24 个原版限制、53 个原版公共监控、27 个链路/内部时钟、
92 个 GMI/DIMM/PCIe 数组值。字段数量不是整个 OCTool 的完成率。

| 组 | 原版名称 | 每项存储槽数 | 字节范围 |
|---|---|---:|---|
| `gmi_storage` | `GMI_FULL_WIDTH`、`GMI_HALF_WIDTH`、`GMI_QUARTER_WIDTH` | 各 12 | `294h..320h` |
| `dimm_storage` | `DIMM_POWER`、`DIMM_TEMP` | 各 24 | `324h..3E0h` |
| `pcie_storage` | `PCIE_ALLOWED`、`PCIE_ACTIVE`、`PCIE_UP_THRESHOLD`、`PCIE_DOWN_THRESHOLD` | 各 1 | `3E4h..3F0h` |
| `pcie_storage` | `PCIE_IOHC_BANDWIDTH[4]` | 4 | `3F4h..400h` |

槽号从 0 开始，只表示原数组中的顺序。24 个 DIMM 槽位不代表机器安装了
24 条内存。`ALLOWED`、`ACTIVE` 仍按原版 binary32 存储读取，不擅自转成布尔值或端口掩码。
所有新值的单位、物理目标和启用状态仍未核实，不参与 BCLK 换算。

GUI 的 `PM telemetry → Display` 增加 `GMI stored arrays`、`DIMM stored arrays`、
`PCIe stored arrays`，原来的九个视图顺序保持一致。多槽字段在名称后显示
`[slot N]`。CLI 继续使用 `amd-pm-read` 与 `amd-pm-decode`，共享同一解码器。

JSON schema 仍为 1，新增 `original_arrays`。每个元素包含 `group`、`name`、
`slot`、`slot_count`、`offset`、`raw`、`stored_value` 等成员；
`original_arrays_units_verified` 与 `original_arrays_targets_verified` 固定为 false。
旧数组的格式和数量保持不变。导入从 `raw_hex` 重新计算全部值和槽号，
忽略外部提供的派生字段；旧快照缺少新成员也能导入。

新增数组会使大表的完整 JSON 超过 64 KiB。PM 的 GUI 文件读取、CLI 文件读取及
解析器统一使用 128 KiB 上限，包含 CLI 报告外层；其他捕获格式仍采用 64 KiB。
共享 JSON 的 16 层嵌套、8192 个节点、重复键和 UTF-8 检查未放宽。
未知版本或长度不符不解码，非有限值保留原字并标为不可用，读取失败或元数据变化
清空旧数据。没有新增硬件访问、驱动自动加载或固件设置。

原指令证据由 `analysis/tools/legacy-amd-pm-arrays.py` 重放固定 ELF 构造函数
与 `get_pm_patch` 公共路径。八个精确版本、八种合成 CCD 输入、两种浮点图样，
共 128 组，每组核对 92 个存储字。夹具和原文件摘要见
[紧凑证据](validation/legacy-amd-pm-arrays.json)。传输、拓扑、C++ 库边界均为合成，
校准假定完成，在逐核路径之前停止；不能据此声称真实硬件验收。

REA 本轮两次分析完成 12 次查询，覆盖 11 个不同函数，包括构造、解码、
快速刷新、三个 `amd_am*::pm0_monitor` 与 `mainwindow2::handleTimeout`。
`amd_bycore::populate_lines` 和 `on_pushButton_clicked` 实际处理调参输入，
相同成员偏移属于不同对象，不能证明 PM 数组的界面绑定。监控入口的原指令
还补齐了伪代码遗漏的调用模式；据此完成的 24 组逐核/CCD 探针仍未解决布局不一致。
REA 伪代码需要与原指令交叉核对，不作为未知固件协议或实际单位的独立证明。
跨系统结果见[整批验证](amd-pm-arrays-validation.md)，REA 的实际结果与限制见[后续研究](amd-pm-rea-arrays.md)。
