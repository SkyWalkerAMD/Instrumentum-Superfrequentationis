# GitHub Actions 实测与修复记录

日期：2026-09-30。当前公开仓库为
[Instrumentum-Superfrequentationis](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis)。
名称从 Instrumentum-Super-accelerandi 按用户要求改名；旧 run 保留原 ID。

## 第一轮：22d0ea0

[Run 36657953069](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36657953069)
已经结束，总结果 failure。矩阵配置门禁通过，Ubuntu20.04 kernel job 通过；其余九个 kernel
job 失败。baseline 缺 GUI 工程而失败，desktop 全部跳过，gate 正确失败。

Ubuntu20.04 使用发行版提供的 `5.4.0-216-generic` headers，真实完成 HAL 编译、loopback、
parity selftest、17 项 Python 测试、Kbuild/modpost、vermagic 检查、临时证书签名、
`octool-hwio-dkms-2.0.1-1.amd64.deb` 构建和安装。DKMS 状态为 installed。
已下载对应 artifact 到本地 build/actions/36657953069/artifacts/ubuntu20.04；不将临时产物提交源码。
这是容器中的安装和签名验证，没有向 runner 内核加载模块，没有 MOK 登记或真实 MMIO 操作。

## EL 头文件目录

EL8/9/10 三项在全部离线测试通过后报 `No target kernel headers`。
kernel-devel 已安装，但只有它提供的 `/usr/src/kernels/<release>` 树，容器未安装 kernel-core。
bootstrap 现在读取配置树的 `include/config/kernel.release`，建立相应 `/lib/modules/<release>/build`
链接。发现已有链接必须核对目的地一致；缺配置仍报错。源树、DKMS 和签名 helper 使用同一目标路径。
不通过安装/使用 runner 的宿主 headers 回避问题，不按内核版本号选择 API。

## class_create 编译探测

Debian12/13 和 Ubuntu22.04/24.04/26.04 均在 Kbuild 中报告两种签名都不能编译。
原 try-run 会吞掉全部 stderr，不能单凭该报错确定 API 改了。
对照[上游 Makefile.lib](https://github.com/torvalds/linux/blob/v6.1/scripts/Makefile.lib)，
常规模块的编译命令额外有 compiler_types.h 的强制包含，以及 module、basename/modname flags；
这些在项目 Kbuild 被解析之后才加入普通编译命令。解析阶段的探测现在显式提供同样的上下文。
两种签名仍分别实际编译，均失败则打印 `.octool-class-1.log` 和 `.octool-class-2.log` 的原始错误。
日志写在模块构建目录并由 Kbuild clean 清理，不写入内核 headers。

此调整须由下一轮矩阵核实；不能把代码审阅当作十目标修复成功。

## Debian11 安全源断档

首轮 apt 获取 live `bullseye-security` 索引成功，但 perl、python、glibc、linux-libc-dev 等包 404。
实测 live Release 日期为 2026-09-12，archive.debian.org/debian-security 的同名 Release 仍 404。
Debian 的[问题 #1147093](https://bugs.debian.org/1147093)记录了同一迁移断档。
不能仅把域名改为 archive.debian.org，也不应关闭包签名。

一次性 Debian11 容器固定到 2026-08-31 LTS 最后一天的官方快照：

```text
deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20260831T235959Z/ bullseye main
deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/20260831T235959Z/ bullseye-security main
```

已通过 HTTPS 核实该 security 快照的 InRelease、linux-libc-dev 5.10.262-1 和 perl-base
5.32.1-4+deb11u5 均可下载；正式安装继续由 APT 核验签名及包哈希。bootstrap 起步没有 CA 包，
因此 APT 使用 HTTP 和发行版签名验证。`check-valid-until=no` 仅用于这两个固定历史源，
不是全局关闭校验；用法依据 [Debian snapshot 文档](https://snapshot.debian.org/#usage)。
此配置仅属于可丢弃测试容器，不随 octool 软件包安装到用户系统。

## 证据保留和后续

每次修复保留失败 run；成功只能按具体源码 SHA、目标镜像和发行版包版本陈述。
Actions artifacts 保留 7 天；实际 kernel release、包 hash 和运行链接应追加到 docs/validation。
GUI 缺失是独立输入问题，不能以示例 Qt 窗口、旧 Ubuntu 二进制或忽略 baseline 来通过总门禁。
