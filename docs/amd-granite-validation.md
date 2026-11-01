# Granite Ridge / 9950X3D 整批验证

2026-11-01，本轮还原固件指数表、幂插值与模型辅助计算，并串接核心 0、CCD 0/1 的调用参数及输出路径。CCD 2 新增命令量化、配置选择和相同代码保留旧值的证据。GUI 提示和 CLI JSON 使用同一份来源说明。
**八行剩余字段的物理单位仍未确认；本轮没有新增已验证单位。** 校准系数仍为合成输入，倒数结果为显式测试输入；这些回放不等同于目标真机验收。

目标为 9950X3D + ASUS X870E Creator（按 ProArt X870E-CREATOR WIFI 匹配）。机器不在手边，实际 BIOS 未知；ASUS 2503 / SMU 98.84.0 是研究参考，未进行目标真机验收。

| 验证 | 实际结果 |
|---|---|
| [可移植核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41269059947) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 35 CTest、480 场景组；6 个 ASan/UBSan 套件 |
| [无 GUI CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41269065220) | 13/13；11 Linux 目标各 37 CTest、78 CLI 场景组、1551 JSON 报告；安装、卸载、重装、无 Qt/显示运行 |
| [GUI 与内核](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41269069944) | 26/26；各目标及 EL7/EL8 基线 238 Qt 测试；142 分析测试、34 移植测试；12 次 QEMU 内核启动 |
| [REA 固件研究](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/41269075306) | 1/1；REA 6.7.0 实际分析两个 UEFI 模块；SMU/MP5 各 12240 项编码自检 |
| 本轮指数/幂内核 | 1453 组指数与 136 组幂函数；41 行固件表、半整数边界及超出表索引上限；8 组零指数返回原底数 |
| 本轮模型输入与组合 | 162 组简单模型、162 组混合模型、24 组幂/温度辅助计算；288 组调用组合检查 864 个字段及两项旁路状态 |
| 本轮 CCD 配置量化 | 80 组命令、320 组选择与相同代码保留；47 次结果取低字节；未据此开放硬件写入 |
| 已有传输与积分 | 2048 组描述符准备、8 组提交/24 次合成 MMIO、32 组模型汇总、128 组积分；未执行真实 DMA |
| 核心 0、CCD 0/1 模型尾部 | 216 组参数组合，另 3 组 CCD 禁用保留旧值；模型辅助函数结果为显式输入，系数单位未确认 |
| CCD 2 配置路径 | 全部 256 个初始字节；64 组更新门控，含 16 组降低和 32 组相同代码保留旧值；不是传感器回读 |
| 核心 4/12 计数率 | 72 组，含 24 组回绕；3 组时间差、6 组门控清零、1 组溢出保留；两行分别有/无最终 0.001 缩放 |
| CCD 6/8 与平均导出 | 288 组计数和共享参数组合；9 组一/双槽导出、261 个输出字；CCD 8 先换算再累加，不能改成分别平均后相乘 |
| 本机扩展检查 | 用户源码树 50 项：37 通过、13 因工具链/平台限制跳过；保留八项既有扩展 |

**44/44 云端任务通过**，四条工作流均基于 `a5ff035a9434c46377f69937670fedca3f157e2b`。
Ubuntu 26.04 首次在安装环境准备阶段遇到 Ubuntu 软件源索引 404/大小不一致；编译及 238 项 GUI 测试已通过。保留失败日志，仅重跑受影响任务及其依赖的最终汇总，代码未改；最终安装验证通过。重试记录见验证 JSON 的 `infrastructure_retries`。
21 个研究源文件与 REA 通过版本一致；全部 14 组回放的云端 JSON 与本机结果逐项相同。
最终 main 发布提交只补充文档和证据，应用与构建输入保持通过版本。
任务、源文件哈希、产物摘要与限制见[验证 JSON](validation/amd-granite-ci-a5ff035.json)。

11 个 x86_64 目标：`el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`。EL7 指 Oracle Linux 7.9；Windows/macOS 只验证核心。
生成 33 个安装包，每目标 147 张截图，共 1617 张。
直接检查 EL7 与 Ubuntu 24.04 的核心模型和 CCD 模型/配置镜像视图，共四张合成截图；视口之外字段由 Qt 断言验证。
QEMU 验证驱动集成，未验证物理寄存器调参或 Secure Boot。

当前有 **13 行核心、6 行 CCD** 的物理/无量纲单位证据，另有 **2 行核心、1 行 CCD** 的代码表示证据。
公共区有 12 个 SVI 遥测字段、3 个命令镜像及 7 个未解释候选字段。
核心 **0/4/12**、CCD **0/1/2/6/8** 的计算路径有了进一步证据，绝对时基、模型系数或配置值物理单位仍未知。
代码表示、计算来源与物理单位是不同层次的证据；字段新鲜度和导入固件来源仍未认证。

物理 CCD/OS 核心绑定、第二 rails 的物理身份与独立设置写入回读、其他未还原调参及其他固件适配仍未完成。
现有通道覆盖路径与软件镜像不能替代真机读回，本轮未增加硬件写入。

交付目录 `dist/gui-cli-amd-granite-a5ff035/` 含 33 个安装包；根源码包对应精确云端验证提交，`updated-source/` 包含用户扩展与最终文档。
`MANIFEST.json` 区分来源，`SHA256SUMS` 覆盖全部交付文件。

[本轮模型输入证据](amd-granite-model-inputs.md)、[传输和地址证据](amd-granite-transfer-paths.md)、[八行来源证据](amd-granite-producer-paths.md)、[已有单位证据](amd-granite-clock-models.md)、[REA 证据](validation/amd-granite-research.json)。
