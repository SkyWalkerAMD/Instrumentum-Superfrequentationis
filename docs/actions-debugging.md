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

第二轮 EL8 已完成实编、签名、RPM 构建和 DKMS 安装，但最后按模块名查询失败。
核对[DKMS 3.4.3 的 do_depmod](https://github.com/dkms-project/dkms/blob/v3.4.3/dkms.in)：
它在目标目录没有 modules.dep 时认为未安装内核映像，跳过创建索引。
headers-only 容器现在在安装 OCTool 包之前对目标 release 执行 depmod 初始化索引，
之后仍由真实 DKMS 安装更新索引，并通过 modinfo 按名称检验，不改成仅检查 .ko 文件存在。
此外 EPEL 的 dracut post_transaction 在无 boot image 的容器中失败，容器 bootstrap
明确禁用该启动映像钩子；配置不进入发行包。真实内核升级/启动映像和 MOK 仍必须在 VM/真机验收。

## class_create 编译探测

Debian12/13 和 Ubuntu22.04/24.04/26.04 均在 Kbuild 中报告两种签名都不能编译。
原 try-run 会吞掉全部 stderr，不能单凭该报错确定 API 改了。
对照[上游 Makefile.lib](https://github.com/torvalds/linux/blob/v6.1/scripts/Makefile.lib)，
常规模块的编译命令额外有 compiler_types.h 的强制包含，以及 module、basename/modname flags；
这些在项目 Kbuild 被解析之后才加入普通编译命令。解析阶段的探测现在显式提供同样的上下文。
两种签名仍分别实际编译，均失败则打印 `.octool-class-1.log` 和 `.octool-class-2.log` 的原始错误。
日志写在模块构建目录并由 Kbuild clean 清理，不写入内核 headers。

第二轮 [run 36658900518](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36658900518)
（提交 f2c494b）的原始 stderr 揭示直接失败原因是 `<stdin>:1:1: error: stray '\\' in program`。
GNU Make 对函数参数内 `\#` 的处理存在差异，导致生成的 `#include` 前留下反斜杠；
Ubuntu20.04 使用的工具链未触发该问题。补 flags 本身并未修复它，不能把它写成已验证根因。
现在用独立 `class_create_probe.c` 编译两种签名，避免在 Make 字符串内生成 C 源码。
原生包和手工 DKMS 安装都必须携带该探测文件；它不会链接到最终模块。

第三轮 [run 36659298597](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36659298597)
（e530b51）中 Debian11、Ubuntu20.04 已通过；其余目标的 C 编译阶段也能探测正确签名，
但 modpost 重新读取 Kbuild 时没有 try-run，误走“两个探测都失败”。
现在只在 Makefile.build 的 C 编译上下文执行探测，clean/modpost 只读取对象列表。
这是构建阶段检测，没有按内核版本分支。EL9 的 5.14 vendor 内核实测为单参数签名，
进一步说明不能从上游版本号推断该 API。

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
第二轮中该快照配置已实际安装完成，Debian11 的 HAL、C 离线自测和 17 项 Python 测试通过，
随后进入 5.10.0-46 headers 的编译探测并触发上述 Make 转义问题。

## 证据保留和后续

### EL8 静态 Qt 独立诊断

[run 36659551087](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36659551087)
实际校验官方 Qt5.15.18 源码并进入静态 C++ 编译。实时 configure 报告 ICU=no、内置 JPEG/TIFF/WebP、
accessibility=yes，但缺少 atspi-2 开发头，X11 AT-SPI bridge 被关闭。
依据[Qt GUI configure.json](https://github.com/qt/qtbase/blob/v5.15.18-lts-lgpl/src/gui/configure.json)，
bridge 需要 accessibility、xcb、D-Bus 和 atspi-2；补 Debian libatspi2.0-dev / EL at-spi2-core-devel，
显式要求 feature 并核验安装后的宏。首轮作为失败配置的诊断证据保留，修复后重新构建。
进一步依据 Qt 的 [top-level configure](https://github.com/qt/qt5/blob/v5.15.18-lts-lgpl/configure)
与 [qt_configure.prf](https://github.com/qt/qtbase/blob/v5.15.18-lts-lgpl/mkspecs/features/qt_configure.prf)，
修正 config.summary 的 qtbase/ 路径；AT-SPI 是 privateFeature，不能只查公开 qtgui-config.h。
第二次诊断因发现这些安装后检查路径问题而停止，修正后重跑，避免已知错误直到长编译结束才暴露。
OpenSSL 在首轮配置中为 no；完整网络/TLS 功能尚待恢复 GUI 后按实际需求实现并验收，
不把单独 SDK 构建当作所有 GUI 功能和运行依赖均已解决。

第四轮 [run 36659550123](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36659550123)
（a8a5f1e）十目标 kernel 全部成功，含三个 Debian、三个 EL、四个 Ubuntu，Ubuntu22.04 同测 GA/HWE。
EL8 的 RPM 安装索引问题已解决，所有目标均通过按模块名 modinfo 和版本/vermagic 检查。
详细 release、镜像 ID/digest、原始/试验签名模块及包 hash 保存于
[actions-run-36659550123.json](validation/actions-run-36659550123.json)。
仍需实测同版本重装及卸载重装，现将这两项从依赖 GUI 的 runtime 任务补到独立 kernel 任务，
以验证 RPM safe-upgrade 锁和 Debian prerm/configure 的实际事务行为。

第五轮 [run 36659952626](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36659952626)
（818af54）补测发现：EL 三项与 Ubuntu26.04 初装成功，但生命周期后 modinfo 按名称失败。
safe-upgrade 锁已实际生效（旧包 preun 正确取消 remove）；问题在 `dkms build --force` 删除
最后一个已安装模块时会清掉空 modules.dep，随后安装又跳过 depmod。因此只在 bootstrap
初始化一次索引不足以解决生命周期问题。
改由发行包的 dkms-register 在每次安装后实际 `depmod -a <target>`，再按模块名核对
MODULE_VERSION/vermagic 后才报告成功；移除 CI 预先创建索引的补丁。
这项修复也覆盖真实 headers-only 目标树，无需修改用户全局 DKMS 配置。

第六轮 [run 36660297759](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36660297759)
（3f7f942）十个 kernel job 再次全部通过，含初装、同版本重装、卸载再安装以及全部离线门禁。
完整机器可读结果和更新后的包哈希见
[actions-run-36660297759.json](validation/actions-run-36660297759.json)。本地 dist/packages/ 已替换为这轮产物。

每次修复保留失败 run；成功只能按具体源码 SHA、目标镜像和发行版包版本陈述。
Actions artifacts 保留 7 天；实际 kernel release、包 hash 和运行链接应追加到 docs/validation。
GUI 缺失是独立输入问题，不能以示例 Qt 窗口、旧 Ubuntu 二进制或忽略 baseline 来通过总门禁。
