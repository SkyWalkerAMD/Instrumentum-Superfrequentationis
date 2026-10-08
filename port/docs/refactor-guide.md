> 2026-09-30 更新：本文保留输入包的历史设计与验证记录。当前范围、修正、构建方式与实际状态见 [../../docs/README.md](../../docs/README.md)。历史 HAL 接入建议不代表本轮已改 GUI；本轮不承诺旧 GUI 全部直接硬件访问在 lockdown 下可用。对拍无稳定可读地址时返回 2，采集会影响时序，不能把易失项当成等价证明。

> 2026-10-08 校正：原 GUI 的模块初始化还依赖自身 MY_KMOD_LOADED 标志及程序目录的旧 .ko 查找流程，
> 不能由提前 modprobe 与 /dev/mydev 存在推断它已采用新模块。MMIO ABI 兼容结论不变。
> 下文“后三条全部被拦”也过于笼统：MSR 读、写的权限检查不同，须区分目标内核与 HAL 自身策略。
> 新的指令证据和边界见 [启动权限分析](../../docs/legacy-el-privilege-analysis.md)。

# octool 多发行版重构：硬件访问层落地指南

面向 el8–el10、Ubuntu 20.04/22.04/24.04/26.04 的重构。本目录提供可直接编译、已验证的两块代码，接进 octool 源码树即可，不需要重写 octool 的界面层。

- 日期：2026-09-30
- 前置分析见 `../../analysis/docs/linux-porting.md`（ABI 地板、预编译 .ko 不可移植、EL 内核实编、DKMS、显示栈）。本篇是在那之上的**可落地代码**。

---

## 0. 一页总览

重构围绕一个事实：**octool 现在通过 `/dev/mydev` 内核模块只做 MMIO；MSR 走 `/dev/cpu/N/msr`，端口走 `iopl`，物理内存走 `/dev/mem`。后三条在 Secure Boot（内核 lockdown）下全部被拦。** 所以要支持广泛发行版（尤其开了 Secure Boot 的机器），正确做法是把**所有**硬件访问都能走一条已签名的内核模块，同时保留非 lockdown 机器上的直接路径。

本目录给出：

| 组件 | 文件 | 状态 |
|---|---|---|
| 可移植内核模块（线级兼容现有 octool 的 MMIO，另加 MSR/端口/PCI/EC） | `kmod/octool_hwio.c` + `kmod/Kbuild` | 11 个内核实编通过（Ubuntu 5.4→7.0 八个 + Rocky 8.10/9.8/10.2） |
| 用户态硬件访问层（一套 API，运行时选后端 + lockdown 判定） | `hal/octool_hwio.{c,h}` | 在 glibc 2.31→2.43 / gcc 9→15 上编译通过 |
| 线级契约（模块与用户态共用一份） | `abi/octool_hwio_abi.h` | 结构 96 字节、字段偏移与现有 octool 二进制逐字节一致 |
| 离线协议自测（无需内核） | `tests/hwio_loopback_test.c` | 全通过 |
| 实机冒烟测试 | `tests/hwio_smoke.c` | 需真机运行 |
| DKMS / rpm / deb 打包 | `packaging/` | 模板，四个 dkms 版本实测 |

关键改动相对原模块：**去掉了每请求的 `kthread_create_on_cpu`**（5.17 前不导出，正是老内核编不过的原因），CPU 相关操作改用 `rdmsr_safe_on_cpu` / `smp_call_function_single`。这样版本敏感点只剩 `class_create` 的参数个数一个，由 Kbuild 探测头文件解决。

---

## 1. 线级契约（`abi/octool_hwio_abi.h`）

模块和用户态各 `#include` 这一个头，杜绝两边漂移。

- 请求 `struct octool_hwio_req`：96 字节，`cmd@0, user_id@8, data0@16, data1@24, data2..data9`，全部 u64。**与现有 octool 二进制反汇编出的布局逐字节一致**（`tests/` 里有 `_Static_assert` 钉死）。
- 邮箱：`mmap(fd, offset 0)` 得到一页，`slot[0]` 是完成标志（0→1），`slot[1]` 是标量结果（字节偏移 8），CPUID 用 `slot[1..4]` 返回四个 dword。这两个下标由现有二进制固定。
- MMIO 操作码（**不可改，现有二进制在用**）：

  | 操作 | cmd | data0 | data1 |
  |---|---|---|---|
  | RD_MEM64 | 0x0a | 物理地址 | — |
  | WR_MEM64 | 0x0b | 物理地址 | 值 |
  | RD_MEM32 | 0x0c | 物理地址 | — |
  | WR_MEM32 | 0x0d | 物理地址 | 值 |
  | RD_MEM16 | 0x0e | 物理地址 | — |
  | WR_MEM16 | 0x0f | 物理地址 | 值 |
  | RD_MEM8  | 0x10 | 物理地址 | — |
  | WR_MEM8  | 0x11 | 物理地址 | 值 |

- 非 MMIO 操作码（本项目自定，与上面不冲突）：MSR 0x20/0x21、TSC 0x22、CPUID 0x23、端口 in 0x30–0x32 / out 0x33–0x35、PCI 0x40/0x41、EC 0x50/0x51、核数 0x60。现有二进制从不发这些，所以无兼容负担；octool 的访问层用同一批常量。

> 因为 MMIO 操作码保持兼容，**新模块是现有 octool 二进制的直接替换**：装上新模块、`modprobe octool_hwio`，现有 octool 的 MMIO 路径照常工作——这给了你一个不改 octool 就能验证新模块的手段。

---

## 2. 内核模块（`kmod/`）

- 字符设备默认 `/dev/mydev`（`devname` 模块参数可改），`open()` 要求 `CAP_SYS_RAWIO`，默认还要 `CAP_SYS_ADMIN`（`allow_unpriv=Y` 放宽到仅 RAWIO）。
- MMIO：`ioremap` 目标页→`readX/writeX`→`iounmap`，与原实现同路径。
- MSR/CPUID/TSC：`rdmsr_safe_on_cpu`/`wrmsr_safe_on_cpu` 和 `smp_call_function_single` 在 `user_id` 指定的 CPU 上执行。
- 端口/PCI/EC：模块内直接 `inX/outX`（内核态不受 lockdown 限制）；PCI 走 0xCF8/0xCFC（intel-conf1），EC 走 0x62/0x66 标准时序。
- `Kbuild` 探测 `class_create` 签名（不是看版本号——RHEL 9 在 9.2 双参数、9.4 起单参数，版本号都是 5.14）。

构建（真机）：

```sh
# EL
sudo dnf install -y kernel-devel-$(uname -r) gcc make elfutils-libelf-devel
# Ubuntu（22.04 装了 HWE 6.8 内核还要 gcc-12）
sudo apt install -y linux-headers-$(uname -r) gcc make
cd kmod && make            # 生成 octool_hwio.ko
```

已验证：11 个内核编译+链接通过，导入符号在各自内核全部导出（`tests/expcheck.sh` 核对，EL 三棵树 0 缺失）。唯一的真实告警是 EL8/GCC9 对 `init_module` 的 `-Wmissing-attributes`，无害。

**未做**：真机 `insmod` 与读写硬件（这里没有目标硬件）。用 `tests/hwio_smoke.c` 在真机上过一遍。

---

## 3. 用户态访问层（`hal/`）

一套 C API（`extern "C"`，可从 octool 的 C++ 直接调），替换 octool 里散落的 `Rdmsr()/Wrmsr()/Read_MMIO*/Write_MMIO*/ReadIoPort*/…`：

```c
hwio_t *h = hwio_open(NULL);          /* 打开 /dev/mydev；判定 lockdown；规划后端 */
hwio_rdmsr(h, cpu, reg, &val);
hwio_mem_read(h, phys, 4, &val);      /* 8/16/32/64 */
hwio_io_write(h, port, 1, v);
hwio_pci_read(h, bus, dev, fn, off, 4, &v32);
hwio_ec_read(h, index, &byte);
hwio_cpuid(h, cpu, leaf, sub, out4);
hwio_cpu_cores(h, &n);
hwio_close(h);
```

后端选择（每族一次，`hwio_backend_for()` 可查）：

| 情况 | MSR/MMIO/IO/PCI/EC | CPU（cpuid/tsc/cores） |
|---|---|---|
| 模块在 | 走模块 | 走模块 |
| 无模块、未 lockdown | 直接路径（`/dev/cpu/N/msr`、`/dev/mem`、`iopl`、`/sys/bus/pci`） | 本地指令 |
| 无模块、lockdown | 返回 `-EPERM` | 本地指令（cpuid/tsc/sysconf 不需要特权） |

lockdown 判定读 `/sys/kernel/security/lockdown`（当前模式在方括号里）。

### 接进 octool 的做法

1. 把 `hal/` 和 `abi/` 加进 octool 源码树，`hal/octool_hwio.c` 参与编译（或 `make -C hal` 出 `liboctool_hwio.a` 链接）。
2. 进程启动时 `hwio_open(NULL)` 存一个全局/单例句柄（octool 现在也是全局 `kernel_fd`）。
3. 把 octool 现有的这些函数体替换为对 `hwio_*` 的调用——签名基本对得上：
   - `Rdmsr/Wrmsr/RdmsrTx/WrmsrTx` → `hwio_rdmsr/hwio_wrmsr`
   - `Read_MMIO{,8,16,64}/Write_MMIO{,8,16,64}` → `hwio_mem_read/write(width)`
   - `ReadIoPortByte/Word` `WriteIoPortByte/Word` → `hwio_io_read/write(width)`
   - libpci 的 `libpci_read/write_*` → `hwio_pci_read/write`
   - `Read_ECRAM_BYTE/Write_ECRAM_BYTE` → `hwio_ec_read/write`
   - `my_processor_cores` → `hwio_cpu_cores`
4. 删掉 octool 里的 `initilize_kernel_driver`、`/dev/mem` 直读、`iopl(3)`、`/dev/cpu/msr` 直读——这些现在都在 HAL 里，且 HAL 在 lockdown 下自动走模块。
5. `MainWindow` 构造里那句 `iopl(3)` 去掉（HAL 的直接端口路径按需自己 `iopl`；模块路径不需要）。
6. 加载模块方式改为 `modprobe octool_hwio`（DKMS 装在 `/lib/modules/$(uname -r)/updates/dkms/`），不要再 `init_module` 读程序目录下的 .ko，也不要 `chdir` 到自身目录。

### 已验证

- `tests/hwio_loopback_test.c`：一个「参考传输」在进程内按模块的语义解码 `octool_hwio_req`、服务于假内存/假 MSR/假 PCI，再填邮箱。测试断言**每个操作**的编码操作码与字段和 octool 反汇编出的字节一致（MMIO 八个操作码逐个核对），且值经 编码→参考派发→解码 往返正确。全通过。
- HAL 在 focal/jammy/noble/resolute 四个 chroot（gcc 9/11/13/15，glibc 2.31/2.35/2.39/2.43）`-Wall -Wextra` 零告警编译并通过 loopback。

### 未验证 / 真机确认

- HAL↔真实模块的端到端（需要 `insmod` 真模块）——用 `tests/hwio_smoke.c`：它对比「模块返回的 CPUID」与「本地 cpuid 指令」，必须一致；再打印核数和一个 MSR 读，以及每族用的后端。
- EL 用户态编译 HAL（这里没有 EL 编译器；代码是标准 C99+POSIX+`<sys/io.h>`，EL 都有）——用打包构建确认。

---

## 4. 打包（`packaging/`）

- 模块走 DKMS。`dkms.conf` 在 dkms 2.8.1/2.8.7/3.0.11/3.2.2 × 八个 Ubuntu 内核上实测通过。
- `octool-hwio-dkms.spec`（EL rpm）、`debian/`（deb）都是模板，接进你现有 octool 的发布流程。
- EL9/EL10 的 kABI 按小版本失效（见 `../../analysis/docs/linux-porting.md` §4.4），所以不要做跨小版本 kmod，统一 DKMS。
- Ubuntu 包带 `packaging/octool-msr.conf`（`/usr/lib/modules-load.d/`，内容 `msr`）保证直接路径可用；EL 内建 MSR，不需要。
- Secure Boot：DKMS 支持 MOK 签名（Ubuntu 经 `update-secureboot-policy`/shim-signed，EL 用 `/var/lib/dkms/mok.*`）；用户 `mokutil --import` 后重启确认一次。签名模块 + HAL 在 lockdown 下走模块，才真正解决 Secure Boot 机器。

安装（真机快速路径）：

```sh
sudo sh packaging/dkms-install.sh 2.0
sudo modprobe octool_hwio          # 生成 /dev/mydev
sudo ./tests/hwio_smoke            # 冒烟
```

---

## 5. 用户态二进制本身的移植（与本目录配合）

访问层解决了「怎么访问硬件」，octool 二进制自身的多发行版落地仍按前一篇的结论：

- 在 **EL8（glibc 2.28）** 上用 gcc-toolset 编一次，覆盖七个目标（glibc 向后兼容）。
- Qt 5.15 静态链接，configure 加 `-no-icu -qt-libjpeg` 去掉两个随发行版变的 soname（EL10 缺 `libjpeg.so.8`、各版 ICU 大版本号不同）。
- 保留 AT-SPI（sckoct 靠它驱动 octool）。
- 丢弃 `mylib` 里的 glibc、`run_lib.sh`（会 unlink 系统 libc）、坏掉的 hwloc 软链、未用的 OpenCV。
- 显示：EL10 无 Xvfb/Xorg/TigerVNC，用 `xwfb-run -c mutter -s '\-geometry' -s <WxH>`（已在 Ubuntu 26.04 上验证），或继续用容器后端。

细节与证据在 `../../analysis/docs/linux-porting.md`。

---

## 6. 落地顺序

1. 把 `abi/`、`hal/` 加进 octool 树，用 `hwio_*` 替换散落的访问函数（§3）。先在一台非 Secure Boot 机上跑通（HAL 走直接路径，行为等同现在）。
2. `packaging/dkms-install.sh` 装模块，`modprobe`，`hwio_smoke` 冒烟；确认 CPUID 经模块与本地一致。
3. 开 Secure Boot 的机器上：DKMS 签名 + MOK 注册；确认 HAL 全族显示 `module`，octool 功能正常。
4. octool 二进制按 §5 在 EL8 上出一次，覆盖七个目标；出 rpm/deb + DKMS 包。
5. EL10 显示用 `xwfb-run` 或容器。

## 7. 仍需真机确认的项

| 项 | 这里做不了的原因 | 确认方法 |
|---|---|---|
| 模块 insmod + 读写硬件 | 无目标硬件/内核 | `hwio_smoke` |
| HAL↔真实模块端到端 | 同上 | `hwio_smoke`（CPUID 对拍） |
| EL 用 RHEL 自带 GCC 编模块 | 这里用 Ubuntu 的 GCC | 真机 `kernel-devel` + `make -C kmod` |
| EL 用户态编 HAL | 无 EL 编译器 | 打包构建 |
| Secure Boot 下 MOK 签名/加载 | 沙箱无 Secure Boot | 真机 |
| lockdown 下各族回退到 module | 沙箱未 lockdown | 真机（`hwio_smoke` 打印后端） |
