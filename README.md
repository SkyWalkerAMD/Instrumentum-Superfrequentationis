# Instrumentum Superfrequentationis

OCTool 多发行版移植。GitHub 仓库名为 `Instrumentum-Superfrequentationis`，
程序、模块、协议及 `octool-x.y.z-src.tar.gz` 的命名继续沿用 octool。

在用户已有 port/ 重构上扩展 EL8/9/10、Ubuntu20.04/22.04/24.04/26.04、Debian11/12/13。

**十个发行版目标的模块实编、GUI 编译、离线回归、rpm/deb 安装和窗口启动全部通过。
[GitHub Actions run 36670288030](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36670288030)
的 23 个 job（含总门禁）全绿；EL10 与 Ubuntu26.04 使用 Mutter/Xwayland。
每个目标 QtTest 报告 12 项通过。**

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
进入支持 Docker 的 Linux 环境后，使用 `python3 port/ci/run-matrix.py` 执行全矩阵。
也保留 `.github/workflows/portability.yml`；两者调用同一容器内入口。
缺失输入会报错，不用预编译 Ubuntu GUI 或示例窗口替代。
