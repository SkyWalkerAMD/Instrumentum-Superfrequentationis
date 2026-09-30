# Instrumentum Superfrequentationis

OCTool 多发行版移植。GitHub 仓库名为 `Instrumentum-Superfrequentationis`，
程序、模块、协议及 `octool-x.y.z-src.tar.gz` 的命名继续沿用 octool。

在用户已有 port/ 重构上扩展 EL8/9/10、Ubuntu20.04/22.04/24.04/26.04、Debian11/12/13。

**十个目标的模块实编、离线自测和 DKMS 包安装/重装已通过真实 GitHub Actions。
GUI 源码待恢复或重构，完整移植验证尚未完成，总门禁保持失败。**

从 [docs/README.md](docs/README.md) 和 [CHANGELOG.md](CHANGELOG.md) 开始。

- [构建、运行与打包](docs/multi-distro.md)
- [GitHub 仓库与 Actions 测试](docs/github-actions.md)
- [OpenAI 托管云端接入状态](docs/cloud-access.md)
- [Linux Docker 执行矩阵](docs/cloud-build.md)
- [实际验证状态](docs/verification-status.md)
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
