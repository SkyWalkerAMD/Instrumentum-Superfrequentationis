# 项目知识入口


2026-10-10 平台恢复增量已接入，验证代码为 `0b514bb`（分支 `refactor/platform-recovery`）。
AMD PStates 增加完整 VID/Idd 原始字段；Intel Controls 增加 RAPL/HWP 读写和温度；
AMD 增加受限 BIOS SMUIO 与 CCD/core/MHz 命令准备；内存与主板页增加 DMI、hwmon、
驱动已暴露的 SPD、DDR4/DDR5 基础 CRC 和 SPD 时序解码。
**仍未完成原版全部功能**：PStates 设置/物理电压电流、Intel VF/逐核 turbo/fabric、
AMD 完整调参/拓扑与曲线、运行时内存时序及 PMIC/VRM/EC/板载时钟仍有缺口。
详细范围见[本轮功能表](platform-controls.md)，不能把测试通过当作四台目标机器的硬件验收。

- [Linux 完整验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068664) 23/23，通过十个发行版目标；每目标 21 Qt / 29 Python，11 套内核 VM 启动。
- [独立核心](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38005068831) 四环境全部通过，每环境 4 CTest / 26 场景组。
- [本轮证据与哈希](validation/platform-recovery-ci-0b514bb.json)；20 份 DEB/RPM 与源码包位于
  `dist/platform-recovery-0b514bb/`，参见[产物说明](artifacts.md)。

以下保留此前记录；最新功能和验证以本段及其链接为准。

2026-10-09 现有重构组件的十目标 Linux 兼容适配及自动化验收已完成，验证提交 `88999aa`，分支 `refactor/linux-integration`。
范围为现有重构版的基础信息、原始 MSR/MMIO/PCI、AMD PStates 只读页，以及其运行依赖、
启动器、独立授权辅助程序、polkit 与 DKMS 交付。原版其余调参面板仍未全部恢复。

- [完整云端验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37923477646)：23/23 成功；Ubuntu20.04/22.04/24.04/26.04、Debian11/12/13、Rocky8/9/10。
- 11 套真实发行版内核在 QEMU 中启动，模块加载、双 CPU 的 HAL/CPUID、设备权限、无效请求及卸载通过。
- EL8 基线与十目标各 17 项 Qt 回归、每目标 28 项 Python 回归通过，无跳过。
- 十目标在无编译器/头文件/DKMS 的环境安装 GUI，实际 pkexec 成功/拒绝/退出清理、无显示诊断、
  受限 CPU、X11/Xwayland 窗口通过；随后安装驱动并验证重装/卸载。核心四环境各 18 个场景组通过。
- [逐目标结果、源码与产物哈希](validation/linux-integration-ci-88999aa.json)；20 份 DEB/RPM、源包与 SHA256SUMS 已保存在
  `dist/linux-integration-88999aa/`，下载/安装方式见 [产物说明](artifacts.md)。

普通用户运行 `octool`，在基础页明确申请硬件授权；`octool --diagnose` 无需显示服务器。
真实主板寄存器操作、交互认证窗口、Secure Boot/MOK、跨版本升级仍按[真机清单](hardware-acceptance.md)验收。
Rocky 的结果不自动等同于 Alma/RHEL；Windows/macOS 目前仅验证核心。
EL8 容器以本地账户测试，移除只在系统启动后可用的 NSS systemd 提供者；变更仅在可丢弃
容器内，前后内容已归档，安装包不修改用户的 NSS。目录账户认证仍需目标环境验收。

以下保留此前研究与验证记录。

前次重构提交 `beb10b7` 已把 PStates、寄存器请求/校验/串行服务和 Linux 硬件适配分离。
独立核心四环境通过，Linux 完整矩阵 23/23；EL8 基线与十目标各 13 项 Qt 测试通过，
每目标 26 项 Python 回归无跳过，见[本次源码、测试及产物哈希](validation/refactor-hardware-ci-beb10b7.json)。
当前主线是先完成功能重构，再移植重构版；Windows/macOS 此时只验证核心，尚无硬件后端/完整 GUI。
该轮之后的系统信息、授权与 Linux 适配已由本文开头的新验证覆盖；多步硬件事务、其他平台面板和真机验收仍待继续。

以下保留此前研究与验证记录。

更新日期：2026-10-09。接手时先读这里，再读根目录 `CHANGELOG.md`。

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
- [原AMD初始化](legacy-amd-initialization.md)：FamilyType不可达比较、Shimada来源、三套SMU软件表及385项实验。
- [原AMD命令传输](legacy-amd-transport.md)：固定BDF、端口等待耗尽仍发送、原libpci失败值和448项实验。
- [原AMD MP1与操作槽](legacy-amd-mp1.md)：固定身份读取、21次轮询、局部标志和失败后Applied的563项实验。
- [原Intel NGU与MMIO目标](legacy-intel-ngu.md)：两完整槽、读加基址/写直接传参、busy耗尽及266项原指令实验。
- [原内存页和64位写路由](legacy-mmio-clients.md)：原槽请求零地址、NVL查询仍写命令、fallback丢高32位及168项实验。
- [原公共邮箱与FIVR/FCH](legacy-mmio-services.md)：耗尽返回0、一致性AND比较、位操作边界及260项原指令实验。
- [原I2C与MMIO直接调用覆盖](legacy-mmio-i2c.md)：119项、错误返回/部分输出/无界等待，以及10函数28处CALL逐点请求核对。
- [原NVL配置文件门禁](legacy-nvl-profile.md)：6386字节导出、无导入上限、短读仍到原MSR请求及42项实验。
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
