# 项目知识入口

更新日期：2026-10-08。接手时先读这里，再读根目录 `CHANGELOG.md`。

本轮在用户提供的 `octool-linux-refactor.tar.gz` 上继续扩展。作者确认旧 GUI 源码丢失后，
授权按 Windows/Linux 二进制证据重构，首批选择基础信息和 MSR/MMIO/PCI 原始读写。
实际 Qt5 工程现已位于 `gui/`，96 字节 ABI 和旧 MMIO 协议保留；新 GUI 使用已有 HAL。
公开 GitHub Actions 的十目标基础版矩阵已有全绿记录，包含 AMD PStates 只读页；
这不代表旧 GUI 直接替换与硬件验收完成。最新复查和修正结果见下列报告。

- [2026-10-08 全面复查](review-2026-10-08.md)：基线证据、边界修正、未解决问题及后续研究顺序。
- [PCI/EC 协调与线程权限](bus-coordination.md)：接续修复、公共内核API选择、离线负例和本轮验证。
- [模块能力识别](module-capabilities.md)：只读查询、未知驱动拒绝扩展、旧模块MMIO对拍和升级检查。
- [采集完整性与映射生命周期](capture-integrity.md)：失效映射、fd 复用、错误/并发拒绝、v2 文件提交及无硬件门禁。

- [多发行版构建、运行和打包](multi-distro.md)：十目标、构建入口、依赖方案、CI 门禁。
- [已验证安装包和源包](artifacts.md)：产物名称、每目标选择方式、下载和哈希校验。
- [GitHub 仓库与 Actions](github-actions.md)：当前测试路径、公开状态与计费边界。
- [Actions 实测修复记录](actions-debugging.md)：软件源、Kbuild 探测与 EL headers 的实际失败及处理。
- [GUI 恢复与重构](gui-recovery.md)：源码丢失后的作者授权、Windows 输入现状与恢复边界。
- [首批 GUI 重构](gui-phase1.md)：作者选择的基础信息和原始读写、传输修复与测试范围。
- [平台面板恢复](platform-recovery.md)：作者已选择继续；23 个原面板类的证据与待补硬件/字段规格。
- [AMD PStates 只读页](amd-pstates.md)：CPU/能力探测、配置频率、原始值及旧算法核对边界。
- [EL8 静态 Qt SDK](qt-sdk.md)：成功 run、产物哈希、依赖配置及复用边界。
- [原版 ELF 的 EL8–EL10 兼容性](legacy-el-compatibility.md)：继续反汇编的调用点证据和私有运行库实验，区别于重构基础版。
- [原版启动权限与失败路径](legacy-el-privilege-analysis.md)：NASM 直接 syscall/端口、模块握手、MSR 错误处理与对拍入口限制。
- [原版平台判断与 Controls 分派](legacy-platform-dispatch.md)：PCI计数、DMI/VRM分支、hwloc及550个原指令决策实验。
- [原 Qt 回调索引](legacy-qt-callbacks.md)：39类451个元方法、原跳表/虚表及607项有界入口核对。
- [原 MSR 失败传播](legacy-msr-failures.md)：两个完整槽到文件接口、栈缓冲复用、148项有界实验和407处忙循环。
- [原 NVL 控件连接](legacy-ui-connections.md)：157个对象名、67项文字赋值、GT/NPU链及XOC双连接证据。
- [原主板与时序菜单](legacy-menu-routing.md)：W790/W890、TRX50/WRX90、客户端与多窗口分派的592项实验。
- [逆向覆盖台账](reverse-engineering-status.md)：清单、算法、模拟和真机证据的区别与尚未完成范围。
- [原版模块重复加载握手](legacy-module-handoff.md)：同名 `.ko` 的真实 EEXIST 路径、runner 探针与 Secure Boot 边界。
- [原版 MMIO 邮箱契约](legacy-mailbox-contract.md)：56 组原指令模拟、三份旧模块映射、错误完成字导致的兼容缺口。
- [原版 MMIO 保护层实验](legacy-mailbox-guard.md)：不改原调用函数的错误退出方案、原机器码原生执行与跨 EL 构建验证。
- [云端接入状态](cloud-access.md)：用户要求 OpenAI 托管云端；当前工具/CLI 的实际核实结果。
- [Linux Docker 执行指南](cloud-build.md)：独立 runner、日志和失败判定。
- [真机验收清单](hardware-acceptance.md)：安装、MOK、insmod/modprobe、MMIO 对拍、GUI。
- [验证状态及输入证据](verification-status.md)：本轮实际做过什么、哪些未做。
- [原始重构指南](../port/docs/refactor-guide.md)和[原始对拍指南](../port/docs/parity-verification.md)：历史结论及工具设计。
- [第一阶段分析](../analysis/docs/linux-porting.md)：原二进制与内核兼容性分析，保留原始验证范围。

## 项目约束

1. 版本使用点号，源码包名为 `octool-x.y.z-src.tar.gz`。
2. 持久知识放源码树 `docs/`，开发日志放根目录 `CHANGELOG.md`。历史日志保留在原路径。
3. 旧 GUI 二进制/调用点和 MMIO 线级协议不改；新 GUI 复用 HAL，不按内核版本号选择 API。
4. 不确定的硬件字段、面板含义、单位、缩放系数必须询问作者，不能从相似值推断规则。
5. 需要作者选择时，在回复末尾使用带选项的编号列表。

## 当前输入与剩余范围

- 已读取 `E:/Download/Edge/` 的两个 ZIP：Windows 包有 158 项但没有工程源码/PDB；
  Linux 包仍只有二进制，SHA-256 与先前输入相同。清单、哈希和元对象证据见
  [输入分析记录](validation/reference-packages-20260930.json)。原来 F 盘不可读的问题已解除。
- `gui/octool.pro`、实际 C++ 窗口/访问实现与 Qt 回归测试已接入 `port/gui/build.json`。
  新文件沿用 HAL 的 GPLv2；没有替旧二进制或附带第三方程序重新授权。
- 四组 CPU 已由作者确认，W790 更正为 w5-2565X、18 核，BIOS 均未知。
  AMD PStates 首批只读频率/原始值已实现；其余平台字段与写入规则继续核对。
- 当前本地执行环境为 Windows；用户现在授权使用公开 GitHub 仓库的 Actions Linux runner。
  真实结果以仓库 run 和验证文档为准，写好 workflow 不等于已经通过。
- 用户要求使用 OpenAI/Codex 托管云端，不是提供自有服务器。Cloud CLI 可连接服务，
  当前改用用户明确指定的 GitHub Actions 路径，不再以 Codex Cloud 环境作为测试前提。
- 当前基础版只依赖 QtBase Widgets/Concurrent，平台插件显式限定 xcb/offscreen。
  EL8 SDK 中其他模块的存在，不表示 GUI 已使用或验收这些功能。

## 范围和未决事项

CI 的 EL 三项默认使用 Rocky 对应大版本镜像。Alma/RHEL 使用相同构建入口，
但 Rocky 结果不是 Alma/RHEL 实测证据。RHEL 还需要已订阅的软件源，见真机清单。
当前只支持 x86_64/amd64，未提出 ARM 或 32 位支持。

MOK 签名解决“内核是否接受模块”。旧 GUI 的 `iopl`、`/dev/mem`、MSR 直接访问仍可能被
lockdown 限制；旧二进制没有被修改。新 GUI 原始访问走 HAL，但签名模块实际加载、权限和
硬件行为必须在真机验证，不能由容器签名/可见窗口推断 Secure Boot 下超频全功能通过。
