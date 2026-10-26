# AMD PM 原版公共功耗与策略监控

2026-10-26 更新：[逐核 / CCD 存储区](amd-pm-core-ccd.md)已由 MP5 原指令闭合边界并接入两端；PM 快照上限 1 MiB，公共显示字段由 24 项改为 22 项，撤回 VDDG 两项误映射。标签/单位/物理映射和双 rails 硬件写入仍待完成。以下保留此前阶段记录。

2026-10-20：GUI 和 CLI 的共同解码器增加 **53 项原版监控存储值**。
加上已有 24 项公共遥测及 24 项原版限制监控，现在共解析 101 个显示字段。
这是显示字段数量，不是整个 PM 表或软件的还原百分比。

| GUI 分组 / JSON group | 项数 | 字节偏移 | 原版名称举例 |
|---|---:|---|---|
| Power / `power` | 7 | 60h–78h | CPU Power、SOC Power、Socket Power |
| Workload / `workload` | 8 | 40h–5Ch | PPT_WC、PPT_ACTUAL、TDC_0_ACTUAL、FIT_ACTUAL |
| Power model / `power_model` | 4 | 7Ch–88h | VDDCR_CPU_DYN_POWER_WC、VDDCR_SOC_LKG_POWER_WC |
| Voltage policy / `voltage_policy` | 10 | 8Ch–B0h | FIT PID0、LATCHUP0、PPT_VOLTAGE_0 |
| Limit contributors / `limit_contributors` | 24 | D0h–12Ch | PPT0、STAPM0、VRHOT0、PPT_APU1 |

以上均为小端 binary32，每项间隔 4 字节。保留原版名称，包括不对称的
`FIT PID0` / `FIT_VOLTAGE_1`。这些名称不能单独证明 W、A、V、百分比、
当前限频原因或策略是否生效；编号 0/1 也没有被绑定到 CPU、CCD 或供电轨。
UI 单位统一显示 Unverified，JSON 为 `unit: unverified`。
不进行 BCLK 修正、单位换算或新的设置操作。

使用原有 `amd-pm-read`、`amd-pm-decode` 即可取得新增字段。
GUI 在 AMD tuning → PM telemetry 的 Display 下选择上述五组。
八个精确版本与长度、已有驱动的读取顺序和有界文件导入沿用
[公共 PM 解码器](amd-pm-recovery.md)。

JSON schema 仍为 1，增加 `original_package` 数组、每项 `group`，
以及始终为 false 的 `original_package_units_verified` 和
`original_package_targets_verified`。旧版 schema 1 快照仍可导入，
所有显示字段从原始字节重算；篡改的名称、组别、显示值或验证标志不会被采信。
非有限浮点保留原始字和逐字段错误，其余有效字段可查看、导出。
版本、长度或驱动元数据检查失败则清空全部分组。

## 原指令证据

原 ELF SHA256：
`44598dc8177050599afcc46f6355504161f942330d7917d1aaa010d864aaff10`。
复用固定夹具 `analysis/fixtures/legacy-amd-pm-limits.json`，
SHA256 `9936d25c9a2ffb61def0035529223cea0710a04ffeb2a067c9356897880f3475`。

新工具 `analysis/tools/legacy-amd-pm-package.py` 执行其中完整构造函数
`2C7910h` 和 `get_pm_patch` 的公共字段路径，核对构造的名称向量、
输出值向量、每次被读取的偏移和输出原始浮点位。五个名称向量位于对象
`118h / 208h / 238h / 178h / 148h`，对应值向量均为名称向量后 18h。

八版本 × 八个合成 CCD 输入 × 两种浮点图样，共 128 组，每组核对 53 项。
传输、拓扑与 C++ 库边界均模拟；校准标志假定已完成；停止于逐核路径之前。
这证明原版程序在所列条件下的名称与存储映射，不证明固件的物理单位或真机行为。
紧凑结果见 [原指令核对记录](validation/legacy-amd-pm-package.json)。

## 尚未接入的路径

逐核、逐 CCD 解码仍待核实。前轮在合成 Shimada 路径观察到固定 CCD 起点
`1F74h / 2134h`，与部分公开版本的完整表长度不相容；
不能只因为落在原版映射窗口内就加入有界解码器。
本次也没有接入原版与公共来源存在偏移冲突的时钟向量或未经核实的 SVI 功率字段。

REA 6.7.0 与 Ghidra 12.1.4 的独立云端研究流程用于进一步查看函数与调用者，
不运行原版 ELF，不加载驱动。该流程及反编译输出属于研究辅助，
不替代上述逐位回放和 GUI/CLI 功能测试，也不自动成为硬件协议依据。
