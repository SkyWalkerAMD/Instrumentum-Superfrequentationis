# Instrumentum Superfrequentationis

<!-- amd-pbo-query-5f41602 -->
2026-10-26：GUI / CLI 同步新增 **Shimada LN2 状态与逐核性能原始字查询**。
应用实现 `5f41602`，GUI/Linux 测试修正与研究提交 `e7cb847`；性能字不解释为瓦特或物理核心。

- [功能用法](docs/amd-pbo-queries.md)、[整批验证](docs/amd-pbo-query-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；核心 443 场景组，CLI 各 1465 JSON，GUI 各 224 项，12 次内核 VM。
- [REA 研究](docs/amd-pbo-query-research.md)：10 函数、848 次原指令实验；VF 数据源及 SoC helper 仍不能证明逐核/CCD 布局或 rails 写入。
- `dist/gui-cli-amd-pbo-query-5f41602/` 含 33 个包与源码；尚未进行真机验收。

以下保留历史记录。
<!-- /amd-pbo-query-5f41602 -->


<!-- amd-curve-all-40d723a -->
2026-10-26：GUI / CLI 同步新增 **Shimada 全核 Curve Optimizer**，应用验证提交 `40d723a`。
完整能力字比较后单次发送 `07`；CO 为 `-50..50` 计数，只有固件确认，没有全核回读。

- [功能用法](docs/amd-curve-all.md)、[整批验证](docs/amd-curve-all-validation.md)：43 个主要任务及 2 个补充视觉任务通过。
- 11 个 Linux 目标含 EL7；核心 436 场景组，CLI 各 1421 JSON，GUI 各 220 项，12 次内核 VM。
- [REA 研究](docs/amd-curve-all-research.md)：8 个函数、120 次原指令实验，新增 PM 长度约束；逐核/CCD 布局与 rails 写入仍未完成。
- `dist/gui-cli-amd-curve-all-40d723a/` 含 33 个包及源码；未进行真机调参验收。

以下保留历史记录。
<!-- /amd-curve-all-40d723a -->


<!-- amd-pm-native-1ad6db9 -->
2026-10-26：最新交付源码提交 `1ad6db9`，GUI / CLI 同步新增 **Shimada 原生 PM 表采集**。
要求固件返回长度与精确版本一致，读取后复查元数据；错误时不发布部分缓冲区。

- [用法](docs/amd-pm-native.md)、[整批验证](docs/amd-pm-native-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；核心 429 场景组，CLI 各 1386 JSON，GUI 各 216 项；12 次内核 VM。
- `dist/gui-cli-amd-pm-native-1ad6db9/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 研究](docs/amd-pm-native-research.md)：10 个函数、1433 次传输/VID 实验及 12 次校准实验，确认旧 PM 完成检查缺失、VID 编码分歧和校准后的布局冲突。
- 核心/CLI 验证提交为 `65a370f`，Linux 为 `1ad6db9`；291 个运行/构建路径逐文件一致。
- 逐核/CCD 布局、rails 写入与真机验收仍未完成；采集不证明采样新鲜度、原子性或物理映射。

以下保留历史记录。
<!-- /amd-pm-native-1ad6db9 -->


<!-- amd-oc-cores-8c85a28 -->
2026-10-21：最新生产验证提交 `8c85a28`，GUI / CLI 同步新增
**同一 CCD 内显式核心列表的频率设置**。每核重查能力与身份，首错停止并报告部分完成状态。

- [用法](docs/amd-manual-oc.md)、[整批验证](docs/amd-oc-cores-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1359 JSON、GUI 各 212 项、核心 422 场景组；12 次内核 VM。
- `dist/gui-cli-amd-oc-cores-8c85a28/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 研究](docs/amd-oc-cores-research.md)：八个原函数、768 组原指令边界实验；不沿用原版无条件八核循环。
- 频率只有固件确认，物理映射/真机验收未验证；逐核/CCD 遥测、rails、动态内存/EC 及其它平台仍未完成。

以下保留历史记录。
<!-- /amd-oc-cores-8c85a28 -->


<!-- amd-manual-oc-f4332b7 -->
2026-10-21：最新生产验证提交 `f4332b7`，GUI / CLI 同步新增
**Shimada 手动 OC 模式切换、全核及明确单核频率设置**。模式位可回读；频率仅有命令确认。

- [用法](docs/amd-manual-oc.md)、[整批验证](docs/amd-manual-oc-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1315 JSON、GUI 各 208 项、核心 415 场景组；12 次内核 VM。
- `dist/gui-cli-amd-oc-frequency-f4332b7/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 研究](docs/amd-manual-oc-research.md)：十个 REA 函数、十二函数 1217 条原指令回放；发现平坦单核编号换算冲突，新接口使用明确 CCD/core。
- 逐核/CCD 遥测、rails 写入、动态内存/EC 及其它平台仍未全部完成；物理映射和真机验收未验证。

以下保留历史记录。
<!-- /amd-manual-oc-f4332b7 -->


<!-- amd-pbo-limits-3b374a0 -->
2026-10-21：最新生产验证提交 `3b374a0`，GUI / CLI 同步新增
**Shimada PPT、TDC、EDC 和温度限制单项设置**，返回固件接受状态；实际限制回读尚未验证。

- [用法](docs/amd-pbo-limits.md)、[整批验证](docs/amd-pbo-limits-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1251 JSON、GUI 各 203 项、核心 405 场景组；12 次内核 VM。
- `dist/gui-cli-amd-pbo-limits-3b374a0/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 后续研究](docs/amd-pbo-dynamic-research.md)：十个函数、30 个动态内存与 281 个 EC 动态 EDC 原指令实验；新增三项本机分析回归。
- rails 写入、逐核/CCD 真实遥测、动态内存与 EC 自动策略仍未完成；真机验收未执行。

以下保留历史记录。
<!-- /amd-pbo-limits-3b374a0 -->


<!-- amd-pbo-fmax-layout-0d360d7 -->
2026-10-21：最新生产验证提交 `0d360d7`，GUI / CLI 同步新增
**Shimada Fmax 能力读取与命令确认、rails 离线编码、逐核/CCD PM 布局诊断**。

- [用法与 REA 研究](docs/amd-pbo-fmax-layout.md)、[整批验证](docs/amd-pbo-fmax-layout-validation.md)：43/43 主矩阵及 2/2 截图补充任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1221 JSON、GUI 各 199 项、核心 398 场景组；12 次内核 VM。
- `dist/gui-cli-amd-pbo-fmax-layout-0d360d7/`：33 个包、精确云端源码、更新用户源码和校验表。
- REA 分析 12 个函数；原指令覆盖 512 个熔丝输入、2 个访问故障、1025 个 rails 输入与 128 个跨版本构造组合。
- Fmax 仅确认命令，未验证实际频率；rails 写入和逐核/CCD 遥测解码尚未完成，真机验收未执行。

以下保留历史记录。
<!-- /amd-pbo-fmax-layout-0d360d7 -->


<!-- amd-pbo-scalar-7467c96 -->
2026-10-21：最新验证提交 `7467c96`，GUI / CLI 同步新增
**Shimada 1Ah/08h PBO Scalar 读取与设置**，包含旧值检查、六参数传输和精确回读。

- [用法](docs/amd-pbo-scalar.md)、[整批验证](docs/amd-pbo-scalar-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1200 JSON、GUI 各 195 项、核心 391 场景组；12 次内核 VM。
- `dist/gui-cli-amd-pbo-scalar-7467c96/`：33 个包、精确云端源码、更新用户源码和校验表。
- [REA 后续研究](docs/amd-pm-percore-layout-research.md)：11 函数，79 个 PBO/邮箱场景、144 个功耗表布局组合。
- 逐核/CCD 功耗表、其它 PBO/曲线及真机验收仍未全部完成；已发现的布局冲突保留为可重放证据。

以下保留历史记录。
<!-- /amd-pbo-scalar-7467c96 -->


<!-- amd-curve-settings-8c5424b -->
2026-10-21：最新验证提交 `8c5424b`，GUI / CLI 同步新增
**Shimada 1Ah/08h 逐核及同 CCD 选核曲线设置**，包含旧值检查、逐核回读和部分完成报告。

- [实现与用法](docs/amd-curve-settings.md)、[整批验证](docs/amd-curve-settings-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1177 JSON，GUI 各 190 项，核心 381 场景组，12 次内核 VM。
- `dist/gui-cli-amd-curve-settings-8c5424b/`：33 个包、精确云端源码、更新用户源码及校验表。
- REA 完成 12 个函数分析；逐核/CCD PM 布局、自动物理映射、其它调参及真机验收仍未全部完成。

以下保留历史记录。
<!-- /amd-curve-settings-8c5424b -->


<!-- amd-pm-arrays-52f823d -->
2026-10-20：最新验证提交 `52f823d`，GUI / CLI 同步增加 **92 个 AMD PM GMI/DIMM/PCIe 数组值**，
PM 共 220 个显示字段、12 个视图。槽位表示原始存储顺序，物理目标、实际数量和单位未认证。

- [实现说明](docs/amd-pm-arrays.md)、[整批验证](docs/amd-pm-arrays-validation.md)：43/43 云端功能验证任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1154 JSON，GUI 各 185 项，核心 373 场景组，12 次内核 VM。
- `dist/gui-cli-amd-pm-arrays-52f823d/`：33 个包、精确源码、本地扩展源码与校验表。
- [REA 后续研究](docs/amd-pm-rea-arrays.md)：两次成功分析，12 次函数查询、11 个不同函数；原版全部功能和真机验收仍未完成。

以下保留历史记录。
<!-- /amd-pm-arrays-52f823d -->

<!-- amd-pm-fabric-c3631f4 -->
2026-10-20：最新验证提交 `c3631f4`，GUI / CLI 同步增加 **27 项 AMD PM 原版链路与片内时钟监控值**，
按两组查看；连同既有字段共 128 项。名称、偏移与浮点存储已核对，物理单位、策略状态及目标仍未认证。

- [实现说明](docs/amd-pm-fabric.md)、[整批验证](docs/amd-pm-fabric-validation.md)：43/43 云端功能验证任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1132 JSON，GUI 各 181 项，核心 367 场景组，12 次内核 VM 启动。
- `dist/gui-cli-amd-pm-fabric-c3631f4/`：33 个包、精确源码、本地扩展源码及校验表。
- [REA 辅助研究](docs/amd-pm-rea-followup.md)另记录；原版全部功能与真机验收仍未完成。

以下保留历史记录。
<!-- /amd-pm-fabric-c3631f4 -->

<!-- amd-pm-package-e2660cb -->
2026-10-20：最新验证提交 `e2660cb`，GUI / CLI 同步增加 **53 项 AMD PM 原版公共功耗与策略监控值**，
按五组查看；连同既有字段共 101 项。名称、偏移与浮点存储已核对，物理单位、策略状态及目标仍未认证。

- [实现说明](docs/amd-pm-package.md)、[整批验证](docs/amd-pm-package-validation.md)：43/43 云端功能验证任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1116 JSON，GUI 各 178 项，核心 362 场景组，12 次内核 VM 启动。
- `dist/gui-cli-amd-pm-package-e2660cb/`：33 个包、精确源码、本地扩展源码及校验表。
- [REA 辅助研究](docs/amd-pm-rea-study.md)另记录；原版全部功能与真机验收仍未完成。

以下保留历史记录。
<!-- /amd-pm-package-e2660cb -->

<!-- intel-iccmax-dbe9206 -->
2026-10-18：最新验证提交 `dbe9206`，GUI / CLI 同步增加 **Alder Lake IA 电流上限读取与设置**。
九个明确型号/stepping 组合，按 SVID 拓扑定位共享 IA 轨；核验旧值和完整回读，保留其它位。

- [实现说明](docs/intel-iccmax.md)、[整批验证](docs/intel-iccmax-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1098 JSON，GUI 各 175 项，核心 357 场景组，12 套内核 VM。
- `dist/gui-cli-intel-iccmax-dbe9206/`：33 个包、精确源码、保留本地扩展的源码和校验表。

bit 31 模式语义未确认，置位时只读；不推断有效限流或无限流状态。其它型号和调参缺口仍保留，未做真机验收。以下为历史记录。
<!-- /intel-iccmax-dbe9206 -->

<!-- amd-pstates-electrical-d02f8fc -->
2026-10-18：最新验证提交 `d02f8fc`，GUI / CLI 同步恢复 **限定型号的 AMD P-state 电压与电流描述值**。
五个明确型号按 SVI2 / SVI3 解码；保留原始字段，增加关闭编码、保留位、虚拟化和失败状态。

- [实现说明](docs/amd-pstates-electrical.md)、[整批验证](docs/amd-pstates-electrical-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1055 JSON，GUI 各 169 项，共同核心 345 场景组，12 套内核 VM。
- `dist/gui-cli-amd-pstates-electrical-d02f8fc/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

数值来自 P-state 定义，不是实时电压、电流或 PBO 限制；其它型号与写入仍见还原清单。以下保留历史记录。
<!-- /amd-pstates-electrical-d02f8fc -->

<!-- intel-memory-temperature-02a2dcd -->
2026-10-18：最新验证提交 `02a2dcd`，GUI / CLI 同步增加 **DDR4 Rank 温度等级与刷新要求**。
B7/A700 内存读取现为 22 寄存器 / 83 字段，v6 快照兼容 v1–v5。

- [实现说明](docs/intel-memory-temperature.md)、[整批验证](docs/intel-memory-temperature-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1028 JSON，GUI 各 166 项，共同核心 339 场景组，12 套内核 VM。
- `dist/gui-cli-intel-memory-temperature-02a2dcd/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

温度类别是控制器存储值，新鲜度未核实，不代表实时摄氏度或实际刷新率。以下保留历史记录。
<!-- /intel-memory-temperature-02a2dcd -->

<!-- amd-pm-limits-687a5f7 -->
2026-10-18：最新验证提交 `687a5f7`，GUI / CLI 同步新增 **24 项 AMD PM 原版限制监控值**。
恢复名称、字节偏移和浮点存储值；单位与物理目标仍标为未核实，已有 24 项公共遥测保留。

- [实现说明](docs/amd-pm-limits.md)、[整批验证](docs/amd-pm-limits-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1010 JSON，GUI 各 163 项，共同核心 335 场景组，12 套内核 VM。
- `dist/gui-cli-amd-pm-limits-687a5f7/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

新增项是原版监控存储值，不代表 PBO 设置、完整 PM 表或真机验收。以下保留历史记录。
<!-- /amd-pm-limits-687a5f7 -->

<!-- intel-memory-odt-cf3e50a -->
2026-10-18：最新验证提交 `cf3e50a`，GUI / CLI 同步增加 **Intel ODT 矩阵与写数据延迟配置读取**。
B7/A700 内存读取现为 21 寄存器 / 79 字段，快照 v5 兼容 v1–v4。

- [实现说明](docs/intel-memory-odt.md)、[整批验证](docs/intel-memory-odt-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 992 JSON，GUI 各 160 项，共同核心 330 场景组，12 套内核 VM。
- `dist/gui-cli-intel-memory-odt-cf3e50a/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

新增项是存储配置，不推断 ODT 激活、电阻值或有效 tCWL/IOL。原版全部功能和真机验收仍未完成；以下保留历史记录。
<!-- /intel-memory-odt-cf3e50a -->

<!-- amd-pstates-legacy-fc330a9 -->
2026-10-18：最新验证提交 `fc330a9`，GUI / CLI 同步恢复 **AMD 17h / 19h PStates 定义读取**，
保留 1Ah 支持；修正 DFS 09h 频率计算，增加原始编码、异常状态和切换 CPU 后旧结果丢弃。

- [实现说明](docs/amd-pstates-legacy.md)、[整批验证](docs/amd-pstates-legacy-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 968 JSON，GUI 各 157 项，共同核心 325 场景组，12 套内核 VM。
- `dist/gui-cli-amd-pstates-legacy-fc330a9/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

PStates 设置、完整物理量换算及其它剩余功能和真机验收仍未完成。以下保留历史记录。
<!-- /amd-pstates-legacy-fc330a9 -->

<!-- intel-hwp-package-783dd96 -->
2026-10-18：最新验证提交 `783dd96`，GUI / CLI 同步新增 **五项 HWP 包级请求设置**，
含共享封装范围提示、PECI 覆盖检查、完整上下文比较和读回验证。

- [实现说明](docs/intel-hwp-package.md)、[整批验证](docs/intel-hwp-package-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 939 JSON，GUI 各 154 项，共同核心 320 场景组，12 套内核 VM。
- `dist/gui-cli-intel-hwp-package-783dd96/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

原版全部功能及真机验收仍未完成。以下保留历史记录。
<!-- /intel-hwp-package-783dd96 -->

<!-- intel-hwp-requests-fb09aea -->
2026-10-13：最新验证生产提交 `fb09aea`。GUI / CLI 同步恢复 **HWP 封装请求、PECI 覆盖和通知/历史**，
按能力最多新增 25 项只读信息，并补齐本地设置的覆盖/极性上下文核验。

- [实现说明](docs/intel-hwp-requests.md)、[整批验证](docs/intel-hwp-requests-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；各 CLI 26 CTest / 911 JSON，GUI 151 项，共同核心 316 场景组；12 套内核 VM。
- `dist/gui-cli-intel-hwp-requests-fb09aea/`：33 个安装包、精确源码、保留扩展源码及 SHA256。

原版全部功能及真机验收仍未完成，见[还原清单](docs/recovery-status.md)。以下保留历史记录。
<!-- /intel-hwp-requests-fb09aea -->

<!-- intel-ddrio-3e25134 -->
2026-10-13：最新验证生产提交 `3e25134`，GUI / CLI 同步增加 **DDRIO 低功耗时序与配置**。
Intel B7/A700 内存读取现为 19 寄存器 / 68 字段，v4 快照兼容 v1/v2/v3。

- [整批交付与验证](docs/intel-ddrio-validation.md)：43/43 云端任务通过，11 个 Linux 目标，包含 EL7。
- 每目标 CLI 26 CTest / 885 份 JSON，GUI 149 项；核心各 311 场景组，12 套内核 VM。
- `dist/gui-cli-intel-ddrio-3e25134/`：33 个安装包、精确源码、保留扩展源码及校验表。

原版全部功能及真机验收仍未完成，剩余部分见[还原清单](docs/recovery-status.md)。以下保留历史记录。
<!-- /intel-ddrio-3e25134 -->

<!-- intel-telemetry-39dc2e5 -->
2026-10-13：最新验证生产提交 `39dc2e5`，GUI / CLI 同步补齐 **BCLK 自适应电压、B7 PL4 峰值功率、Intel 能量计数及 HWP 能力/历史**。

- [本批交付与适用范围](docs/intel-telemetry-validation.md)：43/43 云端任务通过；11 个 Linux 目标，含 EL7。
- 各目标 CLI 26 CTest / 873 份 JSON，GUI 147 项回归；共同核心各 24 CTest / 308 场景组；12 套内核 VM。
- `dist/gui-cli-intel-telemetry-39dc2e5/`：33 个安装包、精确源码、保留扩展的用户源码与 SHA256。

原版全部功能仍未完成；ICCmax/VR、部分 AMD/主板控制及真机验收仍见[还原清单](docs/recovery-status.md)。以下保留此前记录。
<!-- /intel-telemetry-39dc2e5 -->

<!-- el7-recovery-bc30c4f -->
2026-10-13：本批已验证提交 `bc30c4f`，新增 **EL7（Oracle Linux 7.9 x86_64）**，并同步恢复 GUI / CLI 的 AVX2 偏移、TCC 温控、限频原因与历史清除、最大非睿频倍率和封装热事件。

- [EL7 安装与范围](docs/el7-port.md)、[整批验证交付](docs/el7-recovery-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611258) 4/4：各 22 CTest、291 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611273) 13/13：11 目标各 24 CTest、794 份 JSON、32 场景组。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611277) 26/26：11 目标及两个 Qt 基线各 135 项 GUI 测试，12 套匹配内核 VM。
- `dist/gui-cli-el7-recovery-bc30c4f/`：33 个安装包、固定提交源码和 SHA256。

ICCmax/VR 供电域、增强 TVB/Vmax、部分 AMD 与主板调参仍在恢复；[ICCmax 新证据](docs/intel-iccmax-research.md)没有计作已开放功能。真机调参和 Secure Boot 尚未验收。以下为历史记录。
<!-- /el7-recovery-bc30c4f -->

<!-- intel-energy-61a0494 -->
2026-10-13：最新验证生产提交 `61a0494`，GUI / CLI 同步恢复 **Intel 节能睿频与 Race to Halt**。
B7 裸机；两项独立启用/禁用，完整旧值检查、保留其它位、完整读回。

- [实现范围](docs/intel-energy.md)、[验证交付](docs/intel-energy-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323413941) 4/4，各 19 CTest、268 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323414012) 12/12，十目标各 21 CTest、657 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38323413900) 23/23，十目标各 115 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-energy-61a0494/`：30 个安装包、源码及 SHA256。

增强 TVB、Vmax、逐物理核心表及其它原版缺口和真机验收仍未全部完成。以下保留历史记录。
<!-- /intel-energy-61a0494 -->

<!-- intel-ring-c6e9599 -->
2026-10-12：最新验证生产提交 `c6e9599`，GUI / CLI 同步恢复 **Intel Ring DownBin**。
B7 裸机；独立启用/禁用，含锁、旧值和完整回读检查，保留 TVB / Vmax 等其它位。

- [实现范围](docs/intel-ring-downbin.md)、[验证交付](docs/intel-ring-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258982087) 4/4，各 18 CTest、256 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258988264) 12/12，十目标各 20 CTest、585 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38258946852) 23/23，十目标各 109 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-ring-c6e9599/`：30 个安装包、源码及 SHA256。

Vmax、逐物理核心表、其它原版缺口及真机验收仍未全部完成。以下保留历史记录。
<!-- /intel-ring-c6e9599 -->

<!-- intel-tvb-edit-494b8f0 -->
2026-10-12：最新验证生产提交 `494b8f0`，GUI / CLI 同步恢复 **Intel TVB 分档单字段编辑**。
B7 裸机；温度 A/B、降倍率 A/B，保守范围内单项设置，含旧值核对和完整读回。

- [实现范围](docs/intel-tvb-table-edit.md)、[验证交付](docs/intel-tvb-edit-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38238262625) 4/4，各 18 CTest、255 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38238267459) 12/12，十目标各 20 CTest、564 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38238260581) 23/23，十目标各 108 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-tvb-edit-494b8f0/`：30 个安装包、源码及 SHA256。

逐物理核心表、其它原版缺口及真机验收仍未全部完成。以下保留历史记录。

<!-- intel-tvb-table-aca1652 -->
2026-10-12：最新验证生产提交 `aca1652`，GUI / CLI 同步恢复 **TVB 温度与降倍率分档查询**。
限定 Intel 06_B7 裸机；支持单项或八项候选扫描，显示两组温度、降倍率及逐项错误。

- [实现范围](docs/intel-tvb-table.md)、[验证交付](docs/intel-tvb-table-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225310065) 4/4，各 244 场景组；含 8 组新表查询。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225315230) 12/12，十目标各 19 CTest、520 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38225308264) 23/23，十目标各 102 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-tvb-table-aca1652/`：30 个安装包、源码及 SHA256。

分档写入、逐物理核心表、其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-tvb-743c40f -->
2026-10-12：最新验证生产提交 `743c40f`，GUI / CLI 同步恢复 **TVB 高温倍频裁剪与电压优化开关**。
限定 Intel 06_B7 裸机；检查锁、旧配置、错误码和完整回读，保留其它位。

- [实现范围](docs/intel-tvb.md)、[验证交付](docs/intel-tvb-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219382818) 4/4，各 236 场景组；含 10 组 TVB。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219387307) 12/12，十目标各 18 CTest、493 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38219380980) 23/23，十目标各 98 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-tvb-743c40f/`：30 个安装包、源码及 SHA256。

TVB 分档表、其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-refresh-97c1b22 -->
2026-10-12：最新验证生产提交 `97c1b22`，GUI / CLI 同步增加 **Intel 刷新、自刷新与 ZQ 参数**。
限定 B7/A700，18 寄存器 / 57 字段；按类型显示别名及单位，兼容旧 v1/v2 快照。

- [实现范围](docs/intel-memory-refresh.md)、[验证交付](docs/intel-refresh-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752141) 4/4，各 226 场景组；23 组 Intel 内存及内存安全检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752189) 12/12，十目标各 17 CTest、462 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38208752202) 23/23，十目标各 93 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-refresh-97c1b22/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-clock-b3a40d7 -->
2026-10-12：最新验证生产提交 `b3a40d7`，GUI / CLI 同步新增 **Intel BCLK 与最后一次 MRC 请求**。
限定 B7/A700，三个共享 PCU 寄存器和独立快照；历史训练请求不代表实时内存频率。

- [实现范围](docs/intel-clock.md)、[验证交付](docs/intel-clock-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200818742) 4/4，各 221 场景组；12 组 Intel 时钟及内存安全检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200822292) 12/12，十目标各 17 CTest、450 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38200800678) 23/23，十目标各 91 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-clock-b3a40d7/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-extended-ec6077e -->
2026-10-12：最新验证生产提交 `ec6077e`，GUI / CLI 同步补充 **Intel 各 Rank RTL 与掉电时序**。
限定 B7/A700，扩展为 14 寄存器 / 43 字段，按字段保留时钟单位，兼容 v1 快照。

- [实现范围](docs/intel-memory-extended.md)、[验证交付](docs/intel-extended-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638950) 4/4，各 209 场景组；18 组 Intel 内存及内存安全检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638958) 12/12，十目标各 16 CTest、430 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38185638884) 23/23，十目标各 86 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-extended-ec6077e/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- intel-memory-001c804 -->
2026-10-12：最新验证生产提交 `001c804`，GUI / CLI 同步新增 **Intel Raptor Lake-S 内存时序读取与快照**。
限定 B7/A700，读取 30 字段；核对空通道、两次采样和 Gear 状态，支持两端离线快照互通。

- [实现范围](docs/intel-memory.md)、[验证交付](docs/intel-memory-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148549524) 4/4，各 205 场景组；SPD / PM / UMC / Intel 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148554697) 12/12，十目标各 16 CTest、422 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38148404619) 23/23，十目标各 84 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-memory-001c804/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- ddr4-asymmetric-57924bb -->
2026-10-11：最新验证生产提交 `57924bb`，GUI / CLI 同步完成已确认的 **DDR4 非对称容量**：
SDP/DDP rank 分配、离散密度等级与两组 3DS 层数分别计算，显示容量分项和合计。

- [解码范围](docs/ddr4-asymmetric.md)、[验证交付](docs/ddr4-asymmetric-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970745) 4/4，各 191 场景组；SPD / PM / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970568) 12/12，十目标各 15 CTest、402 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38143970549) 23/23，十目标各 79 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr4-asymmetric-57924bb/`：30 个安装包、源码及 SHA256。

其它原版功能及目标真机验收仍未全部完成。以下保留历史记录。

<!-- mixed-turbo-6433918 -->
2026-10-11：最新验证生产提交 `6433918`，GUI / CLI 同步完成 **Intel 中间组活动核心阈值编辑**
和 **DDR5 混合 rank 容量分项**。保留末组阈值和全部倍率；两组内存分别使用自身位宽和堆叠层数。

- [阈值规则](docs/turbo-thresholds.md)、[混合内存](docs/spd-asymmetric.md)、[验证交付](docs/mixed-turbo-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140350467) 4/4，各 185 场景组；SPD / PM / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140353378) 12/12，十目标各 15 CTest、392 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38140349232) 23/23，十目标各 77 Qt 测试、11 套内核 VM。
- `dist/gui-cli-mixed-turbo-6433918/`：30 个安装包、源码及 SHA256 校验文件。

原版全部功能及目标真机验收尚未完成。以下保留历史记录。

<!-- amd-pm-62e7bd8 -->
2026-10-11：最新验证生产提交 `62e7bd8`，GUI / CLI 同步新增 **AMD PM 遥测与快照互通**。
八个精确版本、24 项公共字段；支持原始表和 JSON 离线导入，并可读取已有兼容驱动。

- [实现说明](docs/amd-pm-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/amd-pm-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38135506440) 4/4，各 173 场景组；PM / SPD / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38135503372) 12/12，十目标各 15 CTest、362 份 JSON。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38135499975) 23/23，十目标各 73 Qt 测试、11 套内核 VM。
- `dist/gui-cli-amd-pm-62e7bd8/`：30 个安装包、源码及 SHA256 校验文件。

完整 PM 表、原生传输、其余调参功能及真机验收尚未完成。以下保留历史记录。

<!-- spd-user-enhanced-75c5c8b -->
2026-10-11：最新验证生产提交 `75c5c8b`，GUI / CLI 同步增加 **XMP 3.0 用户槽读取与 EXPO 1.0 增强时序存储值**。
独立检查捕获状态和 CRC，支持有效数据的零 CRC；用户槽与增强功能的启用状态仍为未知。

- [实现说明](docs/spd-user-enhanced.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/spd-user-enhanced-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130003195) 4/4，各 154 场景组；SPD/UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130005949) 12/12，十目标各 14 CTest、342 份 JSON。
- [GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38130002165) 23/23，十目标各 68 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-user-enhanced-75c5c8b/`：30 个安装包、源码及 SHA256 校验文件。
- [AMD PM 表后续研究](docs/amd-pm-table-followup.md)固定八个版本和部分字段，尚未接入实时读取。

原版全部功能与真机调参验收尚未完成。以下保留历史记录。

<!-- spd-profile-geometry-6f544e6 -->
2026-10-11：最新验证生产提交 `6f544e6`，GUI / CLI 同步补齐 **XMP 2.0 CAS/推荐 DIMM 数、对称堆叠容量与 DDR5 3DS 刷新要求**。
同时修正 DDR5 四/八子通道计数，并按基础/公共区修订版判断可解释的组织字段。

- [实现说明](docs/spd-profile-geometry.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/spd-profile-geometry-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934367) 4/4，各 145 场景组；SPD / UMC 内存检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934384) 12/12，十目标各 14 CTest、327 份 JSON。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38125934404) 23/23，十目标各 66 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-profile-geometry-6f544e6/`：30 个安装包、源码及 SHA256 校验文件。

原版全部功能与真机调参验收尚未完成。以下保留历史记录。

<!-- spd-base-timings-88530a9 -->
2026-10-11：最新验证生产提交 `88530a9`，GUI 与 CLI 同步补充 **DDR4 写延迟和 DDR5 基础次级时序**。
DDR4 新增 3 项；DDR5 新增 8 对 ps / nCK 要求，SPD 1.2 起再增加 3 对，按基础修订版判断。
这些是存储要求，不表示当前训练参数，尚未应用或写入配置。

- [实现说明](docs/spd-base-timings.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/spd-base-timings-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233835) 4/4，各 12 CTest / 137 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233886) 12/12，十目标各 14 CTest、313 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38123233832) 23/23，十目标各 64 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-base-timings-88530a9/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- spd-cas-6aca371 -->
2026-10-11：最新验证生产提交 `6aca371`，GUI 与 CLI 同步新增 **SPD CAS 支持列表与 XMP 3.0 命令率**。
读取 DDR4/DDR5 基础 CAS 和三组 XMP 3.0 CAS/命令率；保留原始字节、未知代码及校验状态。
这些是档案要求，不表示当前训练参数，尚未应用或写入档案。

- [实现说明](docs/spd-cas-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/spd-cas-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792862) 4/4，各 12 CTest / 131 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792949) 12/12，十目标各 14 CTest、301 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38121792834) 23/23，十目标各 63 Qt 测试、11 套内核 VM。
- `dist/gui-cli-spd-cas-6aca371/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- xmp-secondary-84f21bc -->
2026-10-11：最新验证生产提交 `84f21bc`，GUI 与 CLI 同步新增 **DDR5 XMP 3.0 次级时序与周期下限**。
三组厂家档案各追加 8 对 ps / nCK 要求值；保留 CRC、独立启用及异常隔离。
档案值不表示当前训练结果，尚未应用或写入档案。

- [实现说明](docs/ddr5-xmp-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/xmp-secondary-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38100545606) 4/4，各 12 CTest / 126 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38100545582) 12/12，十目标各 14 CTest、293 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38100545584) 23/23，十目标各 62 Qt 测试、11 套内核 VM。
- `dist/gui-cli-xmp-secondary-84f21bc/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- ddr4-xmp-b1b9adf -->
2026-10-11：最新验证生产提交 `b1b9adf`，GUI 与 CLI 同步新增 **DDR4 XMP 2.0 档案读取**。
两组档案的电压、12 项时序、独立启用/时间基准、有符号修正和异常隔离已接通。
XMP 2.0 无扩展 CRC，界面与 JSON 明确标注；未应用或写入档案。

- [实现说明](docs/ddr4-xmp-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/ddr4-xmp-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38099021180) 4/4，各 12 CTest / 122 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38099021189) 12/12，十目标各 14 CTest、288 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38099021214) 23/23，十目标各 61 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr4-xmp-b1b9adf/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- ddr5-expo-e4cb81b -->
2026-10-10：最新验证生产提交 `e4cb81b`，GUI 与 CLI 同步新增 **DDR5 EXPO 1.0 基础档案读取**。
两组档案的三项电压、十项时序、独立启用状态及整块 CRC 已接通；XMP/EXPO 错误分区保留。
档案值不代表当前训练值；没有 SPD 写入或配置应用。

- [实现说明](docs/ddr5-expo-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/ddr5-expo-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197597) 4/4，各 12 CTest / 116 场景组，SPD 与 UMC 的 ASan/UBSan 通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197668) 12/12，十目标各 14 CTest、278 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38056197685) 23/23，十目标各 60 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr5-expo-e4cb81b/`：10 GUI + 10 CLI + 10 可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参和稳定性未验收。以下保留历史记录。

<!-- ddr5-xmp-4932f1b -->
2026-10-10：最新验证生产提交 `4932f1b`，GUI 与 CLI 同步新增 **DDR5 XMP 3.0 厂家档案读取**。
显示三个档案的名称、电压和原始时序，分别检查头/档案 CRC，识别 EXPO 重叠区域。
档案值不代表当前训练值；没有 SPD 写入或配置应用。

- [实现说明](docs/ddr5-xmp-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/ddr5-xmp-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131383) 4/4，各 12 CTest / 111 场景组，新增 SPD ASan/UBSan 检查通过。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131391) 12/12，十目标各 14 CTest、268 份 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38048131414) 23/23，十目标各 59 Qt 测试、11 套内核 VM。
- `dist/gui-cli-ddr5-xmp-4932f1b/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参与稳定性尚未验收。以下保留此前记录。

<!-- intel-uncore-94b0655 -->
2026-10-10：最新验证生产提交 `94b0655`，GUI 与 CLI 同步新增 **Intel Ring/LLC 倍率范围**。
限定 Raptor Lake-S B7、Sapphire Rapids 8F；最小、最大倍率一起提交，保留其它位，检查旧值并完整读回。
相同值不重复写入，未测量实际频率或调参稳定性。

- [实现说明](docs/intel-uncore-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/intel-uncore-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299378) 4/4，各 11 CTest / 105 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299343) 12/12，十目标各 13 CTest、259 份模拟 JSON 报告。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38044299384) 23/23，十目标各 57 Qt 测试、11 套内核 VM。
- `dist/gui-cli-intel-uncore-94b0655/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参与稳定性尚未验收。以下保留此前记录。

<!-- umc-offline-672422a -->
2026-10-10：最新验证生产提交 `672422a`，GUI 与 CLI 同步新增 **UMC 离线快照导入与比较**。
GUI 保存的快照可以在 CLI 解码；两端共用 212 字段比较，区分字段值、原始字变化与数据缺失。
无需硬件访问，支持部分离线快照；文件身份和物理通道对应关系仍未经验证。

- [使用说明](docs/umc-offline.md)、[功能对照](docs/frontend-progress.md)、[验证交付](docs/umc-offline-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38039881978) 4/4，各 10 CTest / 98 场景组，另有 ASan/UBSan 检查。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38039885515) 12/12，十目标各 12 CTest、230 份模拟 JSON 报告及离线实际进程检查。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38039881391) 23/23，十目标各 54 Qt 测试、11 套内核 VM。
- `dist/gui-cli-umc-offline-672422a/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、源码与 SHA256SUMS。

原版全部功能仍未还原；目标真机调参与稳定性尚未验收。以下保留此前记录。

<!-- intel-vf-edit-32c89ae -->
2026-10-10：最新验证生产提交 `32c89ae`，GUI 与 CLI 同时新增 **Intel V/F 单点偏移设置**。
支持 B7 client 的 core/cache，明确选择单点；提交前核对配置，提交后复核完整点值与上下文。
只在锁和配置条件允许时开放，不自动改变全域电压或 per-core override。

- [实现及来源](docs/intel-vf-write-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证与交付](docs/intel-vf-edit-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688225) 4/4，各 9 CTest / 90 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688257) 12/12，十目标各 11 CTest、220 份 JSON。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38032688308) 23/23，十目标各 51 Qt 测试、11 套内核 VM。
- `dist/gui-cli-vf-edit-32c89ae/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码与 SHA256SUMS。

仍有其它型号及功能缺口；未进行目标真机调参或稳定性验收。以下保留此前记录。

<!-- intel-turbo-e43bad7 -->
2026-10-10：最新验证生产提交 `e43bad7`，GUI 与 CLI 同时新增 **Intel P/E 睿频分组**。
读取八组倍率和活动核心数量阈值，支持选定组倍率设置、旧值检查与完整读回；
当前限定 family 6/model B7，保留核心数量阈值，不视为逐物理核心设置。

- [实现说明](docs/intel-turbo-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证及交付](docs/intel-turbo-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028784706) 4/4，各 8 CTest / 81 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028751967) 12/12，十目标各 10 CTest、180 份 JSON 验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38028751963) 23/23，十目标各 47 Qt 测试、11 套内核 VM。
- `dist/gui-cli-turbo-e43bad7/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码和 SHA256SUMS。

原版全部功能与四台真机验收仍未完成；驱动/HAL/ABI 未改，模拟测试不等于硬件调参验收。
以下保留此前记录。

<!-- controls-register-c0f97cc -->
2026-10-10：最新验证生产提交 `c0f97cc`，GUI 与 CLI 同步更新。
新增 **HWP 活动窗口**，全部 13 项 RAPL/HWP 设置检查完整配置读回；
CLI 补齐原始 MSR/MMIO/PCI 读写，GUI 可离线导入 CLI 的 UMC 报告并重算 212 字段。

- [实现说明](docs/control-register-recovery.md)、[功能对照](docs/frontend-progress.md)、[验证及交付](docs/control-register-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023681945) 4/4，各 7 CTest / 72 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023680671) 12/12，十目标各 9 CTest、147 份 JSON 验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38023680720) 23/23，十目标各 43 Qt 测试、11 套内核 VM。
- `dist/gui-cli-controls-c0f97cc/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码和 SHA256SUMS。

AMD 写入协议的[交叉核对](docs/amd-shimada-crosscheck.md)另有记录。原版全部功能与四台真机验收仍未完成；
驱动/HAL/ABI 未改，模拟测试不等于硬件调参验收。以下保留此前记录。

<!-- intel-voltage-452f594 -->
2026-10-10：GUI 与独立 CLI 同时新增 **Intel 目标电压 + Adaptive/Override 模式设置**，验证生产提交 `452f594`。
沿用 Raptor Lake-S B7 的 core/cache 配置，明确选择单一域，保留 offset 与倍率；
旧值检查、固件状态和完整回读一致后才显示验证成功。目标配置不代表实测电压。

- [实现与研究](docs/intel-voltage-recovery.md)、[GUI/CLI 功能对照](docs/frontend-progress.md)、[验证及交付](docs/intel-voltage-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38020601340) 4/4；各 7 CTest / 67 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38020601346) 12/12；十目标各 9 CTest，91 份 JSON 验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38020601328) 23/23；十目标各 38 Qt 测试、11 套内核 VM。
- `dist/gui-cli-voltage-452f594/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码与 SHA256SUMS。

驱动/HAL/ABI 未改；VF 点写入、Intel server 电压域及四台目标真机验收仍未完成。以下保留此前记录。

<!-- intel-vf-544f0b6 -->
2026-10-10：GUI 与独立 CLI 同时新增 **Intel V/F 点查询**，验证生产提交 `544f0b6`。
支持现有 Raptor Lake-S B7 profile 的 core/cache 单点或 1..15 候选点读取，显示配置倍率、offset、
原始数据与逐点错误；未开放 V/F 写入、逐核 override 或 Xeon server 配置。

- [实现与研究](docs/intel-vf-recovery.md)、[GUI/CLI 功能对照](docs/frontend-progress.md)、[验证及交付](docs/intel-vf-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38018798924) 4/4；各 7 CTest / 62 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38018796517) 12/12；十目标各 9 CTest，65 份 JSON 输出验证。
- [Linux GUI/驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38018795844) 23/23；十目标各 35 Qt 测试、11 套内核 VM。
- `dist/gui-cli-vf-544f0b6/`：10 个 GUI + 10 个 CLI + 10 个可选 DKMS 包、对应源码与 SHA256SUMS。

驱动/HAL/ABI 未改；四台真机尚未验收。以下保留此前记录。

<!-- headless-cli-b2ef0a3 -->
2026-10-10：新增独立无桌面版 **`octool-cli`**，验证提交 `b2ef0a3`，分支 `refactor/platform-recovery`。
不依赖 Qt、X11、Wayland 或图形授权，可在 SSH/本地终端调用现有已还原核心。
提供诊断、CPU/PStates/Intel/AMD/UMC 查询、主板/传感器/SPD 及受限设置命令；设置需要明确 `--apply`。

- [无界面使用说明](docs/headless-cli.md)、[本轮验证与大小](docs/headless-cli-validation.md)、[证据清单](docs/validation/headless-cli-ci-b2ef0a3.json)。
- [CLI 云端验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466064) 12/12，十目标各 9 CTest，无显示环境安装、普通用户诊断、卸载重装通过。
- [既有 Linux 回归](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38016466047) 23/23，每目标 32 Qt 测试，11 套目标内核 VM 启动通过。
- CLI 包 138,156–173,404 字节，可执行文件 324,256–380,240 字节；不含可选 DKMS 和系统运行库。
- `dist/headless-cli-b2ef0a3/` 含 10 个 CLI + 10 个独立 DKMS 包、对应源码和 SHA256SUMS。

GUI 与 CLI 共用硬件核心和无 Qt 清单读取；驱动/HAL/ABI 未变。CLI 没有扩大硬件支持范围，四台真机仍未验收。

以下保留此前记录。

<!-- amd-curve-recovery-06b477c -->
2026-10-10：最新验证生产代码为 `06b477c`，研究分支 `refactor/platform-recovery`。
新增限定 Shimada 身份的[原始曲线查询](docs/amd-curve-query-recovery.md)：独立 B8/BC 通道、有界等待、严格返回检查和界面失效清理。
手动填写固件 CCD/core；保留 32 位原值，不推导物理核心映射或 mV，不设置曲线。

- [核心验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38013841095) 4/4，各 7 CTest / 56 场景组；[Linux 验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38013841147) 23/23，十目标各 32 Qt / 32 Python。
- 新增 98 组原构造、71 组原查询实验；分析测试共 65 项。11 套内核 VM、20 个 DEB/RPM、200 张截图已归档。
- [验证明细](docs/validation/amd-curve-recovery-ci-06b477c.json)、[安装产物](docs/artifacts.md)、[剩余还原项](docs/recovery-status.md)。四台真机验收仍未完成。

以下保留此前验证记录。

<!-- intel-ratio-recovery-fa39af6 -->
2026-10-10：最新验证生产代码为 `fa39af6`，研究分支 `refactor/platform-recovery`。
新增 Raptor Lake-S 核心/缓存最大 OC 倍频设置；保留电压 offset、target、mode，检查锁/旧值并完整回读。
原版全部功能和四台真机验收仍未完成，范围见[还原状态](docs/recovery-status.md)与[倍频实现](docs/intel-ratio-recovery.md)。

- [核心验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778227) 4/4，各 6 CTest / 49 场景组；[Linux 验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38011778246) 23/23，十目标各 29 Qt / 31 Python。
- 11 套内核 VM、20 个 DEB/RPM、200 张界面截图及源码校验已归档：[验证明细](docs/validation/intel-ratio-recovery-ci-fa39af6.json)、[安装产物](docs/artifacts.md)。
- 原 Intel 倍频 108 场景、AMD 六字段 882 场景、六行标签绑定与 80 组标志分支均进入本次云端研究门禁；不属于真实硬件调参验证。

以下保留此前验证记录。

<!-- intel-oc-recovery-90e16fb -->
2026-10-10：最新验证生产代码为 `90e16fb`，分支 `refactor/platform-recovery`。
在 UMC 212 字段、快照文件和 AMD 拓扑基础上，新增 Raptor Lake-S core/cache 电压 offset：
分别选域、保持其它字段、检查锁和旧值、有限等待与写后回读。
功能边界与剩余工作统一见[还原状态](docs/recovery-status.md)，实现见[Intel offset](docs/intel-oc-recovery.md)。

- [Linux](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38009434518) 23/23，通过十目标；每目标 27 Qt / 31 Python，11 套目标内核 VM 启动。
- [独立核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38009434514) 4/4，各 6 CTest / 45 场景组。
- 20 份 DEB/RPM、源码包和校验表已归档至 `dist/intel-oc-recovery-90e16fb/`；[完整结果](docs/validation/intel-oc-recovery-ci-90e16fb.json)含 200 张截图及源码/产物哈希。
- [AMD 六字段研究](docs/amd-limits-recovery.md)另有 882 项本地原指令实验和 3 项回归；它们没有被算入上述生产代码的云端测试数。

原版全部功能仍未完成：PStates 设置、Intel server 电压域 / VF / fabric、AMD 完整 PBO/MP1 高层调参，
以及 Intel 训练时序与板级 PMIC/VRM/EC/时钟写入仍有缺口。四台目标机器尚无真机验收。

以下为此前记录；最新生产代码及验收结果以上述版本为准。

<!-- umc-recovery-728671d -->
2026-10-10：新增 AMD UMC 212 字段 / 12 分组、显式地址组和刷新槽、离线 JSON 快照，
以及 AMD 扩展 CPUID 拓扑读取；验证源码为 `728671d`。
原构造的通道搜索和同名字段问题已从原指令复现，重构版不会沿用错误地址或以名称覆盖字段。
功能说明见 [UMC](docs/amd-umc-recovery.md)、[CPU 拓扑 / PStates 剩余问题](docs/amd-topology-recovery.md)。

- [Linux 验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38007763354) 23/23；十目标各 24 Qt / 30 Python，11 套目标内核 VM 启动。
- [核心验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38007763396) 四环境通过，各 5 CTest / 35 场景组。
- 20 份 DEB/RPM、源码包及 SHA256SUMS 已保存于 `dist/umc-recovery-728671d/`；[逐项报告](docs/validation/umc-recovery-ci-728671d.json)包含 180 张截图及产物哈希。

PStates 写入、Intel 电压 / VF、AMD 完整调参和主板专用写入仍未完成；
UMC 当前显示原始编码，没有把型号未确认的字段换算为周期。四台目标机器尚无真机验收。

以下保留此前记录；本次新增功能与验证以以上链接为准。


2026-10-10 平台恢复增量已接入，验证代码为 `0b514bb`（分支 `refactor/platform-recovery`）。
AMD PStates 增加完整 VID/Idd 原始字段；Intel Controls 增加 RAPL/HWP 读写和温度；
AMD 增加受限 BIOS SMUIO 与 CCD/core/MHz 命令准备；内存与主板页增加 DMI、hwmon、
驱动已暴露的 SPD、DDR4/DDR5 基础 CRC 和 SPD 时序解码。
**仍未完成原版全部功能**：PStates 设置/物理电压电流、Intel VF/逐核 turbo/fabric、
AMD 完整调参/拓扑与曲线、运行时内存时序及 PMIC/VRM/EC/板载时钟仍有缺口。
详细范围见[本轮功能表](docs/platform-controls.md)，不能把测试通过当作四台目标机器的硬件验收。

- [Linux 完整验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068664) 23/23，通过十个发行版目标；每目标 21 Qt / 29 Python，11 套内核 VM 启动。
- [独立核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068831) 四环境全部通过，每环境 4 CTest / 26 场景组。
- [本轮证据与哈希](docs/validation/platform-recovery-ci-0b514bb.json)；20 份 DEB/RPM 与源码包位于
  `dist/platform-recovery-0b514bb/`，参见[产物说明](docs/artifacts.md)。

以下保留此前记录；最新功能和验证以本段及其链接为准。

OCTool 功能重构与后续跨系统移植。当前目标是先重构整套程序、驱动及配套组件，
完成对应功能与验证后，再移植重构产物。GitHub 仓库名为 `Instrumentum-Superfrequentationis`，
程序、模块、协议及 `octool-x.y.z-src.tar.gz` 的命名继续沿用 octool。

在用户已有 port/ 重构上扩展 EL8/9/10、Ubuntu20.04/22.04/24.04/26.04、Debian11/12/13。

2026-10-09 本轮 `88999aa` 已完成现有重构组件的十发行版 Linux 适配，
[完整验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37923477646) 23/23 成功，包含 11 套真实发行版内核在虚拟机中的加载/通信/卸载，
普通用户 GUI、独立硬件授权、干净环境安装与 DEB/RPM 重装。
EL8 基线及十目标各 17 项 Qt 回归通过；[结果与安装包](docs/artifacts.md)已保存。
`octool --diagnose` 可在无显示环境运行。原版其他调参面板和真实主板操作仍需后续恢复/验收。

**基础版已有历史验证：十个发行版目标的模块实编、GUI 编译、离线回归、rpm/deb 安装和窗口启动通过。
[GitHub Actions run 36670288030](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36670288030)
的 23 个 job（含总门禁）全绿；EL10 与 Ubuntu26.04 使用 Mutter/Xwayland。
每个目标 QtTest 报告 12 项通过。**

2026-10-09重构增量 `beb10b7` 已拆分 PStates、寄存器请求/校验/串行核心与 Linux 后端。
本机缺 C++/Qt 工具链，已通过云端验证：独立核心四环境全通过，Linux 完整矩阵 23/23，
EL8 基线与十个发行版各 13 项 Qt 回归通过；见[本次证据与产物哈希](docs/validation/refactor-hardware-ci-beb10b7.json)。
Windows/macOS 目前验证独立核心；其硬件后端与完整 GUI 尚未实现。

新增 AMD PStates 只读频率/原始值页，CPU 与能力不匹配时拒绝自动读取。
Intel Controls、其他平台面板与真实硬件验收仍在进行；当前结果不代表原 OCTool 全功能恢复。

从 [docs/README.md](docs/README.md) 和 [CHANGELOG.md](CHANGELOG.md) 开始。

- [构建、运行与打包](docs/multi-distro.md)
- [安装包与源码归档](docs/artifacts.md)
- [GitHub 仓库与 Actions 测试](docs/github-actions.md)
- [OpenAI 托管云端接入状态](docs/cloud-access.md)
- [Linux Docker 执行矩阵](docs/cloud-build.md)
- [实际验证状态](docs/verification-status.md)
- [平台参考与已确认硬件](docs/platform-recovery.md)
- [AMD PStates 只读恢复](docs/amd-pstates.md)
- [EL8 静态 Qt SDK 实测与复用](docs/qt-sdk.md)
- [真机验收与 MMIO 对拍](docs/hardware-acceptance.md)
- [原始重构基础](port/docs/refactor-guide.md)

当前版本：2.0.1（未发布）。源码归档：`octool-2.0.1-src.tar.gz`。

无需 GUI 的 Linux 离线检查：

```sh
make -C port/tests check
python3 -m unittest discover -s port/tests -p 'test_*.py' -v
```

用户已明确授权创建公开 GitHub 仓库并用 GitHub Actions 测试，无需自有 SSH 服务器。
进入具备 Docker、QEMU、静态 BusyBox 和 C 编译器的 Linux 环境后，使用 `python3 port/ci/run-matrix.py` 执行全矩阵。
也保留 `.github/workflows/portability.yml`；两者调用同一容器内入口。
缺失输入会报错，不用预编译 Ubuntu GUI 或示例窗口替代。
