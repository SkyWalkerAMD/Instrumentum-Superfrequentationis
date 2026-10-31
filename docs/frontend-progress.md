# GUI / CLI 同步进度

2026-10-31：9950X3D 的 GUI/CLI 已同步增加三个电压命令镜像、两行逐核和一行 CCD 平均代码值，
并显示 CCD/局部核心存储槽位。公共区为 22 项，Granite 另有完整原始字视图。
软件命令镜像不是硬件回读；代码含义、物理 CCD/OS 核心绑定和第二 rails 独立设置回读仍未完成。
当前实现见[字段与协议说明](amd-granite-channel-mirrors.md)，实际测试结果见[本轮验证](amd-granite-validation.md)。
以下为历史阶段记录。

<!-- amd-firmware-pm-472e785 -->
2026-10-26：GUI / CLI 同步恢复 **八种 PM 表的逐核 / CCD 存储区**，修正 OC 模式状态位。

- [数组用法与真实边界](amd-pm-core-ccd.md)：18 行 core、13 行 CCD；最多新增 2512 个值。旧 VDDG 两项误映射已撤回；当前共 532–2730 个存储字段、14 个视图。
- [固件与 REA 研究](amd-smu-firmware-research.md)：72 个复制案例、20160 个 rails 原指令案例。rails 需要先启用 OC，索引是状态槽；第二物理轨和回读尚未证实，运行时写入仍未开放。
- [整批验证](amd-firmware-pm-validation.md)：应用 `472e785`，43/43 云端任务；独立研究 1/1。11 Linux 目标含 EL7，各 225 项 GUI、1465 CLI JSON；12 次内核 VM。
- OC 可用性是 bit 0，运行状态是 bit 16；PM 快照上限 1 MiB，GUI / CLI 导入互通。
- `dist/gui-cli-amd-firmware-pm-472e785/` 含 33 个包及源码。单位、物理映射与真机验收仍待完成。

以下保留历史记录，旧字段数量、状态位和未启用数组的结论以上述更新为准。
<!-- /amd-firmware-pm-472e785 -->


<!-- amd-pbo-query-5f41602 -->
2026-10-26：GUI / CLI 同步新增 **Shimada LN2 状态与逐核性能原始字查询**。
应用实现 `5f41602`，GUI/Linux 测试修正与研究提交 `e7cb847`；性能字不解释为瓦特或物理核心。

- [功能用法](amd-pbo-queries.md)、[整批验证](amd-pbo-query-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；核心 443 场景组，CLI 各 1465 JSON，GUI 各 224 项，12 次内核 VM。
- [REA 研究](amd-pbo-query-research.md)：10 函数、848 次原指令实验；VF 数据源及 SoC helper 仍不能证明逐核/CCD 布局或 rails 写入。
- `dist/gui-cli-amd-pbo-query-5f41602/` 含 33 个包与源码；尚未进行真机验收。

以下保留历史记录。
<!-- /amd-pbo-query-5f41602 -->


<!-- amd-curve-all-40d723a -->
2026-10-26：GUI / CLI 同步新增 **Shimada 全核 Curve Optimizer**，应用验证提交 `40d723a`。
完整能力字比较后单次发送 `07`；CO 为 `-50..50` 计数，只有固件确认，没有全核回读。

- [功能用法](amd-curve-all.md)、[整批验证](amd-curve-all-validation.md)：43 个主要任务及 2 个补充视觉任务通过。
- 11 个 Linux 目标含 EL7；核心 436 场景组，CLI 各 1421 JSON，GUI 各 220 项，12 次内核 VM。
- [REA 研究](amd-curve-all-research.md)：8 个函数、120 次原指令实验，新增 PM 长度约束；逐核/CCD 布局与 rails 写入仍未完成。
- `dist/gui-cli-amd-curve-all-40d723a/` 含 33 个包及源码；未进行真机调参验收。

以下保留历史记录。
<!-- /amd-curve-all-40d723a -->


<!-- amd-pm-native-1ad6db9 -->
2026-10-26：最新交付源码提交 `1ad6db9`，GUI / CLI 同步新增 **Shimada 原生 PM 表采集**。
要求固件返回长度与精确版本一致，读取后复查元数据；错误时不发布部分缓冲区。

- [用法](amd-pm-native.md)、[整批验证](amd-pm-native-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；核心 429 场景组，CLI 各 1386 JSON，GUI 各 216 项；12 次内核 VM。
- `dist/gui-cli-amd-pm-native-1ad6db9/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 研究](amd-pm-native-research.md)：10 个函数、1433 次传输/VID 实验及 12 次校准实验，确认旧 PM 完成检查缺失、VID 编码分歧和校准后的布局冲突。
- 核心/CLI 验证提交为 `65a370f`，Linux 为 `1ad6db9`；291 个运行/构建路径逐文件一致。
- 逐核/CCD 布局、rails 写入与真机验收仍未完成；采集不证明采样新鲜度、原子性或物理映射。

以下保留历史记录。
<!-- /amd-pm-native-1ad6db9 -->


<!-- amd-oc-cores-8c85a28 -->
2026-10-21：最新生产验证提交 `8c85a28`，GUI / CLI 同步新增
**同一 CCD 内显式核心列表的频率设置**。每核重查能力与身份，首错停止并报告部分完成状态。

- [用法](amd-manual-oc.md)、[整批验证](amd-oc-cores-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1359 JSON、GUI 各 212 项、核心 422 场景组；12 次内核 VM。
- `dist/gui-cli-amd-oc-cores-8c85a28/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 研究](amd-oc-cores-research.md)：八个原函数、768 组原指令边界实验；不沿用原版无条件八核循环。
- 频率只有固件确认，物理映射/真机验收未验证；逐核/CCD 遥测、rails、动态内存/EC 及其它平台仍未完成。

以下保留历史记录。
<!-- /amd-oc-cores-8c85a28 -->


<!-- amd-manual-oc-f4332b7 -->
2026-10-21：最新生产验证提交 `f4332b7`，GUI / CLI 同步新增
**Shimada 手动 OC 模式切换、全核及明确单核频率设置**。模式位可回读；频率仅有命令确认。

- [用法](amd-manual-oc.md)、[整批验证](amd-manual-oc-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1315 JSON、GUI 各 208 项、核心 415 场景组；12 次内核 VM。
- `dist/gui-cli-amd-oc-frequency-f4332b7/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 研究](amd-manual-oc-research.md)：十个 REA 函数、十二函数 1217 条原指令回放；发现平坦单核编号换算冲突，新接口使用明确 CCD/core。
- 逐核/CCD 遥测、rails 写入、动态内存/EC 及其它平台仍未全部完成；物理映射和真机验收未验证。

以下保留历史记录。
<!-- /amd-manual-oc-f4332b7 -->


<!-- amd-pbo-limits-3b374a0 -->
2026-10-21：最新生产验证提交 `3b374a0`，GUI / CLI 同步新增
**Shimada PPT、TDC、EDC 和温度限制单项设置**，返回固件接受状态；实际限制回读尚未验证。

- [用法](amd-pbo-limits.md)、[整批验证](amd-pbo-limits-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1251 JSON、GUI 各 203 项、核心 405 场景组；12 次内核 VM。
- `dist/gui-cli-amd-pbo-limits-3b374a0/`：33 个包、精确云端源码、更新用户源码及校验表。
- [REA 后续研究](amd-pbo-dynamic-research.md)：十个函数、30 个动态内存与 281 个 EC 动态 EDC 原指令实验；新增三项本机分析回归。
- rails 写入、逐核/CCD 真实遥测、动态内存与 EC 自动策略仍未完成；真机验收未执行。

以下保留历史记录。
<!-- /amd-pbo-limits-3b374a0 -->


<!-- amd-pbo-fmax-layout-0d360d7 -->
2026-10-21：最新生产验证提交 `0d360d7`，GUI / CLI 同步新增
**Shimada Fmax 能力读取与命令确认、rails 离线编码、逐核/CCD PM 布局诊断**。

- [用法与 REA 研究](amd-pbo-fmax-layout.md)、[整批验证](amd-pbo-fmax-layout-validation.md)：43/43 主矩阵及 2/2 截图补充任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1221 JSON、GUI 各 199 项、核心 398 场景组；12 次内核 VM。
- `dist/gui-cli-amd-pbo-fmax-layout-0d360d7/`：33 个包、精确云端源码、更新用户源码和校验表。
- REA 分析 12 个函数；原指令覆盖 512 个熔丝输入、2 个访问故障、1025 个 rails 输入与 128 个跨版本构造组合。
- Fmax 仅确认命令，未验证实际频率；rails 写入和逐核/CCD 遥测解码尚未完成，真机验收未执行。

以下保留历史记录。
<!-- /amd-pbo-fmax-layout-0d360d7 -->


<!-- amd-pbo-scalar-7467c96 -->
2026-10-21：最新验证提交 `7467c96`，GUI / CLI 同步新增
**Shimada 1Ah/08h PBO Scalar 读取与设置**，包含旧值检查、六参数传输和精确回读。

- [用法](amd-pbo-scalar.md)、[整批验证](amd-pbo-scalar-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1200 JSON、GUI 各 195 项、核心 391 场景组；12 次内核 VM。
- `dist/gui-cli-amd-pbo-scalar-7467c96/`：33 个包、精确云端源码、更新用户源码和校验表。
- [REA 后续研究](amd-pm-percore-layout-research.md)：11 函数，79 个 PBO/邮箱场景、144 个功耗表布局组合。
- 逐核/CCD 功耗表、其它 PBO/曲线及真机验收仍未全部完成；已发现的布局冲突保留为可重放证据。

以下保留历史记录。
<!-- /amd-pbo-scalar-7467c96 -->


<!-- amd-curve-settings-8c5424b -->
2026-10-21：最新验证提交 `8c5424b`，GUI / CLI 同步新增
**Shimada 1Ah/08h 逐核及同 CCD 选核曲线设置**，包含旧值检查、逐核回读和部分完成报告。

- [实现与用法](amd-curve-settings.md)、[整批验证](amd-curve-settings-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1177 JSON，GUI 各 190 项，核心 381 场景组，12 次内核 VM。
- `dist/gui-cli-amd-curve-settings-8c5424b/`：33 个包、精确云端源码、更新用户源码及校验表。
- REA 完成 12 个函数分析；逐核/CCD PM 布局、自动物理映射、其它调参及真机验收仍未全部完成。

以下保留历史记录。
<!-- /amd-curve-settings-8c5424b -->


<!-- amd-pm-arrays-52f823d -->
2026-10-20：最新验证提交 `52f823d`，GUI / CLI 同步增加 **92 个 AMD PM GMI/DIMM/PCIe 数组值**，
PM 共 220 个显示字段、12 个视图。槽位表示原始存储顺序，物理目标、实际数量和单位未认证。

- [实现说明](amd-pm-arrays.md)、[整批验证](amd-pm-arrays-validation.md)：43/43 云端功能验证任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1154 JSON，GUI 各 185 项，核心 373 场景组，12 次内核 VM。
- `dist/gui-cli-amd-pm-arrays-52f823d/`：33 个包、精确源码、本地扩展源码与校验表。
- [REA 后续研究](amd-pm-rea-arrays.md)：两次成功分析，12 次函数查询、11 个不同函数；原版全部功能和真机验收仍未完成。

以下保留历史记录。
<!-- /amd-pm-arrays-52f823d -->

<!-- amd-pm-fabric-c3631f4 -->
2026-10-20：最新验证提交 `c3631f4`，GUI / CLI 同步增加 **27 项 AMD PM 原版链路与片内时钟监控值**，
按两组查看；连同既有字段共 128 项。名称、偏移与浮点存储已核对，物理单位、策略状态及目标仍未认证。

- [实现说明](amd-pm-fabric.md)、[整批验证](amd-pm-fabric-validation.md)：43/43 云端功能验证任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1132 JSON，GUI 各 181 项，核心 367 场景组，12 次内核 VM 启动。
- `dist/gui-cli-amd-pm-fabric-c3631f4/`：33 个包、精确源码、本地扩展源码及校验表。
- [REA 辅助研究](amd-pm-rea-followup.md)另记录；原版全部功能与真机验收仍未完成。

以下保留历史记录。
<!-- /amd-pm-fabric-c3631f4 -->

<!-- amd-pm-package-e2660cb -->
2026-10-20：最新验证提交 `e2660cb`，GUI / CLI 同步增加 **53 项 AMD PM 原版公共功耗与策略监控值**，
按五组查看；连同既有字段共 101 项。名称、偏移与浮点存储已核对，物理单位、策略状态及目标仍未认证。

- [实现说明](amd-pm-package.md)、[整批验证](amd-pm-package-validation.md)：43/43 云端功能验证任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1116 JSON，GUI 各 178 项，核心 362 场景组，12 次内核 VM 启动。
- `dist/gui-cli-amd-pm-package-e2660cb/`：33 个包、精确源码、本地扩展源码及校验表。
- [REA 辅助研究](amd-pm-rea-study.md)另记录；原版全部功能与真机验收仍未完成。

以下保留历史记录。
<!-- /amd-pm-package-e2660cb -->

<!-- intel-iccmax-dbe9206 -->
2026-10-18：最新验证提交 `dbe9206`，GUI / CLI 同步增加 **Alder Lake IA 电流上限读取与设置**。
九个明确型号/stepping 组合，按 SVID 拓扑定位共享 IA 轨；核验旧值和完整回读，保留其它位。

- [实现说明](intel-iccmax.md)、[整批验证](intel-iccmax-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1098 JSON，GUI 各 175 项，核心 357 场景组，12 套内核 VM。
- `dist/gui-cli-intel-iccmax-dbe9206/`：33 个包、精确源码、保留本地扩展的源码和校验表。

bit 31 模式语义未确认，置位时只读；不推断有效限流或无限流状态。其它型号和调参缺口仍保留，未做真机验收。以下为历史记录。
<!-- /intel-iccmax-dbe9206 -->

<!-- amd-pstates-electrical-d02f8fc -->
2026-10-18：最新验证提交 `d02f8fc`，GUI / CLI 同步恢复 **限定型号的 AMD P-state 电压与电流描述值**。
五个明确型号按 SVI2 / SVI3 解码；保留原始字段，增加关闭编码、保留位、虚拟化和失败状态。

- [实现说明](amd-pstates-electrical.md)、[整批验证](amd-pstates-electrical-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1055 JSON，GUI 各 169 项，共同核心 345 场景组，12 套内核 VM。
- `dist/gui-cli-amd-pstates-electrical-d02f8fc/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

数值来自 P-state 定义，不是实时电压、电流或 PBO 限制；其它型号与写入仍见还原清单。以下保留历史记录。
<!-- /amd-pstates-electrical-d02f8fc -->

<!-- intel-memory-temperature-02a2dcd -->
2026-10-18：最新验证提交 `02a2dcd`，GUI / CLI 同步增加 **DDR4 Rank 温度等级与刷新要求**。
B7/A700 内存读取现为 22 寄存器 / 83 字段，v6 快照兼容 v1–v5。

- [实现说明](intel-memory-temperature.md)、[整批验证](intel-memory-temperature-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1028 JSON，GUI 各 166 项，共同核心 339 场景组，12 套内核 VM。
- `dist/gui-cli-intel-memory-temperature-02a2dcd/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

温度类别是控制器存储值，新鲜度未核实，不代表实时摄氏度或实际刷新率。以下保留历史记录。
<!-- /intel-memory-temperature-02a2dcd -->

<!-- amd-pm-limits-687a5f7 -->
2026-10-18：最新验证提交 `687a5f7`，GUI / CLI 同步新增 **24 项 AMD PM 原版限制监控值**。
恢复名称、字节偏移和浮点存储值；单位与物理目标仍标为未核实，已有 24 项公共遥测保留。

- [实现说明](amd-pm-limits.md)、[整批验证](amd-pm-limits-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 1010 JSON，GUI 各 163 项，共同核心 335 场景组，12 套内核 VM。
- `dist/gui-cli-amd-pm-limits-687a5f7/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

新增项是原版监控存储值，不代表 PBO 设置、完整 PM 表或真机验收。以下保留历史记录。
<!-- /amd-pm-limits-687a5f7 -->

<!-- intel-memory-odt-cf3e50a -->
2026-10-18：最新验证提交 `cf3e50a`，GUI / CLI 同步增加 **Intel ODT 矩阵与写数据延迟配置读取**。
B7/A700 内存读取现为 21 寄存器 / 79 字段，快照 v5 兼容 v1–v4。

- [实现说明](intel-memory-odt.md)、[整批验证](intel-memory-odt-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 992 JSON，GUI 各 160 项，共同核心 330 场景组，12 套内核 VM。
- `dist/gui-cli-intel-memory-odt-cf3e50a/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

新增项是存储配置，不推断 ODT 激活、电阻值或有效 tCWL/IOL。原版全部功能和真机验收仍未完成；以下保留历史记录。
<!-- /intel-memory-odt-cf3e50a -->

<!-- amd-pstates-legacy-fc330a9 -->
2026-10-18：最新验证提交 `fc330a9`，GUI / CLI 同步恢复 **AMD 17h / 19h PStates 定义读取**，
保留 1Ah 支持；修正 DFS 09h 频率计算，增加原始编码、异常状态和切换 CPU 后旧结果丢弃。

- [实现说明](amd-pstates-legacy.md)、[整批验证](amd-pstates-legacy-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 968 JSON，GUI 各 157 项，共同核心 325 场景组，12 套内核 VM。
- `dist/gui-cli-amd-pstates-legacy-fc330a9/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

PStates 设置、完整物理量换算及其它剩余功能和真机验收仍未完成。以下保留历史记录。
<!-- /amd-pstates-legacy-fc330a9 -->

<!-- intel-hwp-package-783dd96 -->
2026-10-18：最新验证提交 `783dd96`，GUI / CLI 同步新增 **五项 HWP 包级请求设置**，
含共享封装范围提示、PECI 覆盖检查、完整上下文比较和读回验证。

- [实现说明](intel-hwp-package.md)、[整批验证](intel-hwp-package-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；CLI 各 939 JSON，GUI 各 154 项，共同核心 320 场景组，12 套内核 VM。
- `dist/gui-cli-intel-hwp-package-783dd96/`：33 个包、精确源码、保留本地扩展的源码和 SHA256。

原版全部功能及真机验收仍未完成。以下保留历史记录。
<!-- /intel-hwp-package-783dd96 -->

<!-- intel-hwp-requests-fb09aea -->
2026-10-13：最新验证生产提交 `fb09aea`。GUI / CLI 同步恢复 **HWP 封装请求、PECI 覆盖和通知/历史**，
按能力最多新增 25 项只读信息，并补齐本地设置的覆盖/极性上下文核验。

- [实现说明](intel-hwp-requests.md)、[整批验证](intel-hwp-requests-validation.md)：43/43 云端任务通过。
- 11 个 Linux 目标含 EL7；各 CLI 26 CTest / 911 JSON，GUI 151 项，共同核心 316 场景组；12 套内核 VM。
- `dist/gui-cli-intel-hwp-requests-fb09aea/`：33 个安装包、精确源码、保留扩展源码及 SHA256。

原版全部功能及真机验收仍未完成，见[还原清单](recovery-status.md)。以下保留历史记录。
<!-- /intel-hwp-requests-fb09aea -->

<!-- intel-ddrio-3e25134 -->
2026-10-13：最新验证生产提交 `3e25134`，GUI / CLI 同步增加 **DDRIO 低功耗时序与配置**。
Intel B7/A700 内存读取现为 19 寄存器 / 68 字段，v4 快照兼容 v1/v2/v3。

- [整批交付与验证](intel-ddrio-validation.md)：43/43 云端任务通过，11 个 Linux 目标，包含 EL7。
- 每目标 CLI 26 CTest / 885 份 JSON，GUI 149 项；核心各 311 场景组，12 套内核 VM。
- `dist/gui-cli-intel-ddrio-3e25134/`：33 个安装包、精确源码、保留扩展源码及校验表。

原版全部功能及真机验收仍未完成，剩余部分见[还原清单](recovery-status.md)。以下保留历史记录。
<!-- /intel-ddrio-3e25134 -->

<!-- intel-telemetry-39dc2e5 -->
2026-10-13：最新验证生产提交 `39dc2e5`，GUI / CLI 同步补齐 **BCLK 自适应电压、B7 PL4 峰值功率、Intel 能量计数及 HWP 能力/历史**。

- [本批交付与适用范围](intel-telemetry-validation.md)：43/43 云端任务通过；11 个 Linux 目标，含 EL7。
- 各目标 CLI 26 CTest / 873 份 JSON，GUI 147 项回归；共同核心各 24 CTest / 308 场景组；12 套内核 VM。
- `dist/gui-cli-intel-telemetry-39dc2e5/`：33 个安装包、精确源码、保留扩展的用户源码与 SHA256。

原版全部功能仍未完成；ICCmax/VR、部分 AMD/主板控制及真机验收仍见[还原清单](recovery-status.md)。以下保留此前记录。
<!-- /intel-telemetry-39dc2e5 -->

<!-- el7-recovery-bc30c4f -->
2026-10-13：本批已验证提交 `bc30c4f`，新增 **EL7（Oracle Linux 7.9 x86_64）**，并同步恢复 GUI / CLI 的 AVX2 偏移、TCC 温控、限频原因与历史清除、最大非睿频倍率和封装热事件。

- [EL7 安装与范围](el7-port.md)、[整批验证交付](el7-recovery-validation.md)。
- [共同核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611258) 4/4：各 22 CTest、291 场景组。
- [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611273) 13/13：11 目标各 24 CTest、794 份 JSON、32 场景组。
- [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38335611277) 26/26：11 目标及两个 Qt 基线各 135 项 GUI 测试，12 套匹配内核 VM。
- `dist/gui-cli-el7-recovery-bc30c4f/`：33 个安装包、固定提交源码和 SHA256。

ICCmax/VR 供电域、增强 TVB/Vmax、部分 AMD 与主板调参仍在恢复；[ICCmax 新证据](intel-iccmax-research.md)没有计作已开放功能。真机调参和 Secure Boot 尚未验收。以下为历史记录。
<!-- /el7-recovery-bc30c4f -->

2026-10-13 新增 [Energy Efficient Turbo / Race to Halt](intel-energy.md)：GUI 节能控制页、CLI intel-energy-read/set 同步接入。限定 B7；生产提交 `61a0494` 的 39 项云端任务全部通过，见 [验证交付](intel-energy-validation.md)。

2026-10-12 新增 [Ring DownBin](intel-ring-downbin.md)：GUI 配置页增加第三个开关，CLI 同步增加 ring-down-bin 字段。限定 B7；生产提交 `c6e9599` 的 39 项云端任务全部通过，见 [验证交付](intel-ring-validation.md)。

2026-10-12 新增 [TVB 单字段编辑](intel-tvb-table-edit.md)：两端支持温度 A/B、降倍率 A/B，含准备、旧值核对及完整读回；生产提交 `494b8f0` 的 39 项云端任务全部通过，见 [验证交付](intel-tvb-edit-validation.md)。

2026-10-12 本轮新增 [TVB 温度与降倍率分档查询](intel-tvb-table.md)：GUI `TVB thresholds` 与 CLI `intel-tvb-table` 共用核心。限定 B7 裸机；保守范围的单字段编辑现由本页首项补充。生产提交 `aca1652` 的 39 项云端任务全部通过，见 [验证交付](intel-tvb-table-validation.md)。

2026-10-12 早先恢复 [TVB 两项配置开关](intel-tvb.md)：GUI `TVB configuration`、CLI `intel-tvb-read/set` 共用核心。限定 06_B7 裸机；温度分档读取现已由本页首项补充。生产提交 `743c40f` 的 39 项云端任务全部通过，见 [验证交付](intel-tvb-validation.md)。

GUI `Intel Controls → Clock information` 与 CLI `intel-clock-read` / `intel-clock-decode`
同步提供 [Intel BCLK 与最后一次 MRC 请求](intel-clock.md)，限定 B7/A700。历史训练请求不代表实时内存频率。

GUI Intel Controls / Memory timings 与 CLI `intel-memory-read`、`intel-memory-decode`
同步提供 [Raptor Lake-S 内存控制器时序与快照](intel-memory.md)，限定 B7/A700。
2026-10-12 当前扩展为 57 字段，含 [RTL 与掉电参数](intel-memory-extended.md)及
[刷新、自刷新与 ZQ 参数](intel-memory-refresh.md)，兼容旧 v1/v2 快照。

新增 [Intel 中间组阈值编辑](turbo-thresholds.md)和 [DDR5 混合 rank 容量](spd-asymmetric.md)，两端共用实现与约束。

继续补齐 [DDR4 非对称容量](ddr4-asymmetric.md)：SDP/DDP 的 Rank 分配与 3DS 层数单独处理，GUI/CLI 同步显示分项。

2026-10-11：新增 [AMD PM 遥测与快照互通](amd-pm-recovery.md)。GUI / CLI 同步解码八个精确版本的
24 项公共字段，并可读取系统已有兼容驱动的 sysfs 表；未新增原生传输、目标映射或调参写入。

两个入口共用 `gui/core`，Linux 硬件访问共用 HAL 与驱动。新增功能同时落到 GUI 页面、CLI 命令、
共同核心和两套入口测试；包以同一个 Git 提交标识交付。当前还原范围如下。

| 功能 | GUI | CLI | 共同边界 |
|---|---|---|---|
| AMD PStates | AMD PStates 页 | `amd-pstates` | Family 17h/19h/1Ah 定义读取、范围/编码和逐行状态，未恢复设置 |
| Intel RAPL / HWP / DTS | Power / performance | `intel-read` / `intel-set` | 型号/能力门控；含 HWP 活动窗口、封装/PECI 请求及覆盖、通知/历史；另按能力提供五项 HWP 包级设置，B7 最多 19 项；完整回读并核验 HWP 上下文 |
| Intel core/cache offset、最大倍率、目标电压与模式 | Core / cache voltage and ratio | `intel-oc-read` / `intel-oc-set` | B7；[目标电压 + Adaptive/Override](intel-voltage-recovery.md)，锁、旧值与完整回读 |
| Intel V/F 点 | V/F points 查询、准备与设置 | `intel-vf-read` / `intel-vf-set` | B7，两域，候选 1..15 查询及单点 offset；[完整上下文核对](intel-vf-write-recovery.md)，不改变 override 模式 |
| Intel 睿频分组 | Turbo ratio groups | `intel-turbo-read` / `intel-turbo-set` | B7；P/E 两表，读取活动核心阈值，单组倍率或中间组阈值设置与完整读回；末组阈值保持 |
| Intel Ring / LLC 范围 | Ring / LLC range | `intel-uncore-read` / `intel-uncore-set` | B7/8F；最小与最大倍率同时设置，保留其它位、旧值检查及完整读回；[实现说明](intel-uncore-recovery.md) |
| Intel BCLK 自适应电压 | BCLK voltage | `intel-bclk-voltage-read/set` | B7；独立开关，OC 锁与旧值检查，只改 bit 2 并完整回读；[说明](intel-bclk-voltage.md) |
| Intel 能量与 HWP 能力/历史 | Power / performance | `intel-read` | B7 封装/PP0、8F/CF 封装/DRAM 计数，单位及回绕范围；HWP 四个能力字节及两项事件历史；[说明](intel-energy-counters.md) |
| Intel PL4 峰值功率 | Power / performance | `intel-read` / `intel-set --field pl4` | B7；13 位功率字段、RAPL 单位、锁/旧值检查和完整回读；[说明](intel-pl4.md) |
| Intel IA ICCmax | IA current limit | `intel-iccmax-read/set` | 明确 Alder Lake stepping；共享 IA SVID 地址、1..511.75 A / 0.25 A、旧值/拓扑/锁及回读；bit 31 置位和默认编码只读 |
| Intel AVX2 偏移 | AVX2 offset | `intel-avx-read/set` | B7；0..31 倍率档，锁、旧值检查，保留其它位及完整回读 |
| Intel TCC 温控 | Thermal limit | `intel-thermal-read/set` | B7/8F/CF；偏移、默认与有效阈值，能力/锁/范围检查 |
| Intel 限频原因 | Limit reasons | `intel-limits-read/clear` | B7；11 组当前/历史状态，单项或全部历史 W0C 清除与回读 |
| Intel 平台倍率及封装热事件 | Power / performance | `intel-read` | B7/8F/CF；只读倍率及 6 对封装当前/历史位，错误不会伪装为零 |
| Intel 节能控制 | Energy optimization | `intel-energy-read/set` | 06_B7 裸机；节能睿频 / Race to Halt 独立设置，完整旧值核对和 64 位回读；显式 CPU，不广播 |
| Intel 内存控制器 | Memory timings | `intel-memory-read/decode` | B7/A700；79 字段含 RTL、掉电、刷新、自刷新、ZQ、DDRIO、ODT 矩阵与写数据延迟，v1–v5 双向快照；仅读取 |
| Intel 时钟信息 | Clock information | `intel-clock-read/decode` | B7/A700；报告 BCLK kHz，历史 MRC Gear/倍率/VDDQ 请求；独立只读快照，不推导实时内存频率 |
| Intel TVB / Ring DownBin | TVB & Ring DownBin | `intel-tvb-read/set` | 06_B7 裸机；高温倍频裁剪、电压优化及 Ring DownBin 三项独立开关，锁/旧值检查、保留其它位、完整回读；分档见下行 |
| Intel TVB 分档 | TVB thresholds 查询 / 准备 / 编辑 | `intel-tvb-table` / `intel-tvb-table-set` | 06_B7 裸机；两组温度 / 降倍率查询与保守范围的单字段设置，旧值 / 锁核对及完整读回；逐物理核心表未恢复 |
| AMD SMU | AMD tuning | `amd-smu-probe/read/send` | 现有身份/命令白名单与原始参数 |
| AMD LN2 / 逐核性能原始字 | AMD tuning → Shimada firmware query | `amd-ln2-read` / `amd-core-performance-read --firmware-index` | 1Ah/08h；LN2 仅 0/1；性能字不透明、无物理映射；身份复查与未知/失败状态隔离；不发送设置命令 |
| AMD PBO Scalar | AMD tuning → Read / Apply scalar | `amd-pbo-scalar-read/set` | Shimada 1Ah/08h；旧值比较及精确回读，1..10 整数倍；不是物理电压或功率测量 |
| AMD Fmax 与 PPT/TDC/EDC/温度限制 | AMD tuning → Read PBO capability / Apply | `amd-pbo-capabilities` / `amd-pbo-fmax-set` / `amd-pbo-limit-set` | Shimada 1Ah/08h；完整能力字比较及单项设置，只有命令确认，当前限制未验证 |
| AMD 手动 OC 模式 | AMD tuning → Read OC capability / Apply mode | `amd-oc-mode-set` | Shimada 1Ah/08h；显式开启/关闭、旧能力核对及模式位回读；不隐含重置 Scalar 或写 VID |
| AMD 全核 / 单核 / CCD 选定核心频率 | AMD tuning → Manual OC frequency / Selected cores in CCD | `amd-oc-frequency-set --scope all-cores/core/cores` | Shimada 1Ah/08h；显式核心列表、逐核能力检查、首错停止与部分完成报告；只有命令确认，物理映射与实际频率未验证 |
| AMD 曲线读取 | AMD tuning 曲线查询 | `amd-curve-read` | Shimada；显式固件 CCD/core；不推导 mV |
| AMD 全核曲线设置 | AMD tuning → Read capability / Apply curve to all cores | `amd-curve-all-set --margin --expect-caps --apply` | 1Ah/08h；显式固件全核范围，CO count；完整能力比较，单次 07，仅确认，当前曲线/物理核心数未验证 |
| AMD 逐核 / CCD 选核曲线设置 | AMD tuning → Read curve / Apply curve to selected cores | `amd-curve-set --core/--cores --margin --expect-raw --apply` | 1Ah/08h；旧值检查、逐核回读、部分完成报告；CO count，不是 mV；真机及物理映射未验收 |
| AMD 拓扑 | AMD tuning | `amd-topology` | CPUID 拓扑，不作为固件目标映射 |
| AMD PM 原版功耗与策略监控 | PM telemetry → Display 五组 | `amd-pm-read/decode` 的 `original_package` | 53 项名称/偏移/浮点存储值；单位、策略状态和物理目标未认证；异常隔离与旧快照重算 |
| AMD PM 原版链路与片内时钟监控 | PM telemetry → Display 两组 | `amd-pm-read/decode` 的 `original_fabric` | 27 项名称/偏移/浮点存储值；单位、策略状态和物理目标未认证；异常隔离与旧快照重算 |
| AMD PM 原始数组 | PM telemetry → Display 三组 | `amd-pm-read/decode` 的 `original_arrays` | 92 存储字；槽号和数量不代表物理目标；1 MiB PM 快照、旧格式重算和错误隔离 |
| AMD PM 逐核 / CCD 存储区 | PM telemetry → Core / CCD stored arrays | `amd-pm-read/decode/native-read` 的 `firmware_core_arrays` / `firmware_ccd_arrays` | 18 × 8N 个 core 字、13N 个 CCD 字；N 来自精确版本，非 CPU 数；标签、单位与物理映射未认证 |
| AMD PM 原生采集 | PM telemetry → Read native PM table | `amd-pm-native-read --cpu N` | Shimada 1Ah/08h；固件返回长度、64 位范围、传输确认与读后元数据校验；快照互通；原子性、采样时间、物理映射未验证 |
| AMD PM 遥测 | AMD tuning → PM telemetry | `amd-pm-read/decode` | 八个精确 Shimada 版本、22 项公共字段（撤回 VDDG 误映射）；已有驱动读取、原始文件解码与 JSON 双向导入；BCLK 修正需外部值，未完成整表与物理目标认证 |
| AMD UMC | AMD UMC，打开/比较快照与差异筛选 | `amd-umc-read/decode/diff` | 212 字段、56 原始寄存器；GUI/CLI 双向离线导入与共用比较；无时序写入 |
| DMI / 传感器 / SPD | Memory / Motherboard | `inventory` / `spd-decode` | 读取现有系统节点与离线 SPD；[DDR4 XMP 2.0](ddr4-xmp-recovery.md) 两组档案、电压/时序/独立时间基准（无扩展 CRC）；[DDR5 XMP 3.0](ddr5-xmp-recovery.md) 三个厂家档案含 8 项次级时序和独立周期下限（ps / nCK）；[EXPO 1.0](ddr5-expo-recovery.md) 两组基础档案、电压/时序和逐区 CRC；[XMP 用户槽读取与 EXPO 增强时序存储字](spd-user-enhanced.md)，启用状态未知 |
| 原始 MSR / MMIO / PCI | Register 编辑页 | `register-read` / `register-write` | 相同宽度、范围与对齐校验；明确目标，写入不自动回读 |

GUI 使用图形授权，CLI 使用终端权限。UMC 的 GUI/CLI 快照和 CLI decode 报告可双向导入，
两端支持同 bank/slot 的离线比较，区分字段变化、原始字变化与数据缺失。
[UMC 离线说明](umc-offline.md)记录格式、文件边界和比较语义。

SPD 两端还同步支持[基础 CAS 位图及 XMP 3.0 CAS / 命令率](spd-cas-recovery.md)，
区分零位图、未知修订版和未知代码，保留原始字节；不代表当前训练配置。
现也支持[DDR4 写延迟与 DDR5 基础次级时序](spd-base-timings.md)，DDR5 的时间和周期下限分别显示，
版本 1.2 新增的三对字段按基础修订版判断。
另已接通[DDR4 XMP CAS/推荐 DIMM 数及堆叠组织](spd-profile-geometry.md)，修正 DDR5 四/八子通道容量，
支持对称堆叠、DDR5 混合 rank 分项容量与 3DS 不同逻辑 rank 刷新下限；基础区和模块公共区分别检查修订版。

双方共同未完成：AMD PStates 写入、其它 AMD 平台曲线/Curve Shaper、物理目标映射、逐核/CCD PM 布局、其它 PBO 命令、现有频率/限制的实际回读、rails 硬件写入及动态内存/EC 策略、
Intel 其它型号的 VF 及 W790/W890 电压/逐核/fabric、其它 Intel 型号 RTL、剩余内存时序/IOL 与训练控制、板级 PMIC/VRM/EC/时钟写入。
四台目标真机验收与 Secure Boot 实机签名部署也未完成。详见[还原状态](recovery-status.md)。
