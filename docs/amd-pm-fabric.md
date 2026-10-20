# AMD PM 原版链路与片内时钟监控

GUI、CLI 的共同 PM 解码器新增两组共 27 项原版监控存储值。
与此前 101 项合计 128 个显示字段；这不是完整 PM 表或软件的还原百分比。

| 分组 | 项数 | 连续字节偏移 | 原版标签 |
|---|---:|---|---|
| Link bandwidth / `link_bandwidth` | 9 | `154h..174h` | PCIe、XGMI、CXL 各自的 `UP_THRESHOLD`、`BANDWIDTH`、`DOWN_THRESHOLD` |
| Internal clocks / `internal_clocks` | 18 | `24Ch..290h` | SMNCLK、WAFLCLK、MP0CLK、MP1CLK、MPIOCLK、ACLK、SVI3CLK、MPDMA_TF_CLK、MPDMA_PM_CLK，以及各自 `_EFF` |

每项是小端 binary32，跨度四字节。名称与存储映射来自固定原 ELF 的构造函数
及 `get_pm_patch` 公共路径。`FCLK_PCIE_BANDWIDTH` 等名称并不证明 MB/s、百分比、
当前链路是否激活或物理端口归属；`SMNCLK` 等也没有据此标为 MHz 或实时频率。
两组单位均为 `unverified`，不使用 BCLK 修正。不增加设置、轮询或新的硬件传输。
存在偏移冲突的原版 FCLK/UCLK/MCLK 向量继续排除。

GUI 的 AMD tuning → PM telemetry → Display 增加两项，共九个视图。
CLI 原有 `amd-pm-read` 和 `amd-pm-decode` 输出新增 `original_fabric` 数组，
每项携带 `group`；`original_fabric_units_verified` 和
`original_fabric_targets_verified` 始终为 false。既有三个数组不改变含义。

快照 schema 仍为 1。旧快照从 `raw_hex` 重算全部字段，无需转换；
文件内伪造的名称、单位、组别和验证标志不会被采信。
非有限值逐项输出 null、错误与原始字，保留其它有效字段。
元数据变化、未知版本、截断和读取失败清空全部分组。
完整最大长度表及全部 128 项显示字段的导出、导入受 64 KiB 上限约束。

## 指令依据

原 ELF SHA256：
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
使用既有夹具 `analysis/fixtures/legacy-amd-pm-limits.json`，SHA256：
`9936d25c9a2ffb61def0035529223cea0710a04ffeb2a067c9356897880f3475`。

`analysis/tools/legacy-amd-pm-fabric.py` 重放完整构造函数 `2C7910h`
及公共解码路径。两组名称向量位于对象 `2C8h`、`3E8h`，
浮点值向量分别位于 `2E0h`、`400h`。每项都核对原构造的标签、实际读取偏移
和输出原始字。八个精确版本 × 八个合成 CCD 输入 × 两种浮点图样，共 128 组。
[紧凑结果与完整报告摘要](validation/legacy-amd-pm-fabric.json)保留版本和执行范围。

传输、拓扑与 C++ 库边界为合成结果，校准假定完成，停止于逐核路径之前。
这证明所列原程序路径的映射，不证明固件物理语义或真机效果。
REA/Ghidra 的辅助反编译与这些指令实验分开记录。
逐核/CCD 表、真实链路状态、物理频率、原生传输和调参仍未完成。
