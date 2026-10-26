# 无桌面版 OCTool

<!-- amd-fmax-readback-current -->
新增 [Fmax 固件设置回读](amd-fmax-readback.md)：GUI / CLI 同步支持 SMU 115.46.0 的 `6Eh` 查询与 `70h` 写后校验。
设置前比较旧 MHz 和完整能力字；CLI 现在要求 `--expect-mhz`。固件裁剪或回读不符会保留实际值并报告错误。
读回的是保存的共享上限，运行频率尚未实测。下文原先“Fmax 仅命令确认”的描述属于历史阶段。
<!-- /amd-fmax-readback-current -->

新增 [共享 power adjustment 读写](amd-power-adjustment.md)：`amd-power-adjustment-read`
和 `amd-power-adjustment-set`。仅放行已核对的 SMU 115.46.0；A8 返回有符号固件计数，
A7 接受 −10…5，比较旧值和完整能力字后写入并读回。物理单位尚未确认，不作为瓦数或功耗表偏移。

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

Intel 内存读取已扩展为 v6：22 寄存器 / 83 字段，新增 [DDR4 温度等级与刷新要求](intel-memory-temperature.md)，兼容 v1–v5。GUI 与 CLI 共享解析器；温度新鲜度仍未核实。

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

2026-10-13 上批补充 [AVX2 倍率偏移](intel-avx-offset.md)和
[Intel 温控配置](intel-thermal.md)，以及 [Intel 限频原因与历史清除](intel-limits.md)：
GUI 与 CLI 同步，含独立读取、显式应用和回读检查。
`intel-read` 另增加[最大非睿频倍率](intel-platform.md)只读字段，限定 B7/8F/CF。
同一命令增加[封装热事件](intel-package-events.md)：6 组当前/历史状态，GUI 同步显示。
另新增 [EL7 独立构建目标](el7-port.md)，系统级验证进度与硬件功能验收分开记录。

2026-10-13 新增 [Intel 节能配置](intel-energy.md)：
`intel-energy-read --cpu N`、`intel-energy-set --cpu N --field efficient-turbo|race-to-halt --value 0|1 --apply`。
裸机 B7，逐项设置，保留其余 63 位并完整读回；不广播到其它 CPU，不测量实际能耗。

GUI `Intel Controls → TVB configuration` 与 CLI `intel-tvb-read` / `intel-tvb-set`
同步提供 [TVB 高温倍频裁剪与电压优化开关](intel-tvb.md)，限定 Intel 06_B7 裸机配置访问。
统一使用 1 = 启用、0 = 禁用；检查锁、旧值和完整回读。温度/倍频分档表不在本轮范围。

GUI `Intel Controls → Clock information` 与 CLI `intel-clock-read` / `intel-clock-decode`
同步提供 [Intel BCLK 与最后一次 MRC 请求](intel-clock.md)，限定 B7/A700。历史训练请求不代表实时内存频率。

GUI Intel Controls / Memory timings 与 CLI `intel-memory-read`、`intel-memory-decode`
同步提供 [Raptor Lake-S 内存控制器时序与快照](intel-memory.md)，限定 B7/A700。
2026-10-12 当前扩展为 57 字段，含 [RTL 与掉电参数](intel-memory-extended.md)及
[刷新、自刷新与 ZQ 参数](intel-memory-refresh.md)，兼容旧 v1/v2 快照。

新增 `intel-turbo-set --field active-cores`，只编辑中间活动组阈值，详见[输入与读回规则](turbo-thresholds.md)。
`spd-decode` / `inventory` 同时支持 [DDR5 混合 rank 容量分项](spd-asymmetric.md)。
另支持 [DDR4 已确认的非对称 Rank 布局](ddr4-asymmetric.md)，包括独立的密度等级和 SDP/DDP/3DS 规则。

2026-10-11：新增 [AMD PM 遥测与快照互通](amd-pm-recovery.md)。GUI / CLI 同步支持八个精确表版本的
24 项公共字段；系统已有兼容驱动时可读取 sysfs 表，也可无驱动离线解码原始文件与 JSON。

`octool-cli` 是独立的 Linux x86_64 命令行程序，直接复用现有核心、HAL 和 `.ko` 驱动。
构建和运行均不依赖 Qt、X11、Wayland、桌面会话、polkit 或常驻服务，可从 SSH 或本地终端运行。
与 GUI 可以共存，分别安装 `octool-cli` 和 `octool`。不要同时运行多个硬件调参程序；
现有事务锁只覆盖单进程，不能协调其它进程或内核驱动对间接寄存器的操作。

此前 `octool --diagnose` 只是 GUI 二进制的无显示诊断入口，仍带 GUI 包依赖；本程序提供实际模块命令。
本版没有增加硬件支持范围，原版尚未还原的功能及真机验收缺口见[还原状态](recovery-status.md)。

## 安装与构建

选择与发行版匹配的 `octool-cli-2.0.1-1*.deb` 或 `.rpm`：

```sh
# Debian / Ubuntu，先进入对应发行版安装包目录
sudo apt install ./octool-cli-*.deb
# Rocky / EL
sudo dnf install ./octool-cli-*.rpm
```

CLI 包只依赖系统 C/C++ 运行库，不自动安装 GUI 或 DKMS。
现有 `octool-hwio-dkms` 包可单独安装，为目标内核生成 `octool_hwio.ko`。
诊断、普通 CPUID 和可读的 sysfs 信息无需驱动；MSR/PCI 等访问仍取决于权限、硬件、
内核策略及已加载的驱动。程序不自动加载模块，也不修改权限。
需要特权的操作可通过终端的 `sudo octool-cli ...` 执行，不弹图形授权窗口。

仅构建 CLI 的例子（CMake >=3.16、C/C++ 编译器；测试另外需要 Python >=3.8）：

```sh
cmake -S cli -B build/cli -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/cli --parallel 2
sudo cmake --install build/cli --prefix /usr/local
```

运行测试时启用 `BUILD_TESTING`，在 `build/cli` 目录执行 `ctest --output-on-failure`。
打包使用 `python3 port/tools/build_cli.py --format deb`（或 `rpm`），需要对应原生打包工具。
每个包使用目标发行版自身的 glibc/libstdc++，不能把较新系统的包当作 EL8 通用包。

## 开始使用

```sh
octool-cli --help
octool-cli diagnose
octool-cli inventory > inventory.json
octool-cli spd-decode --file memory.spd > spd.json
```

SPD 报告解码 [DDR4 XMP 2.0 两组档案](ddr4-xmp-recovery.md)、[DDR5 XMP 3.0 厂家档案](ddr5-xmp-recovery.md)和 [EXPO 1.0 基础档案](ddr5-expo-recovery.md)：电压及存储时序；DDR5 档案有区域 CRC，XMP 3.0 另含名称。XMP 2.0 没有扩展 CRC，JSON 和 GUI 会明确区分。
档案校验失败仍保留基础字段及其它完整档案；`spd-decode` 返回退出码 3。
XMP 3.0 每个有效厂家档案有 30 项数值，含 8 项次级时序及其独立周期下限；
分别使用 `ps` 和 `nCK`，原有刷新时序仍为 `ns`。请按 `name` 取值，不固定数组长度。
基础 `fields` 现包含[DDR4 写延迟及 DDR5 次级时序](spd-base-timings.md)，名称标明 ps / nCK，
值仍为字符串；DDR5 1.2 新增字段按修订版判断，未支持版本不输出这些解释值。

新增独立的[CAS 列表与命令率元数据](spd-cas-recovery.md)：基础 `cas` 以及 XMP 3.0 档案的
`cas` / `command_rate`。未知版本/代码与空位图区分，不据此推断当前训练值。
DDR4 XMP 另含[限定 CAS 编码、推荐 DIMM 数](spd-profile-geometry.md)，`cas.encoding` 标识位图定义，
`recommended_dimms_per_channel` 是头部推荐值或 null。基础字段支持对称堆叠容量、DDR5 1/2/4/8
子通道及 3DS 不同逻辑 rank 刷新下限；未知组织版本/保留编码不输出容量。
档案启用位不表示 BIOS 已套用；XMP/EXPO 值不代表当前训练值。现已支持 EXPO 增强时序存储字，
其启用语义与档案写入仍未实现。

AMD PM 遥测使用以下独立命令，不接受 `--cpu` 或 `--apply`：

```sh
octool-cli amd-pm-read > pm.json
octool-cli amd-pm-decode --file pm.json
octool-cli amd-pm-decode --file pm.bin --version 0x730204 --bclk-mhz 102.5
```

`amd-pm-read` 读取已存在的 `/sys/kernel/ryzen_smu_drv`，不安装或加载驱动；
读取可能触发该驱动向固件请求传输。未知版本/不符长度不会读表，读前读后元数据变化会丢弃样本。
此接口不保证原子样本、实际新鲜度或物理目标。`amd-pm-decode` 支持 GUI / CLI JSON 双向导入，
从原始字节重新解码；原始二进制必须指定版本。默认显示存储时钟，可选外部 BCLK 单独修正，
未测量 BCLK。详细单位、错误和 JSON 约定见[PM 实现说明](amd-pm-recovery.md)。
另含独立的 `original_limits` 数组：24 项[原版限制监控值](amd-pm-limits.md)，
保留名称、偏移和浮点存储值，单位标为 `unverified`，不作为 PBO 设置或 W/A/°C 读数。
另有 `original_package` 的 53 项[公共功耗与策略监控](amd-pm-package.md)，按五个 `group` 分组；单位、策略状态和目标仍未核实。
另有 `original_fabric` 的 27 项[链路与片内时钟监控](amd-pm-fabric.md)，按两个 `group` 分组；单位、实际链路/时钟状态和目标仍未核实。
另有 `original_arrays` 的 92 个 [GMI/DIMM/PCIe 数组存储值](amd-pm-arrays.md)，每项含 `slot`、`slot_count`；它们不是物理目标编号或已安装设备数量。
另有 `firmware_core_arrays` / `firmware_ccd_arrays`，提供[已核实边界的逐核 / CCD 存储字](amd-pm-core-ccd.md)；单位和物理映射未认证。GUI 的 Display 现有十四组；旧 schema-1 快照会重算派生字段，撤回两项 VDDG 解释。PM JSON 上限为 1 MiB / 65536 个值，包含 CLI 外层；其它格式保持原限制。

CPU 硬件命令必须明确指定 `--cpu`，从 `diagnose` 的 `data.allowed_cpus` 选择。
原始 MMIO/PCI 命令分别指定物理地址或 BDF，不使用 `--cpu`。
以下 `0` 仅为示例；受限容器、CPU 亲和性和多路机器未必允许 CPU 0。

```sh
octool-cli cpu --cpu 0
sudo octool-cli amd-pstates --cpu 0
sudo octool-cli intel-read --cpu 0
sudo octool-cli intel-oc-read --cpu 0 --domain core
sudo octool-cli intel-vf-read --cpu 0 --domain core
sudo octool-cli intel-vf-read --cpu 0 --domain cache --point 8
sudo octool-cli intel-turbo-read --cpu 0 --core-type p
sudo octool-cli intel-turbo-read --cpu 0 --core-type e
sudo octool-cli intel-uncore-read --cpu 0
sudo octool-cli amd-smu-probe --cpu 0 --profile shimada
sudo octool-cli amd-smu-read --cpu 0 --profile shimada --message 2
octool-cli amd-topology --cpu 0
sudo octool-cli amd-curve-read --cpu 0 --ccd 0 --core 0
sudo octool-cli amd-umc-read --cpu 0 --bank 0 --refresh-slot 0 > umc.json
```

曲线的 CCD/core 是手动确认的固件索引，不由 Linux CPU 号或 CPUID 拓扑推导。
UMC bank 是地址索引，不能直接当作内存通道/DIMM 标签。输出原始编码，不假设物理单位。
曲线及 UMC 查询会写入查询邮箱或地址索引，没有修改曲线或内存设置。
曲线读取新增 `--cores 0,2,7`；Shimada `1Ah/08h` 的 `amd-curve-set` 支持
单核或同一 CCD 内选核设置，使用 `--margin`、逐核 `--expect-raw` 与 `--apply`。
先核对旧值，再逐核设置与回读，失败时报告部分完成情况；完整用法见[曲线设置](amd-curve-settings.md)。
CLI 的 UMC JSON 使用下述统一命令格式。成功且完整的 `amd-umc-read` 报告可以直接在
GUI 的 AMD UMC → Open snapshot 中打开；GUI 重新解码原始寄存器，忽略报告中的预解码数值。
失败、不完整、重复寄存器、越界或未知格式会被拒绝，导入不访问硬件。

`amd-umc-decode --file SNAPSHOT` 也能离线读取 GUI 快照及 CLI 的读取/解码报告；
`amd-umc-diff --file BEFORE --compare AFTER` 比较同 bank/refresh slot 的两份快照。
两者均无需 root 或内核模块，支持部分 GUI 快照并保留缺失值。GUI 同步增加比较和
差异筛选。文件边界、状态语义和示例见 [UMC 离线快照与比较](umc-offline.md)。

`intel-vf-read` 与 GUI 的 Intel Controls → V/F points 共用[同一查询实现](intel-vf-recovery.md)。
仅开放 Raptor Lake-S family 6/model B7；`--point` 取 1..15，省略时逐个查询 15 个候选点，
这不表示硬件一定支持 15 点。输出每点的 `ratio`、`offset_mv`、`raw` 和固件返回状态。
`--cpu` 是执行查询的逻辑 CPU，不是逐核 VF 目标编号；不修改 VF 或 override。
固件拒绝的点数值为 `null`，仍继续查询后续点；传输失败/超时/取消则终止查询。
`scan_completed` 仅表示全部请求得到完成状态；存在任一点错误时 `ok:false`、退出码 3。
该 JSON 可保存作记录，目前没有 GUI 文件导入。单点设置使用下面独立的准备/提交入口。

## 已还原的设置入口

`intel-vf-read --cpu N --domain core|cache --point P --for-edit` 查询选定点及编辑条件，
与 GUI 的 **Prepare selected point** 相同。成功读取但不可编辑时仍返回配置及限制状态。
`intel-vf-set --cpu N --domain core|cache --point P --value MILLIVOLTS --apply` 修改一个点的 offset。
要求 B7、点 1..15、OC_LOCK 清除、该域 Adaptive 且全域目标/offset 为零；core 还要求 per-core override 关闭。
不会自动复位其它电压设置或切换 override。可编码范围 -1000..999.0234375 mV，按 1/1024 V 就近取整。
提交前完整比较准备上下文，提交后完整读回点与上下文；相同编码值不发设置命令，失败不重试。
JSON 的 `edit_context` / `after` 提供配置，`write_attempted` / `verified` / `unchanged` 区分结果，
`settings_firmware_status` 仅描述本次设置。倍率只读，配置读回不代表实际电压测量。
详见[单点偏移设置](intel-vf-write-recovery.md)。

`intel-turbo-set --cpu N --core-type p|e --group 0..7 --value RATIO --apply`：
修改 Raptor Lake-S B7 的一个睿频分组倍率（整数 1..85），保留活动核心数量阈值与其它组。
先检查可编程能力、锁、表内顺序和完整旧值，再一次写入并完整读回。
相同值不写，失败不重试；返回 `verified`、`unchanged`、`write_attempted`、`completed_writes` 和前后快照。
E 核表需要 CPUID 混合架构能力；分组编号不是物理核心编号，配置倍率不代表实测频率。
详见[睿频分组还原](intel-turbo-recovery.md)。

`intel-uncore-set --cpu N --minimum-ratio MIN --maximum-ratio MAX --apply`：
同时设置 B7/8F 的共享 Ring/LLC 最小、最大倍率，要求 `1 <= MIN <= MAX <= 127`。
保留其它位，重新检查身份与旧值，再完整读回；相同值不重复写入。
此功能不改变 OC mailbox，也不把倍率显示为实测频率。参见[Ring/LLC 说明](intel-uncore-recovery.md)。

`intel-set --cpu N --field FIELD --value VALUE --apply`：

| FIELD | VALUE |
|---|---|
| `pl1`, `pl2` | W，按实际 RAPL 单位编码 |
| `pl1-window`, `pl2-window` | 秒，按核心的窗口规则向下取可编码值 |
| `pl1-enable`, `pl2-enable`, `pl1-clamp`, `pl2-clamp` | 0 或 1 |
| `hwp-min`, `hwp-max`, `hwp-desired`, `hwp-epp` | 整数 0..255，额外检查实际硬件能力和字段关系 |
| `hwp-window-us` | 整数 0..1270000000 微秒，0 为自动；需要活动窗口能力和 desired=0，向下编码 |

`intel-oc-set --cpu N --domain core|cache --field offset-mv|max-ratio --value VALUE --apply`：
沿用 Raptor Lake-S family 6/model B7 配置的型号/锁/旧值检查，保留其它字段并回读。
offset 的可编码范围是 -1000..999.0234375 mV，最大 OC 倍频是整数 1..85；
这些是编码范围，不是某颗 CPU 的稳定参数建议。W790/W890 的 server 电压域尚不支持。
RAPL/HWP 与 Intel OC 均检查完整寄存器回读；`verified` 只表示回读一致，不代表稳定性验证。
RAPL/HWP 的 `readback_value` 是实际编码后的配置值，相同配置不重复写入。
HWP 配置属于硬件提示，不等于实测频率或功耗。

原始寄存器命令 `register-read` / `register-write` 已与 GUI 对齐，支持 MSR、MMIO、PCI；
完整参数、宽度和提交语义见[控制与寄存器说明](control-register-recovery.md)。

目标电压与模式使用同一命令的 `--field target-mv --value MILLIVOLTS --mode adaptive|override --apply`，
同时保留 `--cpu N --domain core|cache`。目标必须是整数 1..2000 mV，模式必须明确选择，
零值自动/默认语义未开放。该范围不是安全工作电压范围。
只修改选中的域，保留 offset 与倍率；Adaptive 配置 turbo 目标，Override 配置固定目标。
`before` / `after` 含取整后的 `target_mv` 和 `target_mode`（失败为 `null`）。
详见[目标电压与模式还原](intel-voltage-recovery.md)。

AMD 原始固件入口：

```text
octool-cli amd-smu-send --cpu N --profile shimada|phoenix|gpt \
  --message MESSAGE --arg0 VALUE [--arg1 VALUE ... --arg5 VALUE] --apply
```

只接受核心已有白名单，参数保持原始 32 位编码。`--arg0` 必填，其余默认 0。
`amd-smu-read` 只接受查询消息 1/2，参数全为 0。
Shimada 的已恢复调参消息在当前核心白名单内；Phoenix/GPT 仅开放查询。
固件返回成功仅表示命令获接收，`hardware_effect_measured:false`。
失败后不自动重试或回滚，结果中的 `write_attempted` / `message_attempted` 用于判断是否曾提交。
尚不提供 PStates 设置、其它 AMD 平台的高层曲线设置、完整 PBO 高层参数、Intel server VF/fabric 或板级专用写入。

PCI 目标选项 `--bus`/`--device`/`--function` 默认全为 0，域固定为 0；曲线查询固定 0000:00:00.0。
目标索引等无符号整数使用十进制或 `0x` 前缀十六进制；设置的 `--value` 使用十进制，
offset 等有符号字段可接受负数。禁止截断、重复/未知选项；目标电压和倍率须为整数值。
设置命令缺少 `--apply` 会在打开硬件前返回错误。不会通过交互提示阻塞批处理。

## 输出与自动化

除帮助和版本外，stdout 为一行 JSON，`--json` 可显式指定但不是必需。
统一格式为 `schema_version`, `command`, `ok`, `error`（负 errno）, `error_message`, `data`。
原始寄存器用十六进制字符串，避免 64 位数值被 JSON 客户端丢失精度。
失败字段使用 `null`；PStates/Intel/清单读取可能保留部分有效结果，必须同时检查顶层和逐项错误。
可读 SPD 不存在时列表为空；SPD CRC 未验证和验证失败会明确区分。

退出码：`0` 成功，`2` 命令或参数不合法，`3` 操作失败或部分读取失败。
主板属性被内核隐藏/拒绝访问时 `inventory` 可返回 3 并保留成功行。
诊断不会测试寄存器访问；`register_access_tested:false` 不能作为驱动可用性证明。
硬件命令接受 `--timeout-ms 1..120000`，默认 10000；超时为协作检查，不能强制中断阻塞的内核调用。

可用 shell 重复调用完成采样，或由任务调度器保存 JSON。没有自动应用启动参数、
远程监听端口或自带守护进程。硬件型号和实机验证限制与 GUI 相同。

## 验证范围

专用 `headless CLI` 工作流对 Ubuntu 20.04/22.04/24.04/26.04、Debian 11/12/13、Rocky 8/9/10
分别构建、执行核心与命令行测试、打包，并在干净容器安装/使用/卸载/重装。
容器不提供显示服务或宿主寄存器设备，运行阶段不装 Qt、编译器或 DKMS。
测试包含模拟设备的实际核心调用、设置保护、失败输出、JSON 精度、稀疏 CPU 亲和性、
SPD CRC 和 sysfs 清单。硬件参数测试使用模拟设备；不能替代四台目标机器的真机验收。
实际通过状态以对应提交的 CI 记录和本地产物清单为准。

## AMD PStates 电压/电流描述值

`amd-pstates --cpu N` 的 `rows[].electrical` 与 GUI Voltage / current descriptors 共用解码器。
仅对 17h/11h、18h、20h、A0h 和 19h/50h 开放；完整 VID、电压 mV、预期最大单核电流 A 及各自状态
放在独立对象中，原始 `vid_bits` 保持原义。不可用数值为 `null`；未读行整个对象为 `null`。
这些是 P-state/ACPI 描述值，`electrical_is_measurement` 始终为 false。
[公式、来源和状态说明](amd-pstates-electrical.md)。
