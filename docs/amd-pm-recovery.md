# AMD PM 遥测、离线解码与 GUI / CLI 快照互通

2026-10-30：[Granite Ridge / 9950X3D PM 读取](amd-granite-pm.md)已支持温度与三组驻留百分数，并可在精确 SMU 98.84.0 两槽表上用 `--map-ccd` 标出固件目标 0/1。GUI 与 CLI 共用实现。单槽映射、PM 到 CPUID/OS 核心的关联及独立第二 rails 回读仍未完成；详见[单位、映射与 rails 证据范围](amd-granite-semantics.md)。

2026-10-26：[逐核/CCD 存储区](amd-pm-core-ccd.md)按固件生成端恢复 18/13 行，GUI/CLI 同步。
撤回 `714h/71Ch` 的 VDDG 电压映射；公共字段由 220 项降为 218 项，新增 314–2512 个数组值。
PM 快照上限为 1 MiB / 65536 个 JSON 值。下文旧版 24 项公共字段中这两项不再适用。

2026-10-21：[原生 PM 采集](amd-pm-native.md)新增独立 GUI/CLI 入口，要求固件返回精确长度。下文驱动入口与离线解码保持原合同。

2026-10-20：新增 [92 个原始 GMI/DIMM/PCIe 数组值](amd-pm-arrays.md)，在 `original_arrays` 中保留槽号和原字；PM 共 220 个显示字段，物理目标与单位仍未认证。

2026-10-20：新增 [27 项原版链路/片内时钟监控](amd-pm-fabric.md)，在 `original_fabric` 中分组保留存储值；单位、策略状态和物理目标未核实。

2026-10-20：新增 [53 项原版公共功耗/策略监控](amd-pm-package.md)，在 `original_package` 中分组保留存储值；单位、策略状态和物理目标未核实。

2026-10-18：[原版限制监控区](amd-pm-limits.md)增加独立的 24 项 `original_limits`，
保留原版名称、偏移和浮点存储值。物理单位及目标仍未核实；下文的 24 项公共 `fields` 保持原语义。

2026-10-11：新增八个精确 Shimada PM 表版本的公共字段解析。GUI 位于 AMD tuning → PM telemetry；
CLI 为 `amd-pm-read` 和 `amd-pm-decode`。二者共用无 Qt 的解码、快照格式和驱动读取顺序。
本次是公共遥测功能，尚未恢复整张 PM 表、物理核心映射、曲线设置或完整 PBO。

## 使用

```sh
# 已安装并运行兼容 ryzen_smu 驱动时读取；权限由系统节点决定
octool-cli amd-pm-read > pm.json
# GUI 的 Copy JSON snapshot 保存为文件后，也可在 CLI 读取
octool-cli amd-pm-decode --file pm.json
# 原始二进制没有版本标签，必须显式给出精确版本
octool-cli amd-pm-decode --file pm.bin --version 0x730204
# 可选：使用已知 BCLK 修正时钟，保留未经修正的存储值
octool-cli amd-pm-decode --file pm.json --bclk-mhz 102.5
```

GUI 可打开上述 CLI JSON、自身复制的 JSON，以及选择了精确版本的原始二进制。
导入时根据原始字节重新计算，忽略文件中预先显示的数值；不会根据文件名、表长或版本前缀猜测型号。
文件缺失、损坏或未知版本会清空上一份显示；部分非有限浮点值则保留其它有效字段和原始字。
CLI 成功退出码 0，参数错误 2，文件/驱动/字段解码错误 3。非零结果仍可能包含有效的部分字段。

无桌面系统可独立使用 CLI，不需要 Qt、OCTool 内核模块、图形授权或后台服务。
`amd-pm-read` 不指定 `--cpu`：该 sysfs 接口未提供目标 CPU/插槽选择器。不得将输出归属到任意手动 CPU 编号。

## 精确版本与显示字段

| 版本 | 字节 |
|---|---:|
| 0x00730204 | 2844 |
| 0x00730404 | 4100 |
| 0x00730604 | 5356 |
| 0x00730804 | 6612 |
| 0x00730a04 | 7868 |
| 0x00730c04 | 9124 |
| 0x00730e04 | 10380 |
| 0x00731004 | 11636 |

以上八个版本长度必须完全相符。未采用来源的 `0x000730` 通用回退规则。

| 字段 | 字节偏移 | 单位 |
|---|---|---|
| FCLK / UCLK / MCLK | 0x20c / 0x21c / 0x22c | MHz 存储值 |
| VDDCR_SOC | 0x1a4 | V |
| CLDO_VDDP | 0x5cc | V |
| CLDO_VDDG_IOD / CLDO_VDDG_CCD | 0x714 / 0x71c | V |
| VDD_MISC | 0x1b8 | V |
| SVI3 VDD、VDD1、SoC、Misc | 起点 0x178，每轨跨度 0x14 | 见下文 |

每个 SVI3 电源轨显示请求电压 VID（相对 0，V）、测量电压（4，V）、电流（8，A）、
VRM 温度（0x10，°C）。0x0c 的语义未核实，不解码。这里的 VID 是浮点电压值，
不使用 PStates 的 VID 整数转换公式。共显示 24 项，实际覆盖 22 个不同偏移：
SoC/Misc 测量电压与上表两项电压是同一原始字的不同名称。

数据按小端 IEEE-754 binary32 读取。在接触浮点运算前识别 NaN/Inf，包括 signaling NaN；
异常字段输出 null 与错误码，保留原始 32 位字。未凭空添加物理范围过滤，负数、零、
次正规数等有限值不自动视为正确或错误的硬件读数。

原来源用外部测得的 BCLK/100 修正三个时钟。本次没有测量 BCLK，默认只显示存储值；
明确输入正有限 BCLK 后，另列 `(stored / 100) * supplied_bclk`，不替换存储值。
修正溢出或下溢为零时单独报错。PM 表没有在本实现中可验证的 CRC，不能声称完整性或真实性已认证。

## 已有驱动接口

固定读取 `/sys/kernel/ryzen_smu_drv` 下三个二进制节点：

1. `pm_table_version`：恰好 4 字节，小端版本。
2. `pm_table_size`：恰好 4 或 8 字节，小端长度。
3. 在版本和长度均符合上表后，读取 `pm_table`，从偏移 0 读到 EOF，限制最大 11636 字节。
4. 再读版本和长度；变化或读取失败就丢弃样本，且不重试、不保留旧字段。

不根据 sysfs 的 stat 长度判断内容长度。使用有界、只读、非阻塞文件打开，拒绝目录、FIFO、
设备文件、超长内容及嵌入 NUL 的路径。读取失败不会退回原始 PCI、MMIO 或物理内存访问。
程序不会自动加载、安装或修改外部驱动；OCTool DKMS 包也没有因此添加 PM 传输实现。

这不是无副作用的纯内存查询：已有驱动可能因读取请求向固件发送 PM 表传输命令。
数据刷新、缓存、并发读取和内核等待均由驱动处理。两次元数据检查不能证明表内容原子一致、
足够新鲜或属于某个物理目标。GUI 后台读取期间禁止再次发起操作，关闭面板解除界面回调，
但不宣称能中止已经进入内核的读取。没有伪造用户态超时或自动循环重试。

## 快照约定

JSON 为 `format: octool-amd-pm`、`schema: 1`，含版本、捕获长度、原始 `raw_hex`、
可选 `bclk_mhz` 和解码字段。每项显示名称、单位、偏移、原始字、存储值、修正时钟及独立错误。
CLI 包装为 `schema_version: 1`、`command: amd-pm-read/amd-pm-decode`、`data.pm`。
导入允许保留非有限字段的部分报告，成功与否从原始数据重新计算；不信任复制来的显示值或错误标签。

文件最大 64 KiB、JSON 嵌套最多 16 层、最多 8192 个节点；拒绝重复键、无效 UTF-8、
未知格式/版本、无效十六进制与捕获长度不符。JSON 始终使用独立于区域设置的小数格式，
不会输出非法 nan/inf 字面量。`origin_verified` 和 `atomic_snapshot_guaranteed` 均为 false。
`table_read_attempted` 只说明是否尝试读取驱动表节点，不能证明固件传输或新采样成功。

## 固定来源及验证范围

- [ZenStates-Core PowerTable.cs](https://github.com/irusanov/ZenStates-Core/blob/869b7e82572337e5a673e92230edc92bddf0e44b/Hardware/PowerTable.cs)：
  提交 `869b7e82572337e5a673e92230edc92bddf0e44b`，Git blob `62a46bab220c9b97cda415152fa1e1c173f1946e`。
  核对八个布局、时钟修正及 SVI3 选择器/单位；独立实现，没有复制或链接其解析代码。
- [ryzen_smu drv.c](https://github.com/Bjorgz/ryzen_smu/blob/c12c1e60ba8eb25a2cf19d76a3d5b57ca82057fd/drv.c)：
  提交 `c12c1e60ba8eb25a2cf19d76a3d5b57ca82057fd`，Git blob `7f26dca749f18a7bcce4875ea604fc36271e1e38`。
  核对 sysfs 二进制接口、缓存/刷新和按块读行为；驱动由系统提供，未将候选补丁当成本项目已部署驱动。

核心覆盖 19 组布局、浮点、JSON、驱动读序及故障场景；GUI 和 CLI 另外验证文件互通、
错误呈现和有界文件读取。夹具均是人工合成的固定原始字，不是真机采样。
完整矩阵与精确源码、产物、截图记录见[验证交付](amd-pm-validation.md)。
完整 PM 表、原生 PM 传输、物理核心/固件目标绑定和目标真机验收仍见[还原状态](recovery-status.md)。
