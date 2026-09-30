# 在 Linux Docker 环境运行矩阵

更新：2026-09-30。当前实际测试使用用户明确选择的公开 GitHub 仓库 Actions，
见 [Actions 接入](github-actions.md) 和 [实测结果](verification-status.md)。
此前把 OpenAI 托管云端理解为用户提供服务器有误，历史纠正保存在 [云端接入状态](cloud-access.md)。
本页是通用 Linux Docker runner 的独立复现说明，不要求用户提供 SSH。

## 执行前提

- 已进入可用的 Linux 构建环境，源码已在该环境中。
- 已确定构建目录，Docker 已安装且当前账号可访问该环境内的 Docker daemon。
  Actions 的 Linux/Docker 能力已实测；这个独立 runner 本身不创建云资源、不自动安装 Docker。
- 恢复或重构后的 GUI 源码。作者确认原 Linux GUI 源码丢失；现有 octool-linux.zip 是二进制，OCTool0528.zip
  尚未在指定目录找到；不能拿旧 Ubuntu ELF 代替 EL8 重编。

宿主要求：Linux x86_64、Python 3.8 或更新、可用的本机 Linux/amd64 Docker daemon，
能够访问目标镜像仓库、各发行版软件源及 Qt 官方下载。构建目录必须能保存 Qt 源码/
对象文件、十目标各自的源码副本和产物；实际时间、峰值内存、磁盘量尚无实测记录。
runner 顺序执行任务，Qt/GUI 默认 make -j2，避免在尚未知配置的机器上同时启动多个 Qt 构建。
EL10 容器还取决于宿主 CPU 能否运行该镜像，拉取成功不能替代实际启动检查。

在给定目录解压 `octool-2.0.1-src.tar.gz`，进入归档内 `octool-2.0.1/`。
后续命令在 Linux 构建环境中执行，不在 Windows PowerShell 中直接运行。

## 先验证 Debian 内核

在源码根目录执行：

```sh
# 不访问 Docker，检查将要运行的任务列表：
python3 port/ci/run-matrix.py --plan --scope kernel --targets debian11 debian12 debian13

# 实际执行三项内核编译、C 离线测试、模块包构建和 DKMS 安装：
python3 port/ci/run-matrix.py --scope kernel --targets debian11 debian12 debian13

# 扩展至全部十目标的内核分项：
python3 port/ci/run-matrix.py --scope kernel
```

缺少 GUI 不影响这些内核任务。Debian 头文件从各自发行版仓库安装，编译显式选择
`/lib/modules/<实际版本>/build`，不使用容器里宿主机的 `uname -r` 来挑头文件。
每个目标都执行同一个 `in-container.sh kernel <target>`，与 GitHub CI 的入口相同。
bootstrap 先验证 `/etc/os-release` 的 ID/版本与 target 一致，再安装依赖。
kernel 模式不再安装 Qt、X11、Wayland 的开发包。

## 全矩阵

接入 GUI 并按 [构建指南](multi-distro.md#2-源码接入) 核对 manifest 后：

```sh
python3 port/tools/build_gui.py --preflight
python3 port/ci/run-matrix.py --plan
python3 port/ci/run-matrix.py
```

完整计划为 31 个阶段：10 个 kernel、1 个 EL8 baseline、10 个 desktop、10 个 runtime。
baseline 生成 EL8 静态 Qt SDK 和发行 GUI；每个 desktop 使用 SDK 在对应目标重新编 GUI，
并为同一个 EL8 发行 GUI 生成该目标的 rpm/deb；runtime 在新的同目标容器安装并启动
发行/本地重编的两份 GUI，核对 DKMS，执行卸载和重装。详细门禁见 [多发行版指南](multi-distro.md)。

一个内核任务失败后继续其他目标。GUI preflight/baseline 失败时，将 desktop/runtime
标为 blocked，完整命令退出非零。某个 desktop 失败只阻断它的 runtime。
没有把失败、超时、缺输入或未执行阶段算作通过。

默认每次在 `build/cloud-runs/<UTC时间>-<随机后缀>/` 建新目录，启动时打印 `Evidence:`
路径。可用 `--output /实际目录/新run目录` 指定新位置；已有目录会拒绝，不能混用旧日志。
源码树内输出限于 `build/` 或 `dist/`，防止本次日志和嵌套源码混进输入快照。
`--timeout-minutes` 控制单条构建命令/容器的超时，默认 180 分钟；超时退出 124，
只清理本次创建且名称唯一的容器，保留失败日志与源码副本。

## 隔离和证据

- 每次运行先生成一个源码快照并记录 SHA-256，各编译阶段独立解压。
  不在十个目标之间共享 `.o`、qmake Makefile 或 Kbuild `.cmd`。
- 同一次运行每个镜像 tag 拉取后保存 image inspect，并使用不可变 image ID 启动后续容器。
  系统包仓库仍可能更新，包清单是证据，不宣称已冻结全部外部依赖。
- 容器不使用 privileged，不挂载宿主 `/dev`、`/lib/modules`、Docker socket 或 SSH 密钥。
  每个 runtime 的源码挂载只读，SDK 输入只读。
- bootstrap 需要 `OCTOOL_DISPOSABLE_CONTAINER=1`；runner/workflow 已传递。
  不要在宿主机直接设置这个变量然后执行 bootstrap，因为该脚本会安装目标发行版依赖。
- 源码快照只解压普通文件/目录，拒绝越界路径、链接和设备节点。
  源打包器将树内文件软链物化成文件，并剪枝排除 build/dist，不遍历以往云端源码副本。

每次运行的目录结构：

```text
results.json                         # 持续更新；中断时不会留下成功总状态
local-checks.log                      # Python 门禁、ABI/HAL 冻结、源码归档检查
input/octool-2.0.1-src.tar.gz          # 本次构建输入和 .sha256
images/<target>.json                  # 镜像内容 ID、digest、架构
images/<target>-pull.log
jobs/kernel-<target>/console.log
jobs/kernel-<target>/artifacts/        # 内核、签名结果、module rpm/deb、离线测试
jobs/baseline-el8/artifacts/           # EL8 SDK/GUI tar、ABI 信息
jobs/desktop-<target>/artifacts/      # GUI/模块包、native GUI、runtime/窗口日志
jobs/runtime-<target>/console.log     # 使用相应 desktop 的 artifacts 目录
```

`results.json` 为每个已处理阶段记录 status、原因、退出码、耗时、日志/产物相对路径。
拉镜像等前置步骤发生异常时，会保留 failed 原因，未必已经生成该阶段的全部日志。
两个总字段有不同含义：

| 字段 | 含义 |
|---|---|
| requested_scope_passed | 这次明确请求的所有阶段通过；决定进程退出码 |
| full_matrix_passed | 必须 scope=all、十个目标齐全且 31 阶段全部通过 |
| hardware_acceptance | 固定 not_run；容器不能确认 MOK/真实 MMIO/超频 |

三项 Debian 内核或十项内核分项即使通过，`full_matrix_passed` 仍为 false。
拿到真实日志后，把目标结果和实际内核/包版本补入 [验证状态](verification-status.md) 与 CHANGELOG。
`port/tests/test_cloud_runner.py` 的模拟执行只验证这些状态传播规则，不产生 Linux 编译证据。

## 本次研究收敛的问题

RPM 原来的 `%preun` 只在最终卸载时移除 DKMS，会留下升级前版本的注册。
现在采用 DKMS 的 `--rpm_safe_upgrade`，在 `%post` 的 add 和 `%preun` 的 remove 两侧配对使用；
同模块版本重复 add 的退出 3 只在确认已有注册时接受，其他错误继续失败。
add 保留在 RPM scriptlet 内直接执行，因为上游用父进程关联事务锁，额外 shell helper
层会改变关联。注册 helper 强制重建/安装同版本模块，避免同版本新 RPM release 复用旧对象。
CI 另外核对 DKMS installed、vermagic、MODULE_VERSION，RPM 包管理器警告不等于安装已通过。
**这些脚本已静态复查，原生 RPM 升级/降级事务仍待云机实测。**

依据：[DKMS 2.8.4 手册 RPM 打包说明](https://manpages.debian.org/bullseye/dkms/dkms.8.en.html)、
[DKMS 上游实现](https://github.com/dkms-project/dkms/blob/main/dkms.in)、
[RPM scriptlet 执行顺序](https://rpm.org/docs/4.20.x/manual/triggers.html)。

Qt5 的 ImageFormats 插件会探测系统 TIFF/WebP；只写 `-static` 不能保证编解码库也静态。
实际读取 Qt 5.15.18 上游配置后，新增 `-qt-tiff -qt-webp`，延续旧 GUI 已内嵌这两类库的方式；
ELF 门禁拒绝 libtiff/libtiffxx/libwebp/libwebpdemux/libwebpmux 动态依赖。
原输入分析已有 TIFF/WebP，不在缺少源码时推断或删除 GUI 的图像功能。

依据：[Qt 5.15.18 ImageFormats 配置源码](https://raw.githubusercontent.com/qt/qtimageformats/v5.15.18-lts-lgpl/src/imageformats/configure.json)。
本次读到的原始文本 SHA-256：`c48673f19997b843b7a5e8dc65500161af7800284cb7a85aff943b2f45d58de8`。
配置选项的核实不是 SDK 编译成功；最终仍以十目标 ELF/ldd/窗口实测为准。
