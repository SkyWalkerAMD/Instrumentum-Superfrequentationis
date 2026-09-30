# CHANGELOG — octool Linux 多发行版适配

合并进 octool 源码树的 `CHANGELOG.md` 时，把本节并入对应版本或 Unreleased 条目。详细记录在 `docs/linux-porting.md`。

## Unreleased

### 2026-09-29 — 适配研究（第二轮），无代码改动

- **EL 内核实测**：在 Rocky 8.10 / 9.8 / 10.2 源码树（`ctrliq/kernel-src-tree`）上 `modules_prepare` 后实编 `tools/kmod-probe`。
  - `compat` 写法三个都通过；
  - `asis` 在 8.10 上失败，`old` 在 9.8、10.2 上失败，都是 `class_create` 参数个数的问题。
- **EL9 签名变化**：`class_create` 在 Rocky 9.2（5.14.0-284.30.1）是双参数，9.4（427.42.1）起是单参数，同一个 5.14 版本号下签名不同。已写入 Kbuild 探测的理由。
- **导出情况**：`kthread_create_on_cpu` 在 EL8.10 / 9.8 / 10.2 都有导出（RHEL 8 回移）；上游 5.17 才导出，只有 Ubuntu 5.4 缺它。
- **kABI**：RHEL 9 起按小版本失效（Red Hat 政策）。实测 compat 探针 33 个导入全在稳定列表里，但 CRC 跨小版本在变：EL9 9.4→9.8 有 14 个，EL10 10.0→10.2 有 13 个。**撤回第一轮「EL 可做 kABI kmod」的建议**，EL 统一用 DKMS。
- **DKMS**：模板 `tools/dkms/dkms.conf.example` 在 dkms 2.8.1 / 2.8.7 / 3.0.11 / 3.2.2 × 八个 Ubuntu 内核（5.4 → 7.0）上 build + install 通过，无告警。另记录了 22.04 HWE 6.8 需要 gcc-12 而 headers 包不依赖它的问题。
- **EL 包可用性**（Rocky 仓库目录列表）：
  - xcb-util 全家、xkeyboard-config、libxkbcommon、libjpeg-turbo（`.so.62`）、libicu（EL10 为 74.2）、hwloc-libs（EL10 为 2.11.1）齐全；
  - EL10 没有 Xvfb / Xorg / TigerVNC，有 Xwayland 24.1.9 和 `xwayland-run` 0.0.4；
  - EL8 / EL9 都有 Xvfb 1.20.11。
- **内核配置**：EL8/9/10 的 `CONFIG_X86_MSR=y`（内建），Ubuntu 是 `m`，需要 modules-load.d。
- **EL10 无头显示**：`xwfb-run`（xwayland-run）在 Ubuntu 26.04 上代替 EL10 实测：
  - octool 在 mutter 和 weston 两种无头合成器上都能起来，`xwd -root` 能截整屏；
  - 屏幕大小必须用 Xwayland 参数 `-s '\-geometry' -s WxH` 设（默认 640x480，`-z` 给合成器的尺寸参数不改变 X 屏幕）。
- **顺带发现**：octool 内嵌的 libpci 找不到任何访问方法时，octool 会直接退出（rc=1）。只在 chroot 缺 `/proc`、`/sys` 时遇到。
- **更正第一轮文档**：
  - 节号引用；
  - 6.2 版 .ko 的编译发行版应为 Ubuntu 23.04；
  - `__versions` 条目数；
  - Windows 独有 84 类的分组；
  - AI 功能描述：Windows 调外部 OpenAI 兼容服务，没有内嵌 llama.cpp；Linux 版这些源文件是空单元；
  - 「Linux 独有 19 类」实为 Qt 内部类；
  - ICU/libjpeg 建议改为 Qt `-no-icu -qt-libjpeg`。
- 新增工具：`tools/expcheck.sh`（按源码核对导出）、`tools/kabicrc.sh`（各小版本 kABI CRC 对照）、`tools/dkms/`。

### 2026-09-29 — 适配研究（第一轮），无代码改动

- 盘点 Windows / Linux 两个包；分析 Linux ELF 的依赖与 ABI 地板（glibc 2.35 / GLIBCXX_3.4.29），以及 `mylib` 的问题：
  - 带 glibc；
  - `run_lib.sh` 会毁系统；
  - hwloc 软链损坏；
  - OpenCV 未使用。
- 用户态实测：Ubuntu 20.04 / 22.04 / 24.04 / 26.04 各起一个 chroot；EL8 / 9 / 10 用 Oracle Linux 基础镜像。
- 三个预编译 .ko 与八个 Ubuntu 内核逐符号比对 CRC：一个都加载不了。
- 探针模块在八个 Ubuntu 内核上实编，确定两处兼容改动。
- Windows 版要点与包缺件：`opencv_world4140.dll` 缺失；`Tool_Win7.exe` 导入 Win8.1+ 的 API。
