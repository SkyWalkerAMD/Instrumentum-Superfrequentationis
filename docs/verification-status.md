# 验证状态与输入证据

<!-- umc-recovery-728671d -->
2026-10-10：新增 AMD UMC 212 字段 / 12 分组、显式地址组和刷新槽、离线 JSON 快照，
以及 AMD 扩展 CPUID 拓扑读取；验证源码为 `728671d`。
原构造的通道搜索和同名字段问题已从原指令复现，重构版不会沿用错误地址或以名称覆盖字段。
功能说明见 [UMC](amd-umc-recovery.md)、[CPU 拓扑 / PStates 剩余问题](amd-topology-recovery.md)。

- [Linux 验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38007763354) 23/23；十目标各 24 Qt / 30 Python，11 套目标内核 VM 启动。
- [核心验证](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/38007763396) 四环境通过，各 5 CTest / 35 场景组。
- 20 份 DEB/RPM、源码包及 SHA256SUMS 已保存于 `dist/umc-recovery-728671d/`；[逐项报告](validation/umc-recovery-ci-728671d.json)包含 180 张截图及产物哈希。

PStates 写入、Intel 电压 / VF、AMD 完整调参和主板专用写入仍未完成；
UMC 当前显示原始编码，没有把型号未确认的字段换算为周期。四台目标机器尚无真机验收。

以下保留此前记录；本次新增功能与验证以以上链接为准。


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

更新：2026-10-08。源码、构建配置、容器测试和真实硬件验收分别记录，不能互相替代。

## 当前结果

离线平台/UI/AMD初始化研究在`fa4c541 / portability37766384139`完整23/23成功，
十目标模块、重构GUI、包安装/窗口及总门禁通过，独立模块探针37766384207成功。
Linux23项分析测试无跳过，2501项原指令实验与Windows除environment全部一致，
见[逐项证据与哈希](validation/legacy-initialization-ci-fa4c541.json)。
后续[AMD命令传输448项](legacy-amd-transport.md)已在本地验证并接入下一轮门禁，不混入上述run。
这批改动只扩展分析工具、CI与文档；未改GUI/HAL/模块/96字节ABI，也未完成原版全部功能。

[采集完整性修正](capture-integrity.md)已在`3920018 / portability37749651590`完整23/23通过：
每目标46项采集、21项Python无跳过，十份采集报告逐字节一致；每目标QtTest12项及发行/native窗口通过。
原GUI/HAL/kmod/96字节设备ABI未改；新增的是对拍文件v2、生命周期失效和错误拒绝。
`probe37749651635`元数据/重复加载/卸载也成功。20个包与11套模块已下载，
见[测试证据](validation/capture-integrity-3920018.json)和[交付哈希](validation/deliverables-3920018.json)。
旧GUI主窗口、错误恢复、live MMIO与MOK仍未验收；完整边界在上述报告中列明。

接续修正`60b6975 / portability37740064106`已23/23全绿，PCI/EC核心接口、线程权限及新增负例
在十目标实际执行，真实加载/重复加载/卸载探针也通过。见[系统协调与证据](bus-coordination.md)。
新增[模块能力查询](module-capabilities.md)在`f2142e4 / portability37741335600`单独23/23全绿，
`probe37741335727`真实元数据查询、错误码及卸载成功。全部20个安装包和11套模块产物已本地保存，
逐目标包/模块/截图哈希见[交付记录](validation/deliverables-f2142e4.json)。原GUI/真机范围仍未验收。

作者要求先复查再继续研究。[2026-10-08 全面复查](review-2026-10-08.md)核实基线
`30423b7` 的完整矩阵和探针确已通过，并记录新的 HAL、MMIO 映射、对拍门禁修正及未解决项。
这些修正的验证状态以该报告最后一节为准，不能自动继承以下历史绿色结果。
修改后 `a9ffbcd / 37737505060` 的23项完整矩阵及 `37737505020` 加强探针均已成功，
详见[本轮逐项记录](validation/review-fixes-a9ffbcd.json)。19项Python用例在kernel阶段全部执行，
新增失败令牌/完成字、PCI/MMIO边界和对拍自比较门禁已验证；当时的PCI/EC协调问题已由上述后续修正接续处理，
原GUI与真机缺口仍保留。

2026-10-08 后续邮箱研究发现：原版八个 MMIO 函数都等待完整 64 位 done=1，当前模块高 32 位
携带 errno 的错误应答会使原版持续等待。56 组有界原指令模拟已在 Windows 重现各分支，
三份旧模块双重映射按重定位/DWARF 核对为同页。见[邮箱契约报告](legacy-mailbox-contract.md)。
这是已记录、尚未修复的兼容缺口；此前成功路径/构建门禁不代表失败路径已兼容。

作者已明确允许公开反汇编证据和模拟结果。公开研究提交 `d0c6913` 的
[完整回归 37724109109](validation/actions-run-37724109109.json) 23 项成功；
随后改为只读公开函数样本的 `0e11ed3 / 37724841275` 云端模拟 56 例成功，
[原始结果与比较](validation/legacy-mailbox-run-37724841275.json)已下载归档。
两份 Linux 结果与两种 Windows 提取模式全部观测一致；56 例也已接入 push/PR 自动门禁。
同一 `0e11ed3` 的 [portability37724823205](validation/actions-run-37724823205.json)
最终 23/23 成功，新门禁和十目标构建、GUI、包安装、窗口均实际通过；23 份 artifact 元数据已归档。

2026-10-08 继续研究原版：`268ac92 / portability 37716682302` 的 23 项正式回归全绿，
包括新增汇编标签/对象引用 ELF fixture，见[本次完整记录](validation/actions-run-37716682302.json)。
独立原版诊断 `37716693694` 在 EL8/9/10 实际采到直接 iopl/setuid/setgid 的拒绝返回，
仍只出现 Not supported 对话框，主窗口门禁继续失败；
详见[启动权限与模块握手分析](legacy-el-privilege-analysis.md)。本轮未改 GUI、ABI/HAL、模块或生产包代码。

后续 `0553574 / kmod probe 37732862984` 在 GitHub Ubuntu 24.04.5 runner 的 Linux
`6.17.0-1022-azure` 内核实载项目 `octool_hwio.ko`。内核创建 `/dev/mydev` 后，用同文件字节再次
调用 `init_module` 返回 `-1/EEXIST`，job 成功；模块未签名且没有硬件访问。它验证真实内核重复加载，
不验证 EL vendor kernel、Secure Boot/MOK、旧 GUI 启动或真机 mailbox。见
[探针证据](legacy-module-handoff.md)和[机器记录](validation/legacy-kmod-eexist-run-37732862984.json)。
更正首步 syscall 标签后的[复跑 37733259162](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37733259162)
仍通过，归档的机器记录同时保留两次运行 ID、artifact digest 与观测结果。
随后文档提交 `0f01f2e` 的[探针复测 37733967112](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37733967112)
也成功，仍为同一 runner kernel/module SHA，第二次 `init_module` 精确返回 EEXIST；本轮 artifact digest
和机器字段归档在[独立 JSON](validation/legacy-kmod-eexist-run-37733967112.json)。
同一提交的[完整回归 37733967175](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37733967175)
23/23 job 成功，包含十目标内核、GUI、包安装及窗口冒烟、EL8 基线、离线自测和反汇编/mailbox 门禁。
该提交只有文档与验证记录改动；逐 job 状态和 23 份 artifact 元数据见
[actions-run-37733967175.json](validation/actions-run-37733967175.json)。云端包安装/窗口冒烟不等于作者真机的
模块加载、MOK、硬件寄存器或旧 ELF 全功能验收。

原 GUI 源码丢失后，作者已授权重构，并先选择基础信息与 MSR/MMIO/PCI，随后提供四套平台截图。
可编译的真实 Qt5 GUI 已接入。作者进一步选择先恢复 AMD PStates 的只读频率和完整原始值。

已归档的完整验收为 [run 36670288030](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36670288030)
（c48a38f8d75367c22c9463f680a532d6f5b41426），23 个 job 全部成功，包含总门禁及源码包产出。
十个目标均通过模块、原生 GUI、Qt 回归、GUI+DKMS 包安装/重装和两个真实窗口检查。
EL10 改用独立 Xlib 检查器后通过 Mutter/Xwayland；新增 PStates 页在每个目标的 QtTest 中通过。
详细记录见 [actions-run-36670288030.json](validation/actions-run-36670288030.json)。
后续文档提交仍运行同一矩阵，最新状态可从仓库 Actions 查看；本页保留这一完整验收的精确源码版本。

原版二进制的独立 EL 诊断见 [legacy-el-compatibility.md](legacy-el-compatibility.md)：
配套私有运行库已在 EL8/9/10 越过装载并显示真实 Qt 对话框，但旧硬件检查显示 Not supported，
其主窗口门禁仍失败。此结果不替换上面的重构基础版全绿记录。新打包另增加 GNU_STACK 非执行门禁。
该新增门禁已经在 [446ff37 / run 36676590930](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36676590930)
的 23 个 job 全绿矩阵中验证，见[本轮记录](validation/actions-run-36676590930.json)。

## 验证范围

| 层次 | 已取得的证据 | 尚不能推出的结论 |
|---|---|---|
| EL8 baseline | 静态 Qt5.15.18、实际 GUI/Qt 回归编译、ELF ABI 检查通过 | 原 GUI 全部面板恢复 |
| 十个 kernel job | HAL、loopback/transport/parity selftest、18 项 Python 测试、Kbuild、签名试验、DKMS 安装/同版本重装/卸载再装 | 真正加载模块、真实 MMIO 等价 |
| 十个 desktop job | 各发行版原生 GUI 编译/Qt 回归、GUI+DKMS rpm/deb、新容器安装、发行和 native 两个真实窗口 | 真机寄存器操作有效 |
| Ubuntu26 GUI 视觉检查 | 下载并查看四页真实 Xwayland 截图，表单未裁切 | 真实硬件功能/电压/超频有效 |
| 新 AMD PStates | 十目标均通过新增五项回归；连同既有测试及 init/cleanup，每个目标共 12 项结果全部通过 | 9995WX 实机已验收 |
| MOK/live 对拍 | 有可执行脚本、签名辅助与详细清单 | 固件登记、模块加载、真实读写尚未运行 |

当前 GUI ELF 最高需求 GLIBC_2.28、GLIBCXX_3.4.15、CXXABI_1.3.9；没有动态 Qt/ICU/libjpeg/Wayland
客户端依赖。2026-09-30 已归档的 EL8 Qt SDK tar SHA-256：
`6afaa07b5716c00c92ece61ef42eae007b18e57e2d650f6b450ddfa52afc8d7c`。
SDK 仅按匹配构建配方与 SHA 复用，GUI 每次重编。具体依赖策略见 [多发行版指南](multi-distro.md)。

## 十目标状态

以下内核 release 由已归档的成功 kernel 矩阵确认；每轮的包/镜像细节以对应 run artifact 为准。
GUI/装包列是 c48a38f 的结果，不将此前模块单独成功冒充 GUI 成功。

| 目标 | 已实际编译的内核 release | 原生 GUI 编译 | GUI+DKMS 安装/窗口 |
|---|---|---|---|
| EL8（Rocky） | 4.18.0-553.168.1.el8_10.x86_64 | 通过 | 通过 |
| EL9（Rocky） | 5.14.0-687.52.1.el9_8.x86_64 | 通过 | 通过 |
| EL10（Rocky） | 6.12.0-211.60.1.el10_2.x86_64 | 通过 | Xwayland 通过 |
| Ubuntu20.04 | 5.4.0-216-generic | 通过 | 通过 |
| Ubuntu22.04 | 5.15.0-194-generic、6.8.0-138-generic | 通过 | 通过 |
| Ubuntu24.04 | 6.8.0-142-generic | 通过 | 通过 |
| Ubuntu26.04 | 7.0.0-34-generic | 通过 | Xwayland 通过 |
| Debian11 | 5.10.0-46-amd64 | 通过 | 通过 |
| Debian12 | 6.1.0-53-amd64 | 通过 | 通过 |
| Debian13 | 6.12.111+deb13-amd64 | 通过 | 通过 |

Rocky 结果不是 Alma/RHEL 的单独实测记录。RHEL 需要已订阅的软件源；三者共用 EL 构建入口，
分别验收要求见 [真机清单](hardware-acceptance.md)。容器使用目标内核头文件实编，但实际进程仍
运行在 GitHub runner 宿主内核上，不能把 GUI Information 页的宿主内核当作已启动目标内核。

## 输入包与已确认硬件

| 输入 | SHA-256 | 内容/检查方式 |
|---|---|---|
| octool-linux-refactor.tar.gz | `43c8639802ddec1aae2d68eaae16b54655ddbb2738af6849ed3329fe29e342ac` | 原 port/ 与 analysis/，沿其扩展 |
| octool-linux.zip | `02fd2ae0534c86aed23cb5470fb2520b0772f4421c6ed1c966665d171313b53b` | 原 ELF、旧模块和运行库；静态解析 |
| OCTool0528.zip | `02a50088a94000f651f8783b32bf1ff63a48fd4e6a3909983c7c1ed0a5a417a3` | 158 项；无 C++/qmake/UI/PDB；PE 静态解析 |

两个 ZIP 已从作者新提供的 E:/Download/Edge 路径读取，不再处于“文件找不到”状态。
没有执行旧安装脚本、Windows EXE、驱动或 BIOS。完整清单见
[reference-packages-20260930.json](validation/reference-packages-20260930.json)。

17 张参考图已逐张查看并原样保存。四组硬件由作者文字确认：i9-14900KS、
w5-2565X（18 核，已更正笔误）、Xeon 658X、Threadripper PRO 9995WX；BIOS 未知。
见 [平台记录](platform-recovery.md)及[只读 PStates 依据](amd-pstates.md)。

## 协议与本地检查

- canonical ABI 与输入包相同：`5f07bfefd2a96519756cad1567dfdf4b0c878db142f670e1b208cf39523800d8`。
- HAL 头未变：`d80a19e0e0551ba162e55653bde7975c50b74a1e811a08f6776071af43762dc1`。
- HAL C 修复 CPU user_id 被令牌覆盖和短传输误报，新哈希
  `ba115e258a3d2d346902a9da70c8139424fb5c603dd4a6c9f76a7d38f2ef6ea9`；不再声称与输入包相同。
  MMIO 命令和全部字段不变。详细影响见 [GUI 接入说明](gui-phase1.md)。
- Windows 本地：Python 16 项通过、1 项依赖 Linux parity 二进制的测试跳过；Linux job 跑完整 17 项。
  文档相对链接、Python3.8 语法、源归档一致性/命名/执行位/排除私钥和产物、Bash 语法已检查。
- 旧 GUI 实际 ELF 需要 GLIBC2.35、GLIBCXX3.4.29、ICU70、libjpeg.so.8，按门禁拒绝作为 EL8 发行物；
  [旧 ABI 报告](validation/original-binary-abi.json)保留这一历史诊断。

## 历史证据和交接

输入包原有 11 内核、glibc2.31–2.43 等记录保留在 [原开发日志](../port/CHANGELOG.md)，
不重新推导或冒充本轮实测。[第六轮模块证据](validation/actions-run-36660297759.json)记录全部
DKMS 事务通过；[初次 SDK 记录](qt-sdk.md)保留旧归档哈希，不与当前 SDK 混用。
[Actions 修复日志](actions-debugging.md)说明每个失败、修复及验证范围。

新 GUI 仍是分阶段恢复。完整 Intel/AMD 控制面板、MOK 实际登记加载、真实寄存器操作与旧模块
live 对拍都须继续按 [真机清单](hardware-acceptance.md)留证。没有相应证据就标未完成。
