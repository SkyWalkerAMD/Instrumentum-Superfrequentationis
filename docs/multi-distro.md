# 多发行版构建、运行时与打包

最新重构提交 `beb10b7` 已把 PStates、寄存器请求/校验/串行服务和 Linux 硬件适配分离。
独立核心四环境通过，Linux 完整矩阵 23/23；EL8 基线与十目标各 13 项 Qt 测试通过，
每目标 26 项 Python 回归无跳过，见[本次源码、测试及产物哈希](validation/refactor-hardware-ci-beb10b7.json)。
当前主线是先完成功能重构，再移植重构版；Windows/macOS 此时只验证核心，尚无硬件后端/完整 GUI。
剩余系统信息、多步事务、其他平台面板和目标真机验收仍待继续。

以下保留此前研究与验证记录。

## 1. 目标与证据要求

单一矩阵入口是 `port/ci/targets.json`。架构为 x86_64/amd64。
以下为构建矩阵配置。十目标模块/离线测试/DKMS 包生命周期已实测通过；基础版 GUI 已实现。
具体提交、kernel release 和实际状态见 [verification-status.md](verification-status.md)。

| 目标 ID | 构建镜像 | 头文件包 | 无头 GUI 后端 | 包格式 |
|---|---|---|---|---|
| el8 | rockylinux:8 | kernel-devel | Xvfb | rpm + DKMS |
| el9 | rockylinux:9 | kernel-devel | Xvfb | rpm + DKMS |
| el10 | quay.io/rockylinux/rockylinux:10 | kernel-devel | mutter + Xwayland | rpm + DKMS |
| ubuntu20.04 | ubuntu:20.04 | linux-headers-generic | Xvfb | deb + DKMS |
| ubuntu22.04 | ubuntu:22.04 | generic 与 generic-hwe-22.04 | Xvfb | deb + DKMS |
| ubuntu24.04 | ubuntu:24.04 | linux-headers-generic | Xvfb | deb + DKMS |
| ubuntu26.04 | ubuntu:26.04 | linux-headers-generic | mutter + Xwayland | deb + DKMS |
| debian11 | debian:11 | linux-headers-amd64 | Xvfb | deb + DKMS |
| debian12 | debian:12 | linux-headers-amd64 | Xvfb | deb + DKMS |
| debian13 | debian:13 | linux-headers-amd64 | Xvfb | deb + DKMS |

Debian 11/12 的发行基线内核为 5.10/6.1，Debian 13 为 6.12；构建时由各发行版
头文件元包选出实际补丁版本，不能把表里的系列号当作 `KVER`。
来源：[Debian 12 发行说明（同时列出 11）](https://www.debian.org/releases/bookworm/amd64/release-notes/ch-whats-new.en.html)、
[Debian 13 发行说明](https://www.debian.org/releases/trixie/release-notes/whats-new.en.html)。

仓库软件包和镜像 tag 会变化。CI 保存镜像 digest、完整包清单、编译器、glibc、实际
kernel release、modinfo 和构建日志。发布时保存这些 artifacts，并把镜像锁定为已验收 digest；
当前脚本使用发行版 tag 追踪仓库更新，尚不是冻结软件源的可复现发行构建。
已归档发行版的软件源如发生迁移，应按官方 archive/snapshot 地址修订并记录，不能跳过该目标。

## 2. 源码接入

作者确认原 Linux 源码丢失后，已按选择重构基础版 `gui/octool.pro`，详细范围见
[第一阶段](gui-phase1.md)。`port/gui/build.json` 管理以下真实构建输入，后续扩展按实际工程修改：

- `project`：实际 qmake `.pro` 的仓库相对路径。
- `binary`：shadow build 输出目录中的 ELF 相对路径。
- `test_project`：`gui/tests/regression.pro`，每次 GUI 编译后运行离线 Qt 控件/传输回归。
- `qmake_args`：项目所需附加参数，以数组保存，不做 shell 字符串拼接。
- `resources`：需要分发的额外资源路径，当前复制到 `/opt/octool/share/<原路径>`。
  必须根据真实 GUI 的资源查找方式核对后配置。
- `license_file`：新 GUI 的 GPLv2 文件 `gui/LICENSE`，与所链接 HAL 许可一致。
- `window_title_regex`：匹配当前真实主窗口 `OCTool — Hardware information and registers`。

```sh
python3 port/tools/build_gui.py --preflight
```

此检查当前通过；源码或许可证缺失时仍退出 2。普通用户无模块时可查看基础信息，
显式读取失败会显示实际错误，启动不会自动访问未知寄存器。

## 3. 内核构建与探测

```sh
# 在真实目标系统；头文件必须对应运行内核。
sudo apt-get install "linux-headers-$(uname -r)" gcc make libelf-dev  # Debian/Ubuntu
# 或 EL：
sudo dnf install "kernel-devel-$(uname -r)" gcc make elfutils-libelf-devel
make -C port/kmod KDIR="/lib/modules/$(uname -r)/build"
make -C port/tests check hwio_smoke
```

容器中的 `uname -r` 属于宿主机，所以 CI 枚举 `/lib/modules/*/build` 并逐个显式传 `KDIR`。
每棵树必须有 `Module.symvers`，Kbuild/modpost 不允许缺导出符号时只报 warning。
产物的 vermagic 第一项必须等于选定头文件对应的 kernel release。

唯一已知 API 敏感点仍是 `class_create` 参数个数。本轮把固定排版的头文件正则替换为
Kbuild `try-run` 编译探测：先一参数，再二参数，均使用目标内核的 include/编译选项。
两种都失败就报错，不能把损坏头文件或错误编译器误判成旧 API。
这样保留 RHEL 回移植的兼容思路，也覆盖 Debian common/arch 分离头文件布局。
Debian11/12/13 的真实 headers 均已编译通过。探测使用独立 class_create_probe.c，
仅在 Makefile.build 的 C 编译阶段执行，避开 Make 转义及 modpost 无编译器上下文的问题。
EL 容器按 /usr/src/kernels 中的实际配置建立 build 链接；Debian11 使用官方 LTS 结束日快照，
具体原因与修复证据见 [Actions 实测记录](actions-debugging.md)。

DKMS 的源布局保留 `kmod/`、`abi/` 两级目录，模块包装头只引用 canonical ABI。
`port/abi/octool_hwio_abi.h` 和 HAL 头文件与输入包逐字节相同。
HAL C 修复了 CPU 编号被设备令牌覆盖及短传输误报成功；模块只调整非 MMIO 命令的
无效 CPU 处理，返回 EINVAL，不回退到当前 CPU。MMIO 操作码、96 字节请求、邮箱和
MMIO dispatch 不变。修正依据与新 HAL 哈希见 [第一阶段记录](gui-phase1.md)。

编译器按头文件 `CONFIG_GCC_VERSION` 元数据选择已安装的 `gcc-N`，不用内核版本号猜测。
Ubuntu HWE 对应 gcc-N 由 CI bootstrap 安装；本机升级内核时也需保证对应编译器已安装。
这不属于新的内核 API 分支。

## 4. EL8 静态 Qt 发布基线

默认使用 EL8 自带 GCC，避免较新 gcc-toolset 的 libstdc++ 需求传播给旧系统。
Qt 固定为 5.15.18，官方源码 SHA-256 固定在 `build-qt-el8.sh`，仍为 Qt 5.15 系列。
它与原 5.15.2 的 GUI 行为须通过实际源码和窗口测试验证，不能只凭版本号宣称无变化。
来源：[Qt 官方下载与校验值](https://download.qt.io/archive/qt/5.15/5.15.18/single/qt-everywhere-opensource-src-5.15.18.tar.xz.mirrorlist)。

主要构建选项：`-static -no-icu -qt-libjpeg -qt-libpng -qt-zlib -qt-pcre -qt-harfbuzz -qt-tiff -qt-webp`
以及 `-accessibility -dbus-linked -xcb -xcb-xlib -opengl desktop`。
QtBase/Charts/Connectivity/ImageFormats/Svg/Wayland 根据已有二进制分析作为初始 SDK 配置；
后续源码若使用其他 Qt 模块，必须核对真实依赖和许可。基础版只链接 QtBase。
静态 Wayland 插件保留在 SDK 中，但 GUI 通过 `QTPLUGIN.platforms = qxcb qoffscreen`
排除自动导入的 Wayland 插件，避免 EL8 更新后的 wl_proxy_marshal_flags 需求传播到旧系统。
正式窗口走 xcb/Xwayland，offscreen 只用于 Qt 控件回归。
保留 Qt accessibility 和 D-Bus，安装 AT-SPI 运行时，不禁用辅助功能来绕过启动问题。
Qt 的 X11 依赖依据：[Qt 5.15 X11 requirements](https://doc.qt.io/archives/qt-5.15/linux-requirements.html)。

发布 GUI 必须通过：

```sh
python3 port/tools/check_elf.py build/gui-stage/opt/octool/bin/octool-real
```

门禁直接解析 ELF64 的动态依赖和版本需求，不运行输入文件：

- GLIBC 不得高于 2.28；GLIBCXX 不得高于 3.4.25；CXXABI 不得高于 1.3.11。
- 不接受动态 Qt5、ICU、系统 libjpeg/TIFF/WebP、OpenCV 依赖或作者机器的绝对 RPATH。
- 不捆绑 glibc/动态加载器，不创建 `libjpeg.so.8 -> libjpeg.so.62` 这种 ABI 伪装软链。
- 字体、X11/xcb、OpenGL 驱动、D-Bus、hwloc 等由目标包管理器提供。
- native GUI 在每个目标重新链接用于编译回归；**发行包始终使用 EL8 的同一基线 GUI**。
  native GUI 的较高 glibc 需求只记录，不冒充可供 EL8 使用的发行二进制。

GUI staging 必须为空；重复构建请给新的 `--stage` 路径，防止旧库或资源混入发行包。

如果原源码实际需要更新的 C++ 工具链，应先确认最低语言标准，再决定在 EL8 内构建
新编译器并私有捆绑其 libstdc++/libgcc，或其他兼容方式；当前实现不会放宽 ABI 门禁。
EL8 动态依赖仍要在每个目标真实安装后运行 `ldd` 和窗口测试。
Qt/OpenSSL、Bluetooth、打印等具体功能需要源码和真机补充验收，主窗口出现并不验证这些功能。

## 5. Wayland/Xwayland 和冒烟

发布配置采用 Qt xcb，通过 Xwayland 在 Wayland 桌面运行。
RHEL10 移除了 Xorg server，仍支持 Xwayland；不依赖目标机上的 Xorg/Xvfb。
依据：[RHEL10 removed features](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10/html/10.0_release_notes/removed-features)。

普通桌面从会话中执行 `octool`。launcher 在未显式设置平台时选择 `xcb`，没有 `DISPLAY`
会给出错误；不会自动改为 offscreen 后假装图形界面正常。
EL10 与 Ubuntu26.04 的无头门禁运行：

```sh
# 在有 X11 开发头文件的目标构建环境编译 CI 检查器；不安装到产品包。
mkdir -p build
cc -std=c11 -O2 -Wall -Wextra -Werror port/ci/window-probe.c -lX11 -o build/window-probe
PROBE="$PWD/build/window-probe"
# 非 root 用户、有可写 HOME；依赖 xwayland-run、mutter、Xwayland、xauth。
bash port/ci/headless-smoke.sh xwayland --probe "$PROBE" --log /tmp/octool-gui.log
# 其余矩阵目标：
bash port/ci/headless-smoke.sh xvfb --probe "$PROBE" --log /tmp/octool-gui.log
```

测试必须找到真实 GUI PID 所属、标题匹配、已映射的窗口，连续保持至少 5 秒。
进程早退、崩溃、窗口缺失、显示服务无法启动均为失败，超时不算成功。
普通用户运行，不挂载设备、不加载模块、不注入假硬件应答。
当前基础版正常主窗口无需硬件权限；访问失败显示实际错误，不伪造传感器值。
CI 通过 OCTOOL_SMOKE_SCREENSHOT 指定路径保存各页窗口截图，正常启动不产生截图。
窗口检查器直接使用 Xlib，兼容 EL10 已不提供 xwininfo 的软件源，检查实际 PID/标题/IsViewable。

## 6. 原生打包

```sh
# 只生成 DKMS 包，不需要 GUI：
python3 port/tools/build_packages.py --format deb --module-only --output dist
python3 port/tools/build_packages.py --format rpm --module-only --output dist
# 发布包（先提供经 EL8 构建并验收的 staging）：
python3 port/tools/build_packages.py --format deb --gui-stage build/gui-stage --output dist
python3 port/tools/build_packages.py --format rpm --gui-stage build/gui-stage --output dist
```

工具复用 `port/packaging/` 的 DKMS conf、udev、MSR modules-load 和 RPM spec。
Debian 的原未完整配置 debhelper 模板已改为明确的 control 模板加 `dpkg-deb`，
这样 Ubuntu20.04 无需不存在的 debhelper-compat13，且两个发行版线共用同一 payload。
`dpkg-shlibdeps` 从最终 GUI 计算共享库依赖；RPM 使用 rpmbuild 自带依赖扫描。
所有 native 打包命令要在对应目标系统/容器执行，不在 Windows 伪造 rpm/deb。

安装文件：`/opt/octool/`（GUI、许可证、资源）、`/usr/bin/octool`（launcher）、
`/usr/src/octool-hwio-<version>/`（DKMS 源码）、`/usr/libexec/octool/`（注册和签名辅助）。
设备仍为 `/dev/mydev`，udev 默认 root:root 0600，不授予普通桌面用户硬件写权限。
包安装会注册并构建已安装头文件对应的内核，刷新 depmod 索引后按名称核对版本/vermagic；
失败不得吞掉，未安装头文件会给出具体错误。
容器 CI 另外读取 DKMS installed 状态、vermagic 和 MODULE_VERSION，防止 RPM scriptlet 告警被误当安装成功。
安装过程不自动 modprobe、不卸载旧模块。
RPM 升级使用配对的 `--rpm_safe_upgrade`；同版本包重装强制刷新 DKMS 构建，
初装、同版本重装、卸载再安装已经通过十目标真实事务验证；跨版本升级/内核升级仍需真机验收。
原因与证据见 [Actions 实测记录](actions-debugging.md)。

版本以根目录 `VERSION` 为准；同步变更模块 MODULE_VERSION，并记录日志。
源码归档命令：

```sh
python3 port/tools/make_source.py --require-gui
# dist/octool-2.0.1-src.tar.gz 和对应 .sha256
```

归档排除构建产物、旧 ZIP、签名私钥，统一源码权限和时间元数据。省略 `--require-gui`
仅用于交接尚缺 GUI 的当前源码，不能把这种交接包称作可发布的完整 GUI 源码。
新 GUI RPM 的 License 为 GPL-2.0-only，与 gui/LICENSE 一致；旧二进制许可不变。
GUI release 重打包没有 DWARF，runtime spec 关闭 debug_package，仍保留自动依赖扫描。

## 7. CI 和本地 Linux runner 复现

当前使用用户授权的公开 GitHub 仓库 Actions，见 [GitHub 接入](github-actions.md)。
此前 OpenAI 托管云端的入口核实作为历史保存在 [cloud-access.md](cloud-access.md)。
Linux Docker 环境内的完整执行入口为 `python3 port/ci/run-matrix.py`，
内核先行和证据结构见 [执行指南](cloud-build.md)。该脚本不依赖 GitHub Actions，
也不会自行创建托管云环境。

`.github/workflows/portability.yml` 的 push、PR、手工触发运行：

这里的 Git 仓库根目录应为本源码目录（当前工作区中的 `octool-linux/`），
而非只存放 ZIP 的上一级目录。迁移到已有仓库时，需把本目录内容放到该仓库根，
或按实际布局调整所有 workflow 相对路径。

1. 矩阵校验及 Python 发布门禁测试、ABI/HAL 已审阅哈希检查、文档链接与源码归档一致性检查。
2. 十个独立 kernel job：HAL、loopback、transport、parity selftest、真实 Kbuild/modpost、测试证书签名、
   DKMS 包构建/安装、同版本重装、卸载再安装、installed/vermagic 核对。Ubuntu22.04 还覆盖当前 GA/HWE。
3. EL8 baseline job：实际 GUI 源码 preflight、静态 Qt SDK、GUI/Qt 控件回归、EL8 ABI 门禁。
   SDK 按 Qt/依赖脚本哈希缓存并校验归档，GUI 每次重编；配方改变使旧 seed/cache 失效。
4. 十个 desktop job：使用 baseline SDK 在各目标重新编 GUI、生成 native rpm/deb；
   第二个全新容器安装发行包，检查 ldd，运行发行/本地重编两种 GUI，卸载再安装并核对 DKMS。
5. `gate` 在所有 job 成功时才成功；失败、取消、跳过均不能过门禁。

kernel job 不依赖 GUI baseline，因此 GUI 缺失时仍可收集 Debian 内核和离线测试失败点。
矩阵 `fail-fast: false` 保留所有目标的失败结果；没有 `continue-on-error`。
仓库管理员应把 `portability / gate` 设为受保护分支必需检查。
当前源码目录已配置公开仓库 origin 并多轮实际运行；以验证文档对应 run 的实际结果为准。

Linux Docker 中复现 Debian12 的内核/模块包阶段：

```sh
mkdir -p build/debian12
docker run --rm --init -e OCTOOL_DISPOSABLE_CONTAINER=1 -v "$PWD:/src" -v "$PWD/build/debian12:/out" -w /src \
  debian:12 bash port/ci/in-container.sh kernel debian12
```

替换 image/id 即覆盖其他表中目标。baseline/desktop/runtime 的完整挂载参数见 workflow。
这些入口会安装软件包，只应在可丢弃的构建容器中执行。
不使用 `--privileged`，不把宿主机 `/lib/modules` 或 `/dev` 挂进容器。
容器门禁无法验证 insmod、真实 MMIO、MOK 注册、物理桌面权限与超频行为；这些必须留给真机。
