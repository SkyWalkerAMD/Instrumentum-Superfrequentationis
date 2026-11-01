# Granite Ridge / 9950X3D 整批验证

2026-11-01，本轮 GUI/CLI 共用解码新增四行单位：核心第 3 行 GHz 分频模型、CCD 第 5 行 GHz 频率模型、第 9 行计数百分比、第 10 行 GHz 熔丝频率策略。
频率模型和策略值不是实测时钟或独立设置回读。禁用槽位、零分母及其他条件可能保留旧值；百分比不强制限制在 0..100。

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。机器不在手边，实际 BIOS 未知；ASUS 2503 / SMU 98.84.0 是研究参考。未进行目标真机验收。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41259180292) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 35 CTest、479 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41259188914) | 13/13；11 Linux 目标各 37 CTest、78 CLI 场景组、1551 JSON 报告；安装、卸载、重装、无 Qt/显示运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41259196277) | 26/26；各目标及 EL7/EL8 基线 238 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [REA 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41259203888) | 1/1；REA 6.7.0 实际分析两个 UEFI 模块；SMU/MP5 各 12240 项编码自检 |
| 新增分频模型回放 | 64 个策略选择、384 组分频模型；768 个活动槽位及 2304 个停用槽位保留旧值；分频 1..4 与插值码 0/63 边界 |
| 新增 CCD 百分比回放 | 300 组，含 102 组回绕、30 组零分母保留；另有 8 组启用/窗口门控 |
| 新增 CCD 策略和平均导出 | 32 组初始记录、160 组替换记录/熔丝输入；9 组一/双槽平均导出、99 个新单位输出字 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**44/44 云端任务通过**，四条工作流均基于 `d87c0aba0e3804b8f561f7ddcf348924da7cc0be`。
18 个研究源文件与 REA 通过版本一致；新增回放的云端 JSON 与本机结果逐项相同。
最终 main 发布提交只补充文档和证据，应用与构建输入保持通过版本。
任务、源文件哈希、产物摘要与限制见[验证 JSON](validation/amd-granite-ci-d87c0ab.json)。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。EL7 指 Oracle Linux 7.9；Windows/macOS 只验证核心。
生成 33 个安装包，每目标 146 张截图，共 1606 张。
直接检查 EL7 与 Ubuntu 24.04 的核心模型及 CCD 模型/计数器/策略视图，共四张合成截图；视口之外字段由 Qt 断言验证。
QEMU 验证驱动集成，未验证物理寄存器调参或 Secure Boot。

当前有 **13 行核心、6 行 CCD** 的物理/无量纲单位证据，另有 **2 行核心、1 行 CCD** 的代码表示证据。
公共区有 12 个 SVI 遥测字段、3 个命令镜像及 7 个未解释候选字段。
代码表示不等于已识别物理指标；字段新鲜度和导入固件来源仍未认证。

仍未知的数组单位：核心 **0/4/12**，CCD **0/1/2/6/8**。
物理 CCD/OS 核心绑定、第二 rails 的物理身份与独立设置写入回读、其他未还原调参及其他固件适配仍未完成。
现有通道覆盖路径与软件镜像不能替代真机读回，本轮未增加硬件写入。

交付目录 `dist/gui-cli-amd-granite-d87c0ab/` 含 33 个安装包；根源码包对应精确云端验证提交，`updated-source/` 包含用户扩展与最终文档。
`MANIFEST.json` 区分来源，`SHA256SUMS` 覆盖全部交付文件。

[新增单位证据](amd-granite-clock-models.md)、[请求策略与 CPU 目录](amd-granite-performance-policy.md)、[REA 证据](validation/amd-granite-research.json)。
