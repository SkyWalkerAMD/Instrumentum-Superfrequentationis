# 原生 PM 采集：整批验证

交付源码提交 `1ad6db9d6df0cd19b7e539e531ff6340c6513e64`。核心和 CLI 验证于 `65a370f`，Linux 验证于 `1ad6db9`；
两个提交的全部 291 个运行/构建路径逐文件一致。后者只增加原指令研究并修复历史文档链接。
GUI 与 CLI 共用受约束的 Shimada PM 表采集，
校验非零固件返回长度、完整物理地址范围和传输确认，读取后重新检查元数据。
失败不发布部分原始缓冲区；完整捕获中的异常浮点字段继续按已有规则隔离。

| 验证 | 结果 |
|---|---|
| [跨平台核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39570939273) | 4/4；Ubuntu 22/24、Windows 2022、macOS 14 各 31 CTest、429 场景组 |
| [CLI](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39570945930) | 13/13；11 个 Linux 目标各 33 CTest、66 CLI 场景组、1386 JSON 报告 |
| [GUI / 驱动](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/39573918691) | 26/26；每目标 216 Qt 测试，EL7/EL8 baseline 同样通过；134 分析测试、34 移植测试 |
| 运行与安装 | 各目标无显示、无编译器、无 Qt CLI 安装/重装及非 root diagnose；GUI 显示启动验证 |
| 内核 | 12 次 QEMU 启动，模块加载/卸载、CPUID、权限与能力检查；没有实际寄存器调参或 Secure Boot 验证 |
| 本机用户源码 | 50 项 port 测试，37 通过、13 因 Windows 环境缺少工具链或 Linux 跳过 |
| REA 与后续回放 | 10 个新 REA 函数、1433 个传输/VID 实验；沿用前次 REA 补跑 12 个校准实验；3 项新增回归本机与云端均通过，已计入上述 134 项 |

目标为 `el7, el8, el9, el10, ubuntu20.04, ubuntu22.04, ubuntu24.04, ubuntu26.04, debian11, debian12, debian13`，均为 x86_64。
EL7 对应 Oracle Linux 7.9。Windows/macOS 本批验证可移植核心，不表示完整应用适配。
云端 ASan/UBSan 覆盖既有五个离线解析测试，不声称覆盖原生设备路径。

首次提交 `c4963a0` 的 baseline 有一项旧截图高度检查失败：新增控件挤占了原窗口空间。
本次只增加该截图测试窗口高度，保留全部时钟行可见断言。
`65a370f` 的核心/CLI 通过，Linux 在历史文档索引链接检查失败；修复后重跑 Linux 完整矩阵。
后续源码核对还修复了 Windows CRLF / Git LF 导致的新分析夹具哈希不一致；提取器改为生成固定 LF 字节。
旧任务与失败原因保留在机器记录，未计作本次成功结果。
10 月 21 日首轮 Linux 另有 EL8 安装任务遭遇 EPEL 镜像元数据 404；10 月 26 日只重试失败任务及汇总，保留其余已通过结果。未修改已验证源码。

本批共生成 1364 张截图，人工检查其中 Ubuntu 24.04、EL7 的原生采集成功与复查失败四张。
GUI/CLI 验证使用合成设备；没有向宿主或真实 CPU 写入设置，也未证明固件实际实现返回长度。
[研究记录](amd-pm-native-research.md)解释原版主动写完成标志、VID 路由分歧及校准仍未修复的逐核/CCD 冲突。

交付在 `dist/gui-cli-amd-pm-native-1ad6db9/`：33 个 GUI/CLI/DKMS 包，
根目录源码包对应精确 Linux 云端提交；`updated-source/` 保留用户八项扩展及后续验证文档。
291 个运行/构建路径与云端源码核对，八项历史用户扩展单列保留；源码逐文件比对并生成 SHA256 清单。
包版本仍为 2.0.1，同版本更新时采用对应包管理器的重装操作。

逐核/CCD 完整 PM 布局、物理映射和 rails 写入仍未完成；已有冲突没有因为成功捕获而消失。
动态内存/EC、其它 AMD 平台和剩余板级功能也未声称完成。
