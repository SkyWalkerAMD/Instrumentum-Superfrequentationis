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
早期修复在 headers-only 容器安装 OCTool 包之前对目标 release 执行 depmod 初始化索引，
之后通过 modinfo 按名称检验，不改成仅检查 .ko 文件存在。第五轮发现重装仍会丢索引，
最终修复改为包内每次安装刷新索引，见后面的生命周期记录；不再依赖 CI 预建索引。
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

## EL8 静态 Qt 独立诊断

[run 36659551087](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36659551087)
实际校验官方 Qt5.15.18 源码并进入静态 C++ 编译。实时 configure 报告 ICU=no、内置 JPEG/TIFF/WebP、
accessibility=yes，但缺少 atspi-2 开发头，X11 AT-SPI bridge 被关闭。
依据[Qt GUI configure.json](https://github.com/qt/qtbase/blob/v5.15.18-lts-lgpl/src/gui/configure.json)，
bridge 需要 accessibility、xcb、D-Bus 和 atspi-2；补 Debian libatspi2.0-dev / EL at-spi2-core-devel，
显式要求 feature 并核验安装后的宏。首轮作为失败配置的诊断证据保留，修复后重新构建。
进一步依据 Qt 的 [top-level configure](https://github.com/qt/qt5/blob/v5.15.18-lts-lgpl/configure)
与 [qt_configure.prf](https://github.com/qt/qtbase/blob/v5.15.18-lts-lgpl/mkspecs/features/qt_configure.prf)，
确认 AT-SPI 是 privateFeature，不能只查公开 qtgui-config.h。
第二次诊断因发现这些安装后检查路径问题而停止，修正后重跑，避免已知错误直到长编译结束才暴露。
但 summary 路径从上游脚本的 cd 推断不充分：第三次
[run 36661368766](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36661368766)
实际通过 atspi 检测后，qtbase/config.summary 存在性检查失败，尚未开始 make。
现按实际生成的 config.summary / qtbase/config.summary 选择并打印路径，避免把未验证的推断写死。
OpenSSL 在首轮配置中为 no；完整网络/TLS 功能尚待恢复 GUI 后按实际需求实现并验收，
不把单独 SDK 构建当作所有 GUI 功能和运行依赖均已解决。

[run 36661824443](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36661824443)
（bfa992d）最终成功，耗时约 17 分钟；实际 summary 位于顶层 config.summary。
已下载并核验 SDK tar SHA-256，检查压缩包内静态 xcb/Wayland 插件和 AT-SPI 私有宏，
qmake ELF 通过 EL8 符号版本门禁。配置、哈希和范围说明见 [SDK 记录](qt-sdk.md)。
qmake 检查只覆盖 SDK 工具；GUI 最终链接、soname 闭包与窗口测试仍未执行。

## 模块包生命周期与后续证据

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

当时提交 bfa992d 的
[run 36661823555](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36661823555)
再次确认十个 kernel job 全部通过；baseline/desktop/gate 仍因缺 GUI 未通过，
[结果摘要](validation/actions-run-36661823555.json)保留所有 job ID。

每次修复保留失败 run；成功只能按具体源码 SHA、目标镜像和发行版包版本陈述。
Actions artifacts 保留 7 天；实际 kernel release、包 hash 和运行链接应追加到 docs/validation。
GUI 缺失是独立输入问题，不能以示例 Qt 窗口、旧 Ubuntu 二进制或忽略 baseline 来通过总门禁。

## 基础 GUI 接入后的真实矩阵

作者找回 Windows 二进制包后仍无源码，已按其选择实现基础信息/MSR/MMIO/PCI GUI。
这解除了缺少可编译工程的阻塞；平台功能仍按确认过的定义逐项恢复。

- run 36665814220：EL8 GUI 和测试程序完成链接；Qt 回归发现模拟对话框确认方式错误，
  同时 Ubuntu 的 fortified pread 路径绕过了 transport 测试原 wrap 点。改为点击实际按钮，
  拦截 __pread_chk，并在失败时仍输出测试日志。
- c63582b / [run 36666155701](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36666155701)：
  十个 kernel、EL8 baseline 和四个 desktop 成功。GUI RPM 的空 debugsource、EL9 curl-minimal、
  旧 Debian/Ubuntu 的 Wayland soname 符号、Ubuntu26 xauth 差异见 [GUI 记录](gui-phase1.md)。
- 18c18c5 / [run 36666985989](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36666985989)：
  十个 kernel、EL8 baseline 和九个 desktop 成功；新配方 Qt 从源码重建，GUI GLIBC 最高 2.28。
  EL10 的 fresh runtime 找不到 xwininfo 包提供者，总门禁仍为失败。
- 81ea801：用 CI 独立编译的 window-probe 直接调用 Xlib，保持实际 PID、标题、IsViewable 检查，
  加错误 PID 负向检查。runtime 不再要求 xwininfo/xprop。未替换为 offscreen 或退回 Xvfb。
  第 18c18c5 轮 Ubuntu26 Xwayland 四页截图已下载并逐页检查，基础表单可读且没有裁切。

这几轮可用 GUI 的所有范围都不包含容器外的真实寄存器、MOK 固件登记或模块加载。

c48a38f / [run 36670288030](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/36670288030)
首次全部 23 个 job 成功。EL10 Xlib 检查器、Mutter/Xwayland 路径得到实际验证；新增 AMD PStates
的五项回归也在十个目标通过，QtTest 各 12 项结果无失败/跳过。保存 20 个发行版安装包与 SHA256SUMS，
下载核对源码包和 SDK SHA；Debian11/EL10/Ubuntu26 的新增页截图均已目视核查。
逐目标记录见 [完整证据](validation/actions-run-36670288030.json)。

## 2026-10-08：显示可连接性与Xvfb reset

`ac0ed66 / 37769633840`的Debian11作业113286476637完成GUI编译、装包、DKMS和ldd；
发行GUI可见窗口检查通过，接着native GUI以SIGABRT退出。Qt报告xcb插件已找到，但不能连接`:99`。
[原失败日志](validation/debian11-native-display-failure-ac0ed66.log)及
[作业/原指令门禁记录](validation/legacy-transport-gate-ac0ed66.json)保存准确范围和哈希。

当时Xvfb输出默认被xvfb-run丢到/dev/null，无法证明服务器死亡、启动未就绪或reset哪一种是根因。
我们的window-probe会在Qt启动时频繁开关X连接，存在最后一个客户端退出后自动reset的候选竞态。
[Debian上游讨论](https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1095028)在2025-08-31进一步区分了
启动信号等待与最后客户端断开后的reset；该报告针对其他版本/程序，不能当作本次复现证明。

修正只作用于测试环境：Xvfb加`-noreset`，server stderr保留；启动GUI前用现有Xlib探针做最多10秒
的连接前置检查。只有明确的cannot-open-DISPLAY或探针超时可在此前置阶段等待，其他探针错误直接失败。
随后GUI只启动一次；若退出、标题/PID/可见性不满足，门禁仍失败。没有用offscreen、反复重启GUI或
跳过native检查掩盖崩溃。EL10仍使用Mutter/Xwayland，没有Xvfb回退。

3项本地回归通过；十目标修正后的实际结果须另记，不把此轮红色状态改写成绿色。

后续`eb35626 / 37771462578`又暴露诊断代码本身的问题：降权后xvfb-run用`>> /dev/stderr`
重新打开root持有的管道被拒，Debian11/12及Ubuntu24日志均报Permission denied，GUI尚未启动。
修正为非root测试用户在自己的XDG目录中mktemp日志，再通过继承的fd2输出；退出处理保留原状态。
没有chmod父进程管道、提升测试用户权限或丢弃失败。该轮前置24/26项及原指令门禁成功仍保留，
桌面结果必须由修正后的新run单独确认。
该诊断代码失败的[逐作业证据](validation/headless-log-permission-eb35626.json)单独保留。

修正提交`27d2453`的[portability 37772538641](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37772538641)
最终23/23成功；十目标kernel和desktop、EL8 baseline及总门禁全部通过。
模块重复加载探针`37772538612`也成功。Linux24项port/26项analysis无跳过，2949项原指令门禁通过。
下载8份报告后与Windows除environment完全一致；[记录](validation/headless-ci-27d2453.json)
保存作业、artifact摘要及逐报告SHA。不替代原完整主窗口、真实硬件对拍或MOK固件验收。

## 2026-10-09：授权辅助程序与目标内核启动

首轮 `f0aa7c3 / 37912479085` 的独立核心四环境成功，部分 Linux 目标通过；Ubuntu20/22、
Debian11 的新认证冒烟失败在测试代码寻找 polkitd 的路径，尚未执行授权。第一次补充候选目录仍
遗漏了 Ubuntu22 的 `/usr/libexec/polkitd`，在 f32a7ca 再现。最终改为读取安装包自带的 D-Bus
服务 Exec，校验绝对路径、root 所有权和写权限后用参数数组启动，不经过 shell；报告保存实际命令。
Ubuntu 路径可核对 [官方文件清单](https://packages.ubuntu.com/jammy/all/polkitd/filelist)。
此前被修复提交取消的轮次不计为完整绿色结果。

`7b1cbaf / 37915729368` 随后暴露 polkit 0.105 的第二个兼容点：pkaction 成功列举策略后
仍返回 1。[0.105 上游源码](https://raw.githubusercontent.com/polkit-org/polkit/0.105/src/programs/pkaction.c)
初始化 `ret=1`，正常列举结束未改为 0。冒烟现仅对精确版本 0.105 接受这个历史返回码，
并逐项核对 action ID、any/inactive/active 的 auth_admin 及辅助程序绝对路径；其他版本仍要求 0。
报告保存版本、返回码和实际加载的策略文本，失败包含标准输出与错误信息。没有放宽安装策略或认证要求。

`f2ab389 / 37917582690` 的 Ubuntu/Debian、Rocky10 桌面及全部目标内核通过；Rocky8 的
pkaction 在 30 秒后超时，原启动日志为空，不能据此断言是 OCTool 的权限策略问题。
后续将系统总线连通性、polkit 服务注册和策略查询分开检查，通过 D-Bus NameHasOwner
等待服务就绪，避免查询触发自动启动与显式启动竞争；保留服务启动输出和失败进程状态。
同轮发现最小 EL8 镜像没有 find，原检查的命令替换把失败变成了空串；现使用既有 Python
检查 Debian/Ubuntu 的 linux 目录及 EL 的 kernels 子目录，缺工具不再导致头文件检查误通过。
这些是容器测试流程修正，安装包内的授权要求不变。

`53235e8` 的就绪检查进一步证明卡住的是 D-Bus 本身，polkit 尚未启动。最小复现
`37921161363` 的 strace 显示 dbus-daemon 在读取用户组后连接自己刚创建的 socket，
随后等待 EXTERNAL 认证回复，尚未完成降权和进入事件循环。EL8 的 NSS 含 systemd，
组枚举会通过总线查询动态账户，形成自等待。
[systemd v239 的执行代码](https://raw.githubusercontent.com/systemd/systemd/v239/src/core/execute.c)
专门给总线进程设置 `SYSTEMD_NSS_BYPASS_BUS=1`，改为直接查询账户数据库。
该轮仅在启动 dbus-daemon 的子进程环境中补齐同一设置，未修改 NSS 文件、系统策略或客户端授权。
对照复现 `37921640657` 成功，NameHasOwner 返回 true；
[前后日志与哈希](validation/el8-dbus-startup-2e953c4.json)保存准确提交、环境和证据。
`service-probe` 工作流及 `port/ci/service-probe.sh` 保留为独立诊断，不代替完整桌面门禁。

`2e953c4 / 37921640595` 的完整桌面测试确认总线已正常降权并响应，但随后 polkitd 的账户
枚举请求了容器内不存在的 systemd 动态用户服务。仅给总线设置环境变量不能覆盖 polkit、
runuser 和会清理环境的 setuid pkexec。EL8 的一次性测试容器现使用本地测试账户：仅从
passwd/group/initgroups 的 NSS 提供者列表移除 systemd，保留 files、sss 和其他配置。
脚本要求 root、明确的可丢弃容器标记、Rocky8，且 PID 1 不能是 systemd；遇到未知 NSS
条件规则会拒绝修改。修改前后内容写入 runtime-nss.json。这不进入发行安装流程，也不改
用户主机的 NSS。独立探针扩展到实际 polkit 启动及 pkexec 的 root 成功/无代理普通用户拒绝，
所执行命令只是 /usr/bin/true；完整桌面测试仍要求 OCTool 自己的辅助程序通过同一授权链路。

`88999aa` 的[独立服务探针 37923477621](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37923477621)
通过，总线与 polkit 均正常注册；root 的 pkexec 返回 0，无认证代理的普通用户返回 127。
[服务日志、容器账户配置及哈希](validation/el8-service-startup-88999aa.json)保存完整范围；这项结果
只验证容器服务链路，不代表已完成真实桌面密码窗口或目录账户认证。

第二轮 `53fa72b / 37913786621` 的 EL9 头文件变为 `5.14.0-687.56.1.el9_8.x86_64`，
同一源索引却没有匹配的 kernel-core，下载按预期失败，未用旧镜像冒充新内核。内核 CI 现从
AppStream kernel-devel 与 BaseOS kernel-core 的交集选最新配对，先安装选定头文件，再仅在
可丢弃测试容器移除不配对的头文件。实际版本、初始版本和未配对项写入 kernel-selection.json。
用户 DKMS 安装流程不变；需要与其正在运行的内核匹配，不能使用 CI 的版本替代。

新增虚拟机门禁启动发行版实际内核、加载新模块并通过 HAL 核对两个 CPU 的 CPUID；
同时测试设备权限、能力查询、无效请求和卸载。使用真实 HAL 和已加载模块，CPUID 来自虚拟 CPU；
不以合成传输应答替代这条调用链，也不执行物理寄存器写入。
认证门禁执行实际 pkexec 客户端的 root 成功、普通用户无代理拒绝和退出清理，交互密码窗口
及真实主板/Secure Boot 验收仍单列。后续完整结果见 [验证状态](verification-status.md)。

`88999aa` 的[完整验证 37923477646](https://github.com/SkyWalkerAMD/Instrumentum-Superfrequentationis/actions/runs/37923477646)
最终 23/23 成功；EL8 正式包的授权、最小运行环境、窗口与 DKMS 生命周期全部通过。
十目标及全部 11 套内核的[最终证据](validation/linux-integration-ci-88999aa.json)独立记录，未混用此前失败轮次。
