# Instrumentum Superfrequentationis

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
