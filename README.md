# Instrumentum Superfrequentationis

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
