# octool Linux 多发行版适配分析：el8–el10、Ubuntu 20.04 / 22.04 / 24.04 / 26.04

- 日期：2026-09-29（第二轮修订同日，见文末「修订记录」）
- 分析对象：`OCTool0528.zip`（Windows，`Tool.exe` PE 时间戳 2026-09-06）、`octool-linux.zip`（Linux，`octool` 文件时间 2026-06-14）
- 方法：静态分析（ELF / PE / DWARF / 内核模块 `__versions`）加实测
  - Ubuntu 20.04 / 22.04 / 24.04 / 26.04：debootstrap 建根文件系统，装运行库后跑 `ldd`，再在 Xvfb 下以非 root 用户启动 octool；八个现行内核的 headers 用于 CRC 比对、探针实编和 DKMS 实测
  - EL8 / EL9 / EL10 用户态：Oracle Linux 8.10 / 9.8 / 10.2 官方基础镜像（glibc、libstdc++ 与同一小版本的 RHEL/Rocky 相同）。这个环境访问不到 EL 软件仓库，所以没有在 EL 上装 X 库、跑 GUI
  - EL8 / EL9 / EL10 内核：GitHub 上 CIQ 维护的 Rocky 内核源码树（`ctrliq/kernel-src-tree`，分支 `rocky8_10` = 4.18.0-553.168.1.el8_10、`rocky9_8` = 5.14.0-687.52.1.el9_8、`rocky10_2` = 6.12.0-211.60.1.el10_2），用发行版自带 config 做 `modules_prepare` 后实编探针；kABI 数据取自树内 `redhat/kabi/`
  - EL 包可用性：Rocky 仓库目录列表（网页检索），见各处引用
- 标注：**【实测】** 本次跑过；**【静态】** 由二进制或源码推断；**【待实机】** 需在目标机确认

---

## 0. 结论

1. **Linux 版是 Windows 版的功能子集**【静态】。Linux 版 243 个应用层 QObject 类全部出现在 `Tool.exe` 里；`Tool.exe` 另有 84 个应用类在 Linux 版中没有任何符号（自动调参/自动化向导 49、Qwt 绘图 14、平台与板卡功能 13、依赖 Windows SDK/驱动 5、云与数据 3）。两个包相差约三个月，差异里有一部分是版本先后，不全是平台原因。

2. **用户态的障碍只有两类：ABI 地板和两个随发行版变化的 soname**【实测】。octool 由 Ubuntu 22.04 的 GCC 11.4 编译、静态链接 Qt 5.15.2，需要 glibc ≥ 2.35、`GLIBCXX_3.4.29`。

   | 目标 | 现有二进制 | 缺什么 |
   |---|---|---|
   | Ubuntu 22.04 | ✓ 能起来【实测】 | — |
   | Ubuntu 24.04 | ✓ 带上 ICU 70 能起来【实测】 | `libicu*.so.70` |
   | Ubuntu 26.04 | ✓ 带上 ICU 70 能起来【实测】 | `libicu*.so.70` |
   | EL10 | ABI 满足【实测】，GUI 未跑 | `libicu*.so.70`、`libjpeg.so.8`（EL 只有 `.so.62`）；其余库 EL10 仓库都有 |
   | EL9 | ✗ glibc 层**只差一个符号** `hypot@GLIBC_2.35`【实测】 | 重编 |
   | EL8 | ✗ glibc 2.28、GLIBCXX_3.4.25【实测】 | 重编 |
   | Ubuntu 20.04 | ✗ glibc 2.31、GLIBCXX_3.4.28【实测】 | 重编 |

   在 EL8 上编一次、Qt 用自带第三方库并关掉 ICU，同一个二进制就能覆盖全部七个目标（§7.1）。

3. **三个预编译内核模块在任何现行目标内核上都加载不了**【实测】。它们只对应 6.2.0-20（Ubuntu 23.04）、6.8.0-94（22.04 HWE）、6.17.0-14（24.04 HWE）三个具体内核。内核开着 modversions，加载时逐个比对导入符号的 CRC：同一 6.8 系列的新 ABI（6.8.0-138 / 142）有 13 个不符，6.17.0-42 有 13 个，7.0 有 16 个。模块必须在目标机上编译。

4. **模块源码只需两处兼容改动，已在 11 个内核上验证**【实测】（Ubuntu 八个 + Rocky 8.10 / 9.8 / 10.2）：
   - `class_create` 的参数个数必须**探测头文件**决定。RHEL 9 在小版本之间换了签名：Rocky 9.2（5.14.0-284）还是双参数，9.4（5.14.0-427）起是单参数，而两者 `LINUX_VERSION_CODE` 都是 5.14。按版本号判断在 9.4+ 上必然编译失败。
   - `kthread_create_on_cpu()` 改为 `kthread_create_on_node()` + `kthread_bind()`。三个 EL 内核其实都导出了前者（RHEL 8 做了回移），缺它的只有 Ubuntu 20.04 的 5.4；但统一写法最省事。

5. **EL9/EL10 上不能做「一次编译跨小版本」的 kmod**【实测 + Red Hat 政策】。RHEL 9 起 kABI 稳定列表只在一个小版本内有效。拿探针的 33 个导入符号对 Rocky 的 kABI 列表：EL9 从 9.4 到 9.8 有 14 个 CRC 变过，EL10 从 10.0 到 10.2 有 13 个（`cdev_add`、`device_create`、`class_create`、`kthread_bind`、`wake_up_process` 等每个小版本都变）。所以 EL 上也用 DKMS。同一份 `dkms.conf` 已在 dkms 2.8.1 / 2.8.7 / 3.0.11 / 3.2.2 × 八个 Ubuntu 内核上 build + install 通过（§4.5）。

6. **Secure Boot 机器要让 octool 改走签名模块**【静态】。octool 现在主要靠用户态路径直接访问硬件，这些路径在内核 lockdown 下不成立（§2.3）。只给模块签名、不改 octool 的访问路径，没有用。

7. **EL10 没有 Xorg、Xvfb、TigerVNC**【仓库列表核实】，只有 Xwayland，以及 AppStream 里的 `xwayland-run`（`xwfb-run` 是 xvfb-run 的替代）。`xwfb-run -c mutter -s '\-geometry' -s 1600x1000 -- octool` 已在 Ubuntu 26.04 上实测能跑，mutter 和 weston 都行（§6）。其余六个目标都有 Xvfb 和 TigerVNC。

8. **需要清掉的现有做法**：`run_lib.sh`（unlink 系统 `libc.so.6`，执行后系统不可用）；`mylib` 里的 glibc；损坏的 hwloc 软链（44 字节 `IntxLNK` 文件）；未使用的 OpenCV（50 MB）；`.ko` 放 `/usr/bin` 并靠 `chdir` 加载；`aptinstalls.sh`（开发包 + Fedora 包名）。

---

## 1. 两个包里有什么

### 1.1 Linux 包（`octool-linux/`）

| 文件 | 内容 |
|---|---|
| `octool`（34 MB） | 主程序。x86-64 PIE，`BIND_NOW`，未 strip（有 `.symtab`），只有 NASM 文件 `myasmfunct.asm` 带 DWARF |
| `mylib/`（96 MB，55 项） | 从 Ubuntu 22.04 复制的运行库，另有自编的 hwloc 2.9.1 和 OpenCV 4.14.0-pre |
| `peter_kernel_old.ko` / `peter_kernel.ko` / `peter_kernel_new.ko` | 同一个驱动针对三个内核的构建（§4.1） |
| `run_lib.sh` | 把系统库 unlink 后换成指向 `/mylib` 的软链（**会毁掉系统**，§2.2） |
| `aptinstalls.sh` | 开发环境依赖安装脚本（§9） |
| `com.octool.qt.policy` | polkit 动作，`exec.path=/usr/bin/octool`，`allow_gui=true` |
| `readme.txt` | 写的是「Tested on latest install of ubuntu-23.04」 |

### 1.2 Windows 包（`OCTool0528/`）

| 类别 | 文件 |
|---|---|
| 主程序 | `Tool.exe`（73 MB）、`Tool_Win7.exe`（16 MB） |
| Qt 5.12.12 运行时 | `Qt5{Core,Gui,Widgets,Charts,Bluetooth,Svg,OpenGL}.dll`、`platforms/qwindows.dll`、`styles/`、`imageformats/`、`translations/`、`qwt.dll` |
| 硬件访问驱动 | WinRing0 1.2.0.5（2008）、MSI NTIOLib 3.0.0.7（`AS64.sys` 与 `NTIOLib_X64.sys` 是同一产品）、Intel IPF `ipf_lf.sys` 2.2.10205（2025）、inpout32/64 1.5（2011） |
| 厂商 SDK | CPUID SDK 1.3.3、AMD Ryzen Master SDK（`Device.dll`/`Platform.dll` 2.8.0.1937）、Intel IGCL 1.1.240（`IntelControlLib.dll`/`ControlLib.dll`）、AMI AFU 接口（`amigendrv.dll`） |
| 网络 | libcurl 8.20.0（vcpkg）、lexbor、`z.dll` |
| OCR | `image_txt.dll`（基于 leptonica 1.84.1）、`tdata.dll`（不是 PE，是数据文件） |
| 自带压力 / 发热 / 基准工具 | prime95 26.6、prime95avx、`stress.exe`、`Stress_by_Group.exe`、`Heat_FIVR*.exe`、`heat_gnr.exe`、`heatarl*.exe`、`heats.exe`、`numcores_gnr_sp.exe`、`while*.exe`、`loadall*.exe`、`CB_*.exe`、`GB_BIAS.exe`、`Default.exe`、`Clk_Stretch_detect.exe`、`all_round_bench.exe`、`disocmode.exe` |
| 其他 | `sync.exe`（Sysinternals）、`winspy.exe`、`wsri.dll`（实为 PowerRun 1.6）、`python27.dll`、MinGW 运行库、debug CRT |
| 数据 | `auto_tuner/*.bin`（arrowlake / cyberpunk / dawntrail / wukong 时序模板，以及 ratio/vid/vf/vrm 模板）、`BIOS.CAP`（ROG MAXIMUS XIV FORMULA 的 capsule）、`platforms/cap_header.bin` |

---

## 2. Linux 版剖析

### 2.1 构建与链接【静态】

- 编译器：`GCC: (Ubuntu 11.4.0-1ubuntu1~22.04)`，确认是在 22.04 上编的（readme 里的 23.04 已过时）。
- Qt：5.15.2 **静态**，源码路径 `/media/peter/Ubuntu_Spare/QT_Linux/5.15.2/Src`。静态插件：
  - 平台：xcb（含 GLX）、wayland（含 EGL、XComposite-GLX、xdg-shell v5/v6、wl-shell、ivi、fullscreen-shell）
  - eglfs 的两个设备集成（emulator、KMS EGLDevice），但**没有 eglfs 平台插件本身**：死代码，却带来 `libdrm` 依赖
  - 图像格式：gif / icns / ico / jpeg / tga / tiff / wbmp / webp
  - **没有 offscreen / minimal / vnc / linuxfb**：运行时必须有 X server 或 Wayland 合成器
- 内嵌第三方代码：libpci（pciutils ≥ 3.11）、Qt linuxaccessibility（AT-SPI 桥，sckoct 靠它驱动 octool）、QtBluetooth（Qt btchat 示例改的聊天功能）、qtimageformats 自带的 libtiff/libwebp（2020 年版本）。
- NASM：`myasmfunct.asm`（NASM 2.15.05）是 prime 筛 / FMA 压力内核，按 Win64 调用约定写，被它回调的 C 函数标了 `ms_abi`，这部分与 Windows 共用。
- `DT_NEEDED` 共 45 项（含 `ld-linux`）：
  - glibc / GCC：`libc.so.6 libm.so.6 libstdc++.so.6 libgcc_s.so.1 ld-linux-x86-64.so.2`
  - X / xcb：`libX11 libX11-xcb libxcb libxcb-{glx,icccm,image,shm,keysyms,randr,render-util,render,shape,sync,xfixes,xinerama,xkb,xinput} libSM libICE libXcomposite libxkbcommon libxkbcommon-x11`
  - GL / Wayland / DRM：`libGL libEGL libdrm libwayland-{client,cursor,egl}`
  - 其他：`libfontconfig libfreetype libpng16 libjpeg.so.8 libz libdbus-1 libglib-2.0 libpcre2-16 libudev libicui18n.so.70 libicuuc.so.70 libhwloc.so.15`
- 符号版本需求（`objdump -T`）：

  | 版本 | 符号 |
  |---|---|
  | GLIBC_2.35 | `hypot` |
  | GLIBC_2.34 | `__libc_start_main dlopen dlsym dlclose dlerror pthread_create pthread_cancel pthread_once pthread_key_create pthread_key_delete pthread_setspecific pthread_testcancel pthread_attr_setstacksize pthread_condattr_setclock` |
  | GLIBC_2.33 | `stat stat64 fstat fstat64 lstat64` |
  | GLIBC_2.32 | `__libc_single_threaded` |
  | GLIBC_2.29 | `exp log log2 pow` |
  | GLIBCXX | 最高 3.4.29；CXXABI 最高 1.3.9 |

  这些都是「在新 glibc 上编译就被绑定到新版本」的符号，代码没用任何新接口。在 glibc 2.28 上重编，它们会自动落到旧版本，不用改代码。

### 2.2 `mylib/` 的问题【静态】

- 覆盖 45 项 NEEDED 中的 35 项；另外 10 项必须来自系统：`libudev libdrm libwayland-egl libXcomposite libdbus-1 libwayland-cursor libwayland-client libpcre2-16 libglib-2.0 ld-linux`。
- 带了 `libc.so.6`（`Ubuntu GLIBC 2.35-0ubuntu3.1`）和 `libm.so.6`。一个进程只能有一个 glibc：用 mylib 的 libc 就得用它配套的 `ld-linux`，而那 10 个系统库是按系统 glibc 链接的。这个缺口 mylib 补不上。
- `run_lib.sh` 先执行 `sudo unlink /lib/x86_64-linux-gnu/libc.so.6`，再 `sudo ln -s /mylib/libc.so.6 …`。第一步之后 `sudo` 自己就起不来了，第二步不会执行，系统从此不可用。不能再随包分发。它还写死了 Debian 的库路径，在 EL 上路径也不对。
- ICU 70（34 MB）和 mylib 里其他库一样按 22.04 的 glibc 编（最高要求 GLIBC_2.34），在 EL8 上连 ICU 本身都加载不了。
- OpenCV 4.14.0-pre（50 MB，`RUNPATH=/home/peter/Transcend_P1/opencv2/lib`）：octool 的 NEEDED 里没有，字符串里也没有 `opencv`，即既不链接也不 dlopen。要么给别的组件用，要么是遗留，需要确认。
- hwloc 是自编 2.9.1。octool 只用 `hwloc_topology_init/load/destroy`、`hwloc_get_type_depth`、`hwloc_get_nbobjs_by_depth` 五个函数，hwloc 2.x 都有。各目标自带的 `libhwloc.so.15` 都能用：Ubuntu 20.04 的 2.1.0 到 26.04 的 2.13.0，EL10 的 hwloc-libs 2.11.1。
- `libhwloc.so` / `libhwloc.so.15` 是 `IntxLNK` + UTF-16 目标名的 44 字节文件（Cygwin/Windows 模拟的符号链接），不是 ELF。包经 Windows 转手软链就坏了，Linux 包必须在 Linux 上用 tar 打。

### 2.3 硬件访问路径与 lockdown【静态，反汇编确认】

对适配来说只需要一个结论：**octool 现在主要靠用户态直接访问硬件，这类路径在开启内核 lockdown（Secure Boot 默认开启 lockdown）的机器上不成立。**

- MSR 读写走 `/dev/cpu/N/msr`；`msr` 驱动不在时**静默失败**，界面不报错，建议补提示。
- 一部分寄存器 / EC / VRM 操作走用户态端口 I/O（`MainWindow` 构造时申请 I/O 权限）。
- 未加载内核模块时，MMIO 走 `/dev/mem`；PCI 配置走 libpci（sysfs / `/proc/bus/pci`）。

`peter_kernel` 模块本身已经实现了 MSR、端口、EC、PCI、MMIO 这些操作。所以 Secure Boot 机器上的正确做法是：**octool 检测到 lockdown（读 `/sys/kernel/security/lockdown`）时，统一改走已签名的模块。** 只签模块、octool 仍走用户态路径，lockdown 下依然不工作。非 Secure Boot 机器上用户态路径可用。

### 2.4 目标内核里与 octool 相关的配置【实测：读各内核 config】

| | Ubuntu 5.4 → 7.0 | EL8.10 / EL9.8 / EL10.2 |
|---|---|---|
| `CONFIG_X86_MSR` | `m`：要 `modprobe msr`（octool 不会自己加载） | `y`：内建，`/dev/cpu/*/msr` 总在 |
| lockdown | 仅在 Secure Boot 下启用（`LOCK_DOWN_IN_(EFI_)SECURE_BOOT=y`） | 同左 |
| 模块签名 | `MODULE_SIG=y`，`MODULE_SIG_FORCE` 未开：非 Secure Boot 下未签名模块可加载（会 taint） | 同左 |

打包时 Ubuntu 需要一条 `/etc/modules-load.d/octool.conf`（内容 `msr`），EL 不需要。

---

## 3. Windows 版要点【静态】

- `Tool.exe`：x64，MSVC 2019（链接器 14.29），Qt **5.12.12** 动态链接（Linux 版是 5.15.2 静态，两版 Qt 不同）。另外导入 qwt、libcurl、lexbor、`opencv_world4140.dll`，以及系统的 `wevtapi`（事件日志）、`WINTRUST`、`SETUPAPI/CFGMGR32`、`WS2_32`。
- `Tool.exe` 的 `.rdata` 有 53.4 MB，而 Linux `octool` 连同静态 Qt 的 `.rodata` 才 3.1 MB：Windows 版多带了约 50 MB 常量数据。
- 联网 / AI：`Tool.exe` 里有 OpenAI 兼容接口地址 `https://api.openai.com/v1/chat/completions`、一段 llama.cpp 服务端风格的示例响应（模型 `Qwen3.6-27B-Q6_K.gguf`）以及 `llama_cpp_json_arr` 之类的配置键名，**但没有 ggml/llama 运行时符号**。所以它是通过 libcurl 调外部的 OpenAI 兼容服务（比如 llama.cpp server），不是内嵌模型。Linux 版里 `ai_agent.cpp`、`amd_ai.cpp`、`json_repair.cpp`、`repair_llm_output.cpp`、`kvm_parser.cpp`、`load_scripts.cpp` 都编成了空单元（只有 iostream 静态初始化），这组功能在 Linux 上是编译期关掉的。
- `Tool_Win7.exe` 是删减版：没有 libcurl / lexbor / OpenCV / Bluetooth，也没有自动化向导、蓝牙聊天和 `cloud_widget`。

**Windows 包自身的问题**：

| 问题 | 依据 | 后果 |
|---|---|---|
| `Tool.exe` 硬导入 `opencv_world4140.dll`，包里没有 | 导入表（非延迟加载）；递归检查 13 个可达 PE，缺的只有这一个 | 目标机上没有这个 DLL 时 `Tool.exe` 起不来 |
| `Tool_Win7.exe` 硬导入 `GetDpiForMonitor`（`api-ms-win-shcore-scaling-l1-1-1.dll`，Windows 8.1 起才有） | 导入表 | 按导入表判断在 Windows 7 上无法加载【待 Win7 实机】。`Tool.exe` 还导入 `SetProcessDpiAwarenessContext`（Windows 10 1703 起） |
| `ocp_dll.dll` 需要 `mfc140u.dll`，`python27.dll` 需要 `MSVCR90.dll`，包里都没带 | 导入表 | 依赖目标机已装对应的 VC++ 运行库 |
| debug CRT（`msvcp140d.dll`、`vcruntime140d.dll` 等）、MinGW 运行库（`libstdc++-6.dll` 等） | 包内没有任何二进制导入它们（MinGW 运行库只互相依赖） | 死重 |
| WinRing0 | Microsoft Defender 把它标记为 `VulnerableDriver:WinNT/Winring0` | 开着 Defender 的机器上可能被隔离 |

---

## 4. 内核模块

### 4.1 三个 .ko 的身份【静态】

| 文件 | vermagic | 编译器 | `__versions` 条目 |
|---|---|---|---|
| `peter_kernel_old.ko` | `6.2.0-20-generic` | GCC 12.2.0-17ubuntu1（Ubuntu 23.04） | 36 |
| `peter_kernel.ko` | `6.8.0-94-generic` | GCC 12.3.0（22.04） | 37 |
| `peter_kernel_new.ko` | `6.17.0-14-generic` | GCC 13.3.0（24.04） | 39（另有 `__version_ext_*` 扩展表） |

三个都**没有模块签名**，都是 `/home/peter/Documents/kernel_land_test_mod/kernel_module/peter_kernel.c` 针对不同内核的构建（后两个的 srcversion 相同）。6.2 那个的 `__versions` 用的是 Ubuntu 23.04 为 Rust 加的变长布局，解析要单独处理（`tools/crccheck.py` 已兼容）。

### 4.2 为什么加载不了【实测】

内核开着 `CONFIG_MODVERSIONS` 时会逐个比对模块导入符号的 CRC 与运行内核的 `Module.symvers`，有一个不符或缺失就拒绝加载（`disagrees about version of symbol`）。把三个 .ko 和八个 Ubuntu 内核逐符号比对：

| .ko | 能加载的内核 | 其余 |
|---|---|---|
| `peter_kernel.ko`（6.8.0-94） | 仅 6.8.0-94 | 6.8.0-138 / 142 各 13 个 CRC 不符；其他内核更多 |
| `peter_kernel_new.ko`（6.17.0-14） | 仅 6.17.0-14 | 6.17.0-42 有 13 个不符；7.0 有 16 个 |
| `peter_kernel_old.ko`（6.2.0-20） | 无（没有现行内核是 6.2） | 全部拒绝 |

同一系列的小版本更新照样改 CRC。EL 内核（4.18 / 5.14 / 6.12）与之无关。**结论：不能再发预编译 .ko。**

### 4.3 源码的两处兼容改动【实测：11 个内核】

探针模块（`tools/kmod-probe/`，只调用 peter_kernel 导入的那组接口，不是 peter_kernel.c 本身）三个变体：`asis` = 现在 6.8/6.17 构建的写法（单参数 `class_create` + `kthread_create_on_cpu`）；`old` = 6.2 构建的写法（双参数）；`compat` = 探测头文件决定参数个数 + `kthread_create_on_node`/`kthread_bind`。

| 内核 | asis | old | compat |
|---|---|---|---|
| Ubuntu 5.4.0-216（20.04） | ✗ 参数个数 | ✗ `kthread_create_on_cpu` 未导出 | ✓ |
| Ubuntu 5.15.0-139 / -194（20.04 HWE / 22.04） | ✗ 参数个数 | ✓ | ✓ |
| Ubuntu 6.8.0-138（22.04 HWE，需 gcc-12） | ✓ | ✗ 参数个数 | ✓ |
| Ubuntu 6.8.0-142 / 6.17.0-42 / 7.0.0-34（24.04 各内核） | ✓ | ✗ | ✓ |
| Ubuntu 7.0.0-34（26.04） | ✓ | ✗ | ✓ |
| **Rocky 8.10**（4.18.0-553.168.1） | ✗ 参数个数 | ✓ | ✓ |
| **Rocky 9.8**（5.14.0-687.52.1） | ✓ | ✗ 参数个数 | ✓ |
| **Rocky 10.2**（6.12.0-211.60.1） | ✓ | ✗ 参数个数 | ✓ |

- `class_create` 在 EL9 各小版本上的签名（直接读各分支的 `include/linux/device/class.h`）：9.2（5.14.0-284.30.1）双参数；9.4（427.42.1）、9.5、9.6、9.7、9.8 单参数。**同一个 5.14 版本号下签名不同**，只能探测。探针的 `Kbuild` 用 grep 头文件的办法，EL 和 Ubuntu 上都判断正确。
- 三个 EL 内核的探针导入符号全部在树内有 `EXPORT_SYMBOL`（按源码 grep，`tools/expcheck.sh`）。`kthread_create_on_cpu` 在 EL8.10 / 9.8 / 10.2 都导出了；上游 5.17 才导出，Ubuntu 5.4 没有。
- EL 的构建用的是 Ubuntu chroot 里的 GCC 9 / 11 / 14，不是 RHEL 自己的 GCC 8.5 / 11.5 / 14.3；GCC 9 编 4.18 时要加 `KCFLAGS=-Wno-error=address-of-packed-member`。这些只影响编译器告警，不影响接口结论。没有完整编内核，所以没有 `Module.symvers`；「是否导出」靠源码 grep 判断。

### 4.4 EL 上 kmod 只能按小版本出【实测 + Red Hat 政策】

Red Hat 的 RHEL 9 kABI 政策写明：kABI「现在对 RHEL 9 的每个小版本唯一」，「上一个小版本里稳定不代表下一个小版本稳定」；RHEL 7/8 时代的稳定列表才是整个大版本有效。

用 Rocky 树内 `redhat/kabi/kabi-module/kabi_x86_64/` 验证（`tools/kabicrc.sh`）：

- 探针在 9.8、10.2 上的 33 个导入符号**全部在稳定列表里**；
- 但 CRC 跨小版本在变：EL9 从 9.4 → 9.5 → 9.6 → 9.7 → 9.8，33 个里有 14 个变过；EL10 从 10.0 → 10.1 → 10.2 有 13 个变过。`cdev_add/cdev_del/cdev_init`、`class_create/class_destroy`、`device_create/device_destroy`、`kmalloc_caches`、`kthread_bind`、`kthread_create_on_node`、`wake_up_process` 几乎每个小版本都变；`kfree`、`ioremap`、`copy_*_user` 等不变。`class_create` 在 9.4 的列表里还没有，9.6 才进。

所以在 EL9/EL10 上，为 9.6 编的 kmod 装到 9.8 上会被拒绝。要么每个小版本出一个 kmod，要么用 DKMS。EL8 已经停在最后一个小版本 8.10，按 RHEL 8 的政策稳定列表在大版本内有效；但 8.10 树里没带稳定列表文件，这里没法核对【待实机：`kernel-abi-stablelists` 包】。**建议 EL8/9/10 统一用 DKMS。**

### 4.5 DKMS 模板实测【实测】

`tools/dkms/dkms.conf.example`（用于 peter_kernel）与测试用的 `pkprobe` 版本除了名字完全相同。测试用法是 `dkms add` 后对每个内核执行 `dkms build -k <kver>` 和 `dkms install -k <kver>`：

| 发行版 | dkms 版本 | 内核 | 结果 |
|---|---|---|---|
| 20.04 | 2.8.1 | 5.4.0-216、5.15.0-139 | build + install ✓ |
| 22.04 | 2.8.7 | 5.15.0-194、6.8.0-138 | ✓（6.8 HWE 需要 gcc-12） |
| 24.04 | 3.0.11 | 6.8.0-142、6.17.0-42、7.0.0-34 | ✓ |
| 26.04 | 3.2.2 | 7.0.0-34 | ✓ |

同一个 `dkms.conf` 在 2.8 和 3.x 上都没有告警，`DEST_MODULE_LOCATION` 和 `CLEAN` 两种版本都接受。实测中注意到两点：

- **22.04 + HWE 6.8**：内核要求 gcc-12，但 `linux-headers-6.8.0-*-generic` 不依赖它，缺了就报 `/bin/sh: 1: gcc-12: not found`。dkms 包需要 `Depends: gcc-12`，或在 postinst 里检查。
- **签名**：Ubuntu 的 dkms 靠 `update-secureboot-policy`（shim-signed 包）生成 MOK 并签名；没有它时提示「modules won't be signed」，照样安装未签名模块。EL 的 dkms 3.x 用 `/var/lib/dkms/mok.key`。两边都要用户执行一次 `mokutil --import` 并重启确认【待实机】。
- 在 chroot 里构建 7.0 内核的模块，需要挂上 `/proc`（objtool 会读它）。真机上不会遇到。

---

## 5. 用户态：各目标实测与包可用性

### 5.1 现有二进制（仅补 ICU 70）

| 目标 | glibc | libstdc++ 的 GLIBCXX 上限 | `ldd` 结果 |
|---|---|---|---|
| Ubuntu 20.04 | 2.31 | 3.4.28 | ✗ 缺 GLIBC_2.32/2.33/2.34/2.35、GLIBCXX_3.4.29【实测】 |
| Ubuntu 22.04 | 2.35 | 3.4.30 | ✓ 全解析，Xvfb 下能起来【实测】 |
| Ubuntu 24.04 | 2.39 | 3.4.33 | ✓（系统 ICU 是 74，需带 70）【实测】 |
| Ubuntu 26.04 | 2.43 | 3.4.35 | ✓（需带 ICU 70）【实测】 |
| EL8（OL 8.10） | 2.28 | 3.4.25 | ✗ 缺 GLIBC_2.32/2.33/2.34/2.35、GLIBCXX_3.4.26/28/29【实测】 |
| EL9（OL 9.8） | 2.34 | 3.4.29 | ✗ glibc 层只缺 `hypot@GLIBC_2.35`（`readelf -V` 确认 libm 不定义 GLIBC_2.35）【实测】 |
| EL10（OL 10.2） | 2.39 | 3.4.33 | ABI 满足【实测】；缺 `libjpeg.so.8`、ICU 70 |

启动实测：22.04 / 24.04 / 26.04 上以非 root 跑 octool，进程进入事件循环，打印 `Can't read memory from /dev/mem`（沙箱无权限，属预期），弹出「Not supported!」（沙箱是虚拟 Xeon，属预期）。GUI 栈、字体、xcb 插件完整可用。

### 5.2 两个随发行版变化的 soname

| soname | Ubuntu 20.04–26.04 | EL8 / EL9 / EL10 | 处理 |
|---|---|---|---|
| `libicu*.so.NN` | 66 / 70 / 74 / 78 | 60 / 67 / 74（EL8/9 的版本号未逐一核对，但都不是 70） | Qt `-no-icu` |
| `libjpeg.so.N` | `.so.8`（libjpeg-turbo8）；`.so.62` 只有 universe 里老的 IJG 6b | `.so.62`（libjpeg-turbo 3.0，EL10） | Qt `-qt-libjpeg` |

其余 NEEDED 的 soname 在七个目标上都相同：xcb 系列、X11、xkbcommon、fontconfig、freetype、dbus、glib、GL/EGL、wayland、udev、z、png16、pcre2-16，以及 hwloc 的 `.so.15`。

### 5.3 EL 上的包（Rocky 仓库目录列表）

| 包 | EL8.10 | EL9.8 | EL10 |
|---|---|---|---|
| xcb-util / -image / -keysyms / -renderutil / -wm | ✓ AppStream | ✓ AppStream | ✓ AppStream（0.4.1 / 0.3.10 / 0.4.2） |
| xkeyboard-config | 2.28 | 2.33 | 2.41 |
| libxkbcommon(-x11)、libSM、libICE、libXcomposite、libX11、libxcb、libglvnd | ✓ | ✓ | ✓ AppStream（源码包列表） |
| libjpeg-turbo | ✓（`.so.62`） | ✓（`.so.62`） | 3.0.2（`.so.62`） |
| libicu | 60 | 67 | 74.2（BaseOS） |
| hwloc-libs | ✓ | ✓ | 2.11.1（BaseOS） |
| libpng / libdrm | ✓ | ✓ | 1.6.40 / 2.4.128（BaseOS） |
| Xvfb | 1.20.11（AppStream） | 1.20.11（AppStream） | **无** |
| Xorg | 有 | 有 | **无** |
| Xwayland | 21.1.3 | 24.1.9 | 24.1.9 |
| xwayland-run（`xwfb-run`） | — | — | 0.0.4（AppStream） |
| dkms | EPEL | EPEL | EPEL（3.4.2） |

EL8/EL9 的 ICU、hwloc 具体版本号没有逐一核对；表中「✓」是包存在。

---

## 6. GUI / 显示

octool 静态编入的平台插件只有 xcb 和 wayland，所以必须有 X server 或 Wayland 合成器。

| 目标 | 无头运行 |
|---|---|
| Ubuntu 20.04–26.04 | Xvfb（各版本都有：1.20.13 / 21.1.4 / 21.1.12 / 21.1.22），TigerVNC 也都有 |
| EL8 / EL9 | Xvfb（AppStream），TigerVNC（AppStream） |
| EL10 | 没有 Xvfb、Xorg、TigerVNC。可选 `xwfb-run`（xwayland-run 包，AppStream）：它在无头合成器上跑全屏的 rootful Xwayland，设计上就是 xvfb-run 的替代。支持 weston、cage、mutter、gnome-kiosk，默认 weston；EL10 仓库里 mutter 一定有，weston 未核实。也可以继续用容器后端 |

**`xwfb-run` 实测**（在 Ubuntu 26.04 上代替 EL10，xwayland-run 0.0.5、Xwayland 24.1.10、mutter 50.1、weston 14；EL10 是 xwayland-run 0.0.4、Xwayland 24.1.9，机制相同）【实测】：

```sh
xwfb-run -c mutter -s '\-geometry' -s 1600x1000 -- octool    # 或 -c weston
```

- octool 在 mutter 和 weston 两种无头合成器上都能起来，行为与 Xvfb 下相同。`xwd -root` 能截到整屏，所以 sckoct 的根窗口截图做法照样可用。
- 屏幕大小要通过 Xwayland 参数设：`-s '\-geometry' -s WxH`，每个参数单独一个 `-s`，前导 `-` 要用 `\` 转义。不设时默认 640x480。用 `-z` 传给合成器的尺寸参数（weston `--width/--height`、mutter `--virtual-monitor`）被接受了，但不改变 X 屏幕大小。
- 以普通用户运行时需要设置 `XDG_RUNTIME_DIR`。EL10 本身【待实机】。

桌面会话里用 pkexec 以 root 起 GUI：X 会话下 polkit 的 `allow_gui` 会保留 `DISPLAY`/`XAUTHORITY`。Wayland 会话（EL9/EL10、Ubuntu 22.04+ 默认）经 Xwayland 时，root 可能被拒绝连接；Ubuntu 上有 Xwayland 不用 `XAUTHORITY` 导致 root 程序打不开窗口的已知问题。常见做法是启动前 `xhost +SI:localuser:root`、结束后撤销【待实机】。

---

## 7. 建议的构建与打包策略

### 7.1 用户态：EL8 上编一次，覆盖七个目标

- 构建机：EL8（glibc 2.28）+ gcc-toolset（C++17 足够）。glibc 向后兼容，2.28 上编出的二进制在 2.31 / 2.34 / 2.35 / 2.39 / 2.43 上都能跑。gcc-toolset 会把新版 libstdc++ 的符号静态链进来，二进制只要求系统的 GLIBCXX_3.4.25，七个目标都满足（§5.1 的上限列）。
- Qt 5.15 继续静态链接。EL10 没有 Qt5，各发行版 Qt5 版本也不同，静态链接正好避开。建议换到 5.15 系列较新的 LGPL 源码（Ubuntu 26.04 用的是 5.15.18），5.15.2 在新 GCC 下有已知的头文件缺失编译问题。
- Qt configure 里去掉两个随发行版变的 soname：`-no-icu`、`-qt-libjpeg`（§5.2）。`-qt-libpng`、`-qt-pcre` 可选。同时可以去掉死代码和用不到的依赖：
  - 不编 eglfs 相关（去掉 `libdrm`）；
  - 不需要 Wayland 原生插件时 `-skip qtwayland`：GNOME 上 Qt5 默认忽略 Wayland 会话走 Xwayland，pkexec 以 root 起时也连不上用户的 Wayland socket；
  - 图像格式只留用到的。
- **保留 AT-SPI**（`accessibility`、`dbus`），sckoct 靠它驱动 octool。
- 系统库一律依赖发行版包，不再自带；`mylib` 只剩过渡期可能需要的 ICU 70。
- 建议加一个 ABI 门禁：`objdump -T octool` 的 GLIBC 最高版本 ≤ 2.28、GLIBCXX ≤ 3.4.25，NEEDED ⊆ 白名单。门禁要按惯例做变异验证：故意在 22.04 上编一次，门禁必须变红。

### 7.2 内核模块：DKMS

- `peter_kernel.c` 按 §4.3 改两处；设备名 `/dev/mydev` 太通用，建议改成带 octool 前缀的名字。
- 出 `octool-dkms`（noarch）包，内容为源码 + `dkms.conf`（模板见 `tools/dkms/`）。依赖：
  - Ubuntu：`dkms`、`linux-headers-$(uname -r)`、`gcc`、`make`；22.04 HWE 额外需要 `gcc-12`
  - EL：`dkms`（EPEL）、`kernel-devel-$(uname -r)`、`gcc`、`make`、`elfutils-libelf-devel`
- 不再在 EL9/EL10 上考虑 kABI kmod（§4.4）。
- octool 加载模块的方式改为：先看设备节点，没有就 `modprobe`（模块在 `/lib/modules/$(uname -r)/updates/dkms/`），不再 `init_module` 读自己目录下的 .ko，也不再 `chdir`。
- Secure Boot：DKMS 签名 + MOK 注册，并配合 §2.3 让 octool 在 lockdown 下走模块。

### 7.3 打包格式与命名

- 源码 tar 按现有归档风格：点分版本 + `-src`（如 `octool-<x.y.z>-src.tar.gz`），不用下划线式。
- 二进制包：EL 出 rpm（el8 / el9 / el10），Ubuntu 出 deb（focal / jammy / noble / resolute）。用户态同一个二进制，差别只在依赖声明。
- polkit：`com.octool.qt.policy` 里 `exec.path` 必须等于实际安装路径（现在写死 `/usr/bin/octool`），action id、描述文字还是模板内容，建议改正。
- Ubuntu 包带 `/etc/modules-load.d/octool.conf`（`msr`）；EL 不需要（§2.4）。

---

## 8. Windows 版 vs Linux 版功能差异【静态】

`Tool.exe` 有而 Linux `octool` 没有任何符号的 84 个应用类：

| 类别 | 数量 | 类 |
|---|---|---|
| 自动调参 / 自动化向导 | 49 | `Action_Page_*`（22：截屏、OCR、键鼠输入、进程亲和、等待窗口…）、`Trigger_Page_*`（22：msr / pci / mmio / vid / vrm / co / timing / ratio / smu / loadline / ecram / ecsmb…）、`trigger_wizard`、`intervene_widget`、`auto_tuner`、`auto_tuner_worker`、`auto_tuner_wdt` |
| Qwt 绘图 | 14 | `Plot`、`Canvas`、`Knob`、`Wheel`、`WheelBox`、`CurveData{,2,3}`、`SamplingThread{,2,3}`、`SignalData{,2,3}` |
| 平台 / 板卡功能 | 13 | `arl_tuners`、`gnr_tuners`、`gnr_odt`、`nvl_mb3`、`vrm3701`、`vrmread2`、`vrmread_tr5`、`tr5vrm_Reader_Thread`、`rw_ecram_Reader_Thread`、`vfpoints_arc`、`stress_mem`、`stress_mem_worker`、`mainwindow5` |
| 依赖 Windows SDK / 驱动 | 5 | `CPUIDSDK`、`intel_ipf`、`bios_save`、`bios_save_Thread`、`bios_save_Thread2` |
| 云 / 数据 | 3 | `cloud_widget`、`json_creator`、`TreeModel` |

- 自动化向导整组依赖 Windows 的键鼠注入、窗口、截图和 OCR（tesseract/leptonica），移植到 Linux 等于重写（X11/AT-SPI/xdotool 一套）。
- Qwt 本身跨平台；Linux 版没编入 Qwt，静态链接的是 QtCharts。
- 平台 / 板卡那 13 个没有看到 Windows 专属导入，是移植的首选候选；其中哪些只是这三个月的新增，需要对照源码确认。
- Linux 版没有独有的应用类。按 vtable 比较多出来的 19 个全是静态链接进来的 Qt 内部类（AT-SPI 桥、缓动曲线、TGA 读取等），Windows 版的对应部分在 Qt DLL 里。
- Linux 版仍引用 `loadall.exe`、`loadall2.exe`、`Stress_by_Group.exe`、`Cinebench.exe`、`Rd_ECSMB_*.cmd` 等 Windows 文件名，也引用 `HEDT_EQ/*.csv`、`OG_*.csv`、`BIOS.CAP` 等数据文件，但 Linux 包里都没有。对应界面入口在 Linux 上是死路径，应隐藏或补齐。

---

## 9. 需要清理 / 修正的现状

| 项 | 现状 | 建议 |
|---|---|---|
| `run_lib.sh` | unlink 系统 `libc.so.6`，执行后系统不可用 | 从发布包移除 |
| `mylib` 带 glibc | 两个 glibc 不能共存，缺口也补不上 | 依赖系统库；Qt 去掉 ICU/libjpeg 依赖后不再需要 mylib |
| hwloc 软链 | 44 字节 `IntxLNK`，已损坏 | Linux 包在 Linux 上 tar；hwloc 用系统包 |
| OpenCV 50 MB | 当前 octool 不用 | 确认无组件需要后删除 |
| `.ko` 放 `/usr/bin` | 三个预编译 .ko，只匹配三个内核 | 改 DKMS |
| `chdir` 到程序目录 | 为了加载 .ko；这是进程级的，之后的相对路径文件（`log.txt`、`record.csv`、`profile.csv` 等出现在字符串里）会落到程序目录 | 去掉 `chdir`，数据写到明确的目录【源码确认】 |
| `msr` 驱动缺失 | 静默失败 | 检测 `/dev/cpu/0/msr`，缺失时提示；Ubuntu 包加 modules-load.d |
| pci.ids 路径 | 内嵌 libpci 编成了 `/usr/local/share/pci.ids.gz`；EL 在 `/usr/share/hwdata/pci.ids`，Ubuntu 在 `/usr/share/misc/pci.ids` | 显式设置 id 文件路径（只影响设备名显示） |
| `aptinstalls.sh` | 装开发包，还混入 Fedora 包名 | 运行时依赖由 rpm/deb 声明 |
| polkit | 写死 `/usr/bin/octool`，模板文字 | 与安装路径一致，改 id 和文字 |

---

## 10. 落地顺序

1. 改 `peter_kernel.c` 两处（§4.3）。EL 和 Ubuntu 的编译兼容性已验证，剩下的是在真机上**加载**并跑一遍读写（这里没有硬件）。
2. octool：加载模块改走 `modprobe`，去掉 `chdir`；lockdown 下改走模块（§2.3）；`msr` 缺失提示。
3. EL8 构建环境：静态 Qt（`-no-icu -qt-libjpeg`，保留 AT-SPI）+ 系统库，出一个二进制，加 ABI 门禁（§7.1）。
4. `octool-dkms` 包（§7.2，模板已实测）。
5. 出 rpm / deb；源码 tar 用点分版本 + `-src`。
6. EL10 无头显示：用 `xwfb-run -c mutter -s '\-geometry' -s <WxH>`（Ubuntu 26.04 上已验证），EL10 实机确认后可以替代容器（§6）。

## 11. 仍待实机确认的项

| 项 | 为什么这里做不了 | 怎么确认 |
|---|---|---|
| 模块在真机上加载、读写正常 | 没有硬件；EL 只做了编译 | 各目标 `modprobe` + octool 实操 |
| EL 上用 RHEL 自带 GCC 编译 | 用的是 Ubuntu 的 GCC | 实机 `dnf install kernel-devel-$(uname -r)` 后跑 `tools/kmod-probe` |
| EL8 kABI 稳定列表覆盖 | 8.10 树里没有列表文件 | `dnf install kernel-abi-stablelists`，按 kmod-probe README 核对 |
| EL 上 GUI 实际运行 | 访问不到 EL 仓库，装不了 X 库 | 实机装包后启动 |
| EL10 `xwfb-run -c mutter` | 机制已在 Ubuntu 26.04 上实测通过；EL10 的包版本略旧（xwayland-run 0.0.4） | 实机跑 §6 的命令 |
| Wayland 会话下 pkexec 起 GUI | 没有桌面会话 | 实机；不行就用 `xhost +SI:localuser:root` |
| `Tool_Win7.exe` 在 Windows 7 上的加载 | 只有导入表证据 | Win7 实机 |
| Secure Boot 下 DKMS 签名 + MOK 注册 | 沙箱没有 Secure Boot | 实机 |

---

## 附录：实测环境与可复现命令

- 沙箱：Ubuntu 24.04 x86-64；可访问 archive.ubuntu.com、GitHub、PyPI；EL 仓库、docker.io、conda 不可达。
- Ubuntu 根文件系统：`debootstrap --variant=minbase focal|jammy|noble|resolute <dir> http://archive.ubuntu.com/ubuntu`。
- EL 用户态：`oracle/container-images` 仓库 `dist-amd64` 分支的 `8|9|10/oraclelinux-*-amd64-rootfs.tar.xz`，`docker import` 后跑 `ldd`。
- EL 内核树：`git clone --depth 1 --single-branch --branch rocky8_10|rocky9_8|rocky10_2 https://github.com/ctrliq/kernel-src-tree`；`cp configs/kernel-x86_64{,-rhel}.config .config && make olddefconfig && make modules_prepare`（EL8 用 GCC 9 时加 `KCFLAGS=-Wno-error=address-of-packed-member`；EL10 用 `CC=gcc-14`）。其他小版本只取单个文件：`git fetch --depth 1 --filter=blob:none origin <branch>`，再 `git show origin/<branch>:<path>`。
- Ubuntu 内核 headers：`apt-get download linux-headers-<ver>-generic`。
- chroot 里复现时要挂上 `/proc`、`/sys`、`/dev`：
  - 缺 `/proc` 时，octool 内嵌的 libpci 报 `Cannot find any working access method`，然后**直接退出（rc=1）**；
  - 7.0 内核的 objtool 也要读 `/proc`。
- 工具（`tools/`）：
  - `crccheck.py`：解析 .ko 的 `__versions`（含 Ubuntu 6.2 的变长布局），与各内核 `Module.symvers` 比对 CRC
  - `kmod-probe/`：探针模块，三个变体
  - `expcheck.sh <内核树> <ko>`：.ko 的每个导入符号在树里有没有 `EXPORT_SYMBOL`
  - `kabicrc.sh <rocky 树> <ko> <分支…|HEAD>`：导入符号在各小版本 kABI 稳定列表里的 CRC
  - `dkms/dkms.conf.example`：DKMS 模板

## 修订记录

- 2026-09-29 第一轮：包盘点、Linux ELF/依赖、三个 .ko 的 CRC 比对、Windows 版要点、Ubuntu 四版本实测、探针在 Ubuntu 八个内核上实编。
- 2026-09-29 第二轮：
  - Rocky 8.10 / 9.8 / 10.2 内核树实编探针；
  - EL9 `class_create` 在 9.2 与 9.4 之间改签名；
  - EL8.10 也导出 `kthread_create_on_cpu`；
  - EL9/EL10 kABI 按小版本变化（14/33、13/33），据此撤回「EL 可用 kABI kmod」的建议；
  - EL 包可用性（EL10 无 Xvfb/Xorg，有 xwayland-run）；
  - DKMS 模板在四个 dkms 版本 × 八个内核上实测；
  - 各目标内核 `CONFIG_X86_MSR` 差异；
  - `xwfb-run` 在 Ubuntu 26.04 上以 mutter / weston 无头运行 octool 实测通过，并确认屏幕大小要用 Xwayland 的 `-geometry` 设。

  同时更正第一轮的几处错误：
  - §0 里指向「§7.1」的节号；
  - 6.2 版 .ko 的编译器归属应为 Ubuntu 23.04，不是 22.10；
  - `__versions` 条目数；
  - 84 个 Windows 独有类的分组（`intervene_widget` 属于自动化向导，`TreeModel` 属于云/数据，`intel_ipf`/`bios_save` 属于 Windows SDK/驱动）；
  - AI 功能一段：`Tool.exe` 调的是外部 OpenAI 兼容服务，没有内嵌 llama.cpp；Linux 版这些源文件是空单元，也没有对应类；
  - 「Linux 独有 19 类」其实全是 Qt 内部类；
  - 自带 ICU 的建议改为 Qt `-no-icu` + `-qt-libjpeg`。
