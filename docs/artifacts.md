# 安装包与源码归档

更新：2026-10-08。当前功能范围为基础信息、原始 MSR/MMIO/PCI、AMD PStates 只读频率/原始值。
原 OCTool 的全部 Intel/AMD 调参面板尚未恢复，实机验收见 [清单](hardware-acceptance.md)。

## 完整通过的构建

后续研究代码`fa4c541`已完成[run37766384139](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37766384139)
23/23验证；原指令2501项、GUI/包与模块范围详见[证据](validation/legacy-initialization-ci-fa4c541.json)。
该轮包在对应Actions artifact中，未把其文件哈希混用于下述已保存的3920018包。

已完整下载保存的安装包来自[3920018 / run37749651590](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37749651590)，
23/23成功，包含采集生命周期/v2完整提交、每目标46项合成采集和21项Python，以及原有GUI/模块门禁。
十目标20份安装包保存到`dist/packages-3920018/<目标>/`，附`SHA256SUMS`；名称仍沿用2.0.1。
[最新文件哈希记录](validation/deliverables-3920018.json)包含11套kernel release、包SHA与100张截图SHA；
[Actions记录](validation/capture-integrity-3920018.json)另存artifact ZIP digest，二者不混用。
原始下载目录为`build/actions/37749651590/`。本轮未改GUI/HAL/kmod生产代码，GUI包内的docs会更新。

之前[f2142e4 / run37741335600](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37741335600)
的PCI/EC、线程权限、模块能力查询结果及`dist/packages-f2142e4/`仍保留，
见[先前交付记录](validation/deliverables-f2142e4.json)，勿混淆不同构建的文件哈希。

以下是历史构建，不能用它的旧包验证新能力查询：
[run 36670288030](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36670288030)
（c48a38f）有 23 个 job 全部成功。十目标各生成一个 GUI 包与一个 DKMS 包，共 20 份。
[完整证据](validation/actions-run-36670288030.json)记录包名、SHA-256、大小、镜像 digest、
11 个 kernel release、日志与截图哈希。文件哈希与 GitHub artifact ZIP digest 是不同对象，不混用。

| 目标目录 | GUI 包 | 模块包 |
|---|---|---|
| el8 | octool-2.0.1-1.el8.x86_64.rpm | octool-hwio-dkms-2.0.1-1.el8.noarch.rpm |
| el9 | octool-2.0.1-1.el9.x86_64.rpm | octool-hwio-dkms-2.0.1-1.el9.noarch.rpm |
| el10 | octool-2.0.1-1.el10.x86_64.rpm | octool-hwio-dkms-2.0.1-1.el10.noarch.rpm |
| ubuntu20.04 / ubuntu22.04 / ubuntu24.04 / ubuntu26.04 | octool-2.0.1-1.amd64.deb | octool-hwio-dkms-2.0.1-1.amd64.deb |
| debian11 / debian12 / debian13 | octool-2.0.1-1.amd64.deb | octool-hwio-dkms-2.0.1-1.amd64.deb |

deb 文件名相同，但依赖由各目标 dpkg-shlibdeps 分析生成，必须从对应目录选择。
发行 GUI 均来自 EL8 基线；每目标另编的 native GUI 仅用于回归，不替代发行包的基线程序。
RPM 运行包可能由 rpmbuild strip，不能把安装包内 ELF 哈希直接当作未 strip 的 native ELF 哈希。

## 取得产物

Actions artifacts 保留 7 天，需及时保存。每个 `desktop-<目标>` 的 `packages/` 含两份安装包，
`kernel-<目标>` 含编译日志、原始和临时测试签名模块。不要将 CI 测试证书当作本机可信 MOK。

```sh
gh run download 37749651590 --repo SkyWalkerAMD/Instrumentum-Superfrequentationis \
  --pattern 'desktop-*' --pattern 'kernel-*' --pattern 'octool-source' --dir build/actions/37749651590
```

Windows工作副本的`dist/packages-3920018/<目标>/`是最新验证副本；旧目录保留历史版本。
后续若仅更新文档并重新构建，新的包/源码哈希也可能改变，必须按实际run核对，不能混用记录。

Linux 上安装（先满足当前内核头文件和 DKMS 依赖，完整步骤见真机清单）：

```sh
# 举例：Debian12，当前目录是源代码根目录
cd dist/packages-3920018/debian12
sudo apt-get install ./octool-hwio-dkms-2.0.1-1.amd64.deb ./octool-2.0.1-1.amd64.deb
# EL 对应目录使用 sudo dnf install ./octool-hwio-dkms-*.rpm ./octool-2.0.1-*.rpm
```

## 源码

`octool-source` artifact 只在总门禁成功后生成，名称为 `octool-2.0.1-src.tar.gz`，有同名 `.sha256`。
本轮cloud源包存于`dist/cloud-3920018/`，SHA为`1e149d3d85602456a96c2c32aa93be7a55c66166d1a33c6634aaf4938dd184ea`。
它精确对应已验证3920018；下列命令生成的`dist/octool-2.0.1-src.tar.gz`另含最新文档/证据归档。
本地重新归档当前源码及最新文档：

```sh
python3 port/tools/make_source.py --require-gui
sha256sum -c dist/octool-2.0.1-src.tar.gz.sha256
```

源包包括 gui/、port/、analysis/、docs/、根 CHANGELOG 和 Actions workflow，不含旧预编译二进制、
build/dist、编译对象、模块或私钥。可选二进制分析工具的依赖见 analysis/tools/requirements-reference.txt，
普通编译不需要这些分析依赖。
